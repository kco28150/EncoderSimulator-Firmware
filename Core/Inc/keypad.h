/**
  ******************************************************************************
  * @file    keypad.h
  * @brief   Pave numerique plein ecran pour la saisie d'une valeur
  ******************************************************************************
  */
#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*KP_Done_t)(double value);

void KP_Open(const char *title, const char *unit, double value,
             double vmin, double vmax, uint8_t decimals, KP_Done_t on_ok);
bool KP_Active(void);
void KP_Draw(void);                       /* dessin complet */
int  KP_Hit(uint16_t x, uint16_t y);      /* index de touche ou -1 */
void KP_DrawKey(int k, bool pressed);
bool KP_Key(int k);                       /* action ; false si le pave est ferme */

#endif /* KEYPAD_H */
