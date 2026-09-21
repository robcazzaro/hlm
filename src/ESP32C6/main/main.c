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

#include "freertos/FreeRTOS.h"  // IWYU pragma: keep
#include "freertos/task.h"
#include "rs485_9n2.h"
#include "main.h"
#include "bsp.h"                // IWYU pragma: keep
#include "oled.h"               // IWYU pragma: keep
#include "glm_library.h"
#include "hub_app.h"            // IWYU pragma: keep

#if !defined(HLM_HUB) && !defined(HLM_HEX_DUMP) && !defined(HLM_ANALYZER)
    #error "Compilation Error: You must define one of HLM_HUB, HLM_HEX_DUMP, or HLM_ANALYZER."
#endif

//volatile int16_t global_volume = -500;
volatile uint16_t parse_buffer[RX_BUFFER_SIZE];
volatile uint16_t parse_length = 0;
volatile bool message_ready = false;

extern TaskHandle_t main_task_handle;

// Pin definitions as requested
#define PIN_RO     9
#define PIN_DI     10
#define PIN_DE_RE  11


void app_main(void) {
    rs485_9bit_config_t config = {
        .rmt_tx_gpio = PIN_DI,
        .rmt_rx_gpio = PIN_RO,
        .de_re_gpio  = PIN_DE_RE,
        .baud_rate   = 296000
    };

    // Initialize the library
    rs485_9bit_init(&config);

 #ifdef HLM_HUB    // I2C and encoder are used only for the hub, not protocol analyzers
    button_init();
    encoder_volume_init();
    oled_i2c_init();
    oled_init();
    hub_init();
#endif

    main_task_handle = xTaskGetCurrentTaskHandle();

    while (1) {

#ifdef HLM_HUB
        // HUB MODE: Wait for a message, but timeout after 1ms.
        // If a message arrives, it wakes up IMMEDIATELY (0ms).
        // If no message arrives, it wakes up after 1ms to run the UI/Hub tasks.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));
#else
        // SNIFFER MODE: Block infinitely until a message arrives.
        // Uses 0% CPU, no 1ms tick overhead.
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#endif

        // 1. Process any incoming messages
        if (message_ready) {
#ifdef HLM_HEX_DUMP
            dump_message((uint16_t *)parse_buffer, parse_length);
#else
            // Pass the double-buffer to your parser
            parse_message((uint16_t*)parse_buffer, parse_length);
#endif
            message_ready = false;
        }

        // 2. Run background hub duties
#ifdef HLM_HUB 
        // These will execute every 1ms (or slightly faster if a message woke the task early)
        encoder_volume_update();
        button_task();
        hub_main_loop();
#endif
    }
}