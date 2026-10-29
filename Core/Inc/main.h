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
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define Touch_CLK_Pin GPIO_PIN_0
#define Touch_CLK_GPIO_Port GPIOC
#define Touch_CS_Pin GPIO_PIN_1
#define Touch_CS_GPIO_Port GPIOC
#define Touch_MISO_Pin GPIO_PIN_2
#define Touch_MISO_GPIO_Port GPIOC
#define Touch_MOSI_Pin GPIO_PIN_3
#define Touch_MOSI_GPIO_Port GPIOC
#define Touch_IRQ_Pin GPIO_PIN_4
#define Touch_IRQ_GPIO_Port GPIOC
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define LD2_Pin GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define ENC_A_Pin GPIO_PIN_8
#define ENC_A_GPIO_Port GPIOA
#define ENC_B_Pin GPIO_PIN_9
#define ENC_B_GPIO_Port GPIOA
#define Screen_DCRS_Pin GPIO_PIN_1
#define Screen_DCRS_GPIO_Port GPIOB
#define Screen_CS_Pin GPIO_PIN_10
#define Screen_CS_GPIO_Port GPIOB
#define Screen_RST_Pin GPIO_PIN_12
#define Screen_RST_GPIO_Port GPIOB
#define Screen_SCK_Pin GPIO_PIN_13
#define Screen_SCK_GPIO_Port GPIOB
#define Screen_MISO_Pin GPIO_PIN_14
#define Screen_MISO_GPIO_Port GPIOB
#define Screen_MOSI_Pin GPIO_PIN_15
#define Screen_MOSI_GPIO_Port GPIOB
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define OLED_SCL_Pin GPIO_PIN_8
#define OLED_SCL_GPIO_Port GPIOB
#define OLED_SDA_Pin GPIO_PIN_9
#define OLED_SDA_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
