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

#include "oled.h"
#include "bsp.h"
#include <string.h>

// Double buffer
static uint8_t oled_buffer[OLED_BUFFER_SIZE];

// Font storage: 19 characters, 36 bytes each (12 cols * 3 pages)
#define FONT24_GLYPH_SIZE  36
#define FONT24_CHARS       19
static uint8_t font24_data[FONT24_CHARS][FONT24_GLYPH_SIZE];

// ---------- Raw BDF Font Data ----------
// Extracted from spleen-12x24.bdf
// Characters: ' ', '-', '.', '0'-'9', 'd', 'B', 'M', 'u', 't', 'e'
// 
static const char bdf_rows[FONT24_CHARS][24][5] = {
    // ' ' (Index 0)
    {"0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000"},
    // '-' (Index 1)
    {"0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","3FC0","3FC0","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000"},
    // '.' (Index 2)
    {"0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0000","0E00","0E00","0E00","0000","0000","0000","0000","0000"},
    // '0' (Index 3)
    {"0000","0000","0000","0000","1F80","30C0","6060","6060","60E0","61E0","6360","6660","6C60","7860","7060","6060","6060","30C0","1F80","0000","0000","0000","0000","0000"},
    // '1' (Index 4)
    {"0000","0000","0000","0000","0E00","1E00","3600","2600","0600","0600","0600","0600","0600","0600","0600","0600","0600","0600","3FC0","0000","0000","0000","0000","0000"},
    // '2' (Index 5)
    {"0000","0000","0000","0000","1F80","30C0","6060","0060","0060","00C0","0180","0300","0600","0C00","1800","3000","6060","7FE0","0000","0000","0000","0000","0000","0000"},
    // '3' (Index 6)
    {"0000","0000","0000","0000","1F80","30C0","6060","0060","00C0","0F80","00C0","0060","0060","0060","0060","6060","6060","30C0","1F80","0000","0000","0000","0000","0000"},
    // '4' (Index 7)
    {"0000","0000","0000","0000","6000","6000","6000","6180","6180","6180","6180","6180","6180","6180","7FE0","0180","0180","0180","0180","0000","0000","0000","0000","0000"},
    // '5' (Index 8)
    {"0000","0000","0000","0000","7FE0","6060","6000","6000","6000","6000","7F80","00C0","0060","0060","0060","0060","6060","30C0","1F80","0000","0000","0000","0000","0000"},
    // '6' (Index 9)
    {"0000","0000","0000","0000","1FC0","3060","6000","6000","6000","6000","7F80","60C0","6060","6060","6060","6060","6060","30C0","1F80","0000","0000","0000","0000","0000"},
    // '7' (Index 10)
    {"0000","0000","0000","0000","7FE0","6060","0060","0060","0060","00C0","0180","0300","0600","0C00","0C00","0C00","0C00","0C00","0C00","0000","0000","0000","0000","0000"},
    // '8' (Index 11)
    {"0000","0000","0000","0000","1F80","30C0","6060","6060","6060","30C0","1F80","30C0","6060","6060","6060","6060","6060","30C0","1F80","0000","0000","0000","0000","0000"},
    // '9' (Index 12)
    {"0000","0000","0000","0000","1F80","30C0","6060","6060","6060","6060","6060","3060","1FE0","0060","0060","0060","0060","60C0","3F80","0000","0000","0000","0000","0000"},
    // 'd' (Index 13)
    {"0000","0000","0000","0000","0060","0060","0060","0060","1FE0","3060","6060","6060","6060","6060","6060","6060","6060","3060","1FE0","0000","0000","0000","0000","0000"},
    // 'B' (Index 14)
    {"0000","0000","0000","0000","7F80","60C0","6060","6060","6060","6060","60C0","7F80","60C0","6060","6060","6060","6060","60C0","7F80","0000","0000","0000","0000","0000"},
    // 'M' (Index 15)
    {"0000","0000","0000","0000","6060","70E0","79E0","6F60","6660","6060","6060","6060","6060","6060","6060","6060","6060","6060","6060","0000","0000","0000","0000","0000"},
    // 'u' (Index 16)
    {"0000","0000","0000","0000","0000","0000","0000","0000","6060","6060","6060","6060","6060","6060","6060","6060","6060","6060","3060","1FE0","0000","0000","0000","0000"},
    // 't' (Index 17)
    {"0000","0000","0000","0000","0C00","0C00","0C00","0C00","3F80","0C00","0C00","0C00","0C00","0C00","0C00","0C00","0C00","0E00","07C0","0000","0000","0000","0000","0000"},
    // 'e' (Index 18)
    {"0000","0000","0000","0000","0000","0000","0000","0000","1FE0","3060","6060","6060","6060","7FE0","6000","6000","6000","3000","1FE0","0000","0000","0000","0000","0000"}
};


