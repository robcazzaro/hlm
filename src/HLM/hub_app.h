// 
// This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/hlm).
// Copyright (c) 2026 Rob Cazzaro.
// 
// This program is free software: you can redistribute it and/or modify  
// it under the terms of the GNU General Public License as published by  
// the Free Software Foundation, version 3.
//
// This program is distributed in the hope that it will be useful, but 
// WITHOUT ANY WARRANTY; without even the implied warranty of 
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
// General Public License for more details.
// 
// You should have received a copy of the GNU General Public License 
// along with this program. If not, see <http://www.gnu.org/licenses/>.
// 

#ifndef HUB_APP_H
#define HUB_APP_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

// --- Configuration ---
#define MAX_SPEAKERS 30
#define TX_QUEUE_DEPTH 32
#define MAX_FRAME_SIZE 256
#define TX_MAX_FRAME_SIZE 64

// --- State Machine ---
typedef enum {
    STATE_STANDALONE_IDLE,   // boot wake: send power-on burst, then discover
    STATE_DISCOVERY,         // FE probes, config any newly-answered speaker
    STATE_ACTIVE_POLLING,    // normal operation: volume + 04 keep-alive + polls
    STATE_ISS_IDLE,          // silence timeout: heartbeat only, speakers sleep
    STATE_USER_STANDBY       // user-initiated standby: heartbeat only, OLED off
} HubState_t;

// --- Device class (0x84 first byte) ---
#define DEVICE_CLASS_UNKNOWN    0x00
#define DEVICE_CLASS_SUBWOOFER  0x01
#define DEVICE_CLASS_TWO_WAY    0x02
// 0x03 is a transient "standby" marker; it must not overwrite a known class.

// --- Meter floors (spec section 8.1) ---
#define METER_FLOOR_OUTPUT      0x80
#define METER_FLOOR_INPUT       0x89

// --- Speaker Registry ---
typedef struct {
    uint8_t hw_id[3];
    uint8_t sess_id;
    bool active;
    uint8_t missed_polls;
    bool config_queried;
    
    // Telemetry fields (spec section 8.1)
    uint8_t temperature;         // 0x41 frame header, main temp in C
    uint8_t device_class;        // 0x84 first byte: 01 sub, 02 two-way (03 standby does not overwrite)
    uint8_t system_status;       // 0x47: 01 = ON, 02 = standby (SOLE authority on power state)
    uint8_t input_meter;         // 0x42 (pre-volume input level)
    uint8_t vu_hf;               // 0x43 (HF output meter)
    uint8_t vu_mid;              // 0x44 (midrange output meter; three-way only, 0x80 when absent)
    uint8_t vu_lf;               // 0x45 (LF output meter)
    uint8_t vu_sub;              // 0x46 (subwoofer driver meter; subs only)
    uint8_t state_flags_dynamic; // 0x84 second byte (opaque dynamic metric)
} Speaker_t;

// --- Global Variables ---
// The global volume, updated by encoder_volume_update() 
extern volatile int16_t global_volume; 

extern Speaker_t speaker_registry[MAX_SPEAKERS];
extern uint8_t next_available_sess_id;
extern HubState_t hub_state;

// --- Public Functions ---
void hub_init(void);
void hub_main_loop(void);
void send_frame(uint16_t addr_9bit, const uint8_t *payload, size_t payload_len);

// Button Callbacks (Implemented in hub_app.c, called by bsp.c)
void button_click_callback(void);
void button_longpress_callback(void);
void button_doubleclick_callback(void);

void wake_up_system(void);
void format_volume_payload(int16_t vol_db_tenths, uint8_t *out_payload);

#endif // HUB_APP_H