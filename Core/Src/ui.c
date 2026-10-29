/**
  ******************************************************************************
  * @file    ui.c
  * @brief   Interface tactile multi-pages (version simplifiee de l'appli WPF
  *          EncoderSimulator) : ETAT, VA, JOG/RUN, GRAPHIQUE, SCENARIOS,
  *          REGLAGES. Barre du haut : titre + mode + profondeur. Barre du bas :
  *          STOP (toujours visible) + onglets.
  *
  *  Ecran (paysage 320 x 240, origine en haut a gauche)
  *  ---------------------------------------------------
  *     y   0..21   barre du haut : titre de page (ou message temporaire) a
  *                 gauche, mode + vitesse et profondeur a droite
  *     y  22..205  contenu de la page courante
  *     y 206..239  barre du bas  : [STOP][ETAT][VA][JOG][GRAPH][SCEN][REGL]
  *
  *  Organisation du fichier
  *  -----------------------
  *   1. Mise en page : pages, identifiants et rectangles des boutons
  *   2. Etat interne (page courante, tactile, textes, graphe, calibration)
  *   3. Utilitaires (formatage des nombres sans printf flottant, messages)
  *   4. Dessin des boutons
  *   5. Graphique profondeur / temps
  *   6. Pages : partie fixe (dessinee une fois) / partie dynamique (100 ms)
  *   7. Actions declenchees par les boutons
  *   8. Calibration tactile
  *   9. Gestion du tactile (anti-rebond, appui / suivi / relache)
  *  10. API publique : UI_Init / UI_Task
  *
  *  Principe d'affichage
  *  --------------------
  *  - ui_full_redraw() efface l'ecran et redessine tout (changement de page,
  *    de theme, fermeture du pave numerique...). Couteux (~60 ms) donc rare.
  *  - ui_update() (toutes les UI_REFRESH_MS) ne met a jour que les valeurs :
  *    les zones de texte GText_t ne redessinent que les caracteres modifies,
  *    la barre de progression et le graphe ne redessinent que ce qui bouge.
  *    Le bus SPI reste ainsi peu charge, meme en mouvement.
  *
  *  Ajouter un bouton
  *  -----------------
  *   1. ajouter un identifiant dans l'enum B_xxx (avant B_COUNT) ;
  *   2. lui donner une page et un rectangle dans s_btn[] ;
  *   3. dire comment le dessiner dans draw_btn() ;
  *   4. dire ce qu'il fait dans act().
  *  Ajouter une page : ajouter PG_xxx (avant PG_N), son libelle d'onglet et
  *  son titre, puis ses cases dans draw_page_static() et ui_update(). Les
  *  onglets sont repartis sur 258 px : au-dela de 6 pages, revoir s_btn[].
  ******************************************************************************
  */
#include "ui.h"
#include "tft.h"
#include "gui.h"
#include "theme.h"
#include "keypad.h"
#include "touch.h"
#include "splash.h"
#include "settings.h"
#include "encoder_sim.h"
#include "scenario.h"
#include "cmd_parser.h"
#include "oled_ui.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================== */
/* Mise en page                                                               */
/* ========================================================================== */
#define TOP_H        22      /* hauteur de la barre du haut (px)                     */
#define NAV_Y        206     /* ordonnee de la barre du bas (STOP + onglets)         */
#define NAV_H        34      /* hauteur de la barre du bas                           */
#define TOUCH_MS     20u     /* periode de lecture du tactile (ms)                   */
#define MSG_MS       2500u   /* duree d'affichage d'un message dans la barre du haut */
#define UI_TOUCH_DEBUG 0     /* 1 : trace chaque appui dans le terminal (calibration) */

/* Pages : l'ordre de l'enum = l'ordre des onglets de gauche a droite.
 * s_tab_lbl : libelle court de l'onglet (5 car. max en taille 1, 43 px)
 * s_title   : titre affiche dans la barre du haut (17 car. max en taille 2) */
typedef enum { PG_ETAT = 0, PG_VA, PG_JOG, PG_GRAPH, PG_SCEN, PG_REGL, PG_N } Page_t;
static const char *const s_tab_lbl[PG_N]  = { "ETAT", "VA", "JOG", "GRAPH", "SCEN", "REGL" };
static const char *const s_title[PG_N]    = { "ETAT", "VITESSE D'AVANCE", "JOG / RUN",
                                              "GRAPHIQUE 30 s", "SCENARIOS", "REGLAGES" };

/* Identifiants de boutons. Ils servent d'index dans s_btn[] et sont
 * utilises par draw_btn() (dessin) et act() (action).
 * Les groupes (onglets, pas de JOG, lignes de scenario) sont des plages
 * contigues : B_xxx0..B_xxxN, pour pouvoir les traiter par boucle / soustraction.
 * B_NONE (-1) = aucun bouton sous le doigt. */
enum
{
  B_NONE = -1,
  B_STOP = 0, B_TAB0, B_TAB5 = B_TAB0 + PG_N - 1,
  B_VA_SPEED, B_VA_DEPTH, B_VA_DOWN, B_VA_UP,
  B_JOG_SPEED, B_RUN_UP, B_RUN_DOWN,
  B_JOG_U0, B_JOG_U3 = B_JOG_U0 + 3, B_JOG_D0, B_JOG_D3 = B_JOG_D0 + 3,
  B_SCEN0, B_SCEN3 = B_SCEN0 + 3,
  B_PPCM, B_RST, B_TH_DARK, B_TH_LIGHT, B_CAL,
  B_COUNT
};

/* Un bouton = la page ou il apparait + son rectangle {x, y, largeur, hauteur}.
 * Les boutons de la barre du bas (page ALL) sont presents sur toutes les pages.
 * Le contenu d'une page doit rester entre y = TOP_H et y = NAV_Y - 1. */
typedef struct { int8_t page; GRect r; } Btn_t;      /* page -1 = toutes */

#define ALL  (-1)
static const Btn_t s_btn[B_COUNT] =
{
  /* barre du bas : STOP (62 px) + 6 onglets de 43 px = 320 px */
  [B_STOP]      = { ALL,     {   0, NAV_Y,  62, NAV_H } },
  [B_TAB0 + 0]  = { ALL,     {  62, NAV_Y,  43, NAV_H } },
  [B_TAB0 + 1]  = { ALL,     { 105, NAV_Y,  43, NAV_H } },
  [B_TAB0 + 2]  = { ALL,     { 148, NAV_Y,  43, NAV_H } },
  [B_TAB0 + 3]  = { ALL,     { 191, NAV_Y,  43, NAV_H } },
  [B_TAB0 + 4]  = { ALL,     { 234, NAV_Y,  43, NAV_H } },
  [B_TAB0 + 5]  = { ALL,     { 277, NAV_Y,  43, NAV_H } },

  /* page VA : 2 champs de saisie + descente / remontee */
  [B_VA_SPEED]  = { PG_VA,   {   8,  36, 196, 40 } },
  [B_VA_DEPTH]  = { PG_VA,   {   8,  92, 196, 40 } },
  [B_VA_DOWN]   = { PG_VA,   {   8, 140, 150, 60 } },
  [B_VA_UP]     = { PG_VA,   { 162, 140, 150, 60 } },

  /* page JOG : vitesse commune JOG/RUN, RUN haut/bas, puis 2 rangees de pas
   * (rangee du haut = remontee, rangee du bas = descente) */
  [B_JOG_SPEED] = { PG_JOG,  {   8,  36, 148, 38 } },
  [B_RUN_UP]    = { PG_JOG,  { 162,  36,  73, 38 } },
  [B_RUN_DOWN]  = { PG_JOG,  { 239,  36,  73, 38 } },
  [B_JOG_U0 + 0]= { PG_JOG,  {   8,  82,  72, 52 } },
  [B_JOG_U0 + 1]= { PG_JOG,  {  84,  82,  72, 52 } },
  [B_JOG_U0 + 2]= { PG_JOG,  { 160,  82,  72, 52 } },
  [B_JOG_U0 + 3]= { PG_JOG,  { 236,  82,  76, 52 } },
  [B_JOG_D0 + 0]= { PG_JOG,  {   8, 140,  72, 52 } },
  [B_JOG_D0 + 1]= { PG_JOG,  {  84, 140,  72, 52 } },
  [B_JOG_D0 + 2]= { PG_JOG,  { 160, 140,  72, 52 } },
  [B_JOG_D0 + 3]= { PG_JOG,  { 236, 140,  76, 52 } },

  /* page SCEN : une ligne par scenario (4 max) ; une ligne sans scenario
   * correspondant n'est ni dessinee ni cliquable (voir btn_visible) */
  [B_SCEN0 + 0] = { PG_SCEN, {   8,  28, 304, 42 } },
  [B_SCEN0 + 1] = { PG_SCEN, {   8,  74, 304, 42 } },
  [B_SCEN0 + 2] = { PG_SCEN, {   8, 120, 304, 42 } },
  [B_SCEN0 + 3] = { PG_SCEN, {   8, 166, 304, 36 } },

  /* page REGL */
  [B_PPCM]      = { PG_REGL, {   8,  38, 172, 38 } },
  [B_RST]       = { PG_REGL, { 190,  92, 122, 40 } },
  [B_TH_DARK]   = { PG_REGL, {   8, 154,  98, 38 } },
  [B_TH_LIGHT]  = { PG_REGL, { 110, 154,  98, 38 } },
  [B_CAL]       = { PG_REGL, { 214, 154,  98, 38 } },
};

