/* 
 * This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/obcazzaro/hlm).
 * Copyright (c) 2026 Rob Cazzaro.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

// ============================================================================
// ============================================================================
// @file           : hub_app.c
// @brief          : Genelec GLM protocol library
// ============================================================================

#include "hub_app.h"
#include "glm_library.h"        // IWYU pragma: keep
#include "math.h"               // IWYU pragma: keep
#include "string.h"             // IWYU pragma: keep
#include "stdio.h"              // IWYU pragma: keep
#include "stddef.h"             // IWYU pragma: keep
#include "main.h"               // IWYU pragma: keep
#include "bsp.h"                // IWYU pragma: keep
#include "oled.h"               // IWYU pragma: keep

#ifdef HLM_HUB

// ============================================================================
// Tuning Constants
// ============================================================================
#define ISS_IDLE_TIMEOUT_MS    30000UL // Silence before the hub enters ISS idle
#define HEARTBEAT_INTERVAL_MS  610     // OEM standalone heartbeat cadence (ms)
#define VU_MUSIC_THRESHOLD     163     // VU above this = music (matches OLED zero-bar cutoff)

// ============================================================================
// Global State & Variables
// ============================================================================
Speaker_t speaker_registry[MAX_SPEAKERS];
uint8_t next_available_sess_id = 0x02; // Start at 0x02 (0x01 is Hub)
HubState_t hub_state = STATE_STANDALONE_IDLE;
bool is_muted = false; // Global mute state tracker
bool new_speaker_discovered = false; // Flag to trigger immediate state transition
bool standby_active = false;

// TX Queue
typedef struct {
    uint16_t data[TX_MAX_FRAME_SIZE];
    uint16_t len;
} TxFrame_t;

// The global volume, updated by Encoder_Volume_Update() 
volatile int16_t global_volume = -500;   // starts at -50 dB. Value is dB*10 

volatile TxFrame_t tx_queue[TX_QUEUE_DEPTH];
volatile uint16_t tx_head_hub = 0;
volatile uint16_t tx_tail_hub = 0;

extern volatile uint16_t parse_buffer[RX_BUFFER_SIZE];
extern volatile uint16_t parse_length;
extern volatile bool message_ready;

// Timing trackers
uint32_t last_state_tick = 0;
uint32_t last_tx_tick = 0;
uint32_t last_oled_update = 0;
uint32_t last_oled_attempt = 0; // Throttle for I2C safety
uint32_t last_telemetry_tick = 0;
uint32_t last_music_ms = 0;     // Last time any speaker VU showed music (ISS idle timer base)
int16_t last_sent_volume = -999;

uint8_t last_polled_sess_id = 0;
uint8_t telemetry_poll_index = 0;

// ============================================================================
// RS 485 TX Queue Management
// ============================================================================
bool tx_queue_push(const uint16_t *frame, uint16_t len) {
    if (len > TX_MAX_FRAME_SIZE) return false; // Safety check
    uint16_t next_head = (tx_head_hub + 1) % TX_QUEUE_DEPTH;
    if (next_head == tx_tail_hub) return false; // Queue full
    memcpy((void*)tx_queue[tx_head_hub].data, (const void*)frame, len * sizeof(uint16_t));
    tx_queue[tx_head_hub].len = len;
    tx_head_hub = next_head;
    return true;
}


bool tx_queue_pop(uint16_t *out_frame, uint16_t *out_len) {
    if (tx_head_hub == tx_tail_hub) return false; // Empty
    *out_len = tx_queue[tx_tail_hub].len;
    memcpy(out_frame, (const void*)tx_queue[tx_tail_hub].data, *out_len * sizeof(uint16_t));
    tx_tail_hub = (tx_tail_hub + 1) % TX_QUEUE_DEPTH;
    return true;
}


void send_frame(uint16_t addr_9bit, const uint8_t *payload, size_t payload_len) {
    uint16_t frame[MAX_FRAME_SIZE];
    int len = build_frame_16bit(addr_9bit, payload, payload_len, frame, MAX_FRAME_SIZE);
    if (len > 0) {
        if (!tx_queue_push(frame, (uint16_t)len)) {
            printf("TX Queue Full!\r\n");
        }
    }
}


// ============================================================================
// Display Update Helper (throttled to 20Hz max to prevent I2C lockups)
// ============================================================================
void update_oled_display(void) {
    uint32_t now = bsp_millis();
    if (now - last_oled_attempt < 50) return;
    last_oled_attempt = now;

    // Never draw while the display is powered down (standby / ISS idle)
    if (standby_active || hub_state == STATE_ISS_IDLE) return;

    int8_t bar1 = 0, bar2 = 0;
    uint8_t active_count = 0;

    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active) {
            uint8_t raw = speaker_registry[i].vu1;
            int16_t mapped = 0;

            // Remapped boundaries to shift the curve down
            if (raw <= 163) {
                mapped = 0;
            } else if (raw <= 191) {
                // Map 164-191 to 0-13
                mapped = ((raw - 163) * 13) / 28;
            } else {
                // Map 192+ to 14-63 (hits exactly 63 at raw=241)
                mapped = 14 + (raw - 192);
            }

            // Clamp to OLED limits (just in case raw is > 241)
            if (mapped > 63) mapped = 63;
            
            if (active_count == 0) {
                bar1 = (int8_t)mapped;
            } else if (active_count == 1) {
                bar2 = (int8_t)mapped;
                break; // Only need the first two active speakers
            }
            active_count++;
        }
    }

    if (is_muted)
        oled_display_update(-9999, bar1, bar2);
    else
        oled_display_update(global_volume, bar1, bar2);
}


void enter_standby(void) {
    // Duplicates OEM hub sequence
    send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x02}, 3);
    send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x02}, 3);
    send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x00}, 3);
    send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x00}, 3);

    // Synchronise the stored volume to prevent an immediate wake-up
    last_sent_volume = global_volume;

    oled_off();      // OLED is off, but the HLM keeps sending keep alive packets like the OEM hub
    standby_active = true;
    hub_state = STATE_ACTIVE_POLLING;
    last_state_tick = bsp_millis();
}


void wake_up_system(void) {
    oled_write_command(0xAF);   // turn on OLED

    // Clean wake state: never resume muted (matches build_config_sequence's
    // 2B 04, which unmutes the speakers on every re-enumeration anyway),
    // and open a fresh ISS idle window.
    is_muted = false;
    last_music_ms = bsp_millis();

    // Always clear the speaker registry - ensures a clean re-enumeration
    memset((void*)speaker_registry, 0, sizeof(speaker_registry));
    next_available_sess_id = 0x02;
    telemetry_poll_index = 0;
    new_speaker_discovered = false;

    if (standby_active) {
        // Wake from USER STANDBY: the speakers are powered down and need the
        // OEM power-on sequence. Proven by weeks of use - do not touch.
        send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x7F}, 3);  // keep-alive 1
        send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x01}, 3);  // keep-alive 2
        send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x02}, 3);  // System ON
    }
    // Wake from ISS IDLE: send NO 3A 03 power commands at all. The speakers
    // are either ON and playing (a power command burst to playing speakers
    // puts them in standby ~1.2 s later - see the 183 s wake capture, where
    // 3A 03 02 was followed by 47 02 in telemetry) or ISS-sleeping (the OEM
    // heartbeat capture shows the correct action is nothing: audio wakes
    // them via ISS auto-restart). Re-enumeration alone is the "wake".

    // Restore current volume (fixed 3-byte OEM encoding)
    uint8_t vol_payload[4];
    format_volume_payload(global_volume, vol_payload);
    send_frame(0x01FF, vol_payload, 4);
    last_sent_volume = global_volume;   // wake already asserted it (no duplicate send)

    // Immediate discovery
    send_frame(0x01FF, (uint8_t[]){0xFE}, 1);

    standby_active = false;
    hub_state = STATE_DISCOVERY;
    last_state_tick = bsp_millis();
}


// ============================================================================
// Volume Control
// ============================================================================
void format_volume_payload(int16_t vol_db_tenths, uint8_t *out_payload) {
    // vol_db_tenths is negative (0 to -1600)
    // OEM-correct 3-byte encoding. 24-bit fixed-point linear gain,
    // scale 2^23 (0x7FFFFF = 0 dB, clipped). Payload is 4 bytes total
    // (command + 3). Verified against the OEM log: -50 dB -> 1F 00 67 9F
    // (CRC EF 0B on 0x01F0).
    float db = (float)vol_db_tenths / 10.0f;
    float linear_gain = powf(10.0f, db / 20.0f);
    uint32_t raw_value = (uint32_t)llroundf(linear_gain * 8388608.0f);   // 2^23
    
    if (raw_value > 0x7FFFFF) raw_value = 0x7FFFFF;
    if (vol_db_tenths <= -1600) raw_value = 0x000000;    // hard mute
    
    out_payload[0] = 0x1F;
    out_payload[1] = (raw_value >> 16) & 0xFF;
    out_payload[2] = (raw_value >> 8)  & 0xFF;
    out_payload[3] = raw_value & 0xFF;
}


void check_volume_change(void) {

    if (global_volume != last_sent_volume) {
        if (standby_active || hub_state == STATE_ISS_IDLE) {
            // Any user action restarts the hub from a clean state.
            // wake_up_system() already asserts the NEW volume (and updates
            // last_sent_volume) and starts discovery, so we are done here.
            wake_up_system();
            return;
        }

        // A volume change is user activity: refresh the ISS window so the
        // hub cannot go idle right after a knob turn into silence.
        last_music_ms = bsp_millis();

        last_sent_volume = global_volume;
        uint8_t vol_payload[4];
        format_volume_payload(global_volume, vol_payload);
        send_frame(0x01FF, vol_payload, 4);
        update_oled_display();
    }
}


// ============================================================================
// ISS Idle (standalone heartbeat)
// ============================================================================
// The OEM hub, when no Windows app is connected, transmits nothing except a
// "Set Volume" broadcast on the hub multicast address 0x01F0 every ~610 ms.
// That frame is ISS-neutral: the speakers still run their own ISS timer and
// enter standby by themselves. This is exactly the bus state we must hold to
// let the speakers sleep. The hub exits ISS idle only on user action
// (volume change / click / long press), then re-enumerates from scratch:
// only global_volume survives; mute and standby are cleared.

// OEM-exact standalone heartbeat: 3-byte Set Volume payload on 0x01F0 with
// the current global_volume. At the default -50 dB this reproduces the
// captured OEM frame "1F0 1F 00 67 9F EF 0B 7E" byte for byte.
void send_standalone_heartbeat(void) {
    uint8_t vol_payload[4];
    format_volume_payload(global_volume, vol_payload);
    send_frame(0x01F0, vol_payload, 4);
}

// Enter ISS idle. No command is sent to the speakers on entry: they go to
// standby on their own once the telemetry polling stops. The hub keeps
// asserting the standalone volume (heartbeat) with the OLED off.
void enter_iss_idle(void) {
    send_standalone_heartbeat();   // take over the bus like the OEM hub does
    oled_off();
    hub_state = STATE_ISS_IDLE;
    last_state_tick = bsp_millis();
}


// ============================================================================
// Button Callbacks
// ============================================================================
void button_click_callback(void) {
    // Any user action restarts the hub from a clean state
    if (standby_active || hub_state == STATE_ISS_IDLE) {
        wake_up_system();
        return;
    }

    // Toggle mute (unchanged)
    is_muted = !is_muted;

    // Refresh the ISS idle window: idle entry is suppressed while muted, so
    // last_music_ms can be stale. Without this, unmuting into music would
    // idle the hub instantly, before the VU recovers (~1 s after 2B 04).
    last_music_ms = bsp_millis();

    uint8_t payload[2] = {0x2B, is_muted ? 0x03 : 0x04};
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active) {
            send_frame(0x0100 | speaker_registry[i].sess_id, payload, 2);
        }
    }
    update_oled_display();
}


void button_longpress_callback(void) {
    // A long press while sleeping (standby or ISS idle) restarts the hub.
    // The user may long-press thinking the system is muted: restarting is
    // the safe interpretation either way.
    if (standby_active || hub_state == STATE_ISS_IDLE) {
        wake_up_system();   // exit standby / restart from ISS idle
        return;
    }
    enter_standby();        // go to standby
}


void button_doubleclick_callback(void) {
    // No action for now
}


// ============================================================================
// Session ID Allocator
// ----------------------------------------------------------------------------
// Returns the next unused Session ID in [0x02, 0xFE], scanning forward from
// next_available_sess_id and skipping any ID currently held by an active
// speaker. This is what prevents a collision when a flapping speaker keeps
// re-registering and the counter eventually wraps: the counter alone is not
// a source of truth, the registry is.
//
// Returns 0x00 if every ID is taken (impossible with sane MAX_SPEAKERS,
// but kept as a safety valve).
// ============================================================================
static uint8_t allocate_next_sess_id(void) {
    for (uint16_t attempts = 0; attempts < 0xFD; attempts++) {   // 0x02..0xFE = 253 IDs
        uint8_t candidate = next_available_sess_id;

        // Advance the cursor for the next call, wrapping in range
        next_available_sess_id++;
        if (next_available_sess_id > 0xFE) next_available_sess_id = 0x02;

        // Reject any candidate already held by an active speaker
        bool in_use = false;
        for (int j = 0; j < MAX_SPEAKERS; j++) {
            if (speaker_registry[j].active && speaker_registry[j].sess_id == candidate) {
                in_use = true;
                break;
            }
        }
        if (!in_use) return candidate;
    }
    return 0x00;   // no free ID
}


// ============================================================================
// Event Handlers (on_* functions)
// ============================================================================
void on_speaker_hw_id(const uint8_t hw_id[3], const uint8_t *payload, size_t len) {
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active && 
            speaker_registry[i].hw_id[0] == hw_id[0] &&
            speaker_registry[i].hw_id[1] == hw_id[1] &&
            speaker_registry[i].hw_id[2] == hw_id[2]) {
            return; // Already registered
        }
    }
    
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (!speaker_registry[i].active) {
            uint8_t new_sess = allocate_next_sess_id();
            if (new_sess == 0x00) return;   // registry full, drop the probe

            memcpy(speaker_registry[i].hw_id, hw_id, 3);
            speaker_registry[i].sess_id = new_sess;
            speaker_registry[i].active = true;
            speaker_registry[i].config_queried = false;
            speaker_registry[i].missed_polls = 0;
            speaker_registry[i].vu1 = 100;
            speaker_registry[i].vu2 = 100;
            
            uint8_t assign_payload[5] = {0x02, hw_id[0], hw_id[1], hw_id[2], new_sess};
            send_frame(0x01F0, assign_payload, 5);
            
            // Immediately trigger configuration without waiting for the next tick
            new_speaker_discovered = true; 
            return;
        }
    }
}


void on_telemetry_response(const uint8_t *data, size_t len) {
    if (len < 2) return;
    if (data[0] == 0x06) return;   // flash write complete

    uint8_t target_sess_id = last_polled_sess_id;
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active && speaker_registry[i].sess_id == target_sess_id) {
            speaker_registry[i].missed_polls = 0;
            speaker_registry[i].temperature = data[1];

            size_t idx = 2;
            while (idx < len) {
                uint8_t tag = data[idx++];

                if (tag == 0x43 && idx < len) {
                    speaker_registry[i].vu1 = data[idx++];
                    if (speaker_registry[i].vu1 > VU_MUSIC_THRESHOLD) {
                        last_music_ms = bsp_millis();
                    }
                }
                else if (tag == 0x84 && idx < len) {
                    speaker_registry[i].play_state = data[idx++];   // 1-byte value
                }
                else if (tag == 0x47 && idx < len) {
                    uint8_t sys_state = data[idx];
                    speaker_registry[i].play_state = sys_state;
                    if (sys_state == 0x02) {
                        // Standby: VU meters are dropped from the frame, so the cached
                        // values would freeze the OLED bar. Force them to the floor.
                        speaker_registry[i].vu1 = 0;
                        speaker_registry[i].vu2 = 0;
                    }
                    idx += 1;
                }
                else if (tag == 0x81 || tag == 0x83) {
                    idx += 2;   // 2-byte value
                }
                else if (tag == 0x42 || tag == 0x45) {
                    idx += 1;   // 1-byte value
                }
                else if (tag == 0xB0) {
                    idx += 2;   // 2-byte value (not used)
                }
                else {
                    // Unknown tag – we can’t know its length, so stop parsing.
                    break;
                }
            }

            update_oled_display();   // refresh instantly
            return;
        }
    }
}


void on_generic_ack(void) {
    // The hub just polled last_polled_sess_id. An ACK on address 0x0101
    // is that speaker confirming receipt. Only reset THAT speaker's
    // watchdog, not every active speaker: a dead speaker that never
    // answers must be allowed to accrue missed polls and be evicted.
    uint8_t target_sess_id = last_polled_sess_id;
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active && speaker_registry[i].sess_id == target_sess_id) {
            speaker_registry[i].missed_polls = 0;
            break;
        }
    }
}


void on_flash_write_complete(void) {
    // The speaker we just polled is busy writing flash.
    // Don't count this as a missed telemetry poll.
    uint8_t target_sess_id = last_polled_sess_id;
    for (int i = 0; i < MAX_SPEAKERS; i++) {
        if (speaker_registry[i].active && speaker_registry[i].sess_id == target_sess_id) {
            speaker_registry[i].missed_polls = 0;
            break;
        }
    }
}


// ============================================================================
// Configuration Sequencer. Will need to be optimized for any specific setup
// This configuration is for two calibrated 8320A speakers
// ============================================================================
void build_config_sequence(uint8_t sess_id) {
    uint16_t unicast_addr = 0x0100 | sess_id;
    uint8_t payload[32];
    
    // 1. Broadcast Sync
    payload[0] = 0x04; payload[1] = 0xBC; payload[2] = 0x84;
    send_frame(0x01FF, payload, 3);
    
    // 2. CMD 05 (Signal Generator / Restore)
    payload[0] = 0x05; payload[1] = 0x04; payload[2] = 0x00; payload[3] = 0x00; 
    payload[4] = 0x00; payload[5] = 0xFF; payload[6] = 0xFF; payload[7] = 0xEA;
    payload[8] = 0x00; payload[9] = 0x01; payload[10] = 0x90; payload[11] = 0x00;
    payload[12] = 0x02; payload[13] = 0xDA;
    send_frame(unicast_addr, payload, 14);
    
    // 3. CMD 17 (Prepare for Configuration)
    payload[0] = 0x17; payload[1] = 0x01;
    send_frame(unicast_addr, payload, 2);
    
    // 4. Broadcast Sync
    payload[0] = 0x04; payload[1] = 0xBC; payload[2] = 0x84;
    send_frame(0x01FF, payload, 3);
    
    // DSP Commands (0x10) intentionally omitted to preserve speaker calibration
    
    // 5. CMD 3B (Bass Mgmt)
    payload[0] = 0x3B; payload[1] = 0x00; payload[2] = 0x01;
    send_frame(unicast_addr, payload, 3);
    
    // 6. CMD 3D (Input Sel)
    payload[0] = 0x3D; payload[1] = 0x00; payload[2] = 0x00;
    send_frame(unicast_addr, payload, 3);
    
    // 7. CMD 40 (Standalone Config: Analog Input)
    payload[0] = 0x40; payload[1] = 0x00; payload[2] = 0x01; payload[3] = 0x02; payload[4] = 0x00;
    send_frame(unicast_addr, payload, 5);
    
    // 8. System Info Request
    payload[0] = 0x39;
    send_frame(unicast_addr, payload, 1);
    
    // 9. Serial Number Request
    payload[0] = 0x19; payload[1] = 0x01;
    send_frame(unicast_addr, payload, 2);
    
    // 10. Restore Normal State / Unmute
    payload[0] = 0x2B; payload[1] = 0x04;
    send_frame(unicast_addr, payload, 2);
}


// ============================================================================
// Main Hub Loop
// ============================================================================
void process_tx_queue(void) {
    if (bsp_millis() - last_tx_tick >= 20) {
        uint16_t frame[MAX_FRAME_SIZE];
        uint16_t len;
        if (tx_queue_pop(frame, &len)) {
            send_data(frame, len);
            last_tx_tick = bsp_millis();
        }
    }
}


void hub_init(void) {
    last_state_tick = bsp_millis();
    last_tx_tick = bsp_millis();
    last_telemetry_tick = bsp_millis();
    last_music_ms = bsp_millis();
    hub_state = STATE_STANDALONE_IDLE;
    memset((void*)speaker_registry, 0, sizeof(speaker_registry));
    next_available_sess_id = 0x02; // Ensure this is reset on boot
    telemetry_poll_index = 0;
    new_speaker_discovered = false;
}


void hub_main_loop(void) {
    uint32_t now = bsp_millis();

    // 1. Process incoming RX
    if (message_ready) {
        parse_message((uint16_t*)parse_buffer, parse_length);
        message_ready = false;
    }

    // 2. Process TX Queue (20ms pacing)
    process_tx_queue();

    // 3. Real-time Volume Change check (handles standby/ISS idle wake-up internally)
    check_volume_change();

    // 4. State machine (always runs, even during standby_active)
    uint32_t state_interval;
    if (hub_state == STATE_STANDALONE_IDLE)      state_interval = 1220;
    else if (hub_state == STATE_DISCOVERY)       state_interval = 200;
    else if (hub_state == STATE_ISS_IDLE)        state_interval = HEARTBEAT_INTERVAL_MS;   // 610 ms, OEM cadence
    else /* STATE_ACTIVE_POLLING */              state_interval = 1080;

    if (now - last_state_tick >= state_interval) {
        last_state_tick = now;

        // Prepare volume payload - always real volume, never mute
        uint8_t vol_payload[4];
        format_volume_payload(global_volume, vol_payload);

        switch (hub_state) {
            case STATE_STANDALONE_IDLE:
                // In standby, skip the wake-up triggers (7F/01) to avoid waking speakers
                if (standby_active) {
                    hub_state = STATE_DISCOVERY;
                    break;
                }
                send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x7F}, 3);
                send_frame(0x01FF, (uint8_t[]){0x3A, 0x03, 0x01}, 3);
                send_frame(0x01FF, vol_payload, 4);
                hub_state = STATE_DISCOVERY;
                break;

            case STATE_DISCOVERY:
                send_frame(0x01FF, (uint8_t[]){0xFE}, 1);

                // Immediately configure any unconfigured speakers
                for (int i = 0; i < MAX_SPEAKERS; i++) {
                    if (speaker_registry[i].active && !speaker_registry[i].config_queried) {
                        build_config_sequence(speaker_registry[i].sess_id);
                        speaker_registry[i].config_queried = true;
                    }
                }
                new_speaker_discovered = false;

                // Transition to ACTIVE_POLLING if at least one speaker is alive
                bool any_active = false;
                for (int i = 0; i < MAX_SPEAKERS; i++) {
                    if (speaker_registry[i].active) {
                        any_active = true;
                        break;
                    }
                }
                hub_state = any_active ? STATE_ACTIVE_POLLING : STATE_DISCOVERY;
                if (any_active) {
                    // Fresh ISS idle window: telemetry just (re)started, so
                    // don't idle on a stale last_music_ms timestamp
                    last_music_ms = bsp_millis();
                }
                break;

            case STATE_ACTIVE_POLLING:
                send_frame(0x01FF, (uint8_t[]){0xFE}, 1);
                send_frame(0x01FF, vol_payload, 4);

                // Configure any speaker that answered an FE after we already left
                // DISCOVERY (race at wake-up: the first respondent flips us to
                // ACTIVE_POLLING, the second registers milliseconds later and
                // would otherwise never receive its config sequence).
                // Sending the config sequence to a running speaker is proven
                // safe - it happens on every standby wake.
                for (int i = 0; i < MAX_SPEAKERS; i++) {
                    if (speaker_registry[i].active && !speaker_registry[i].config_queried) {
                        build_config_sequence(speaker_registry[i].sess_id);
                        speaker_registry[i].config_queried = true;
                    }
                }
                break;

            case STATE_ISS_IDLE:
                // OEM standalone behavior: heartbeat only, nothing else on the
                // bus. No polls, no FE probes, no 1FF volume. The speakers are
                // left alone so their own ISS timer can put them in standby.
                // Only a user action exits this state (handled in callbacks).
                send_standalone_heartbeat();
                break;
        }
    }

    // 4.5 ISS idle entry: silence for ISS_IDLE_TIMEOUT_MS while operating
    // normally. Never from DISCOVERY (may be mid-recovery), never in user
    // standby, and never while muted: the speakers do not ISS-sleep when
    // muted, so the hub must not idle either. Accepted trade-off: if the
    // user mutes and stops the streamer, the hub stays awake until acted on.
    int32_t iss_delta = (int32_t)(now - last_music_ms);
    if (hub_state == STATE_ACTIVE_POLLING && !standby_active && !is_muted &&
        iss_delta >= (int32_t)ISS_IDLE_TIMEOUT_MS) {
        enter_iss_idle();
    }

    // 5. Telemetry Polling (ACTIVE_POLLING only: never polls while in ISS idle)
    if (hub_state == STATE_ACTIVE_POLLING) {
        if (now - last_telemetry_tick >= 200) {
            last_telemetry_tick = now;
            for (int i = 0; i < MAX_SPEAKERS; i++) {
                int idx = (telemetry_poll_index + i) % MAX_SPEAKERS;
                if (speaker_registry[idx].active) {
                    speaker_registry[idx].missed_polls++;
                    if (speaker_registry[idx].missed_polls >= 4) {
                        speaker_registry[idx].active = false;

                        // Stay in ACTIVE_POLLING as long as at least one
                        // speaker is alive. The FE broadcast in
                        // ACTIVE_POLLING (~1 s cadence) will re-discover
                        // the lost speaker in the background, and the
                        // "configure any unconfigured active speaker"
                        // block will pick it up when it answers.
                        // Tearing down to DISCOVERY here stops polling the
                        // surviving speaker, so its VU stops refreshing
                        // last_music_ms, and the ISS idle timer expires on
                        // stale data -> screen goes dark at 30 s.
                        bool still_any_active = false;
                        for (int j = 0; j < MAX_SPEAKERS; j++) {
                            if (speaker_registry[j].active) {
                                still_any_active = true;
                                break;
                            }
                        }
                        if (!still_any_active) {
                            hub_state = STATE_DISCOVERY;
                        }
                        break;
                    } else {
                        last_polled_sess_id = speaker_registry[idx].sess_id;
                        send_frame(0x0100 | speaker_registry[idx].sess_id, (uint8_t[]){0x08}, 1);
                        telemetry_poll_index = (idx + 1) % MAX_SPEAKERS;
                        break;
                    }
                }
            }
        }
    }

    // 6. OLED Display Update (skip if in standby or ISS idle)
    if (!standby_active && hub_state != STATE_ISS_IDLE && now - last_oled_update >= 500) {
        last_oled_update = now;
        update_oled_display();
    }
}

#endif  // HLM_HUB