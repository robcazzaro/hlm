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

#ifndef BSP_H
#define BSP_H

#include "stdint.h"
#include "esp_timer.h"

// Encoder section
#define PIN_CH_A    6
#define PIN_CH_B    7
#define PIN_PUSH    8

// I2C OLED section
#define PIN_SDA     4
#define PIN_SCL     5
#define I2C_TIMEOUT_MS     50

// Encoder section -------------------------------------------------------------------------
// ----- User adjustable definitions ----- 
#define ENCODER_TIMER              TIM2
#define VOLUME_MIN                 (-1204)
#define VOLUME_MAX                 0
#define VOLUME_DB_INC              5        // each encoder detent is 0.5dB

// Set to 1 if rotating the encoder clockwise *decreases* the volume,
//   otherwise 0. Swap the physical A/B wires if needed. 
#define ENCODER_INVERT_DIRECTION   0

// Helper: milliseconds since boot (replaces HAL_GetTick) 
static inline uint32_t bsp_millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Encoder section ---------------------------------------------------------------------------------------
// Call once after PCNT is initialised (e.g. at the start of main()) 
void encoder_volume_init(void);

// Call this function periodically (e.g. every 1 ms in SysTick handler,
// in the main loop, or in a timer update interrupt) 
void encoder_volume_update(void);

// Button section ---------------------------------------------------------------------------------------
// User‑defined callback functions – implement them in your application 
void button_click_callback(void);        // single short press & release 
void button_longpress_callback(void);    // held ≥ LONG_PRESS_MS 
void button_doubleclick_callback(void);  // two clicks within DOUBLE_CLICK_MS 
void button_init(void);

// Must be called frequently (e.g. every 1 ms) – handles debouncing & events 
void button_task(void);

// I2C OLED
void oled_i2c_init(void);
void oled_i2c_write(uint8_t *data, uint16_t size);

#endif // BSP_H