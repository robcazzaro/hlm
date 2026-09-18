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
 
#include "bsp.h"
#include "main.h"
#include "stdbool.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_bus.h"   // only if you want to check/use LL utilities 
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_cortex.h"   // optional, for NVIC if using interrupts 
#include "stm32f4xx_hal.h"         // for HAL_GetTick() 
#include "stm32f4xx_ll_i2c.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

// OLED section
#define OLED_ADDR          0x3C
#define OLED_I2C_ADDR      (OLED_ADDR << 1)
#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_BUFFER_SIZE   (OLED_WIDTH * OLED_HEIGHT / 8)

#define I2C_TIMEOUT        125      // F407 needs longer timeout compared to F103

// Holds the current volume.
// Range: -1200 … 0 corresponding to -120dB to 0dB
extern volatile int16_t global_volume;

extern uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
extern uint8_t recvDone;
extern uint32_t recvLen;

extern volatile uint8_t usb_port_opened;
extern USBD_HandleTypeDef hUsbDeviceFS; // Expose the USB handle to access the state machine

volatile uint8_t tx_ring_buffer[TX_RING_BUFFER_SIZE];
volatile uint16_t tx_head = 0; // Where we write new data
volatile uint16_t tx_tail = 0; // Where we read data from

uint16_t tx_data[16];
uint16_t rx_data[RX_BUFFER_SIZE];
int indx = 0;

// 1-character holding variable used by the interrupt
uint16_t rx_char = 0; 

// Internal ISR state (Do not touch outside the interrupt)
uint16_t isr_buffer[RX_BUFFER_SIZE];
uint16_t isr_index = 0;


// Encoder section -------------------------------------------------------------------------
// ----- User adjustable definitions ----- 
#define ENCODER_TIMER              TIM2
#define VOLUME_MIN                 (-1200)
#define VOLUME_MAX                 0
#define VOLUME_DB_INC              5        // each encoder detent is 0.5dB

// Set to 1 if rotating the encoder clockwise *decreases* the volume,
//   otherwise 0. Swap the physical A/B wires if needed. 
#define ENCODER_INVERT_DIRECTION   0

// ----- Static variables ----- 
static uint16_t last_counter;   // previous TIM2->CNT value 
static int16_t enc_accumulator = 0;  // accumulates partial steps 

extern volatile int16_t global_volume; // Defined in hub_app.h


// ---------- Button configuration (tune if needed) ---------- 
#define BUTTON_PORT              EncButton_GPIO_Port
#define BUTTON_PIN               EncButton_Pin

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

static btn_state_t state = BTN_IDLE;

// Debouncing variables 
static uint8_t raw_level;                 // Last read raw pin state 
static uint8_t debounced_level;           // 1 = released, 0 = pressed 
static uint32_t last_change_time;         // Last time raw pin changed 

// Timestamps (milliseconds from HAL_GetTick) 
static uint32_t press_timestamp;          // when button went DOWN 
static uint32_t release_timestamp;        // when button went UP 

// Previous debounced state for edge 
static uint8_t prev_pressed = 0;

extern volatile bool dump;


// redefine _write to enable printf() over USB CDC
int _write(int file, char *ptr, int len) {
    for (int i = 0; i < len; i++) {
        uint16_t next_head = (tx_head + 1) % TX_RING_BUFFER_SIZE;
        
        // If the buffer is full, we must sacrifice the oldest byte
        if (next_head == tx_tail) {
            // Advance the tail pointer to "forget" the oldest byte
            tx_tail = (tx_tail + 1) % TX_RING_BUFFER_SIZE;
        }
        
        // Write the new byte and advance the head
        tx_ring_buffer[tx_head] = ptr[i];
        tx_head = next_head;
    }
    return len;
}


