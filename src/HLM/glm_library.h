// 
// This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/obcazzaro/hlm).
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

#define SAMPLE_RATE 48000.0

typedef enum {
    FILTER_LOW_SHELF,
    FILTER_HIGH_SHELF,
    FILTER_PEAKING       // The UI calls this "Notch", but it behaves as a Peaking EQ
} FilterType;

// Flash commit struct
typedef struct {
    float max_level_db;         // Max Level Restriction (0.0 to -20.0 dB)
    float startup_level_db;     // Startup Level (0.0 to -60.0 dB)
    uint16_t iss_sleep_delay;   // Sleep delay in minutes (0xFFFF = Never)
    uint8_t iss_sensitivity;    // 0 = High, 1 = Medium, 2 = Low
    bool led_on;                // true = LED On, false = LED Off
    uint8_t input_select;       // 0 = Analog, 1 = Digital, 2 = Automatic
    uint8_t config_counter;     // Session counter/ID (0-255)
} FlashCommitSettings_t;

int build_frame_16bit(uint16_t address_9bit, const uint8_t *payload, size_t payload_len, 
                      uint16_t *out_frame, size_t out_max_len);
int send_flash_commit(uint8_t sess_id, const FlashCommitSettings_t *settings);
void parse_message(uint16_t *rx_data, uint16_t bytes_read);
void dump_message(uint16_t *rx_data, uint16_t bytes_read);
void send_data(uint16_t *data, uint16_t size);

#endif // __GLM_LIBRARY_H