// --- Font Generation ---
static int8_t get_font24_index(char c) {
    if (c == ' ') return 0;
    if (c == '-') return 1;
    if (c == '.') return 2;
    if (c >= '0' && c <= '9') return 3 + (c - '0');
    if (c == 'd') return 13;
    if (c == 'B') return 14;
    if (c == 'M') return 15;
    if (c == 'u') return 16;
    if (c == 't') return 17;
    if (c == 'e') return 18;
    return -1;
}


static uint8_t hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}


static void generate_font(void) {
    memset(font24_data, 0, sizeof(font24_data));

    for (int ch = 0; ch < FONT24_CHARS; ch++) {
        for (int r = 0; r < 24; r++) {
            const char *hex = bdf_rows[ch][r];
            // Parse 4 hex chars into a 16-bit integer
            uint16_t val = (hex_char_to_val(hex[0]) << 12) |
                           (hex_char_to_val(hex[1]) << 8)  |
                           (hex_char_to_val(hex[2]) << 4)  |
                           (hex_char_to_val(hex[3]));
            
            // Map row to OLED column-major page format
            for (int c = 0; c < 12; c++) {
                // BDF pads to 16 bits, leftmost pixel is bit 15.
                // We need the top 12 bits (15 down to 4).
                if (val & (1 << (15 - c))) {
                    if (r < 8) {
                        font24_data[ch][c] |= (1 << r);           // Page 0
                    } else if (r < 16) {
                        font24_data[ch][c + 12] |= (1 << (r - 8)); // Page 1
                    } else {
                        font24_data[ch][c + 24] |= (1 << (r - 16));// Page 2
                    }
                }
            }
        }
    }
}


//
// Draw a 24x12 character at any (x,y) coordinate.
//
static void draw_char24(uint8_t x, uint8_t y, char c) {
    int8_t idx = get_font24_index(c);
    if (idx < 0) return;

    const uint8_t *glyph = font24_data[idx];
    uint8_t y_shift = y & 0x07;
    uint8_t start_page = y / 8;
    uint8_t pages_needed = (y_shift + 24 - 1) / 8 + 1;

    for (uint8_t col = 0; col < 12; col++) {
        uint8_t b0 = glyph[col];
        uint8_t b1 = glyph[col + 12];
        uint8_t b2 = glyph[col + 24];

        uint32_t src = ((uint32_t)b2 << 16) | ((uint32_t)b1 << 8) | b0;
        src <<= y_shift;

        for (uint8_t p = 0; p < pages_needed; p++) {
            uint8_t page = start_page + p;
            if (page >= 8) break;
            uint8_t byte_to_write = (src >> (p * 8)) & 0xFF;
            if (byte_to_write) {
                oled_buffer[page * OLED_WIDTH + x + col] |= byte_to_write;
            }
        }
    }
}


void oled_write_command(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; 
    oled_i2c_write(buf, 2);
}


// --- Initialization ---
void oled_init(void) {
    generate_font();
    memset(oled_buffer, 0, OLED_BUFFER_SIZE);

    // OLED Init Sequence
    oled_write_command(0xAE); // Display OFF
    oled_write_command(0x40); // Set Start Line
    oled_write_command(0xA1); // Segment Remap
    oled_write_command(0xC8); // COM Output Scan Dir
    oled_write_command(0x81); // Contrast
    oled_write_command(0xFF); 
    oled_write_command(0xA8); // Multiplex Ratio
    oled_write_command(0x3F); // 1/64 duty
    oled_write_command(0xA4); // Output follows RAM
    oled_write_command(0xA6); // Normal Display
    oled_write_command(0xD3); // Display Offset
    oled_write_command(0x00);
    oled_write_command(0xD5); // Display Clock Divide
    oled_write_command(0x80);
    oled_write_command(0xD9); // Pre-charge Period
    oled_write_command(0xF1);
    oled_write_command(0xDA); // COM Pins
    oled_write_command(0x12);
    oled_write_command(0xDB); // VCOMH Deselect
    oled_write_command(0x40);
    oled_write_command(0x8D); // Charge Pump
    oled_write_command(0x14); // Enable

    // Clear screen RAM before turning on
    for (uint8_t p = 0; p < 8; p++) {
        uint8_t cmd_buf[4] = {0x00, (uint8_t)(0xB0 + p), 0x02, 0x10};
        oled_i2c_write(cmd_buf, 4);
        
        uint8_t data_buf[129];
        data_buf[0] = 0x40;
        memset(&data_buf[1], 0, 128);
        oled_i2c_write(data_buf, 129);
    }

    oled_write_command(0xAF); // Display ON
}