/* Pas de JOG (en cm) et leurs libelles, dans l'ordre des boutons B_JOG_U0..3 / D0..3 */
static const double s_jog_cm[4]   = { 0.1, 1.0, 10.0, 100.0 };
static const char  *const s_jog_lbl[4] = { "1mm", "1cm", "10cm", "1m" };

/* ========================================================================== */
/* Etat                                                                       */
/* ========================================================================== */
static Page_t   s_page = PG_ETAT;      /* page affichee                                 */
static bool     s_splash = false;      /* true pendant les 5 s du splash                */
static uint32_t s_splash_t0;           /* instant d'affichage du splash (ms)            */
static uint32_t s_last_ui, s_last_touch;  /* derniers rafraichissement / lecture tactile */
static uint32_t s_msg_t = 0;           /* instant du message en cours, 0 = aucun        */
static char     s_msg[40];             /* message temporaire (erreur, confirmation)     */
static uint16_t s_msg_col;             /* couleur du message                            */

/* Tactile : anti-rebond et suivi du bouton presse.
 * Un appui (resp. relache) n'est valide qu'apres 2 lectures consecutives
 * identiques, soit 40 ms : evite les rebonds et les appuis fantomes. */
static uint8_t  s_tp_on = 0, s_tp_off = 0;  /* compteurs de lectures appui / relache   */
static bool     s_tp_down = false;           /* etat valide : doigt pose                */
static int      s_down_btn = B_NONE;         /* bouton (ou touche pave) presse          */
static bool     s_down_inside = false;       /* le doigt est-il encore sur ce bouton ?  */
static uint16_t s_tx, s_ty;                  /* derniere position du doigt (px)         */

/* Zones de texte dynamiques : { x, y, taille de police, largeur en caracteres }.
 * Une taille n donne des caracteres de 6n x 8n px. Seuls les caracteres qui
 * changent sont redessines (voir GUI_TextSet). Les 3 derniers champs (couleurs
 * et memoire de l'affichage) sont geres par gui.c. */
static GText_t g_title = {   6,   3, 2, 17, 0, 0, {0} };
static GText_t g_mode  = { 214,   3, 1, 17, 0, 0, {0} };
static GText_t g_depth = { 214,  12, 1, 17, 0, 0, {0} };

/* page ETAT */
static GText_t e_state = {   8,  26, 3, 10, 0, 0, {0} };
static GText_t e_dir   = { 200,  30, 2,  9, 0, 0, {0} };
static GText_t e_hz    = { 170,  58, 1, 23, 0, 0, {0} };
static GText_t e_speed = {   8,  68, 4,  7, 0, 0, {0} };
static GText_t e_imp   = { 170, 108, 1, 23, 0, 0, {0} };
static GText_t e_pos   = {   8, 118, 4,  8, 0, 0, {0} };
static GText_t e_pct   = { 256, 161, 1,  9, 0, 0, {0} };
static GText_t e_rest  = {   8, 176, 1, 50, 0, 0, {0} };
static GText_t e_foot  = {   8, 190, 1, 50, 0, 0, {0} };
/* barre de progression du MOVE / scenario (page ETAT) */
#define BAR_X 8
#define BAR_Y 159
#define BAR_W 240
#define BAR_H 11
static int16_t s_bar_px = -1;           /* longueur remplie actuellement dessinee, -1 = a redessiner */
static uint16_t s_bar_col = 0;          /* couleur actuellement dessinee */

/* pages VA / JOG / REGL */
static GText_t v_dur   = { 214,  50, 2,  8, 0, 0, {0} };
static GText_t v_pos   = { 214, 106, 2,  8, 0, 0, {0} };
static GText_t j_info  = {   8, 196, 1, 50, 0, 0, {0} };
static GText_t r_fmax  = { 190,  44, 1, 20, 0, 0, {0} };
static GText_t r_fmh   = { 190,  56, 1, 20, 0, 0, {0} };
static GText_t r_pos   = {   8, 104, 2, 14, 0, 0, {0} };
static int      s_scen_drawn = -1;      /* scenario en cours lors du dernier dessin de la page SCEN */

/* GRAPH : zone de trace (GX, GY, GW x GH). Un echantillon par colonne de
 * pixel : GW echantillons espaces de G_DT_MS couvrent les 30 s affichees.
 * L'echantillonnage tourne en permanence (meme hors page GRAPH) pour que la
 * courbe soit deja remplie quand on ouvre la page. */
#define GX   40                                /* gauche (place pour les graduations) */
#define GY   26                                /* haut                                 */
#define GW   272                               /* largeur = nombre d'echantillons      */
#define GH   158                               /* hauteur                              */
#define G_DT_MS  (30000u / GW)                 /* 110 ms -> fenetre 29,9 s */
static float    s_gs[GW];                      /* profondeur (m), anneau */
static uint16_t s_ghead = 0, s_gn = 0;         /* prochaine case a ecrire / nb d'echantillons valides */
static uint32_t s_glast = 0;                   /* instant du dernier echantillon      */
static int16_t  s_seg_lo[GW], s_seg_hi[GW];    /* segment vertical dessine par colonne (-1 = vide) */
static double   s_glo = 0.0, s_ghi = 0.0;      /* echelle verticale courante (m)      */
static bool     s_gdirty = true;               /* nouvel echantillon a tracer          */

/* CALIBRATION TACTILE : 5 croix (4 coins + centre) aux positions ecran
 * s_cal_tx/ty. Pour chacune on memorise la moyenne des valeurs brutes lues
 * pendant l'appui, puis TOUCH_ComputeCal() calcule la transformation affine. */
#define CAL_N        5
#define CAL_TIMEOUT  30000u                    /* abandon apres 30 s sans appui */
static const int16_t s_cal_tx[CAL_N] = {  20, 300, 300,  20, 160 };
static const int16_t s_cal_ty[CAL_N] = {  20,  20, 220, 220, 120 };
static int          s_cal_step = -1;      /* -1 inactif, 0..4 cibles, 5 resultat */
static float        s_cal_rx[CAL_N], s_cal_ry[CAL_N];   /* moyennes brutes par croix */
static uint32_t     s_cal_sx, s_cal_sy, s_cal_cnt;      /* accumulateurs de l'appui en cours */
static uint32_t     s_cal_t;                            /* dernier appui (timeout) */
static TOUCH_Cal_t  s_cal_old, s_cal_new; /* calibration avant / calculee (pour annuler) */
static const GRect  s_cal_ok  = {  40, 146, 110, 46 };
static const GRect  s_cal_redo= { 170, 146, 110, 46 };
static void cal_start(void);

