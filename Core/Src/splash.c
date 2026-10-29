/**
  ******************************************************************************
  * @file    splash.c
  * @brief   Ecran de demarrage APAGEO, 320 x 240 paysage :
  *          logo centre sur toute la hauteur disponible, nom centre en bas.
  ******************************************************************************
  */
#include "splash.h"
#include "splash_img.h"
#include "tft.h"

#define APAGEO_BLUE    0x0C37   /* RGB (14,132,188), couleur du logo     */
#define APAGEO_LIGHT   0x2D1B   /* bleu eclairci, lisible sur fond sombre */
#define NIGHT_BLUE     0x08C5   /* fond bleu nuit                         */

/* Mise en page (identique pour les 3 styles) */
#define TOP_MARGIN     6
#define BOTTOM_MARGIN  8
#define NAME_Y         (TFT_HEIGHT - BOTTOM_MARGIN - SPLASH_NAME_H)
#define LOGO_Y         TOP_MARGIN
#define BAR_Y          (TFT_HEIGHT - 4)
#define BAR_H          2
#define BAR_W          SPLASH_NAME_W
#define BAR_X          ((TFT_WIDTH - BAR_W) / 2)

#if   SPLASH_STYLE == 2
  #define C_BG    APAGEO_BLUE
  #define C_LOGO  WHITE
  #define C_NAME  WHITE
#elif SPLASH_STYLE == 3
  #define C_BG    NIGHT_BLUE
  #define C_LOGO  APAGEO_LIGHT
  #define C_NAME  WHITE
  #define C_BAR   APAGEO_LIGHT
#else
  #define C_BG    WHITE
  #define C_LOGO  APAGEO_BLUE
  #define C_NAME  APAGEO_BLUE
#endif

void SPLASH_Draw(void)
{
  TFT_FillScreen(C_BG);
  TFT_DrawAlpha4((TFT_WIDTH - SPLASH_LOGO_W) / 2, LOGO_Y,
                 SPLASH_LOGO_W, SPLASH_LOGO_H, splash_logo_a4, C_LOGO, C_BG);
  TFT_DrawAlpha4((TFT_WIDTH - SPLASH_NAME_W) / 2, NAME_Y,
                 SPLASH_NAME_W, SPLASH_NAME_H, splash_name_a4, C_NAME, C_BG);
}

void SPLASH_Progress(uint32_t elapsed)
{
#if SPLASH_STYLE == 3
  static uint16_t done = 0;
  if (elapsed > SPLASH_MS) elapsed = SPLASH_MS;
  uint16_t px = (uint16_t)((elapsed * BAR_W) / SPLASH_MS);
  if (px > done)
  {
    TFT_FillRect((uint16_t)(BAR_X + done), BAR_Y, (uint16_t)(px - done), BAR_H, C_BAR);
    done = px;
  }
#else
  (void)elapsed;
#endif
}
