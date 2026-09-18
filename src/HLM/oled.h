/* 
 * This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/obcazzaro/hlm).
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
 
#ifndef OLED_H
#define OLED_H

#include "stdint.h"

#define OLED_ADDR          0x3C
#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_BUFFER_SIZE   (OLED_WIDTH * OLED_HEIGHT / 8)

void oled_write_command(uint8_t cmd);
void oled_init(void);
void oled_off(void);
void oled_display_update(int16_t volume, int8_t bar1, int8_t bar2);
void screen_test_page(void);

#endif // OLED_H