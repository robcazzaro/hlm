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

#ifndef RS485_9N2_H
#define RS485_9N2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Serial port GPIO assignment
#define PIN_RO     9
#define PIN_DI     10
#define PIN_DE_RE  11

typedef struct {
    int rmt_tx_gpio;      // DI pin
    int rmt_rx_gpio;      // RO pin
    int de_re_gpio;       // DE/RE pin
    uint32_t baud_rate;   // 296000
} rs485_9bit_config_t;

// Initialize the RMT peripheral, GPIO, and RX background task
void rs485_9bit_init(const rs485_9bit_config_t *config);

// Send a 9 bit packet stored in a 16 bit array
void send_data(uint16_t *data, uint16_t size);

// Receive a complete packet (blocks until packet with 0x7E terminator is received)
//uint16_t rs485_9bit_receive(uint8_t *payload_buffer, size_t *length, uint32_t timeout_ms);

#endif // RS485_9N2_H