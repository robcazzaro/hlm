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

#ifndef __GLM_LIBRARY_H
#define __GLM_LIBRARY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "stdbool.h"
#include "string.h"
#include "math.h"
#include "stdio.h"

#define RX_BUFFER_SIZE 256
#define MAX_FRAME_SIZE 256
#define MAX_PRINT_BUFFER 1024

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ----------------------------------------------------------------------------
// PEQ design rates, per spec sections 4 and 5.
//   Two-way monitors (8320A, 8330A, etc.): 48 kHz (confirmed).
//   Subwoofers (7350A, etc.):              12 kHz (confirmed from OEM config).
//   Higher-end monitors (83x1 series):     unresolved -- default to 48 kHz.
//
// Callers must select the rate per device class. See hub_app.c Speaker_t
// (device_class field) and the dispatch in parse_message() in glm_library.c.
// ----------------------------------------------------------------------------
#define SAMPLE_RATE_2WAY    48000.0f
#define SAMPLE_RATE_SUB     12000.0f
#define SAMPLE_RATE         SAMPLE_RATE_2WAY   // legacy alias for the default rate

typedef enum {
    FILTER_LOW_SHELF,
    FILTER_HIGH_SHELF,
    FILTER_PEAKING       // The UI calls this "Notch", but it behaves as a Peaking EQ
} FilterType;

// ----------------------------------------------------------------------------
// Flash commit settings (spec section 9, "Flash Commit / Standalone Saving").
//
// STAGE 9 changes:
//   - iss_sleep_delay promoted to uint32_t (4-byte BE field, 0xFFFFFFFF = Never)
//   - config_counter removed; the 4th byte of the 0x40 payload is aes3_channel
//   - explicit input_idx / pair_selector fields for 0x40
// ----------------------------------------------------------------------------
typedef struct {
    float max_level_db;          // Max Level Restriction (0.0 to -20.0 dB)
    float startup_level_db;      // Startup Level (0.0 to -60.0 dB)

    // 0x3A 01: ISS Sleep Delay, 4-byte BE, value in minutes.
    // 0x00000002 = 2 min, 0x0000000A = 10 min, 0x000000F0 = 240 min,
    // 0xFFFFFFFF = Never.
    uint32_t iss_sleep_delay;

    uint8_t iss_sensitivity;     // 0 = High, 1 = Medium, 2 = Low
    bool led_on;                 // true = LED On, false = LED Off

    // 0x40 Standalone Config Block: [40] [input_idx] [source] [pair] [aes3]
    uint8_t input_idx;           // 00 = primary, 01 = secondary (subwoofers)
    uint8_t input_select;        // 0 = Analog, 1 = Digital, 2 = Automatic
                                 // (wire source byte = input_select + 1)
    uint8_t pair_selector;       // pair selection / input pairing
    uint8_t aes3_channel;        // 00 = none, 01 = A, 02 = B, 03 = A+B sum
} FlashCommitSettings_t;

int build_frame_16bit(uint16_t address_9bit, const uint8_t *payload, size_t payload_len, 
                      uint16_t *out_frame, size_t out_max_len);
int send_flash_commit(uint8_t sess_id, const FlashCommitSettings_t *settings);
void parse_message(uint16_t *rx_data, uint16_t bytes_read);
void dump_message(uint16_t *rx_data, uint16_t bytes_read);
void send_data(uint16_t *data, uint16_t size);

// STAGE 9: store/apply prelude and epilogue helpers.
// Call send_store_duck() once before looping send_flash_commit() over all
// speakers, and send_store_restore() once after the loop.
void send_store_duck(void);
void send_store_restore(const uint8_t *vol_payload);

#endif // __GLM_LIBRARY_H