// --- I2C LL Transmission ---
void oled_i2c_write(uint8_t *data, uint16_t size) {
    uint32_t timeout = I2C_TIMEOUT;

    LL_I2C_GenerateStartCondition(OLED_I2C);
    while (!LL_I2C_IsActiveFlag_SB(OLED_I2C) && timeout--) {}
    
    LL_I2C_TransmitData8(OLED_I2C, OLED_I2C_ADDR);
    timeout = I2C_TIMEOUT;
    while (!LL_I2C_IsActiveFlag_ADDR(OLED_I2C) && timeout--) {}
    LL_I2C_ClearFlag_ADDR(OLED_I2C);

    while (size--) {
        timeout = I2C_TIMEOUT;
        while (!LL_I2C_IsActiveFlag_TXE(OLED_I2C) && timeout--) {}
        LL_I2C_TransmitData8(OLED_I2C, *data++);
    }
    
    timeout = I2C_TIMEOUT;
    while (!LL_I2C_IsActiveFlag_BTF(OLED_I2C) && timeout--) {}
    LL_I2C_GenerateStopCondition(OLED_I2C);
}


void process_usb_tx_task(void) {
    static uint8_t linear_buf[256];
    static uint16_t pending_bytes = 0;

    // 1. If the port is NOT open, abort transmission.
    if (!usb_port_opened) {
        // Clear any half-sent data so we don't send garbage when reconnected
        pending_bytes = 0; 
        
        // We do NOT clear the ring buffer. We let it keep spinning and 
        // overwriting itself in the background.
        return; 
    }

    // 2. If we have data pending from a previous loop, try sending it again.
    if (pending_bytes > 0) {
        if (CDC_Transmit_FS(linear_buf, pending_bytes) == USBD_OK) {
            pending_bytes = 0; 
        }
        return; 
    }

    // 3. If USB is free, pull the next chunk from the ring buffer.
    if (tx_head != tx_tail) {
        while (tx_tail != tx_head && pending_bytes < sizeof(linear_buf)) {
            linear_buf[pending_bytes++] = tx_ring_buffer[tx_tail];
            tx_tail = (tx_tail + 1) % TX_RING_BUFFER_SIZE;
        }
        
        if (CDC_Transmit_FS(linear_buf, pending_bytes) == USBD_OK) {
            pending_bytes = 0; 
        }
    }
}


// Encoder section
void encoder_volume_init(void) {
    // 1. ENABLE THE COUNTER - Without this, TIM2->CNT will never change! 
    LL_TIM_EnableCounter(ENCODER_TIMER);

    // 2. Capture the current counter value so the first update doesn’t jump 
    last_counter = LL_TIM_GetCounter(ENCODER_TIMER);
}


void encoder_volume_update(void) {
    uint16_t current = LL_TIM_GetCounter(ENCODER_TIMER);

    // 16‑bit unsigned subtraction yields the modulo‑65536 difference. 
    int16_t delta = (int16_t)(current - last_counter);
    last_counter = current;

    // Apply direction inversion if needed 
#if ENCODER_INVERT_DIRECTION
    delta = -delta;
#endif

    // Add the raw timer counts to the accumulator 
    enc_accumulator += delta;

    // In X4 mode, one mechanical click = 4 timer counts.
    //   Only process if we have reached a full click. 
    if (enc_accumulator >= 4 || enc_accumulator <= -4) {
        // Calculate how many full clicks were turned 
        int16_t clicks = enc_accumulator / 4;
        
        // Apply the clicks to the volume. 
        int32_t new_val = (int32_t)global_volume + clicks * VOLUME_DB_INC;

        // Clamp to the allowed range 
        if (new_val > VOLUME_MAX)
            new_val = VOLUME_MAX;
        else if (new_val < VOLUME_MIN)
            new_val = VOLUME_MIN;

        global_volume = (int16_t)new_val;
        
        // Subtract what we just applied, keeping any remainder in the accumulator 
        enc_accumulator -= (clicks * 4);
    }
}


// Button section 
void button_init(void) {
    // Initialise state to current pin level 
    raw_level = LL_GPIO_IsInputPinSet(BUTTON_PORT, BUTTON_PIN);
    debounced_level = raw_level;
    prev_pressed = (debounced_level == BUTTON_PRESSED_LEVEL);
    last_change_time = HAL_GetTick();
    state = BTN_IDLE;
}


