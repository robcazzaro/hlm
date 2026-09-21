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

#include "rs485_9n2.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "soc/uart_struct.h"
#include "soc/interrupts.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"      // IWYU pragma: keep
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include <string.h>

#define TAG "RS485_9N2"
#define APB_CLOCK_HZ 80000000
#define MAX_PACKET_LEN 85
#define UART_PORT UART_NUM_1

static uint32_t ticks_per_bit = 270; 
static int pin_de_re = 0;
static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;
static SemaphoreHandle_t tx_done_sem = NULL;
static intr_handle_t uart_isr_handle = NULL;
static uart_dev_t *uart_dev = NULL;

// 9-bit word ring buffer (ISR to Task)
#define RX_BUFFER_SIZE 1024  // Increased to handle bursts
uint16_t isr_buffer[RX_BUFFER_SIZE];
uint16_t isr_index = 0;

extern volatile uint16_t parse_buffer[RX_BUFFER_SIZE];
extern volatile uint16_t parse_length;
extern volatile bool message_ready;

// 1. Create a global handle for the main task
TaskHandle_t main_task_handle = NULL;

// Packet structure for internal queue passing
typedef struct {
    uint16_t addr;
    uint8_t len;
    uint8_t payload[MAX_PACKET_LEN];
} packet_t;


// --- Custom UART ISR (One Byte Per ISR) ---
static void IRAM_ATTR uart_rx_isr(void *arg) {
    uint32_t uart_intr = uart_dev->int_st.val;
    
    // Read exactly ONE byte per ISR invocation.
    // This guarantees the frm_err bit matches the byte we are reading.
    if (uart_dev->status.rxfifo_cnt > 0) {
        
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        bool frm_err = uart_dev->int_st.frm_err_int_st;
        uint8_t data = uart_dev->fifo.rxfifo_rd_byte & 0xFF;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
        bool frm_err = uart_dev->int_st.frm_err;
        uint8_t data = uart_dev->fifo.val & 0xFF;
#else
        // fallback - needs work
        bool frm_err = uart_dev->int_st.frm_err_int_st; // TBD
        uint8_t data = uart_dev->fifo.val & 0xFF;
#endif

        // Reconstruct the 9-bit character
        uint16_t rx_char = frm_err ? data : (data | 0x100);
        
        // --- STM32 State Machine Port ---
        
        // 5. Normal processing: Is this the start of a new frame? (9th bit HIGH)
        if (rx_char & 0x0100) {
            isr_index = 0;
            isr_buffer[isr_index++] = rx_char;
        }
        else if (isr_index > 0) {
            isr_buffer[isr_index++] = rx_char;

            if ((rx_char & 0x00FF) == 0x7E) {
                if (!message_ready) {
                    for (int i = 0; i < isr_index; i++) {
                        parse_buffer[i] = isr_buffer[i];
                    }
                    parse_length = isr_index;
                    message_ready = true; 
                    
                    // 2. Wake up the main task IMMEDIATELY
                    if (main_task_handle != NULL) {
                        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                        vTaskNotifyGiveFromISR(main_task_handle, &xHigherPriorityTaskWoken);
                        
                        // If waking the task makes it the highest priority ready task,
                        // trigger an immediate context switch upon exiting the ISR.
                        if (xHigherPriorityTaskWoken) {
                            portYIELD_FROM_ISR(); 
                        }
                    }
                }
                isr_index = 0; 
            }
            // 8. Buffer Overflow Protection
            else if (isr_index >= RX_BUFFER_SIZE) {
                isr_index = 0; 
            }
        }
    }
    uart_dev->int_clr.val = uart_intr;
}


// --- TX Encoder ---
static int encode_9bit_word(uint16_t word, rmt_symbol_word_t *dest) {
    uint8_t bits[13];
    bits[0] = 0; // Start
    for (int i = 0; i < 9; i++) {
        bits[i+1] = (word >> i) & 1;
    }
    bits[10] = 1; bits[11] = 1; bits[12] = 1; // Stops + Idle

    int sym_idx = 0;
    int i = 0;
    while (i < 13) {
        int lvl0 = bits[i];
        int cnt0 = 0;
        while (i < 13 && bits[i] == lvl0) { cnt0++; i++; }

        int lvl1 = 0, cnt1 = 0;
        if (i < 13) {
            lvl1 = bits[i];
            while (i < 13 && bits[i] == lvl1) { cnt1++; i++; }
        }

        dest[sym_idx].level0 = lvl0;
        dest[sym_idx].duration0 = cnt0 * ticks_per_bit;
        dest[sym_idx].level1 = lvl1;
        dest[sym_idx].duration1 = cnt1 * ticks_per_bit;
        sym_idx++;
    }
    return sym_idx;
}