/* ========================================================================== */
/* Utilitaires                                                                */
/* ========================================================================== */
/* Formate v avec 'dec' decimales (0..3), arrondi au plus proche.
 * newlib-nano n'a pas %f par defaut : on passe par des entiers.
 * Ex. fmt_fix(b, n, -1.2345, 2) -> "-1.23" ; ne produit jamais "-0.00". */
static void fmt_fix(char *b, size_t sz, double v, unsigned dec)
{
  static const uint32_t p10[] = {1u, 10u, 100u, 1000u};
  bool neg = (v < 0.0);
  if (neg) v = -v;
  uint64_t n  = (uint64_t)(v * p10[dec] + 0.5);
  uint32_t ip = (uint32_t)(n / p10[dec]), fp = (uint32_t)(n % p10[dec]);
  if (neg && n == 0u) neg = false;
  if (dec == 0u)      snprintf(b, sz, "%s%lu", neg ? "-" : "", (unsigned long)ip);
  else if (dec == 1u) snprintf(b, sz, "%s%lu.%01lu", neg ? "-" : "", (unsigned long)ip, (unsigned long)fp);
  else if (dec == 2u) snprintf(b, sz, "%s%lu.%02lu", neg ? "-" : "", (unsigned long)ip, (unsigned long)fp);
  else                snprintf(b, sz, "%s%lu.%03lu", neg ? "-" : "", (unsigned long)ip, (unsigned long)fp);
}

/* Valeur sans zeros inutiles (300, 2.5, 0.125) */
static void fmt_trim(char *b, size_t sz, double v, unsigned dec)
{
  fmt_fix(b, sz, v, dec);
  if (strchr(b, '.'))
  {
    size_t L = strlen(b);
    while (L && b[L - 1] == '0') b[--L] = '\0';
    if (L && b[L - 1] == '.') b[--L] = '\0';
  }
}

/* Aligne a droite sur w caracteres (les chiffres ne "sautent" pas d'un rafraichissement a l'autre) */
static void rjust(char *d, size_t sz, const char *s, unsigned w) { snprintf(d, sz, "%*s", (int)w, s); }

/* Profondeur simulee en metres (positive = descente), d'apres la position
 * du codeur en etats de Gray et la resolution courante */
static double depth_m(void)
{
  return (double)ENC_GetStates() / ENC_STATES_PER_PULSE / ENC_GetPulsesPerCm() / 100.0;
}

/* Affiche un message pendant MSG_MS a la place du titre de la page.
 * Le "| 1u" garantit s_msg_t != 0 (0 signifie "pas de message"). */
static void ui_msg(const char *m, uint16_t col)
{
  strncpy(s_msg, m, sizeof s_msg - 1);
  s_msg[sizeof s_msg - 1] = '\0';
  s_msg_col = col;
  s_msg_t = HAL_GetTick() | 1u;
}

/* Traduit un code d'erreur du module codeur en message pour l'utilisateur */
static void enc_err(ENC_Status_t st)
{
  switch (st)
  {
    case ENC_ERR_BUSY:    ui_msg("Mouvement en cours", T->warning); break;
    case ENC_ERR_FREQ:    ui_msg("Vitesse hors plage", T->danger);  break;
    case ENC_ERR_COUNT:   ui_msg("Distance invalide", T->danger);   break;
    case ENC_ERR_PPCM:    ui_msg("PPCM hors plage", T->danger);     break;
    case ENC_ERR_PROFILE: ui_msg("Scenario en cours", T->warning);  break;
    default:              ui_msg("Erreur", T->danger);              break;
  }
}

/* ========================================================================== */
/* Dessin des boutons                                                         */
/* ========================================================================== */
/* Dessine le bouton 'id' a l'etat normal ou enfonce (pressed = retour visuel
 * pendant l'appui). Les champs de saisie affichent la valeur courante :
 * il suffit de redessiner le bouton pour mettre a jour la valeur. */
static void draw_btn(int id, bool pressed)
{
  const GRect *r = &s_btn[id].r;
  char v[24];

  if (id == B_STOP) { GUI_Button(r, "STOP", 2, GST_DANGER, GIC_NONE, pressed); return; }
  if (id >= B_TAB0 && id <= B_TAB5)
  {
    int p = id - B_TAB0;
    GUI_Button(r, s_tab_lbl[p], 1, (p == (int)s_page) ? GST_TAB_ON : GST_TAB, GIC_NONE, pressed);
    return;
  }
  switch (id)
  {
    case B_VA_SPEED:  fmt_trim(v, sizeof v, g_set.va_speed_mh, 1);  GUI_Field(r, v, 3, "m/h", pressed); break;
    case B_VA_DEPTH:  fmt_trim(v, sizeof v, g_set.va_depth_m, 3);   GUI_Field(r, v, 3, "m", pressed);   break;
    case B_VA_DOWN:   GUI_Button(r, "DESCENDRE", 2, GST_ACCENT, GIC_DOWN, pressed); break;
    case B_VA_UP:     GUI_Button(r, "REMONTER",  2, GST_ACCENT, GIC_UP,   pressed); break;
    case B_JOG_SPEED: fmt_trim(v, sizeof v, g_set.jog_speed_mh, 1); GUI_Field(r, v, 3, "m/h", pressed); break;
    case B_RUN_UP:    GUI_Button(r, "RUN", 2, GST_NORMAL, GIC_UP,   pressed); break;
    case B_RUN_DOWN:  GUI_Button(r, "RUN", 2, GST_NORMAL, GIC_DOWN, pressed); break;
    case B_PPCM:      fmt_trim(v, sizeof v, ENC_GetPulsesPerCm(), 2); GUI_Field(r, v, 3, "imp/cm", pressed); break;
    case B_RST:       GUI_Button(r, "RST", 2, GST_NORMAL, GIC_NONE, pressed); break;
    case B_TH_DARK:   GUI_Button(r, "Sombre", 2, THEME_Get() == THEME_DARK  ? GST_SELECTED : GST_NORMAL, GIC_NONE, pressed); break;
    case B_TH_LIGHT:  GUI_Button(r, "Clair",  2, THEME_Get() == THEME_LIGHT ? GST_SELECTED : GST_NORMAL, GIC_NONE, pressed); break;
    case B_CAL:       GUI_Button(r, "CALIBRER", 1, GST_NORMAL, GIC_NONE, pressed); break;
    default:
      if (id >= B_JOG_U0 && id <= B_JOG_U3)
        GUI_Button(r, s_jog_lbl[id - B_JOG_U0], 2, GST_NORMAL, GIC_UP, pressed);
      else if (id >= B_JOG_D0 && id <= B_JOG_D3)
        GUI_Button(r, s_jog_lbl[id - B_JOG_D0], 2, GST_NORMAL, GIC_DOWN, pressed);
      else if (id >= B_SCEN0 && id <= B_SCEN3)
      {
        /* ligne de scenario : nom sur la 1re ligne, distance / vitesses /
         * duree estimee sur la 2e ; surlignee pendant l'execution */
        int i = id - B_SCEN0;
        const SCN_Def_t *sc = SCN_Get((uint16_t)(i + 1));
        if (!sc) { TFT_FillRect(r->x, r->y, r->w, r->h, T->bg); break; }
        bool run = (SCN_Running() == (uint16_t)(i + 1));
        uint16_t bg = (pressed || run) ? T->accent : T->card;
        uint16_t fg = (pressed || run) ? T->accent_text : T->text;
        uint16_t mu = (pressed || run) ? T->accent_text : T->muted;
        TFT_FillRect(r->x, r->y, r->w, r->h, bg);
        GUI_Frame(r, (pressed || run) ? T->accent : T->border);
        char l1[56], l2[64], a[12], b[12], c[12], d[12];
        snprintf(l1, sizeof l1, "%d  %.46s", i + 1, sc->name);
        fmt_trim(a, sizeof a, sc->dist_cm / 100.0, 3);
        fmt_trim(b, sizeof b, sc->vmin_mh, 0);
        fmt_trim(c, sizeof c, sc->vmax_mh, 0);
        fmt_fix(d, sizeof d, SCN_DurationS(sc), 1);
        snprintf(l2, sizeof l2, "   %s m  %s..%s m/h  ~%s s%s", a, b, c, d, run ? "   EN COURS" : "");
        GUI_Text((uint16_t)(r->x + 6), (uint16_t)(r->y + 9), l1, fg, bg, 1);
        GUI_Text((uint16_t)(r->x + 6), (uint16_t)(r->y + 25), l2, mu, bg, 1);
      }
      break;
  }
}

