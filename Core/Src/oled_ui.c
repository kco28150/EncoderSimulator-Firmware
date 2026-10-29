/**
  ******************************************************************************
  * @file    oled_ui.c
  * @brief   Tableau de bord OLED (voir oled_ui.h pour la disposition).
  *
  *  Toute l'image est redessinee dans le tampon RAM a chaque rafraichissement
  *  puis envoyee par DMA : pas de gestion de zones "sales", le cout CPU est
  *  negligeable (~1 ms) et l'I2C travaille en tache de fond.
  ******************************************************************************
  */
#include "oled_ui.h"
#include "ssd1306.h"
#include "ssd1306_splash_img.h"
#include "encoder_sim.h"
#include "scenario.h"
#include "cmd_parser.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static bool     s_splash = false;     /* splash en cours                     */
static uint32_t s_t0;                 /* debut du splash (ms)                */
static uint32_t s_last;               /* dernier rafraichissement (ms)       */
static uint32_t s_last_try;           /* derniere tentative de detection (ms) */

/* ========================================================================== */
/* Formatage (newlib-nano n'a pas %f)                                         */
/* ========================================================================== */

/* v avec 'dec' decimales (0..3), signe '-' seulement si non nul a l'affichage */
static void fmt_fix(char *b, size_t n, double v, unsigned dec)
{
  static const uint32_t p10[] = { 1u, 10u, 100u, 1000u };
  bool     neg = v < 0.0;
  double   a   = neg ? -v : v;
  if (a > 4.0e6) a = 4.0e6;                         /* borne : tient sur 32 bits */
  uint32_t q   = (uint32_t)(a * (double)p10[dec] + 0.5);
  uint32_t ip  = q / p10[dec], fp = q % p10[dec];
  neg = neg && q != 0u;
  if (dec == 0u) snprintf(b, n, "%s%lu", neg ? "-" : "", (unsigned long)ip);
  else           snprintf(b, n, "%s%lu.%0*lu", neg ? "-" : "", (unsigned long)ip, (int)dec, (unsigned long)fp);
}

/* Texte aligne a droite sur l'abscisse x_right (exclue) */
static void text_right(int16_t x_right, int16_t y, const char *s, uint8_t scale)
{
  SSD1306_Text((int16_t)(x_right - (int16_t)SSD1306_TextWidth(s, scale)), y, s, scale, SSD1306_ON);
}

/* Barre de progression : contour + remplissage proportionnel (0..1000 = 0..100 %) */
static void progress_bar(int16_t x, int16_t y, int16_t w, int16_t h, uint32_t permil)
{
  if (permil > 1000u) permil = 1000u;
  SSD1306_Rect(x, y, w, h, SSD1306_ON);
  SSD1306_FillRect((int16_t)(x + 2), (int16_t)(y + 2), (int16_t)((int32_t)(w - 4) * (int32_t)permil / 1000), (int16_t)(h - 4), SSD1306_ON);
}

/* ========================================================================== */
/* Ecrans                                                                     */
/* ========================================================================== */

/* Splash : nom APAGEO centre dans la bande jaune, logo a gauche dans le bleu,
 * libelle + adresse I2C detectee + barre d'attente a droite. */
static void draw_splash(uint32_t el)
{
  char a[16];

  SSD1306_Clear();
  SSD1306_Bitmap((int16_t)((SSD1306_WIDTH - OLED_NAME_W) / 2), 1, OLED_NAME_W, OLED_NAME_H, oled_name, SSD1306_ON);
  SSD1306_Bitmap(4, (int16_t)(SSD1306_HEIGHT - OLED_LOGO_H - 1), OLED_LOGO_W, OLED_LOGO_H, oled_logo, SSD1306_ON);

  SSD1306_Text(50, 20, "Simulateur", 1, SSD1306_ON);
  SSD1306_Text(50, 30, "de codeur", 1, SSD1306_ON);
  snprintf(a, sizeof a, "I2C 0x%02X", (unsigned)SSD1306_Address());
  SSD1306_Text(50, 42, a, 1, SSD1306_ON);
  progress_bar(50, 55, 76, 7, el * 1000u / OLED_SPLASH_MS);
}