void button_task(void) {
    uint32_t now = HAL_GetTick();
    uint8_t raw = LL_GPIO_IsInputPinSet(BUTTON_PORT, BUTTON_PIN);

    // -------- 1. Time-based Debouncing ---------- 
    // If the raw pin changed, reset the timer 
    if (raw != raw_level) {
        raw_level = raw;
        last_change_time = now;
    }

    // If the raw pin has been stable for DEBOUNCE_MS, accept it as the new debounced state 
    if ((now - last_change_time) >= DEBOUNCE_MS) {
        debounced_level = raw_level;
    }

    // -------- 2. Edge Detection -------- 
    uint8_t is_pressed = (debounced_level == BUTTON_PRESSED_LEVEL);
    
    // Detect transitions only 
    uint8_t pressed_edge  = (is_pressed && !prev_pressed);
    uint8_t released_edge = (!is_pressed && prev_pressed);
    
    prev_pressed = is_pressed;

    // -------- 3. State Machine -------- 
    switch (state) {
        case BTN_IDLE:
            if (pressed_edge) {
                press_timestamp = now;
                state = BTN_DOWN;
            }
            break;

        case BTN_DOWN:
            // Check for long press while held 
            if (is_pressed && ((now - press_timestamp) >= LONG_PRESS_MS)) {
                button_longpress_callback();
                state = BTN_LONG_PRESS;
            } 
            else if (released_edge) {
                // Short press released: start waiting to see if it becomes a double click 
                release_timestamp = now;
                state = BTN_WAIT_DOUBLE;
            }
            break;

        case BTN_WAIT_DOUBLE:
            if (pressed_edge) {
                // Second press detected within the time window! 
                press_timestamp = now; // Update timestamp in case they hold the second press 
                state = BTN_DOWN_DOUBLE;
            } 
            else if ((now - release_timestamp) >= DOUBLE_CLICK_MS) {
                // Timeout reached without a second press. It was a single click. 
                button_click_callback();
                state = BTN_IDLE;
            }
            break;

        case BTN_DOWN_DOUBLE:
            // Check if they hold the second press for a long press 
            if (is_pressed && ((now - press_timestamp) >= LONG_PRESS_MS)) {
                button_longpress_callback();
                state = BTN_LONG_PRESS;
            } 
            else if (released_edge) {
                // Second press released quickly -> Double Click! 
                button_doubleclick_callback();
                state = BTN_IDLE;
            }
            break;

        case BTN_LONG_PRESS:
            // Wait for the button to be released, then go back to idle 
            if (released_edge) {
                state = BTN_IDLE;
            }
            break;
    }
}


// ============================================================================
// Send payload to RS485 bus
// ============================================================================
void send_data(uint16_t *data, uint16_t size) {
    // 1. Switch MAX485 to Transmit mode
    LL_GPIO_SetOutputPin(DE_RE_GPIO_Port, DE_RE_Pin);

    uint32_t tickstart = bsp_millis();
    const uint32_t Timeout = 1000; // 1000 ms timeout

    // 2. Loop through and send each byte
    for (uint16_t i = 0; i < size; i++) {
        // Wait until the Transmit Data Register is Empty (TXE)
        while (!LL_USART_IsActiveFlag_TXE(USART1)) {
            if ((bsp_millis() - tickstart) > Timeout) {
                // On timeout, ensure we drop back to receive mode before exiting
                LL_GPIO_ResetOutputPin(DE_RE_GPIO_Port, DE_RE_Pin);
                return;
            }
        }

        // Load the 9-bit data into the data register
        LL_USART_TransmitData9(USART1, data[i]);
    }

    // 3. CRITICAL RS-485 STEP: Wait for Transmission Complete (TC)
    // We cannot toggle the DE/RE pin until the very last bit of the very
    // last byte has physically left the shift register.
    //
    while (!LL_USART_IsActiveFlag_TC(USART1)) {
        if ((bsp_millis() - tickstart) > Timeout) {
            break; // Timeout, proceed to reset pin
        }
    }

    // 4. Switch MAX485 back to Receive mode immediately after the line goes idle
    LL_GPIO_ResetOutputPin(DE_RE_GPIO_Port, DE_RE_Pin);
}