/* Un bouton est actif s'il appartient a la page courante (ou a toutes) et,
 * pour les lignes de scenario, si le scenario correspondant existe */
static bool btn_visible(int id)
{
  if (s_btn[id].r.w == 0u) return false;
  if (s_btn[id].page == ALL) return true;
  if (s_btn[id].page != (int8_t)s_page) return false;
  if (id >= B_SCEN0 && id <= B_SCEN3) return SCN_Get((uint16_t)(id - B_SCEN0 + 1)) != 0;
  return true;
}

/* Bouton actif sous le point (x, y) en pixels, ou B_NONE */
static int btn_hit(uint16_t x, uint16_t y)
{
  for (int id = 0; id < B_COUNT; id++)
    if (btn_visible(id) && GUI_Hit(&s_btn[id].r, x, y)) return id;
  return B_NONE;
}

/* ========================================================================== */
/* Graphique : profondeur (m, positive vers le bas) sur 30 s                  */
/* ========================================================================== */
/* Partie entiere par defaut (floor) sans libm, correcte aussi pour x < 0 */
static long lfloor(double x) { long i = (long)x; return (x < (double)i) ? i - 1 : i; }

/* Echelle "ronde" en 4 divisions (pas 1/2/5 x 10^n) contenant [mn, mx].
 * Etendue minimale 1 cm pour ne pas amplifier le bruit a l'arret.
 * Ex. mn = 0.12, mx = 0.31 -> pas 0.05 -> [0.10 ; 0.30] trop court -> pas 0.1 -> [0.1 ; 0.5] */
static void graph_scale(double mn, double mx, double *lo, double *hi)
{
  static const double mult[] = { 1.0, 2.0, 5.0 };
  double span = mx - mn;
  if (span < 0.01) span = 0.01;
  double raw = span / 4.0, mag = 0.001;
  while (mag * 10.0 <= raw) mag *= 10.0;
  for (int k = 0; k < 9; k++)
  {
    double step = mult[k % 3] * mag;
    for (int e = 0; e < k / 3; e++) step *= 10.0;
    if (step < raw) continue;
    double l = step * (double)lfloor(mn / step + 1e-9);
    if (l + 4.0 * step >= mx - 1e-9) { *lo = l; *hi = l + 4.0 * step; return; }
  }
  *lo = mn; *hi = mn + span;
}

/* Ordonnee de la ligne de grille j (0 = haut, 4 = bas) */
static int16_t grid_y(int j) { return (int16_t)(GY + 1 + (j * (GH - 4) + 2) / 4); }

/* Ordonnee ecran d'une profondeur : profondeur positive vers le bas, donc
 * s_glo (plus faible profondeur) en haut, s_ghi en bas. Bornee a la zone. */
static int16_t graph_y(double d)
{
  double t = (d - s_glo) / (s_ghi - s_glo);
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  return (int16_t)(GY + 1 + t * (GH - 4));
}

/* true si y est une ligne de grille (a restaurer quand on efface la courbe) */
static bool grid_row(int16_t y)
{
  for (int j = 0; j <= 4; j++) if (y == grid_y(j)) return true;
  return false;
}

/* Redessine fond, cadre, grille et graduations, et oublie la courbe tracee.
 * Appele a l'ouverture de la page et a chaque changement d'echelle. */
static void graph_axes(void)
{
  char a[16];
  TFT_FillRect(0, GY - 4, GX - 1, GH + 8, T->bg);
  TFT_FillRect(GX, GY, GW, GH, T->chart_bg);
  GRect fr = { GX - 1, GY - 1, GW + 2, GH + 2 };
  GUI_Frame(&fr, T->border);
  double st = (s_ghi - s_glo) / 4.0;
  unsigned dec = st >= 1.0 ? 0 : st >= 0.1 ? 1 : st >= 0.01 ? 2 : 3;
  for (int j = 0; j <= 4; j++)
  {
    int16_t y = grid_y(j);
    TFT_FillRect(GX, (uint16_t)y, GW, 1, T->border);
    fmt_fix(a, sizeof a, s_glo + st * j, dec);
    GUI_Text((uint16_t)(GX - 4 - GUI_TextW(a, 1)), (uint16_t)(y - 3), a, T->muted, T->bg, 1);
  }
  GUI_Text(GX, GY + GH + 4, "-30 s", T->muted, T->bg, 1);
  GUI_Text(GX + GW / 2 - 15, GY + GH + 4, "-15 s", T->muted, T->bg, 1);
  GUI_Text(GX + GW - 18, GY + GH + 4, "0 s", T->muted, T->bg, 1);
  GUI_Text(4, GY + GH + 4, "(m)", T->muted, T->bg, 1);
  for (int i = 0; i < GW; i++) { s_seg_lo[i] = -1; s_seg_hi[i] = -1; }
}

/* Enregistre un echantillon de profondeur toutes les G_DT_MS (appele a chaque UI_Task) */
static void graph_sample(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - s_glast) < G_DT_MS) return;
  s_glast = now;
  s_gs[s_ghead] = (float)depth_m();
  s_ghead = (uint16_t)((s_ghead + 1u) % GW);
  if (s_gn < GW) s_gn++;
  s_gdirty = true;
}

/* Trace la courbe. Chaque colonne c est un segment vertical reliant
 * l'echantillon precedent au courant : la courbe reste continue meme en
 * descente rapide. Une colonne n'est redessinee que si son segment a change
 * (efface l'ancien en couleur de fond, restaure la grille, trace le nouveau),
 * ce qui reste rapide meme quand la courbe defile.
 * full = true : redessin complet (ouverture de la page). */
static void graph_draw(bool full)
{
  if (s_gn == 0u)
  {
    if (full) { s_glo = 0.0; s_ghi = 0.04; graph_axes(); }
    return;
  }

  /* min / max sur la fenetre -> echelle ; si elle change, tout est redessine */
  double mn = 1e30, mx = -1e30;
  for (uint16_t k = 0; k < s_gn; k++)
  {
    double d = s_gs[(s_ghead + GW - 1u - k) % GW];
    if (d < mn) mn = d;
    if (d > mx) mx = d;
  }
  double lo, hi;
  graph_scale(mn, mx, &lo, &hi);
  if (full || lo != s_glo || hi != s_ghi) { s_glo = lo; s_ghi = hi; graph_axes(); }

  /* une colonne par echantillon, le plus recent a droite */
  int16_t prev = -1;
  for (int c = 0; c < GW; c++)
  {
    int age = GW - 1 - c;
    int16_t a = -1, b = -1;
    if (age < (int)s_gn)
    {
      int16_t y = graph_y(s_gs[(s_ghead + GW - 1u - (uint16_t)age) % GW]);
      a = (prev < 0) ? y : (prev < y ? prev : y);
      b = (prev < 0) ? y : (prev > y ? prev : y);
      prev = y;
      b = (int16_t)(b + 1);                       /* trait de 2 px */
      if (b > GY + GH - 1) b = GY + GH - 1;
    }
    if (a == s_seg_lo[c] && b == s_seg_hi[c]) continue;
    if (s_seg_lo[c] >= 0)                          /* efface l'ancien segment */
    {
      TFT_FillRect((uint16_t)(GX + c), (uint16_t)s_seg_lo[c], 1,
                   (uint16_t)(s_seg_hi[c] - s_seg_lo[c] + 1), T->chart_bg);
      for (int16_t y = s_seg_lo[c]; y <= s_seg_hi[c]; y++)
        if (grid_row(y)) TFT_FillRect((uint16_t)(GX + c), (uint16_t)y, 1, 1, T->border);
    }
    if (a >= 0)
      TFT_FillRect((uint16_t)(GX + c), (uint16_t)a, 1, (uint16_t)(b - a + 1), T->accent);
    s_seg_lo[c] = a; s_seg_hi[c] = b;
  }
  s_gdirty = false;
}

