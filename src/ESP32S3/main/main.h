// 
// This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/HLM).
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

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

// enable only one of the following to change the app behavior
#define HLM_HUB         // to compile a simplified replacement of the GLM hub. 
//#define HLM_HEX_DUMP      // to dump all RS485 traffic as timestamp + hex codes
//#define HLM_ANALYZER    // prints timestamp + message decoded

// ensure only one is defined
#ifdef HLM_HUB
  #undef HLM_HEX_DUMP
  #undef HLM_ANALYZER
#endif

#ifdef HLM_HEX_DUMP
  #undef HLM_HUB
  #undef HLM_ANALYZER
#endif

#ifdef HLM_ANALYZER
  #undef HLM_HEX_DUMP
  #undef HLM_HUB
#endif

void button_click_callback(void);
void button_longpress_callback(void);
void button_doubleclick_callback(void);

#ifdef __cplusplus
}
#endif

#endif // __MAIN_H