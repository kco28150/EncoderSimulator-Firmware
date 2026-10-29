/**
  ******************************************************************************
  * @file    theme.c
  ******************************************************************************
  */
#include "theme.h"

static const Theme_t s_dark =
{
  .bg = 0x18E3, .card = 0x2945, .border = 0x39C7, .text = 0xF79E, .muted = 0xA534,
  .accent = 0x4E1F, .accent_text = 0x00C5, .danger = 0xD1A7, .danger_text = 0xFFFF,
  .success = 0x6E4B, .warning = 0xF585, .neutral = 0x8C71, .chart_bg = 0x2104,
};

static const Theme_t s_light =
{
  .bg = 0xF79E, .card = 0xFFFF, .border = 0xE71C, .text = 0x18C3, .muted = 0x5AEB,
  .accent = 0x0B77, .accent_text = 0xFFFF, .danger = 0xC143, .danger_text = 0xFFFF,
  .success = 0x0BC1, .warning = 0xC3C0, .neutral = 0x8C51, .chart_bg = 0xFFDF,
};

const Theme_t   *T    = &s_dark;
static ThemeId_t s_id = THEME_DARK;

void THEME_Set(ThemeId_t id)
{
  s_id = (id == THEME_LIGHT) ? THEME_LIGHT : THEME_DARK;
  T = (s_id == THEME_LIGHT) ? &s_light : &s_dark;
}

ThemeId_t THEME_Get(void) { return s_id; }