/* ========================================================================== */
/* Pages : partie fixe                                                        */
/* ========================================================================== */
/* Invalide la memoire de toutes les zones de texte : au prochain GUI_TextSet,
 * elles seront entierement redessinees (apres un effacement d'ecran). */
static void reset_texts(void)
{
  GText_t *all[] = { &g_title, &g_mode, &g_depth, &e_state, &e_dir, &e_hz, &e_speed, &e_imp,
                     &e_pos, &e_pct, &e_rest, &e_foot, &v_dur, &v_pos, &j_info, &r_fmax,
                     &r_fmh, &r_pos };
  for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) GUI_TextReset(all[i]);
  s_bar_px = -1;
  s_scen_drawn = -1;
}

/* Elements fixes de la page courante (libelles, separateurs, cadres) et
 * ses boutons. Les valeurs sont ensuite remplies par ui_update(). */
static void draw_page_static(void)
{
  uint16_t bg = T->bg;
  switch (s_page)
  {
    case PG_ETAT:
      TFT_HLine(0, 54, TFT_WIDTH, T->border);
      GUI_Text(8, 58, "VITESSE", T->muted, bg, 1);
      GUI_Text(184, 84, "m/h", T->muted, bg, 2);
      TFT_HLine(0, 104, TFT_WIDTH, T->border);
      GUI_Text(8, 108, "PROFONDEUR", T->muted, bg, 1);
      GUI_Text(206, 126, "m", T->muted, bg, 3);
      TFT_HLine(0, 154, TFT_WIDTH, T->border);
      { GRect f = { BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2 }; GUI_Frame(&f, T->border); }
      break;
    case PG_VA:
      GUI_Text(8, 26, "VITESSE", T->muted, bg, 1);
      GUI_Text(8, 82, "PROFONDEUR (relative)", T->muted, bg, 1);
      GUI_Text(214, 38, "Duree ~", T->muted, bg, 1);
      GUI_Text(214, 94, "Position (m)", T->muted, bg, 1);
      break;
    case PG_JOG:
      GUI_Text(8, 26, "VITESSE JOG / RUN", T->muted, bg, 1);
      break;
    case PG_GRAPH:
      graph_draw(true);
      break;
    case PG_SCEN:
      if (SCN_Count() == 0u) GUI_Text(8, 40, "Aucun scenario", T->muted, bg, 2);
      break;
    case PG_REGL:
      GUI_Text(8, 28, "RESOLUTION", T->muted, bg, 1);
      GUI_Text(8, 92, "POSITION", T->muted, bg, 1);
      GUI_Text(8, 142, "THEME", T->muted, bg, 1);
      GUI_Text(214, 142, "TACTILE", T->muted, bg, 1);
      GUI_Text(8, 197, "Reglages memorises en flash", T->muted, bg, 1);
      break;
    default: break;
  }
  for (int id = 0; id < B_COUNT; id++)
    if (btn_visible(id) && s_btn[id].page != ALL) draw_btn(id, false);
}

static void ui_update(void);

/* Redessin complet : pave numerique s'il est ouvert, sinon barres du haut et
 * du bas + page courante, puis valeurs. A appeler apres tout changement qui
 * modifie la structure de l'ecran (page, theme, fermeture d'un pave...). */
static void ui_full_redraw(void)
{
  reset_texts();
  if (KP_Active()) { KP_Draw(); return; }
  TFT_FillScreen(T->bg);
  TFT_FillRect(0, 0, TFT_WIDTH, TOP_H - 1, T->card);
  TFT_FillRect(0, TOP_H - 1, TFT_WIDTH, 1, T->border);
  for (int id = 0; id < B_COUNT; id++) if (s_btn[id].page == ALL) draw_btn(id, false);
  draw_page_static();
  ui_update();
}

/* ========================================================================== */
/* Pages : partie dynamique (toutes les 100 ms)                               */
/* ========================================================================== */
/* Barre de progression : px pixels remplis en couleur col, le reste en fond.
 * Ne fait rien si rien n'a change depuis le dernier dessin. */
static void draw_bar(int16_t px, uint16_t col)
{
  if (px == s_bar_px && col == s_bar_col) return;
  if (px > 0)     TFT_FillRect(BAR_X, BAR_Y, (uint16_t)px, BAR_H, col);
  if (px < BAR_W) TFT_FillRect((uint16_t)(BAR_X + px), BAR_Y, (uint16_t)(BAR_W - px), BAR_H, T->bg);
  s_bar_px = px; s_bar_col = col;
}

/* Concatenation bornee a la taille du tampon de destination */
static void strcat_s(char *d, size_t sz, const char *s) { strncat(d, s, sz - strlen(d) - 1); }

/* Rafraichit les valeurs de la barre du haut et de la page courante.
 * Toutes les informations sont relues dans les modules (ENC_..., SCN_...,
 * g_set) : l'affichage suit donc aussi les commandes venues du terminal. */
