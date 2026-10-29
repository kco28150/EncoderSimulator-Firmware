/**
  ******************************************************************************
  * @file    theme.h
  * @brief   Palettes sombre / claire reprises de l'appli WPF EncoderSimulator
  *          (Themes/Palette.Dark.xaml et Palette.Light.xaml), en RGB565.
  ******************************************************************************
  */
#ifndef THEME_H
#define THEME_H

#include <stdint.h>

typedef struct
{
  uint16_t bg, card, border, text, muted;
  uint16_t accent, accent_text, danger, danger_text;
  uint16_t success, warning, neutral, chart_bg;
} Theme_t;

typedef enum { THEME_DARK = 0, THEME_LIGHT = 1 } ThemeId_t;

extern const Theme_t *T;            /* theme courant */

void      THEME_Set(ThemeId_t id);
ThemeId_t THEME_Get(void);

#endif /* THEME_H */
