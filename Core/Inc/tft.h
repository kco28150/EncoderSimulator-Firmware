/**
  ******************************************************************************
  * @file    tft.h
  * @brief   Driver ILI9341 en C (porte de TFTpp/Core/Lib/ILI9341) + texte.
  *          SPI2 : PB13 SCK, PB14 MISO, PB15 MOSI
  *          PB1 = DC (Screen_DCRS), PB10 = CS (Screen_CS), PB12 = RST (Screen_RST)
  ******************************************************************************
  */
#ifndef TFT_H
#define TFT_H

#include <stdint.h>

#define TFT_WIDTH    320     /* paysage (SCREEN_HORIZONTAL_1) */
#define TFT_HEIGHT   240

/* Rotations (memes valeurs que ILI9341.h) */
#define SCREEN_VERTICAL_1    0
#define SCREEN_HORIZONTAL_1  1
#define SCREEN_VERTICAL_2    2
#define SCREEN_HORIZONTAL_2  3

/* Couleurs RGB565 (memes valeurs que ILI9341.h) */
#define BLACK       0x0000
#define NAVY        0x000F
#define DARKGREEN   0x03E0
#define DARKCYAN    0x03EF
#define MAROON      0x7800
#define PURPLE      0x780F
#define OLIVE       0x7BE0
#define LIGHTGREY   0xC618
#define DARKGREY    0x7BEF
#define BLUE        0x001F
#define GREEN       0x07E0
#define CYAN        0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define WHITE       0xFFFF
#define ORANGE      0xFD20
#define GREENYELLOW 0xAFE5

/* Police 5x7 dans une cellule 6x8, agrandie par 'scale' (1..4) */
#define TFT_CHAR_W(scale)  (6u * (scale))
#define TFT_CHAR_H(scale)  (8u * (scale))

void TFT_Init(void);
void TFT_SetRotation(uint8_t rotation);
void TFT_FillScreen(uint16_t color);
void TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void TFT_HLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color);
void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
void TFT_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

/* Image monochrome antialiasee : masque alpha 4 bits (2 px/octet, poids fort a
 * gauche), melange fg/bg. Largeur max 768 px. */
void TFT_DrawAlpha4(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const uint8_t *a4, uint16_t fg, uint16_t bg);

#endif /* TFT_H */
