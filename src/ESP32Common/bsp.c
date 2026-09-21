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
 
#include "bsp.h"
#include "oled.h"
#include "main.h"
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

// Global handle for the registered OLED I2C device
static i2c_master_dev_handle_t oled_handle = NULL;

// Encoder section
// Holds the current volume.
// Range: -1200 … 0 corresponding to -120dB to 0dB
extern volatile int16_t global_volume;

// ----- Static variables ----- 
static pcnt_unit_handle_t pcnt_unit = NULL;
static int last_counter = 0;         // previous PCNT count 
static int16_t enc_accumulator = 0;  // accumulates partial steps

extern volatile int16_t global_volume; // Defined in ub_app.h

// ---------- Button configuration (tune if needed) ---------- 
#define DEBOUNCE_MS              20u     // required stable time for new state 
#define LONG_PRESS_MS            500u
#define DOUBLE_CLICK_MS          300u

// Active logic level: our button closes to GND → pressed = LOW 
#define BUTTON_PRESSED_LEVEL     0u

// ---------- Button state machine ---------- 
typedef enum {
    BTN_IDLE,
    BTN_DOWN,            // First press detected, waiting for release or long press 
    BTN_WAIT_DOUBLE,     // First release detected, waiting for second press 
    BTN_DOWN_DOUBLE,     // Second press detected, waiting for release 
    BTN_LONG_PRESS       // Long press already reported, waiting for release 
} btn_state_t;

static btn_state_t btn_state = BTN_IDLE;

// Debouncing variables 
static uint8_t raw_level;                 // Last read raw pin state 
static uint8_t debounced_level;           // 1 = released, 0 = pressed 
static uint32_t last_change_time;         // Last time raw pin changed 

// Timestamps (milliseconds from bsp_millis) 
static uint32_t press_timestamp;          // when button went DOWN 
static uint32_t release_timestamp;        // when button went UP 

// Previous debounced state for edge 
static uint8_t prev_pressed = 0;

extern volatile bool dump;


// Encoder section
void encoder_volume_init(void) {
    // 1. Create a PCNT unit
    pcnt_unit_config_t unit_config = {
        // Set limits to standard 16-bit integer boundaries for safe wrap-around
        .high_limit = 32767,
        .low_limit = -32768,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // 2. Set up the hardware glitch filter (1us filter ignores mechanical bounce)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000, 
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // 3. Create two channels (Channel A and Channel B)
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = PIN_CH_A,
        .level_gpio_num = PIN_CH_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = PIN_CH_B,
        .level_gpio_num = PIN_CH_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // 4. Configure X4 Quadrature decoding logic
    // Channel A
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    // Channel B
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // 5. Enable, clear, and start the counter
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // 6. Capture initial state
    last_counter = 0;
}


void encoder_volume_update(void) {
    int current_count = 0;
    
    // Read the current hardware count (this is non-blocking and instant)
    pcnt_unit_get_count(pcnt_unit, &current_count);

    // 16-bit subtraction handles hardware wrap-around gracefully
    int16_t delta = (int16_t)(current_count - last_counter);
    last_counter = current_count;

    // Apply direction inversion if needed 
#if ENCODER_INVERT_DIRECTION
    delta = -delta;
#endif

    // Add the raw timer counts to the accumulator 
    enc_accumulator += delta;

    // In X4 mode, one mechanical click = 4 timer counts.
    // Only process if we have reached a full click. 
    if (enc_accumulator >= 4 || enc_accumulator <= -4) {
        // Calculate how many full clicks were turned 
        int16_t clicks = enc_accumulator / 4;
        
        // Apply the clicks to the volume. 
        int32_t new_val = (int32_t)global_volume + clicks * VOLUME_DB_INC;

        // Clamp to the allowed range 
        if (new_val > VOLUME_MAX) {
            new_val = VOLUME_MAX;
        } else if (new_val < VOLUME_MIN) {
            new_val = VOLUME_MIN;
        }

        global_volume = (int16_t)new_val;
        
        // Subtract what we just applied, keeping any remainder in the accumulator 
        enc_accumulator -= (clicks * 4);
    }
}


// Button section 
void button_init(void) {
    // External pull-up is fitted on the board -> disable internal pulls 
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_PUSH),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Seed the debouncer with the current pin level
    raw_level        = (uint8_t)gpio_get_level(PIN_PUSH);
    debounced_level  = raw_level;
    prev_pressed     = (debounced_level == BUTTON_PRESSED_LEVEL);
    last_change_time = bsp_millis();
    btn_state        = BTN_IDLE;
}


void button_task(void) {
    uint32_t now = bsp_millis();
    uint8_t  raw = (uint8_t)gpio_get_level(PIN_PUSH);

    // 1. Time-based debouncing
    if (raw != raw_level) {
        raw_level        = raw;
        last_change_time = now;
    }
    if ((now - last_change_time) >= DEBOUNCE_MS) {
        debounced_level = raw_level;
    }

    // 2. Edge detection
    uint8_t is_pressed    = (debounced_level == BUTTON_PRESSED_LEVEL);
    uint8_t pressed_edge  = (is_pressed && !prev_pressed);
    uint8_t released_edge = (!is_pressed && prev_pressed);
    prev_pressed = is_pressed;

    // 3. State machine – same logic as the STM32 version
    switch (btn_state) {

        case BTN_IDLE:
            if (pressed_edge) {
                press_timestamp = now;
                btn_state = BTN_DOWN;
            }
            break;

        case BTN_DOWN:
            if (is_pressed && ((now - press_timestamp) >= LONG_PRESS_MS)) {
                button_longpress_callback();
                btn_state = BTN_LONG_PRESS;
            } else if (released_edge) {
                release_timestamp = now;
                btn_state = BTN_WAIT_DOUBLE;
            }
            break;

        case BTN_WAIT_DOUBLE:
            if (pressed_edge) {
                press_timestamp = now;
                btn_state = BTN_DOWN_DOUBLE;
            } else if ((now - release_timestamp) >= DOUBLE_CLICK_MS) {
                button_click_callback();
                btn_state = BTN_IDLE;
            }
            break;

        case BTN_DOWN_DOUBLE:
            if (is_pressed && ((now - press_timestamp) >= LONG_PRESS_MS)) {
                button_longpress_callback();
                btn_state = BTN_LONG_PRESS;
            } else if (released_edge) {
                button_doubleclick_callback();
                btn_state = BTN_IDLE;
            }
            break;

        case BTN_LONG_PRESS:
            if (released_edge) {
                btn_state = BTN_IDLE;
            }
            break;
    }
}


// --- I2C Initialization (Call this before OLED_Init) ---
void oled_i2c_init(void) {
    // 1. Configure the I2C Master Bus
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1, // Automatically assign an available I2C port
        .scl_io_num = PIN_SCL,
        .sda_io_num = PIN_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // 2. Configure and add the OLED device to the bus
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDR,
        .scl_speed_hz = 400000, // 400kHz Fast Mode
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &oled_handle));
}


// --- I2C Transmission ---
void oled_i2c_write(uint8_t *data, uint16_t size) {
    // ESP-IDF handles the Start condition, Addressing, TXE polling, and Stop condition internally.
    if (oled_handle != NULL) {
        // Convert the 50 timeout to actual milliseconds
        i2c_master_transmit(oled_handle, data, size, I2C_TIMEOUT_MS);
    }
}