static void ui_update(void)
{
  char a[40], b[40];
  ENC_Mode_t m   = ENC_GetMode();
  uint16_t   scn = SCN_Running();
  double     hz  = ENC_GetHz();
  double     d   = depth_m();
  uint16_t   tb  = T->card;

  /* --- barre du haut : message temporaire ou titre ; mode, sens (v = descente,
   *     ^ = remontee) et vitesse ; profondeur --- */
  if (s_msg_t && (HAL_GetTick() - s_msg_t) < MSG_MS) GUI_TextSet(&g_title, s_msg, s_msg_col, tb);
  else { s_msg_t = 0; GUI_TextSet(&g_title, s_title[s_page], T->text, tb); }

  uint16_t mc;
  const char *dir = ENC_GetDir() > 0 ? "v" : "^";
  if (m == ENC_IDLE)      { snprintf(a, sizeof a, "ARRET"); mc = T->muted; }
  else if (scn)           { snprintf(a, sizeof a, "SCEN %u %s", (unsigned)scn, dir); mc = T->warning; }
  else if (m == ENC_MOVE) { snprintf(a, sizeof a, "MOVE %s", dir); mc = T->success; }
  else                    { snprintf(a, sizeof a, "RUN %s", dir);  mc = T->accent; }
  if (m != ENC_IDLE) { fmt_fix(b, sizeof b, ENC_HzToMh(hz), 0); strcat_s(a, sizeof a, "  "); strcat_s(a, sizeof a, b); strcat_s(a, sizeof a, " m/h"); }
  rjust(b, sizeof b, a, g_mode.width);
  GUI_TextSet(&g_mode, b, mc, tb);
  fmt_fix(a, sizeof a, d, 3); strcat_s(a, sizeof a, " m");
  rjust(b, sizeof b, a, g_depth.width);
  GUI_TextSet(&g_depth, b, T->text, tb);

  /* --- page --- */
  switch (s_page)
  {
    case PG_ETAT:
    {
      uint16_t bg = T->bg;
      if (m == ENC_IDLE)      GUI_TextSet(&e_state, "ARRET", T->muted, bg);
      else if (scn)         { snprintf(a, sizeof a, "SCEN %u", (unsigned)scn); GUI_TextSet(&e_state, a, T->warning, bg); }
      else if (m == ENC_MOVE) GUI_TextSet(&e_state, "MOVE", T->success, bg);
      else                    GUI_TextSet(&e_state, "RUN", T->accent, bg);
      if (m == ENC_IDLE)          GUI_TextSet(&e_dir, "", T->text, bg);
      else if (ENC_GetDir() > 0)  GUI_TextSet(&e_dir, "DESCENTE", T->text, bg);
      else                        GUI_TextSet(&e_dir, "REMONTEE", T->warning, bg);

      fmt_fix(a, sizeof a, ENC_HzToMh(hz), 1); rjust(b, sizeof b, a, e_speed.width);
      GUI_TextSet(&e_speed, b, m == ENC_IDLE ? T->neutral : T->text, bg);
      fmt_fix(a, sizeof a, hz, 3); strcat_s(a, sizeof a, " Hz"); rjust(b, sizeof b, a, e_hz.width);
      GUI_TextSet(&e_hz, b, T->muted, bg);

      fmt_fix(a, sizeof a, d, 3); rjust(b, sizeof b, a, e_pos.width);
      GUI_TextSet(&e_pos, b, T->text, bg);
      int32_t st = ENC_GetStates();
      if (st % (int32_t)ENC_STATES_PER_PULSE == 0) snprintf(a, sizeof a, "%ld imp", (long)(st / (int32_t)ENC_STATES_PER_PULSE));
      else { fmt_fix(a, sizeof a, (double)st / ENC_STATES_PER_PULSE, 2); strcat_s(a, sizeof a, " imp"); }
      rjust(b, sizeof b, a, e_imp.width);
      GUI_TextSet(&e_imp, b, T->muted, bg);

      /* progression : uniquement pour un mouvement borne (MOVE, VA, JOG, SCEN) */
      uint32_t tot = ENC_GetTotalStates();
      if (m == ENC_MOVE && tot > 0u)
      {
        uint32_t rem = ENC_GetRemainingStates(), done = tot - rem;
        draw_bar((int16_t)(((uint64_t)done * BAR_W) / tot), scn ? T->warning : T->success);
        snprintf(a, sizeof a, "%3lu %%", (unsigned long)(((uint64_t)done * 100u) / tot));
        GUI_TextSet(&e_pct, a, T->text, bg);
        fmt_fix(b, sizeof b, (double)rem / ENC_STATES_PER_PULSE / ENC_GetPulsesPerCm() / 100.0, 3);
        snprintf(a, sizeof a, "Reste a parcourir : %.16s m", b);
        GUI_TextSet(&e_rest, a, T->muted, bg);
      }
      else
      {
        draw_bar(0, T->bg);
        GUI_TextSet(&e_pct, m == ENC_RUN ? "continu" : "", T->accent, bg);
        GUI_TextSet(&e_rest, "", T->muted, bg);
      }
      fmt_trim(a, sizeof a, ENC_GetPulsesPerCm(), 2);
      fmt_fix(b, sizeof b, ENC_GetFmaxHz(), 1);
      char f[80];
      snprintf(f, sizeof f, "PPCM %.10s imp/cm   Fmax %.10s Hz (%u m/h)", a, b, (unsigned)ENC_VMAX_MH);
      GUI_TextSet(&e_foot, f, T->muted, bg);
      break;
    }

    case PG_VA:
    {
      /* duree estimee = profondeur (m) / vitesse (m/h), en s ou en min */
      double t = (double)g_set.va_depth_m / (double)g_set.va_speed_mh * 3600.0;
      if (t < 600.0) { fmt_fix(a, sizeof a, t, 1); strcat_s(a, sizeof a, " s"); }
      else           { fmt_fix(a, sizeof a, t / 60.0, 1); strcat_s(a, sizeof a, " min"); }
      GUI_TextSet(&v_dur, a, T->text, T->bg);
      fmt_fix(a, sizeof a, d, 3);
      GUI_TextSet(&v_pos, a, T->text, T->bg);
      break;
    }

    case PG_JOG:
    {
      char f[64];
      fmt_trim(b, sizeof b, g_set.jog_speed_mh, 1);
      snprintf(f, sizeof f, "Pas et RUN a %.10s m/h (modifiable pendant RUN)", b);
      GUI_TextSet(&j_info, f, T->muted, T->bg);
      break;
    }

    case PG_GRAPH:
      if (s_gdirty) graph_draw(false);
      break;

    case PG_SCEN:
      /* les lignes ne sont redessinees qu'au debut / a la fin d'un scenario */
      if ((int)scn != s_scen_drawn)
      {
        for (int id = B_SCEN0; id <= B_SCEN3; id++) if (btn_visible(id)) draw_btn(id, false);
        s_scen_drawn = (int)scn;
      }
      break;

    case PG_REGL:
      fmt_fix(a, sizeof a, ENC_GetFmaxHz(), 1);
      snprintf(b, sizeof b, "Fmax %.16s Hz", a);
      GUI_TextSet(&r_fmax, b, T->text, T->bg);
      snprintf(b, sizeof b, "(%u m/h)", (unsigned)ENC_VMAX_MH);
      GUI_TextSet(&r_fmh, b, T->muted, T->bg);
      fmt_fix(a, sizeof a, d, 3); strcat_s(a, sizeof a, " m");
      GUI_TextSet(&r_pos, a, T->text, T->bg);
      break;

    default: break;
  }
}

/* ========================================================================== */
/* Actions                                                                    */
/* ========================================================================== */
/* Callbacks du pave numerique : appeles sur OK avec une valeur deja verifiee
 * dans la plage passee a KP_Open(). Modifier g_set suffit pour que la valeur
 * soit sauvegardee en flash (SETTINGS_Task) et reaffichee. */
static void kp_va_speed(double v)  { g_set.va_speed_mh  = (float)v; }
static void kp_va_depth(double v)  { g_set.va_depth_m   = (float)v; }
static void kp_jog_speed(double v)
{
  g_set.jog_speed_mh = (float)v;
  if (ENC_GetMode() == ENC_RUN)                      /* applique en direct (SPEED) */
  {
    ENC_Status_t st = ENC_SetSpeed(ENC_MhToHz(v));
    if (st != ENC_OK) enc_err(st);
    else { char a[24], m[40]; fmt_trim(a, sizeof a, v, 1); snprintf(m, sizeof m, "SPEED %smh", a); CMD_Notify(m); }
  }
}
static void kp_ppcm(double v)
{
  ENC_Status_t st = ENC_SetPulsesPerCm(v);
  if (st != ENC_OK) { enc_err(st); return; }
  char a[24], m[40]; fmt_trim(a, sizeof a, v, 2); snprintf(m, sizeof m, "PPCM %s", a); CMD_Notify(m);
}

/* Lance un deplacement relatif de 'meters' (signe = sens : + descente,
 * - remontee) a 'speed_mh', converti en impulsions avec la resolution
 * courante. 'what' sert a la trace terminal ("VA", "JOG"). */
static void do_move_m(double meters, double speed_mh, const char *what)
{
  double p = meters * 100.0 * ENC_GetPulsesPerCm();
  int32_t pulses = (int32_t)(p >= 0.0 ? p + 0.5 : p - 0.5);
  if (pulses == 0) { ui_msg("Pas < 1 impulsion", T->warning); return; }
  ENC_Status_t st = ENC_Move(pulses, ENC_MhToHz(speed_mh));
  if (st != ENC_OK) { enc_err(st); return; }
  char a[16], b[16], m[48];
  fmt_trim(a, sizeof a, speed_mh, 1); fmt_trim(b, sizeof b, meters, 3);
  snprintf(m, sizeof m, "%s %s %s", what, a, b);
  CMD_Notify(m);
}

/* Execute l'action du bouton 'id' (appele au relache, sauf STOP : a l'appui).
 * Les erreurs du module codeur (mouvement deja en cours, vitesse hors plage...)
 * sont affichees dans la barre du haut ; les actions reussies sont tracees
 * dans le terminal par CMD_Notify(). */
