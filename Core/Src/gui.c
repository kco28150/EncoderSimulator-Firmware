/**
  ******************************************************************************
  * @file    gui.c
  ******************************************************************************
  */
#include "gui.h"
#include "tft.h"
#include "theme.h"
#include <string.h>

bool GUI_Hit(const GRect *r, uint16_t x, uint16_t y)
{
  return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

uint16_t GUI_TextW(const char *s, uint8_t scale)
{
  return (uint16_t)(strlen(s) * TFT_CHAR_W(scale));
}

void GUI_Text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
  TFT_DrawString(x, y, s, fg, bg, scale);
}

void GUI_TextCenter(const GRect *r, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
  uint16_t w = GUI_TextW(s, scale), h = (uint16_t)TFT_CHAR_H(scale);
  uint16_t x = (uint16_t)(r->x + ((r->w > w) ? (r->w - w) / 2u : 0u));
  uint16_t y = (uint16_t)(r->y + ((r->h > h) ? (r->h - h + 1u) / 2u : 0u));
  TFT_DrawString(x, y, s, fg, bg, scale);
}

void GUI_Triangle(uint16_t cx, uint16_t ytop, uint8_t h, bool up, uint16_t color)
{
  if (h < 2u) return;
  uint8_t hw = (uint8_t)(h / 2u);
  for (uint8_t k = 0; k < h; k++)
  {
    uint8_t row = up ? k : (uint8_t)(h - 1u - k);           /* k=0 : pointe */
    uint8_t half = (uint8_t)((uint16_t)row * hw / (h - 1u));
    TFT_FillRect((uint16_t)(cx - half), (uint16_t)(ytop + k), (uint16_t)(2u * half + 1u), 1, color);
  }
}

void GUI_Frame(const GRect *r, uint16_t c)
{
  TFT_FillRect(r->x, r->y, r->w, 1, c);
  TFT_FillRect(r->x, (uint16_t)(r->y + r->h - 1u), r->w, 1, c);
  TFT_FillRect(r->x, r->y, 1, r->h, c);
  TFT_FillRect((uint16_t)(r->x + r->w - 1u), r->y, 1, r->h, c);
}

static void style_colors(GStyle_t st, bool pressed, uint16_t *bg, uint16_t *fg, uint16_t *bd)
{
  switch (st)
  {
    case GST_ACCENT:   *bg = T->accent;  *fg = T->accent_text; *bd = T->accent;  break;
    case GST_DANGER:   *bg = T->danger;  *fg = T->danger_text; *bd = T->danger;  break;
    case GST_TAB_ON:   *bg = T->bg;      *fg = T->accent;      *bd = T->accent;  break;
    case GST_TAB:      *bg = T->card;    *fg = T->muted;       *bd = T->border;  break;
    case GST_SELECTED: *bg = T->accent;  *fg = T->accent_text; *bd = T->accent;  break;
    case GST_FIELD:    *bg = T->card;    *fg = T->text;        *bd = T->accent;  break;
    case GST_NORMAL:
    default:           *bg = T->card;    *fg = T->text;        *bd = T->border;  break;
  }
  if (pressed)
  {
    if (st == GST_DANGER || st == GST_ACCENT || st == GST_SELECTED)
    { *bg = T->text; *fg = T->bg; *bd = T->text; }
    else
    { *bg = T->accent; *fg = T->accent_text; *bd = T->accent; }
  }
}

void GUI_Button(const GRect *r, const char *label, uint8_t scale,
                GStyle_t style, GIcon_t icon, bool pressed)
{
  uint16_t bg, fg, bd;
  style_colors(style, pressed, &bg, &fg, &bd);

  TFT_FillRect(r->x, r->y, r->w, r->h, bg);
  if (style == GST_TAB_ON)
    TFT_FillRect(r->x, r->y, r->w, 3, bd);                  /* soulignement de l'onglet actif */
  else
    GUI_Frame(r, bd);

  uint16_t th = (uint16_t)TFT_CHAR_H(scale);
  uint16_t tw = GUI_TextW(label, scale);
  uint8_t  ih = (uint8_t)(th * 3u / 4u);                    /* hauteur du triangle */
  uint16_t iw = (icon != GIC_NONE) ? (uint16_t)(ih + 1u + (tw ? 6u : 0u)) : 0u;
  uint16_t x0 = (uint16_t)(r->x + ((r->w > tw + iw) ? (r->w - tw - iw) / 2u : 2u));
  uint16_t yt = (uint16_t)(r->y + ((r->h > th) ? (r->h - th + 1u) / 2u : 0u));

  if (icon != GIC_NONE)
    GUI_Triangle((uint16_t)(x0 + ih / 2u), (uint16_t)(yt + (th - ih) / 2u), ih, icon == GIC_UP, fg);
  if (tw)
    TFT_DrawString((uint16_t)(x0 + iw), yt, label, fg, bg, scale);
}

void GUI_Field(const GRect *r, const char *value, uint8_t scale, const char *unit, bool pressed)
{
  uint16_t bg, fg, bd;
  style_colors(GST_FIELD, pressed, &bg, &fg, &bd);
  TFT_FillRect(r->x, r->y, r->w, r->h, bg);
  GUI_Frame(r, bd);

  uint16_t uw = unit ? GUI_TextW(unit, 2) : 0u;
  uint16_t vw = GUI_TextW(value, scale);
  uint16_t xr = (uint16_t)(r->x + r->w - 6u - (uw ? uw + 6u : 0u));
  uint16_t vy = (uint16_t)(r->y + (r->h - TFT_CHAR_H(scale) + 1u) / 2u);
  if (vw < xr - r->x) TFT_DrawString((uint16_t)(xr - vw), vy, value, fg, bg, scale);
  if (uw)
  {
    uint16_t uy = (uint16_t)(vy + TFT_CHAR_H(scale) - TFT_CHAR_H(2));
    TFT_DrawString((uint16_t)(r->x + r->w - 6u - uw), uy, unit, pressed ? fg : T->muted, bg, 2);
  }
}

void GUI_TextSet(GText_t *f, const char *txt, uint16_t fg, uint16_t bg)
{
  char s[GTEXT_MAX + 1];
  uint8_t w = (f->width > GTEXT_MAX) ? GTEXT_MAX : f->width;
  size_t n = strlen(txt);
  if (n > w) n = w;
  memcpy(s, txt, n);
  memset(s + n, ' ', w - n);
  s[w] = '\0';

  bool all = (fg != f->fg) || (bg != f->bg);
  f->fg = fg; f->bg = bg;
  for (uint8_t i = 0; i < w; i++)
  {
    if (all || s[i] != f->cur[i])
    {
      TFT_DrawChar((uint16_t)(f->x + i * TFT_CHAR_W(f->scale)), f->y, s[i], fg, bg, f->scale);
      f->cur[i] = s[i];
    }
  }
}

void GUI_TextReset(GText_t *f) { memset(f->cur, 0xFF, sizeof f->cur); }
