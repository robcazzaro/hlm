# Homebrew Loudspeaker Manager

Welcome to the HLM documentation. The goal of this project is to enable limited control of any Genelec speaker with a [RS485 jack](https://support.genelec.com/hc/en-us/articles/360018933899-GLM-compatibility-with-Genelec-products), mostly [SAM speakers](https://www.genelec.com/key-technologies/smart-active-monitor-systems).

## ⚠️ Disclaimer & Trademark Notice

**Just to be clear:** This is an independent, open-source community project. It is **not** affiliated with, authorized, funded, or endorsed by Genelec Oy. 

* "Genelec" and "GLM" (Genelec Loudspeaker Manager) are registered trademarks of Genelec Oy. 
* This project is a standalone, alternative implementation created via reverse-engineering for hobbyist use. It does not contain any official Genelec software or proprietary code.
* Because this is a DIY tool, it comes with absolutely no warranties. Use it at your own risk! (Though your Genelec speakers have great built-in hardware protection, I'm still not responsible if you accidentally blast white noise at 2:00 AM). It has been tested only with two 8320A and it's not guaranteed to work with other speaker models or even a different number of speakers. The use of this project implies that you are knowledgeable in basic electronics and software engineering, and will implement changes as needed for your configuration.
* All errors are mine, and mine only. Genelec's own [GLM](https://www.genelec.com/glm-kit) offers significantly more functionality and allows full speaker calibration and subwoofer integration into any room. 
* The main goal of this project is to enable volume control (including mute and standby) of speakers not continuously connected to a GLM. If you own Genelec speakers, you owe it to yourself to get a GLM Calibration kit to get the best from your system. If you have multiple speakers in separate rooms, the HLM will allow you to control your speakers independently.
<br>

The GLM RS485 protocol has been reverse engineered with the help of LLMs, and I created a [GLM protocol spec](https://github.com/robcazzaro/hlm/blob/main/docs/Genelec%20GLM%20Protocol.12.md).<br>

**I recently also discovered an independent parallel effort, complementary to this one. The author of that project and I started collaborating. I highly recommend looking at [espgensam](https://github.com/markbergsma/espgensam), especially if you are interested in a ESPHome solution with Home Assistant integration.**

I only own a pair of 8320A speakers, so I could only reverse engineer part of the protocol. I'm aware of differences in higher end speakers (e.g. 83x1 with 96 kHz DSP). For areas where a command or parameter is not clear, I added a confidence factor. The code in the enclosed repository can be compiled as a control hub (default), protocol decoder or a protocol hex dump, allowing easy capture of unknown packets and future decoding.

## Code structure
The code in this repository is structured as a set of libraries and hub functionality. The libraries implement the complete specs as documented and allow full decoding and sending of messages. A separate library implements the functionality for the replacement hub. I wrote and tested versions for the STM32F103 Bluepill, STM32F407, ESP32-C6, ESP32-S3. The code also supports an I2C SH1106 OLED, and encoder volume+button. To build a hub or an analyzer, you will also need a MAX485 board and an RJ45 jack. The current version uses individual OLED and encoders or integrated OLED/encoder board like <br><br>
<img src="images/I2C_Encoder.jpg?raw=true" alt="I2C Encoder" style="max-width: 50%; height: auto;">

The code is structured with a common GLM library functionality (library and hub). Each processor is a separate VSCode workspace. Each processor has a different main.c, main.h and a set of BSP (Board Support Package) files to hide the processor differences from the libraries. The STM32 processors each have a BSP, while the ESP32 has a common set of files and a separate BSP for each processor. The rs485_9n2 library is written to work on C6 and S3. I didn't test with other processors, but it should be pretty easy to make it work across all processors. Even if there are common BSP files for all ESP32 processors, the SDK differences are too big to bridge, and it's simpler to have a separate workspace for each processor

In main.h I define one of the 3 settings:

```
// enable only one of the following to change the app behavior<br>
#define HLM_HUB         // to compile a simplified replacement of the GLM hub.<br>
//#define HLM_HEX_DUMP      // to dump all RS485 traffic as timestamp + hex codes<br>
//#define HLM_ANALYZER    // prints timestamp + message decoded<br>
```

* If HLM_HUB is defined, the device works as a GLM interface replacement, with volume control and oled UI. There's no data output on the USB CDC interface<br>
* If HLM_HEX_DUMP is defined, the device connected to a GLM RS485 bus will print on the serial port (USB CDC) a timestamp, packet length and the payload in hex format. It will report CRC errors (with received vs expected values). This mode is particularly helpful to decode commands not yet reverse engineered<br>
* If HLM_ANALYZER is defined, the device will print the received messages on the serial port (USB CDC) in a human readable format<br>

## RS485 9N2 @296 kbps
One of the peculiarities of the Genelec RS485 protocol is the use of a 9-bit data frame (9N2) like a Multidrop bus, with the 9th bit indicating an address or data byte. Few microprocessors support 9 bits, and even fewer do so at the required 296 kBps rate. That ruled out the ubiquitous 8 bit Arduino board (9 bit capable, but not fast enough) and the ESP32 boards, initially. The Raspberry Pico and Pico 2 could support 9 bit using a custom PIO driver, but I'm not familiar enough with the PIO to use it. 

## STM32 support
The project has been created with STM32CubeMx and the STM32 VSCode extensions. I provide the *.ioc file for both a [Bluepill STM32F103](https://stm32-base.org/boards/STM32F103C8T6-Blue-Pill.html) and a [DIY More STM32F407VGT6](https://stm32-base.org/boards/STM32F407VGT6-diymore.html) board. Using STM32CubeIDE it can easily be ported to most STM32Fxx boards.
I'm using the LL libraries as much as possible. I hate the HAL with a burning passion and it's way too slow for the STM32F103. Only USB requires the HAL.
The STM32F103 Bluepill was chosen as a cheap, readily available board, but it's marginally too slow for full speed packet capture, even if it works well as a hub. It works perfectly well with the STM32F407, which also happens to be the same processor in the GLM interface. Given the similarities between processors in the STM32 family (and all support 9N2), it would be trivial to add more processors.

## ESP32 support
The code is written using IDF v6.0.2 and the IDF extensions for VSCode. IDF v6 is significantly different from v5, so if you need to use v5, be prepared for a lot of changes.
Natively, the ESP32 family does not support 9 bit serial. I implemented a pair of clever hacks (if I say so myself). For sending, I use the RMT peripheral to send 9N2 serial. The RMT in sending mode is extremely reliable on all the processors I tested. Alas, on processors like the C6, the lack of RMT DMA makes RMT reception unreliable for smaller packets, and impossible for longer packets. So, I'm using the UART peripheral in a non-standard way and using the frame error flag to deduce the value of the 9th bit (1 for addresses or 0 for data). It's implemented as a separate library because it might be help for other projects using 9 bit serial and Multidrop bus. So far, it's been reliable

Sooner or later, I plan to write an extension for Home Assistant. But, for now, there's no BLE or WiFi functionality enabled even if it should not be difficult to implement

## Physical implementation
At the moment, the device is a rat's nest and unfit for serious use. I might design a PCB and a 3D printed enclosure to improve usability and reliability. 

<img src="images/stm32f103.jpg?raw=true" alt="stm32f103" style="max-width: 100%; height: auto;">
<img src="images/stm32f407_integrated.jpg?raw=true" alt="stm32f407" style="max-width: 100%; height: auto;">

The schematic is similarly poorly documented. 

<img src="images/stm32f103-hlm.png?raw=true" alt="stm32_schematic" style="max-width: 100%; height: auto;">
<img src="images/esp32-s3-hlm.png?raw=true" alt="esp32_schematic" style="max-width: 100%; height: auto;">

The MAX485 module should be powered by 5V, and it is for the STM32 implementation. The STM32 pins are mostly 5V tolerant. The ESP32 5V tolerance is a matter of much debate. The safe option is to use a level shifter. In my case, I have been powering the cheap MAX485 boards with 3.3V and, at least for the modules I have, that seems to work just fine and it has been reliable. Of course, that's outside the MAX485 voltage requirement, and might not work for you. In that case, you can decide if you prefer to use level shifters or risk testing the ESP32 5V tolerance.

Even if the current code implements only controls volume (the encoder), mute/unmute (clicking on the encoder button) and standby/resume (long press on the encoder button), the glm_library supports decoding and sending the full 20 bands PEQ filters. In theory, nothing prevents using REW to measure a Genelec speaker in a room, create the appropriate PEQ filters, and send those filters to the speaker for use in the internal DSP (not implemented). The protocol specs also fully document the format and how to implement the filters. This of course only works for 48 kHz DSPs and not for higher end speakers with 96 kHz DSPs. If you want to loan or donate a 83x1 speaker, I won't say no.

In the library, there is also functionality to save settings (including the PEQ filters) in the speaker flash memory

## UI design
The OLED screen shows the volume, the mute/unmute status and two VU bars on the side. There's no guarantee that the left VU shows the data from the left speaker (but a VU bar always shows the same speaker). Also, the current code handles more than 2 speakers, but the VU bars will only show the first 2 enumerated speakers. The VU scaling has been eyeballed, so it might need adjustments. When in mute (button press), the UI will show "Mute". When the system is in standby, the screen is off but, just like the OEM hub, it keeps sending messages. If the speakers stop receiving music for more than 30 seconds, the screen will go off and the device will go into "ISS mode" to let the speakers go into ISS on their own. When the music restarts, the speakers will turn on but the device won't. Any action (encoder rotation, button press) will wake up the device

<br>

*This file is part of the Homebrew Loudspeaker Manager distribution (https://github.com/robcazzaro/hlm).<br>
Copyright (c) 2026 Rob Cazzaro.*