static bool IRAM_ATTR tx_done_callback(rmt_channel_handle_t channel, const rmt_tx_done_event_data_t *edata, void *user_data) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(tx_done_sem, &higher_priority_task_woken);
    return higher_priority_task_woken == pdTRUE;
}


// --- Public API ---

void rs485_9bit_init(const rs485_9bit_config_t *config) {
    pin_de_re = config->de_re_gpio;
    ticks_per_bit = APB_CLOCK_HZ / config->baud_rate;

    // Setup DE/RE Pin
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin_de_re),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(pin_de_re, 0); // LOW = Receive mode

    // 1. Setup RMT TX Channel
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = config->rmt_tx_gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = APB_CLOCK_HZ,
        .mem_block_symbols = 192,
        .trans_queue_depth = 4
    };
    rmt_new_tx_channel(&tx_config, &tx_channel);
    
    rmt_copy_encoder_config_t copy_enc_config;
    rmt_new_copy_encoder(&copy_enc_config, &copy_encoder);
    
    tx_done_sem = xSemaphoreCreateBinary();
    rmt_tx_event_callbacks_t tx_cbs = {
        .on_trans_done = tx_done_callback
    };
    rmt_tx_register_event_callbacks(tx_channel, &tx_cbs, NULL);
    rmt_enable(tx_channel);

    // 2. Setup Hardware UART for RX (8N2 + Framing Error Trick)
    
    // Note: In IDF v6, uart_param_config() handles bus clock enablement and resets.
    // Removing uart_ll_enable_bus_clock() bypasses the __DECLARE_RCC_ATOMIC_ENV macro errors.
    
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_2,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_PORT, &uart_config);
    
    // Enable internal pull-up on RX pin to prevent noise when bus is idle
    gpio_set_pull_mode(config->rmt_rx_gpio, GPIO_PULLUP_ONLY);
    uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, config->rmt_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    uart_dev = UART_LL_GET_HW(UART_PORT);
    uart_ll_rxfifo_rst(uart_dev);
    uart_dev->int_clr.val = 0xFFFFFFFF;

    // Set RX FIFO full threshold to 1 byte
    uart_dev->conf1.rxfifo_full_thrhd = 1;
    
    // Enable RX FIFO Full and Framing Error interrupts
    // Updated for IDF v6: Use _int_ena suffix for the interrupt enable register
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    uart_dev->int_ena.rxfifo_full_int_ena = 1;
    uart_dev->int_ena.frm_err_int_ena = 1;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    uart_dev->int_ena.rxfifo_full = 1;
    uart_dev->int_ena.frm_err = 1;
#else
        // fallback - needs work
    uart_dev->int_ena.rxfifo_full_int_ena = 1;  // TBD
    uart_dev->int_ena.frm_err_int_ena = 1;      // TBD
#endif
    
    // Register our custom ISR
    esp_intr_alloc(ETS_UART1_INTR_SOURCE, ESP_INTR_FLAG_LOWMED, uart_rx_isr, NULL, &uart_isr_handle);

    ESP_LOGI(TAG, "RS485 9N2 Initialized (RMT TX + UART Framing Error Trick). Baud: %lu", config->baud_rate);
}


void send_data(uint16_t *data, uint16_t size) {
    // Limit to 81 bytes (1 address + 80 payload) max
    if (size > 81) size = 81;
    
    // Max 81 bytes * 7 symbols/byte = 567 symbols. Allocate 1024 statically.
    static rmt_symbol_word_t symbols[1024];
    int sym_idx = 0;

    // Encode all 9-bit words directly from the data array.
    // The encode_9bit_word function automatically handles whether the 9th bit is 1 (address) or 0 (payload).
    for (uint16_t i = 0; i < size; i++) {
        sym_idx += encode_9bit_word(data[i], &symbols[sym_idx]);
    }

    // Enable Driver, Disable Receiver
    gpio_set_level(pin_de_re, 1);
    vTaskDelay(pdMS_TO_TICKS(1)); // Allow MAX485 to fully enable

    // Transmit using IDF v6 API
    rmt_transmit_config_t tx_trans_config = {
        .loop_count = 0,
        .flags.eot_level = 1 // Leave bus high when done
    };
    rmt_transmit(tx_channel, copy_encoder, symbols, sym_idx * sizeof(rmt_symbol_word_t), &tx_trans_config);
    
    // Wait for completion (matching the STM32 TC flag blocking behavior)
    // Using a 1000ms timeout to match your STM32 implementation
    xSemaphoreTake(tx_done_sem, pdMS_TO_TICKS(1000));

    // Disable Driver, Enable Receiver
    gpio_set_level(pin_de_re, 0);
}