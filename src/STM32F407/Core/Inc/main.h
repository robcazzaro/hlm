/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

#include "stm32f4xx_ll_i2c.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_exti.h"
#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_pwr.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_tim.h"
#include "stm32f4xx_ll_usart.h"
#include "stm32f4xx_ll_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EncoderA_Pin LL_GPIO_PIN_0
#define EncoderA_GPIO_Port GPIOA
#define EncoderB_Pin LL_GPIO_PIN_1
#define EncoderB_GPIO_Port GPIOA
#define EncButton_Pin LL_GPIO_PIN_2
#define EncButton_GPIO_Port GPIOA
#define BOOT1_Pin LL_GPIO_PIN_2
#define BOOT1_GPIO_Port GPIOB
#define K2_Pin LL_GPIO_PIN_15
#define K2_GPIO_Port GPIOB
#define DE_RE_Pin LL_GPIO_PIN_8
#define DE_RE_GPIO_Port GPIOA
#define LED_Pin LL_GPIO_PIN_0
#define LED_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */
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

#define TX_RING_BUFFER_SIZE 2048
#define RX_BUFFER_SIZE 256

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