static void act(int id)
{
  if (id == B_STOP)
  {
    ENC_Stop();
    CMD_Notify("STOP");
    return;
  }
  if (id >= B_TAB0 && id <= B_TAB5)
  {
    Page_t p = (Page_t)(id - B_TAB0);
    if (p != s_page) { s_page = p; ui_full_redraw(); }
    return;
  }
  switch (id)
  {
    /* champs de saisie : ouvre le pave numerique (titre, unite, valeur
     * actuelle, min, max, nb de decimales, callback appele sur OK) */
    case B_VA_SPEED:  KP_Open("Vitesse VA", "m/h", g_set.va_speed_mh, 0.1, ENC_VMAX_MH, 1, kp_va_speed); ui_full_redraw(); return;
    case B_VA_DEPTH:  KP_Open("Profondeur VA", "m", g_set.va_depth_m, 0.001, 10000.0, 3, kp_va_depth); ui_full_redraw(); return;
    case B_JOG_SPEED: KP_Open("Vitesse JOG/RUN", "m/h", g_set.jog_speed_mh, 0.1, ENC_VMAX_MH, 1, kp_jog_speed); ui_full_redraw(); return;
    case B_PPCM:
      if (ENC_GetMode() != ENC_IDLE) { ui_msg("Arreter d'abord", T->warning); return; }
      KP_Open("Resolution", "imp/cm", ENC_GetPulsesPerCm(), ENC_PPCM_MIN, ENC_PPCM_MAX, 2, kp_ppcm);
      ui_full_redraw();
      return;

    case B_VA_DOWN:   do_move_m(+g_set.va_depth_m, g_set.va_speed_mh, "VA"); return;
    case B_VA_UP:     do_move_m(-g_set.va_depth_m, g_set.va_speed_mh, "VA"); return;

    case B_RUN_UP:
    case B_RUN_DOWN:
    {
      double hz = ENC_MhToHz(g_set.jog_speed_mh) * (id == B_RUN_UP ? -1.0 : 1.0);
      ENC_Status_t st = ENC_Run(hz);
      if (st != ENC_OK) { enc_err(st); return; }
      char a[16], m[32]; fmt_trim(a, sizeof a, g_set.jog_speed_mh, 1);
      snprintf(m, sizeof m, "RUN %s%smh", id == B_RUN_UP ? "-" : "", a);
      CMD_Notify(m);
      return;
    }

    case B_CAL:       cal_start(); return;
    case B_RST:       ENC_Zero(); CMD_Notify("RST"); ui_msg("Position remise a 0", T->success); return;
    case B_TH_DARK:
    case B_TH_LIGHT:
      THEME_Set(id == B_TH_LIGHT ? THEME_LIGHT : THEME_DARK);
      g_set.theme = (uint8_t)THEME_Get();
      ui_full_redraw();
      return;

    default:
      if (id >= B_JOG_U0 && id <= B_JOG_U3)
        do_move_m(-s_jog_cm[id - B_JOG_U0] / 100.0, g_set.jog_speed_mh, "JOG");
      else if (id >= B_JOG_D0 && id <= B_JOG_D3)
        do_move_m(+s_jog_cm[id - B_JOG_D0] / 100.0, g_set.jog_speed_mh, "JOG");
      else if (id >= B_SCEN0 && id <= B_SCEN3)
      {
        uint16_t n = (uint16_t)(id - B_SCEN0 + 1);
        ENC_Status_t st = SCN_Start(n);
        if (st != ENC_OK) { enc_err(st); return; }
        char m[16]; snprintf(m, sizeof m, "SCEN %u", (unsigned)n); CMD_Notify(m);
      }
      return;
  }
}

/* ========================================================================== */
/* Calibration tactile : 5 croix -> affine par moindres carres                */
/* ========================================================================== */
/* Croix de visee de 25 px centree en (x, y), point central rouge */
static void cal_cross(int16_t x, int16_t y, uint16_t c)
{
  TFT_FillRect((uint16_t)(x - 12), (uint16_t)y, 25, 1, c);
  TFT_FillRect((uint16_t)x, (uint16_t)(y - 12), 1, 25, c);
  TFT_FillRect((uint16_t)(x - 1), (uint16_t)(y - 1), 3, 3, T->danger);
}

/* Ecran de calibration : croix a toucher (etapes 0..4) ou resultat (etape 5)
 * avec l'ecart entre les croix et la position calculee par la nouvelle
 * calibration. Vert si l'ecart max est <= 6 px. */
static void cal_draw(void)
{
  char m[48];
  GRect line = { 0, 52, TFT_WIDTH, 16 };
  TFT_FillScreen(T->bg);
  if (s_cal_step < CAL_N)
  {
    GUI_TextCenter(&line, "CALIBRATION TACTILE", T->text, T->bg, 2);
    snprintf(m, sizeof m, "Touchez le centre de la croix  %d/%d", s_cal_step + 1, CAL_N);
    line.y = 76; line.h = 8;  GUI_TextCenter(&line, m, T->muted, T->bg, 1);
    line.y = 88;              GUI_TextCenter(&line, "avec un stylet si possible", T->muted, T->bg, 1);
    cal_cross(s_cal_tx[s_cal_step], s_cal_ty[s_cal_step], T->accent);
    return;
  }
  /* resultat */
  float emax = 0.0f, esum = 0.0f;
  for (int i = 0; i < CAL_N; i++)
  {
    const TOUCH_Cal_t *c = &s_cal_new;
    float dx = c->a * s_cal_rx[i] + c->b * s_cal_ry[i] + c->c - (float)s_cal_tx[i];
    float dy = c->d * s_cal_rx[i] + c->e * s_cal_ry[i] + c->f - (float)s_cal_ty[i];
    float e = dx * dx + dy * dy, r = 0.0f;
    if (e > 0.0f) { r = e; for (int k = 0; k < 8; k++) r = 0.5f * (r + e / r); }   /* sqrt */
    if (r > emax) emax = r;
    esum += r;
  }
  char a[12], b[12];
  fmt_fix(a, sizeof a, emax, 1); fmt_fix(b, sizeof b, esum / CAL_N, 1);
  line.y = 24; GUI_TextCenter(&line, "CALIBRATION", T->text, T->bg, 2);
  snprintf(m, sizeof m, "Ecart max %s px   moyen %s px", a, b);
  line.y = 50; line.h = 8; GUI_TextCenter(&line, m, emax <= 6.0f ? T->success : T->warning, T->bg, 1);
  line.y = 66; GUI_TextCenter(&line, "Touchez l'ecran pour verifier", T->muted, T->bg, 1);
  for (int i = 0; i < CAL_N; i++) cal_cross(s_cal_tx[i], s_cal_ty[i], T->border);
  GUI_Button(&s_cal_ok,   "VALIDER", 2, GST_ACCENT, GIC_NONE, false);
  GUI_Button(&s_cal_redo, "REFAIRE", 2, GST_NORMAL, GIC_NONE, false);
}

/* Demarre la calibration en memorisant la calibration actuelle (pour REFAIRE / abandon) */
static void cal_start(void)
{
  TOUCH_Cal_t *cur = (TOUCH_Cal_t *)g_set.tcal;
  if (g_set.tcal_ok) s_cal_old = *cur; else TOUCH_DefaultCal(&s_cal_old);
  s_cal_step = 0;
  s_cal_sx = s_cal_sy = s_cal_cnt = 0;
  s_cal_t = HAL_GetTick();
  cal_draw();
}

/* Termine la calibration : keep = true -> adopte et memorise (flash) la
 * nouvelle ; false -> restaure l'ancienne. Retour a la page REGL. */
static void cal_end(bool keep)
{
  if (keep)
  {
    memcpy(g_set.tcal, &s_cal_new, sizeof g_set.tcal);
    g_set.tcal_ok = 1;
    TOUCH_SetCal(&s_cal_new);
    ui_msg("Calibration OK", T->success);
    CMD_Notify("calibration tactile enregistree");
  }
  else TOUCH_SetCal(&s_cal_old);
  s_cal_step = -1;
  ui_full_redraw();
}

/* Appele a chaque lecture tactile (20 ms) pendant la calibration.
 * p : doigt detecte ; press_edge / release_edge : appui / relache valides.
 * Pour chaque croix : les 2 premieres lectures de l'appui sont ignorees (le
 * doigt s'etale), les suivantes sont moyennees jusqu'au relache. */
