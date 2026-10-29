/**
  ******************************************************************************
  * @file    keypad.c
  * @brief   7 8 9 <-  /  4 5 6 C  /  1 2 3 ESC  /  . [  0  ] OK
  ******************************************************************************
  */
#include "keypad.h"
#include "gui.h"
#include "tft.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>

#define BUF_MAX 10

enum { K_7, K_8, K_9, K_BS, K_4, K_5, K_6, K_C, K_1, K_2, K_3, K_ESC, K_DOT, K_0, K_OK, K_N };

static const GRect s_keys[K_N] =
{
  {  4,  66, 72, 40 }, { 84,  66, 72, 40 }, { 164,  66, 72, 40 }, { 244,  66, 72, 40 },
  {  4, 110, 72, 40 }, { 84, 110, 72, 40 }, { 164, 110, 72, 40 }, { 244, 110, 72, 40 },
  {  4, 154, 72, 40 }, { 84, 154, 72, 40 }, { 164, 154, 72, 40 }, { 244, 154, 72, 40 },
  {  4, 198, 72, 40 }, { 84, 198,152, 40 },                       { 244, 198, 72, 40 },
};
static const char *const s_lbl[K_N] =
{ "7", "8", "9", "<-", "4", "5", "6", "C", "1", "2", "3", "ESC", ".", "0", "OK" };

static const GRect s_disp = { 4, 24, 312, 38 };

static bool       s_active = false;
static char       s_buf[BUF_MAX + 1];
static bool       s_fresh;          /* la premiere touche remplace la valeur */
static bool       s_err;
static const char *s_title, *s_unit;
static double     s_min, s_max;
static uint8_t    s_dec;
static KP_Done_t  s_cb;

static void fmt_val(char *b, size_t sz, double v, uint8_t dec)
{
  /* format sans %f (newlib-nano) */
  long scale = 1; for (uint8_t i = 0; i < dec; i++) scale *= 10;
  bool neg = v < 0; if (neg) v = -v;
  long n = (long)(v * scale + 0.5);
  if (dec == 0) snprintf(b, sz, "%s%ld", neg ? "-" : "", n);
  else
  {
    char f[8]; long fp = n % scale;
    for (int i = dec - 1; i >= 0; i--) { f[i] = (char)('0' + fp % 10); fp /= 10; }
    f[dec] = '\0';
    snprintf(b, sz, "%s%ld.%s", neg ? "-" : "", n / scale, f);
    /* retire les zeros inutiles : 300.00 -> 300 ; 2.50 -> 2.5 */
    size_t L = strlen(b);
    while (L && b[L - 1] == '0') b[--L] = '\0';
    if (L && b[L - 1] == '.') b[--L] = '\0';
  }
}

static double parse(const char *s, bool *ok)
{
  double v = 0, sc = 1; bool frac = false, dig = false;
  for (; *s; s++)
  {
    if (*s >= '0' && *s <= '9')
    { dig = true; if (frac) { sc *= 0.1; v += (*s - '0') * sc; } else v = v * 10 + (*s - '0'); }
    else if (*s == '.' && !frac) frac = true;
    else { *ok = false; return 0; }
  }
  *ok = dig;
  return v;
}

static void draw_display(void)
{
  uint16_t bg = T->card;
  GUI_Field(&s_disp, s_buf[0] ? s_buf : "0", 3, s_unit, false);
  if (s_err) GUI_Frame(&s_disp, T->danger);
  if (s_fresh) GUI_Text((uint16_t)(s_disp.x + 6), (uint16_t)(s_disp.y + 4), "actuel", T->muted, bg, 1);
}

static void draw_header(void)
{
  char a[16], b[16], r[40];
  TFT_FillRect(0, 0, TFT_WIDTH, 22, T->card);
  TFT_FillRect(0, 21, TFT_WIDTH, 1, T->border);
  GUI_Text(6, 3, s_title, T->text, T->card, 2);
  fmt_val(a, sizeof a, s_min, s_dec);
  fmt_val(b, sizeof b, s_max, s_dec);
  snprintf(r, sizeof r, "%s..%s", a, b);
  GUI_Text((uint16_t)(TFT_WIDTH - 6 - GUI_TextW(r, 1)), 8, r, s_err ? T->danger : T->muted, T->card, 1);
}

void KP_Open(const char *title, const char *unit, double value,
             double vmin, double vmax, uint8_t decimals, KP_Done_t on_ok)
{
  s_title = title; s_unit = unit; s_min = vmin; s_max = vmax; s_dec = decimals; s_cb = on_ok;
  fmt_val(s_buf, sizeof s_buf, value, decimals);
  s_fresh = true; s_err = false; s_active = true;
}

bool KP_Active(void) { return s_active; }

void KP_DrawKey(int k, bool pressed)
{
  GStyle_t st = (k == K_OK) ? GST_ACCENT : (k == K_ESC) ? GST_DANGER : GST_NORMAL;
  GUI_Button(&s_keys[k], s_lbl[k], 2, st, GIC_NONE, pressed);
}

void KP_Draw(void)
{
  TFT_FillScreen(T->bg);
  draw_header();
  draw_display();
  for (int k = 0; k < K_N; k++) KP_DrawKey(k, false);
}

int KP_Hit(uint16_t x, uint16_t y)
{
  for (int k = 0; k < K_N; k++) if (GUI_Hit(&s_keys[k], x, y)) return k;
  return -1;
}

bool KP_Key(int k)
{
  size_t L = strlen(s_buf);
  bool was_err = s_err;
  s_err = false;

  switch (k)
  {
    case K_ESC: s_active = false; return false;
    case K_OK:
    {
      bool ok; double v = parse(s_buf, &ok);
      if (!ok || v < s_min || v > s_max) { s_err = true; break; }
      s_active = false;
      if (s_cb) s_cb(v);
      return false;
    }
    case K_C:  s_buf[0] = '\0'; s_fresh = false; break;
    case K_BS:
      if (s_fresh) { s_buf[0] = '\0'; s_fresh = false; }
      else if (L) s_buf[L - 1] = '\0';
      break;
    case K_DOT:
      if (s_fresh) { strcpy(s_buf, "0."); s_fresh = false; break; }
      if (s_dec && !strchr(s_buf, '.') && L < BUF_MAX) { if (!L) s_buf[L++] = '0'; s_buf[L++] = '.'; s_buf[L] = '\0'; }
      break;
    default:
    {
      static const char dg[K_N] = { '7','8','9',0,'4','5','6',0,'1','2','3',0,0,'0',0 };
      char c = dg[k];
      if (!c) break;
      if (s_fresh) { s_buf[0] = '\0'; L = 0; s_fresh = false; }
      const char *dot = strchr(s_buf, '.');
      if (dot && (size_t)(s_buf + L - dot) > s_dec) break;  /* trop de decimales */
      if (L == 1 && s_buf[0] == '0') L = 0;                  /* pas de 0 en tete */
      if (L < BUF_MAX) { s_buf[L++] = c; s_buf[L] = '\0'; }
      break;
    }
  }
  if (s_err != was_err) draw_header();
  draw_display();
  return true;
}