/* Tableau de bord */
static void draw_dash(uint32_t now)
{
  char a[24], b[32];
  ENC_Mode_t mode = ENC_GetMode();
  uint16_t   scn  = SCN_Running();
  double     ppcm = ENC_GetPulsesPerCm();
  double     hz   = (mode == ENC_IDLE) ? 0.0 : ENC_GetHz();

  SSD1306_Clear();

  /* ---- Bande jaune : mode + sens + activite ---- */
  if (scn)                    snprintf(a, sizeof a, "SCEN%u", (unsigned)scn);
  else if (mode == ENC_MOVE)  strcpy(a, "MOVE");
  else if (mode == ENC_RUN)   strcpy(a, "RUN");
  else                        strcpy(a, "ARRET");
  int16_t x = SSD1306_Text(0, 1, a, 2, SSD1306_ON);

  /* Fleche : bas = descente (position croissante), haut = remontee.
   * ENC_DIR_INVERT ne change que les signaux A/B, pas le sens logique. */
  if (mode != ENC_IDLE)
    SSD1306_Triangle((int16_t)(x + 6), 2, 12, ENC_GetDir() < 0, SSD1306_ON);

  bool from_touch;
  uint32_t act = CMD_LastActivity(&from_touch);
  if (act != 0u && (now - act) < OLED_ACTIVITY_MS)
  {
    const char *src = from_touch ? "TAC" : "PC";
    int16_t w = (int16_t)(SSD1306_TextWidth(src, 1) + 3);
    SSD1306_FillRect((int16_t)(SSD1306_WIDTH - w), 3, w, 10, SSD1306_ON);
    SSD1306_Text((int16_t)(SSD1306_WIDTH - w + 2), 4, src, 1, SSD1306_OFF);   /* texte en negatif */
  }

  /* ---- Zone bleue, ligne 1 : libelle + vitesse ---- */
  SSD1306_Text(0, 18, "PROF (m)", 1, SSD1306_ON);
  fmt_fix(a, sizeof a, ENC_HzToMh(hz), (hz > 0.0 && ENC_HzToMh(hz) < 100.0) ? 1u : 0u);
  snprintf(b, sizeof b, "%s m/h", a);
  text_right(SSD1306_WIDTH, 18, b, 1);

  /* ---- Profondeur en taille 3, 7 caracteres max (126 px) ---- */
  double d_m = (double)ENC_GetStates() / ((double)ENC_STATES_PER_PULSE * ppcm * 100.0);
  double ad  = d_m < 0.0 ? -d_m : d_m;
  fmt_fix(a, sizeof a, d_m, ad < 99.9995 ? 3u : ad < 999.995 ? 2u : ad < 9999.95 ? 1u : 0u);
  text_right(SSD1306_WIDTH, 29, a, 3);

  /* ---- Ligne du bas : progression ou reglages ---- */
  uint32_t total = ENC_GetTotalStates();
  if (mode == ENC_MOVE && total > 0u)
  {
    uint32_t done   = total - ENC_GetRemainingStates();
    uint32_t permil = (uint32_t)((uint64_t)done * 1000u / total);
    progress_bar(0, 55, 100, 9, permil);
    snprintf(a, sizeof a, "%lu%%", (unsigned long)(permil / 10u));
    text_right(SSD1306_WIDTH, 56, a, 1);
  }
  else
  {
    fmt_fix(a, sizeof a, ppcm, (ppcm < 10.0) ? 2u : (ppcm < 100.0) ? 1u : 0u);
    /* retire les zeros inutiles : 50.0 -> 50 */
    char *dot = strchr(a, '.');
    if (dot) { char *e = a + strlen(a) - 1; while (e > dot && *e == '0') *e-- = '\0'; if (e == dot) *e = '\0'; }
    snprintf(b, sizeof b, "PPCM %s", a);
    SSD1306_Text(0, 56, b, 1, SSD1306_ON);

    fmt_fix(a, sizeof a, hz, hz < 100.0 ? 2u : 0u);
    snprintf(b, sizeof b, "%s Hz", a);
    text_right(SSD1306_WIDTH, 56, b, 1);
  }
}

/* ========================================================================== */
/* API                                                                        */
/* ========================================================================== */
void OLED_UI_Init(void)
{
  s_t0 = s_last = s_last_try = HAL_GetTick();
  s_splash = SSD1306_Init();
  if (s_splash) { draw_splash(0u); SSD1306_Update(); }
}

void OLED_UI_Task(void)
{
  uint32_t now = HAL_GetTick();

  SSD1306_Task();                                   /* envoi des pages en cours */

  /* Ecran absent ou perdu (erreur I2C) : nouvelle detection periodique */
  if (!SSD1306_Present())
  {
    if ((now - s_last_try) < OLED_RETRY_MS) return;
    s_last_try = now;
    if (!SSD1306_Init()) return;
    s_last = now - OLED_REFRESH_MS;                 /* redessin immediat */
  }

  if ((now - s_last) < OLED_REFRESH_MS || SSD1306_Busy()) return;
  s_last = now;

  if (s_splash)
  {
    uint32_t el = now - s_t0;
    if (el < OLED_SPLASH_MS) { draw_splash(el); SSD1306_Update(); return; }
    s_splash = false;
  }
  draw_dash(now);
  SSD1306_Update();
}
