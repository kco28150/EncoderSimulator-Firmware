/**
  ******************************************************************************
  * @file    cmd_parser.h
  * @brief   Interpreteur de commandes ASCII sur USART2 (VCP ST-Link, PuTTY)
  ******************************************************************************
  */
#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdint.h>
#include <stdbool.h>

void CMD_Init(void);      /* active la reception en IT et affiche la banniere */
void CMD_Process(void);   /* a appeler en boucle dans le while(1)             */
void CMD_UartIRQ(void);   /* a appeler depuis USART2_IRQHandler               */
void CMD_Notify(const char *msg); /* trace "[TACTILE] msg" dans le terminal */

/* Instant (HAL_GetTick) de la derniere commande recue, 0 si aucune.
 * *from_touch = true si elle venait du tactile (CMD_Notify), false si PuTTY.
 * Sert a l'indicateur d'activite de l'OLED. */
uint32_t CMD_LastActivity(bool *from_touch);

#endif /* CMD_PARSER_H */