static void cal_touch(bool p, const TOUCH_State_t *t, bool press_edge, bool release_edge)
{
  if (p) s_cal_t = HAL_GetTick();
  else if ((HAL_GetTick() - s_cal_t) > CAL_TIMEOUT) { cal_end(false); return; }

  if (s_cal_step < CAL_N)
  {
    if (press_edge) { s_cal_sx = s_cal_sy = s_cal_cnt = 0; }
    if (p && s_tp_down)
    {
      static uint8_t skip;
      if (press_edge) skip = 2;                       /* ignore le debut de l'appui */
      if (skip) skip--; else { s_cal_sx += t->raw_x; s_cal_sy += t->raw_y; s_cal_cnt++; }
    }
    if (release_edge && s_cal_cnt >= 3u)
    {
      s_cal_rx[s_cal_step] = (float)s_cal_sx / (float)s_cal_cnt;
      s_cal_ry[s_cal_step] = (float)s_cal_sy / (float)s_cal_cnt;
      s_cal_step++;
      if (s_cal_step == CAL_N)
      {
        float sx[CAL_N], sy[CAL_N];
        for (int i = 0; i < CAL_N; i++) { sx[i] = s_cal_tx[i]; sy[i] = s_cal_ty[i]; }
        if (!TOUCH_ComputeCal(s_cal_rx, s_cal_ry, sx, sy, CAL_N, &s_cal_new))
        { s_cal_step = 0; ui_msg("Calibration impossible", T->danger); cal_draw(); return; }
        TOUCH_SetCal(&s_cal_new);                       /* pour tester et viser les boutons */
      }
      cal_draw();
    }
    return;
  }

  /* ecran resultat : test libre + VALIDER / REFAIRE */
  if (press_edge)
  {
    if (GUI_Hit(&s_cal_ok, t->x, t->y))        { cal_end(true); return; }
    if (GUI_Hit(&s_cal_redo, t->x, t->y))      { TOUCH_SetCal(&s_cal_old); s_cal_step = 0; cal_draw(); return; }
  }
  if (p) TFT_FillRect((uint16_t)(t->x > 0 ? t->x - 1 : 0), (uint16_t)(t->y > 0 ? t->y - 1 : 0), 3, 3, T->accent);
}

/* ========================================================================== */
/* Tactile : appui -> bouton enfonce ; relache dedans -> action               */
/*           STOP agit des l'appui                                            */
/* ========================================================================== */
/* Machine d'etat appelee toutes les TOUCH_MS :
 *   - appui confirme  : memorise le bouton sous le doigt, le dessine enfonce
 *                       (STOP agit tout de suite pour une reaction immediate) ;
 *   - doigt pose      : si le doigt sort du bouton, le bouton redevient normal
 *                       et l'action est annulee (comme sur un smartphone) ;
 *   - relache confirme: si le doigt etait encore sur le bouton -> act().
 * Pendant la calibration, les evenements sont passes a cal_touch(). */
static void touch_task(void)
{
  TOUCH_State_t t;
  bool p = TOUCH_Read(&t);

  /* anti-rebond : 2 lectures identiques consecutives */
  if (p) { s_tp_off = 0; if (s_tp_on < 2u) s_tp_on++; }
  else   { s_tp_on = 0;  if (s_tp_off < 2u) s_tp_off++; }

  if (s_cal_step >= 0)
  {
    bool pe = (!s_tp_down && s_tp_on >= 2u);
    bool re = ( s_tp_down && s_tp_off >= 2u);
    if (pe) s_tp_down = true;
    if (re) s_tp_down = false;
    cal_touch(p, &t, pe, re);
    return;
  }

  if (!s_tp_down && s_tp_on >= 2u)                    /* appui confirme */
  {
    s_tp_down = true;
    s_tx = t.x; s_ty = t.y;
#if UI_TOUCH_DEBUG
    {
      char m[64];
      snprintf(m, sizeof m, "touch brut x=%u y=%u z=%u -> px %u,%u bouton %d",
               t.raw_x, t.raw_y, t.z, t.x, t.y,
               KP_Active() ? KP_Hit(t.x, t.y) : btn_hit(t.x, t.y));
      CMD_Notify(m);
    }
#endif
    if (KP_Active())
    {
      s_down_btn = KP_Hit(s_tx, s_ty);
      if (s_down_btn >= 0) KP_DrawKey(s_down_btn, true);
    }
    else
    {
      s_down_btn = btn_hit(s_tx, s_ty);
      if (s_down_btn != B_NONE)
      {
        draw_btn(s_down_btn, true);
        if (s_down_btn == B_STOP) act(B_STOP);
      }
    }
    s_down_inside = true;
  }
  else if (s_tp_down && p)                            /* suivi du doigt */
  {
    s_tx = t.x; s_ty = t.y;
    if (s_down_btn >= 0)
    {
      bool in = KP_Active() ? (KP_Hit(s_tx, s_ty) == s_down_btn)
                            : GUI_Hit(&s_btn[s_down_btn].r, s_tx, s_ty);
      if (in != s_down_inside)
      {
        s_down_inside = in;
        if (KP_Active()) KP_DrawKey(s_down_btn, in); else draw_btn(s_down_btn, in);
      }
    }
  }
  else if (s_tp_down && s_tp_off >= 2u)               /* relache confirme */
  {
    s_tp_down = false;
    int id = s_down_btn;
    s_down_btn = B_NONE;
    if (id < 0) return;

    if (KP_Active())
    {
      KP_DrawKey(id, false);
      if (s_down_inside && !KP_Key(id)) ui_full_redraw();   /* pave ferme */
    }
    else
    {
      draw_btn(id, false);
      if (s_down_inside && id != B_STOP) act(id);
    }
  }
}

/* ========================================================================== */
/* API                                                                        */
/* ========================================================================== */
/* Ordre important : les reglages doivent etre charges avant d'initialiser le
 * tactile (calibration) et avant CMD_Init (affichage du PPCM memorise). */
void UI_Init(void)
{
  SETTINGS_Load();
  (void)ENC_SetPulsesPerCm(g_set.ppcm);
  THEME_Set((ThemeId_t)g_set.theme);
  TOUCH_SetCal(g_set.tcal_ok ? (const TOUCH_Cal_t *)g_set.tcal : NULL);

  TFT_Init();
  TOUCH_Init();
  SPLASH_Draw();                 /* 5 s, non bloquant : les commandes restent actives */
  s_splash    = true;
  s_splash_t0 = HAL_GetTick();

  OLED_UI_Init();                /* OLED SSD1306 (facultatif) : splash 5 s puis tableau de bord */
}

/* Ordonnanceur de l'IHM (cooperatif, base sur HAL_GetTick) :
 *   - a chaque appel : sauvegarde flash differee + echantillon du graphe ;
 *   - pendant le splash : barre de progression, puis 1er dessin complet ;
 *   - toutes les TOUCH_MS : lecture du tactile ;
 *   - toutes les UI_REFRESH_MS : mise a jour des valeurs (sauf pendant le
 *     pave numerique ou la calibration, qui gerent leur propre affichage). */
void UI_Task(void)
{
  uint32_t now = HAL_GetTick();

  SETTINGS_Task();
  graph_sample();
  OLED_UI_Task();                /* avant le test du splash TFT : l'OLED vit sa propre vie */

  if (s_splash)
  {
    uint32_t el = now - s_splash_t0;
    SPLASH_Progress(el);
    if (el < SPLASH_MS) return;
    s_splash = false;
    ui_full_redraw();
    s_last_ui = now;
  }

  if ((now - s_last_touch) >= TOUCH_MS)
  {
    s_last_touch = now;
    touch_task();
  }

  if (!KP_Active() && s_cal_step < 0 && (now - s_last_ui) >= UI_REFRESH_MS)
  {
    s_last_ui = now;
    ui_update();
  }
}
