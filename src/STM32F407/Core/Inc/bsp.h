/* 
 * This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/hlm).
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

#ifndef BSP_H
#define BSP_H

#include "stdint.h"
#include "stm32f4xx_hal.h"         // for HAL_GetTick() 

// Define your I2C instance here if not already defined elsewhere
#ifndef OLED_I2C
#define OLED_I2C I2C1
#endif

// Helper: milliseconds since boot (replaces HAL_GetTick)
static inline uint32_t bsp_millis(void) {
    return  HAL_GetTick();
}

void oled_i2c_write(uint8_t *data, uint16_t size);
void process_usb_tx_task(void);
void send_data(uint16_t *data, uint16_t size);

// Encoder section ---------------------------------------------------------------------------------------
// Call once after TIM2 is initialised (e.g. at the start of main()) 
void encoder_volume_init(void);

// Call this function periodically (e.g. every 1 ms in SysTick handler,
// in the main loop, or in a timer update interrupt) 
void encoder_volume_update(void);

// Button section ---------------------------------------------------------------------------------------
// User‑defined callback functions – implement them in your application 
void button_click_callback(void);        // single short press & release 
void button_longpress_callback(void);    // held ≥ LONG_PRESS_MS 
void button_doubleclick_callback(void);  // two clicks within DOUBLE_CLICK_MS 

// Initialise PA2 as input with internal pull‑up and reset state 
void button_init(void);

// Must be called frequently (e.g. every 1 ms) – handles debouncing & events 
void button_task(void);

// Send payload to RS485 bus
void send_data(uint16_t *data, uint16_t size);

#endif // BSP_H