void oled_off(void) {
    // 0xAE is the OLED/SSD1306 command to turn the display OFF (standby)
    oled_write_command(0xAE); 
}


// --- Fast Integer String Builder ---
static uint8_t format_volume(int16_t volume, char* buf) {
    uint8_t len = 0;
    int16_t vol = volume;

    if (vol < 0) {
        buf[len++] = '-';
        vol = -vol;
    } else {
        buf[len++] = ' ';
    }

    uint16_t int_part = vol / 10;
    uint16_t dec_part = vol % 10;

    if (int_part >= 100) {
        buf[len++] = '0' + (int_part / 100);
        int_part %= 100;
        buf[len++] = '0' + (int_part / 10);
        buf[len++] = '0' + (int_part % 10);
    } else if (int_part >= 10) {
        buf[len++] = '0' + (int_part / 10);
        buf[len++] = '0' + (int_part % 10);
    } else {
        buf[len++] = '0' + int_part;
    }

    buf[len++] = '.';
    buf[len++] = '0' + dec_part;
    buf[len++] = ' ';
    buf[len++] = 'd';
    buf[len++] = 'B';
    buf[len++] = '\0';

    return len - 1; 
}


// --- Main Update Function ---
void oled_display_update(int16_t volume, int8_t bar1, int8_t bar2) {
    // 1. Clear Buffer
    memset(oled_buffer, 0, OLED_BUFFER_SIZE);

    // 2. Draw VU Meters (Growing upwards from the bottom)
    if (bar1 < 0) bar1 = 0;
    if (bar1 > 63) bar1 = 63;
    if (bar2 < 0) bar2 = 0;
    if (bar2 > 63) bar2 = 63;

    for (int8_t p = 0; p < 8; p++) {
        uint8_t mask1 = 0x00;
        uint8_t mask2 = 0x00;
        
        int8_t bar1_in_page = bar1 - (7 - p) * 8;
        int8_t bar2_in_page = bar2 - (7 - p) * 8;
        
        if (bar1_in_page >= 8) {
            mask1 = 0xFF;
        } else if (bar1_in_page > 0) {
            mask1 = 0xFF ^ ((1 << (8 - bar1_in_page)) - 1);
        }
        
        if (bar2_in_page >= 8) {
            mask2 = 0xFF;
        } else if (bar2_in_page > 0) {
            mask2 = 0xFF ^ ((1 << (8 - bar2_in_page)) - 1);
        }

        for (uint8_t c = 0; c < 8; c++) {
            oled_buffer[p * 128 + c] = mask1;       // Left 8 columns
            oled_buffer[p * 128 + 120 + c] = mask2; // Right 8 columns
        }
    }

    // 3. Format String (Mute vs Numeric Volume)
    char str[10];
    uint8_t str_len;

    if (volume <= -9999) {
        // Mute condition
        str[0] = 'M'; str[1] = 'u'; str[2] = 't'; str[3] = 'e'; str[4] = '\0';
        str_len = 4;
    } else {
        // Normal volume condition
        str_len = format_volume(volume, str);
    }

    // 4. Draw Text (24px tall, centered vertically: (64 - 24) / 2 = 20)
    uint8_t x_start = (128 - (str_len * 12)) / 2;
    uint8_t y_start = 20;
    
    for (uint8_t i = 0; i < str_len; i++) {
        draw_char24(x_start + (i * 12), y_start, str[i]);
    }

    // 5. Transmit buffer using Page Addressing Mode
    static uint8_t tx_buf[OLED_WIDTH + 1];
    tx_buf[0] = 0x40; // Data stream

    for (uint8_t p = 0; p < 8; p++) {
        uint8_t cmd_buf[4];
        cmd_buf[0] = 0x00; // Command stream
        cmd_buf[1] = 0xB0 + p;      // Set Page address (0-7)
        cmd_buf[2] = 0x02;          // Set lower column address (Offset 2)
        cmd_buf[3] = 0x10;          // Set higher column address
        oled_i2c_write(cmd_buf, 4);
        
        memcpy(&tx_buf[1], &oled_buffer[p * OLED_WIDTH], OLED_WIDTH);
        oled_i2c_write(tx_buf, OLED_WIDTH + 1);
    }
}