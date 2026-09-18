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
    STATE_STANDALONE_IDLE,
    STATE_DISCOVERY,
    STATE_ACTIVE_POLLING,
    STATE_ISS_IDLE          // NEW: heartbeat only, speakers left to their own ISS
} HubState_t;

// --- Speaker Registry ---
typedef struct {
    uint8_t hw_id[3];
    uint8_t sess_id;
    bool active;
    uint8_t missed_polls;
    bool config_queried;
    
    // Telemetry fields
    uint8_t temperature;
    uint8_t vu1; // Absolute dBFS (0 = Loudest)
    uint8_t vu2;
    uint8_t play_state;
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