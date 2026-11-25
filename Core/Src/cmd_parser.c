/**
  ******************************************************************************
  * @file    cmd_parser.c
  * @brief   Commandes ASCII ligne par ligne (115200 8N1, fin de ligne CR/LF/CRLF)
  *
  *   MOVE x f    x impulsions a f (signe de x = sens)
  *   RUN f       flux continu (signe de f = sens) jusqu'a STOP
  *   SPEED f     change la frequence pendant un mouvement
  *   STOP        arret immediat, sorties figees
  *   POS?        position courante
  *   ZERO        remise a zero de la position
  *   STATUS?     etat complet
  *   HELP / ?    aide
  *
  *   Unites : x en imp (defaut), mm, cm, m      f en Hz (defaut), mh (m/h)
  ******************************************************************************
  */
#include "cmd_parser.h"
#include "encoder_sim.h"
#include "scenario.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define RX_BUF_SIZE    256u          /* puissance de 2 */
#define LINE_MAX       80u

static volatile uint8_t  s_rx[RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;   /* ecrit par l'IT   */
static volatile uint16_t s_rx_tail = 0;   /* lu par la boucle */

static char     s_line[LINE_MAX];
static uint16_t s_len = 0;
static bool     s_last_cr = false;
static uint32_t s_act_t = 0;          /* instant de la derniere commande (0 = aucune) */
static bool     s_act_touch = false;   /* true si elle venait du tactile (CMD_Notify)  */

/* ========================================================================== */
/* Bas niveau UART                                                            */
/* ========================================================================== */
/**
 * @brief  Routine d'interruption USART2 (appelee par USART2_IRQHandler a la
 *         place de la HAL). Range l'octet recu dans le tampon circulaire s_rx.
 *         Un octet en erreur de trame ou de bruit est ignore ; si le tampon est
 *         plein, l'octet est perdu. La lecture SR puis DR acquitte aussi
 *         ORE / FE / NE.
 */
void CMD_UartIRQ(void)
{
  uint32_t sr = USART2->SR;

  if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE))
  {
    uint8_t c = (uint8_t)USART2->DR;     /* lecture SR puis DR : efface aussi ORE/FE/NE */
    if ((sr & (USART_SR_FE | USART_SR_NE)) == 0u)
    {
      uint16_t next = (uint16_t)((s_rx_head + 1u) & (RX_BUF_SIZE - 1u));
      if (next != s_rx_tail)
      {
        s_rx[s_rx_head] = c;
        s_rx_head = next;
      }
    }
  }
}

/**
 * @brief  Extrait un octet du tampon de reception (cote boucle principale).
 * @retval Octet recu (0..255), ou -1 si le tampon est vide.
 */
static int rx_get(void)
{
  if (s_rx_tail == s_rx_head) return -1;
  uint8_t c = s_rx[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1u) & (RX_BUF_SIZE - 1u));
  return c;
}

/**
 * @brief  Envoie un caractere sur USART2 en mode bloquant (attente de TXE).
 * @param  c  Caractere a envoyer.
 */
static void tx_char(char c)
{
  while ((USART2->SR & USART_SR_TXE) == 0u) { }
  USART2->DR = (uint8_t)c;
}

/**
 * @brief  Envoie une chaine terminee par '\0' sur USART2 (bloquant).
 * @param  s  Chaine a envoyer.
 */
static void tx_str(const char *s)
{
  while (*s) tx_char(*s++);
}

/* ========================================================================== */
/* Formatage / parsing numerique (sans printf flottant ni libm)               */
/* ========================================================================== */
/**
 * @brief  Formate un nombre en virgule fixe sans printf flottant
 *         (newlib-nano n'a pas %f), arrondi au plus proche.
 * @param  buf  Tampon de sortie.
 * @param  sz   Taille du tampon.
 * @param  v    Valeur a formater.
 * @param  dec  Nombre de decimales (0..4, borne a 4).
 */
static void fmt_fixed(char *buf, size_t sz, double v, unsigned dec)
{
  static const uint32_t p10[] = {1u, 10u, 100u, 1000u, 10000u};
  if (dec > 4u) dec = 4u;
  bool neg = (v < 0.0);
  if (neg) v = -v;

  uint64_t n  = (uint64_t)(v * (double)p10[dec] + 0.5);
  uint32_t ip = (uint32_t)(n / p10[dec]);
  uint32_t fp = (uint32_t)(n % p10[dec]);

  if (dec == 0u)
  {
    snprintf(buf, sz, "%s%lu", neg ? "-" : "", (unsigned long)ip);
  }
  else
  {
    char frac[6];
    for (int i = (int)dec - 1; i >= 0; i--) { frac[i] = (char)('0' + fp % 10u); fp /= 10u; }
    frac[dec] = '\0';
    snprintf(buf, sz, "%s%lu.%s", neg ? "-" : "", (unsigned long)ip, frac);
  }
}

/**
 * @brief  Lit un nombre [+|-]chiffres[.|,chiffres] (virgule ou point
 *         decimal) puis renvoie le suffixe (unite) dans *unit.
 * @param  s     Chaine a analyser.
 * @param  val   Recoit la valeur lue.
 * @param  unit  Recoit un pointeur sur le premier caractere apres le nombre.
 * @retval true si au moins un chiffre a ete lu.
 */
static bool parse_num(const char *s, double *val, const char **unit)
{
  double v = 0.0, scale = 1.0;
  bool neg = false, digits = false, frac = false;

  if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
  for (;; s++)
  {
    if (*s >= '0' && *s <= '9')
    {
      digits = true;
      if (frac) { scale *= 0.1; v += (*s - '0') * scale; }
      else      { v = v * 10.0 + (*s - '0'); }
    }
    else if ((*s == '.' || *s == ',') && !frac) frac = true;
    else break;
  }
  if (!digits) return false;
  *val  = neg ? -v : v;
  *unit = s;
  return true;
}

/**
 * @brief  Convertit une chaine ASCII en minuscules, sur place.
 * @param  s  Chaine a convertir.
 */
static void to_lower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }
/**
 * @brief  Convertit une chaine ASCII en majuscules, sur place.
 * @param  s  Chaine a convertir.
 */
static void to_upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32; }

/**
 * @brief  Convertit un jeton de distance en impulsions.
 *         Unites : (rien) | imp (ou i) | mm | cm | m, converties avec la
 *         resolution courante (PPCM). Le jeton est mis en minuscules sur place.
 * @param  tok       Jeton a analyser (ex. "12.5cm").
 * @param  pulses    Recoit le nombre d'impulsions signe, arrondi au plus proche.
 * @param  def_mult  Multiplicateur applique quand aucune unite n'est donnee.
 * @retval false si syntaxe ou unite invalide, ou resultat hors +/-2e9.
 */
static bool parse_distance_def(char *tok, int32_t *pulses, double def_mult)
{
  double v; const char *u;
  to_lower(tok);
  if (!parse_num(tok, &v, &u)) return false;

  double mult;
  if      (*u == '\0')                        mult = def_mult;
  else if (!strcmp(u, "imp") || !strcmp(u, "i")) mult = 1.0;
  else if (!strcmp(u, "mm")) mult = ENC_GetPulsesPerCm() / 10.0;
  else if (!strcmp(u, "cm")) mult = ENC_GetPulsesPerCm();
  else if (!strcmp(u, "m"))  mult = ENC_GetPulsesPerCm() * 100.0;
  else return false;

  double p = v * mult;
  if (p > 2.0e9 || p < -2.0e9) return false;
  *pulses = (int32_t)(p >= 0.0 ? p + 0.5 : p - 0.5);
  return true;
}

/**
 * @brief  Distance de la commande MOVE : sans unite = impulsions.
 * @param  tok     Jeton a analyser.
 * @param  pulses  Recoit le nombre d'impulsions signe.
 * @retval false si le jeton est invalide.
 */
static bool parse_distance(char *tok, int32_t *pulses)
{
  return parse_distance_def(tok, pulses, 1.0);
}

/**
 * @brief  Convertit un jeton de frequence en Hz.
 *         Unites : (rien) | hz | mh | m/h (m/h converti avec la resolution
 *         courante). Le jeton est mis en minuscules sur place.
 * @param  tok  Jeton a analyser (ex. "800mh").
 * @param  hz   Recoit la frequence en Hz (signee).
 * @retval false si syntaxe ou unite invalide.
 */
static bool parse_freq(char *tok, double *hz)
{
  double v; const char *u;
  to_lower(tok);
  if (!parse_num(tok, &v, &u)) return false;

  if      (*u == '\0' || !strcmp(u, "hz"))   *hz = v;
  else if (!strcmp(u, "mh") || !strcmp(u, "m/h")) *hz = ENC_MhToHz(v);
  else return false;
  return true;
}

/**
 * @brief  Formate une position : "6025.00 imp / 120.500 cm".
 * @param  buf     Tampon de sortie.
 * @param  sz      Taille du tampon.
 * @param  states  Position en etats de Gray (4 etats par impulsion).
 */
static void fmt_pos(char *buf, size_t sz, int32_t states)
{
  char a[20], b[20];
  fmt_fixed(a, sizeof a, (double)states / ENC_STATES_PER_PULSE, 2);
  fmt_fixed(b, sizeof b, (double)states / ENC_STATES_PER_PULSE / ENC_GetPulsesPerCm(), 3);
  snprintf(buf, sz, "%s imp / %s cm", a, b);
}

/**
 * @brief  Formate une frequence : "1111.111 Hz (800.0 m/h)".
 * @param  buf  Tampon de sortie.
 * @param  sz   Taille du tampon.
 * @param  hz   Frequence en Hz.
 */
static void fmt_freq(char *buf, size_t sz, double hz)
{
  char a[20], b[20];
  fmt_fixed(a, sizeof a, hz, 3);
  fmt_fixed(b, sizeof b, ENC_HzToMh(hz), 1);
  snprintf(buf, sz, "%s Hz (%s m/h)", a, b);
}

/**
 * @brief  Envoie la configuration courante :
 *         "PPCM 50.000 imp/cm  Fmax 1388.889 Hz (1000.0 m/h)  1 m/h = 1.389 Hz".
 * @param  prefix  Texte ajoute en tete de ligne ("" ou "OK ").
 */
static void print_config(const char *prefix)
{
  char a[20], f[40], k[20], buf[120];
  fmt_fixed(a, sizeof a, ENC_GetPulsesPerCm(), 3);
  fmt_freq(f, sizeof f, ENC_GetFmaxHz());
  fmt_fixed(k, sizeof k, ENC_MhToHz(1.0), 4);
  snprintf(buf, sizeof buf, "%sPPCM %s imp/cm  Fmax %s  1 m/h = %s Hz\r\n", prefix, a, f, k);
  tx_str(buf);
}

/**
 * @brief  Envoie le message "ERR ..." correspondant a un code d'erreur du
 *         module codeur.
 * @param  st  Code renvoye par une fonction ENC_xxx / SCN_Start.
 */
static void err_status(ENC_Status_t st)
{
  char f[40], buf[96];
  switch (st)
  {
    case ENC_ERR_BUSY:        tx_str("ERR BUSY mouvement en cours (STOP pour arreter)\r\n"); break;
    case ENC_ERR_FREQ:
      fmt_freq(f, sizeof f, ENC_GetFmaxHz());
      snprintf(buf, sizeof buf, "ERR FREQ hors plage (0.01 Hz .. %s)\r\n", f);
      tx_str(buf);
      break;
    case ENC_ERR_PROFILE:     tx_str("ERR SCEN vitesse imposee par le scenario (STOP pour arreter)\r\n"); break;
    case ENC_ERR_PPCM:        tx_str("ERR PPCM hors plage (0.1 .. 1000 imp/cm)\r\n"); break;
    case ENC_ERR_COUNT:       tx_str("ERR COUNT nombre d'impulsions invalide\r\n"); break;
    case ENC_ERR_NOT_RUNNING: tx_str("ERR IDLE aucun mouvement en cours\r\n"); break;
    default:                  tx_str("ERR\r\n"); break;
  }
}

/**
 * @brief  Duree estimee d'un scenario : integrale de dd / v(d), calculee par
 *         la methode du point milieu sur 400 pas.
 * @param  sc  Scenario.
 * @retval Duree en secondes.
 */
static double scen_duration_s(const SCN_Def_t *sc)
{
  double D = (sc->dist_cm < 0.0) ? -sc->dist_cm : sc->dist_cm;
  double t_h = 0.0;
  const int n = 400;
  for (int i = 0; i < n; i++)
  {
    double d = D * ((double)i + 0.5) / n;                 /* cm */
    t_h += (D / n / 100.0) / SCN_SpeedAt(sc, d);           /* m / (m/h) = h */
  }
  return t_h * 3600.0;
}

/**
 * @brief  Envoie une ligne decrivant un scenario : numero, nom, distance,
 *         vitesses min / max et duree estimee.
 * @param  i       Numero du scenario (1..SCN_Count()).
 * @param  prefix  Texte ajoute en tete de ligne ("SCEN " ou "OK SCEN ").
 */
static void scen_describe(uint16_t i, const char *prefix)
{
  const SCN_Def_t *sc = SCN_Get(i);
  char d[20], a[20], b[20], t[20], buf[160];
  fmt_fixed(d, sizeof d, sc->dist_cm, 1);
  fmt_fixed(a, sizeof a, sc->vmin_mh, 0);
  fmt_fixed(b, sizeof b, sc->vmax_mh, 0);
  fmt_fixed(t, sizeof t, scen_duration_s(sc), 1);
  snprintf(buf, sizeof buf, "%s%u : %s  [%s cm, %s..%s m/h, ~%s s]\r\n",
           prefix, (unsigned)i, sc->name, d, a, b, t);
  tx_str(buf);
}

/* ========================================================================== */
/* Commandes                                                                  */
/* ========================================================================== */
/**
 * @brief  Envoie l'aide : liste des commandes, unites et exemples.
 */
static void cmd_help(void)
{
  tx_str("Commandes :\r\n"
         "  VA v y     vitesse d'avance v (m/h) sur y (m), relatif, y<0 = remontee\r\n"
         "  MOVE x f   x impulsions a f (signe de x = sens)\r\n"
         "  RUN f      flux continu (signe de f = sens) jusqu'a STOP\r\n"
         "  SPEED f    change f pendant un mouvement\r\n"
         "  STOP       arret immediat\r\n"
         "  POS?       position courante\r\n"
         "  RST        remise a zero de la position (RESET, ZERO)\r\n"
         "  STATUS?    etat complet\r\n"
         "  PPCM n     resolution en imp/cm (PPCM? pour lire), a l'arret\r\n"
         "  SCEN n     lance le scenario n (SCEN? pour la liste)\r\n"
         "  HELP       cette aide (ou ?)\r\n"
         "Unites : x = imp (defaut) | mm | cm | m    f = Hz (defaut) | mh\r\n"
         "Ex : VA 300 2.5   VA 500 -50cm   MOVE 120.5cm 800mh   RUN -500mh\r\n");
}

/**
 * @brief  Interprete et execute une ligne de commande.
 *         La ligne est decoupee en 4 mots maximum ; la commande est insensible
 *         a la casse et un '?' final est ignore (POS? == POS). Repond "OK ...",
 *         une valeur ou "ERR ...". Memorise l'instant de la commande (activite
 *         "PC" affichee sur l'OLED).
 * @param  line  Ligne terminee par '\0' (modifiee : strtok, casse).
 */
static void execute(char *line)
{
  char *argv[4];
  int argc = 0;
  char buf[128];

  for (char *t = strtok(line, " \t"); t && argc < 4; t = strtok(NULL, " \t"))
    argv[argc++] = t;
  if (argc == 0) return;

  s_act_t = HAL_GetTick() | 1u;          /* | 1 : jamais 0, meme a t = 0 */
  s_act_touch = false;

  to_upper(argv[0]);
  size_t l = strlen(argv[0]);
  if (l > 0 && argv[0][l - 1] == '?') argv[0][l - 1] = '\0';   /* POS? == POS */

  if (!strcmp(argv[0], "VA"))
  {
    /* VA x y : vitesse d'avance x (m/h par defaut, ou Hz), sur y (m par defaut, relatif, signe = sens) */
    double v, hz; const char *u; int32_t p;
    if (argc != 3) { tx_str("ERR SYNTAX VA vitesse profondeur  (ex : VA 300 2.5)\r\n"); return; }
    to_lower(argv[1]);
    if (!parse_num(argv[1], &v, &u) || v <= 0.0) { tx_str("ERR VALUE vitesse\r\n"); return; }
    if      (*u == '\0' || !strcmp(u, "mh") || !strcmp(u, "m/h")) hz = ENC_MhToHz(v);
    else if (!strcmp(u, "hz"))                                     hz = v;
    else { tx_str("ERR VALUE vitesse (unite : mh ou hz)\r\n"); return; }
    if (!parse_distance_def(argv[2], &p, ENC_GetPulsesPerCm() * 100.0))
    { tx_str("ERR VALUE profondeur\r\n"); return; }
    ENC_Status_t st = ENC_Move(p, hz);
    if (st != ENC_OK) { err_status(st); return; }
    char f[40], d[20];
    fmt_freq(f, sizeof f, ENC_GetHz());
    fmt_fixed(d, sizeof d, (double)p / ENC_GetPulsesPerCm() / 100.0, 3);
    snprintf(buf, sizeof buf, "OK VA %s m %s a %s (%ld imp)\r\n",
             d, (p > 0) ? "descente" : "remontee", f, (long)p);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "MOVE"))
  {
    int32_t p; double hz;
    if (argc != 3)                    { tx_str("ERR SYNTAX MOVE x f\r\n"); return; }
    if (!parse_distance(argv[1], &p)) { tx_str("ERR VALUE x\r\n"); return; }
    if (!parse_freq(argv[2], &hz) || hz < 0.0) { tx_str("ERR VALUE f\r\n"); return; }
    ENC_Status_t st = ENC_Move(p, hz);
    if (st != ENC_OK) { err_status(st); return; }
    char f[40];
    fmt_freq(f, sizeof f, ENC_GetHz());
    snprintf(buf, sizeof buf, "OK MOVE %ld imp a %s\r\n", (long)p, f);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "RUN"))
  {
    double hz;
    if (argc != 2)                 { tx_str("ERR SYNTAX RUN f\r\n"); return; }
    if (!parse_freq(argv[1], &hz)) { tx_str("ERR VALUE f\r\n"); return; }
    ENC_Status_t st = ENC_Run(hz);
    if (st != ENC_OK) { err_status(st); return; }
    char f[40];
    fmt_freq(f, sizeof f, ENC_GetHz());
    snprintf(buf, sizeof buf, "OK RUN %s a %s\r\n", (hz < 0.0) ? "-" : "+", f);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "SPEED"))
  {
    double hz;
    if (argc != 2)                            { tx_str("ERR SYNTAX SPEED f\r\n"); return; }
    if (!parse_freq(argv[1], &hz) || hz < 0.0) { tx_str("ERR VALUE f\r\n"); return; }
    ENC_Status_t st = ENC_SetSpeed(hz);
    if (st != ENC_OK) { err_status(st); return; }
    char f[40];
    fmt_freq(f, sizeof f, ENC_GetHz());
    snprintf(buf, sizeof buf, "OK SPEED %s\r\n", f);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "STOP"))
  {
    ENC_Stop();
    (void)ENC_TakeDone();
    char p[48];
    fmt_pos(p, sizeof p, ENC_GetStates());
    snprintf(buf, sizeof buf, "OK STOP pos=%s\r\n", p);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "POS"))
  {
    char p[48];
    fmt_pos(p, sizeof p, ENC_GetStates());
    snprintf(buf, sizeof buf, "POS %s\r\n", p);
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "PPCM"))
  {
    if (argc == 1) { print_config(""); return; }
    double v; const char *u;
    if (argc != 2 || !parse_num(argv[1], &v, &u) || *u != '\0')
    { tx_str("ERR SYNTAX PPCM n\r\n"); return; }
    ENC_Status_t st = ENC_SetPulsesPerCm(v);
    if (st != ENC_OK) { err_status(st); return; }
    print_config("OK ");
  }
  else if (!strcmp(argv[0], "SCEN"))
  {
    if (argc == 1)
    {
      for (uint16_t i = 1; i <= SCN_Count(); i++) scen_describe(i, "SCEN ");
      return;
    }
    double v; const char *u;
    if (argc != 2 || !parse_num(argv[1], &v, &u) || *u != '\0' || v < 1.0)
    { tx_str("ERR SYNTAX SCEN n\r\n"); return; }
    uint16_t n = (uint16_t)v;
    if (SCN_Get(n) == 0) { tx_str("ERR SCEN inconnu (SCEN? pour la liste)\r\n"); return; }
    ENC_Status_t st = SCN_Start(n);
    if (st != ENC_OK) { err_status(st); return; }
    scen_describe(n, "OK SCEN ");
  }
  else if (!strcmp(argv[0], "ZERO") || !strcmp(argv[0], "RESET") || !strcmp(argv[0], "RST"))
  {
    ENC_Zero();
    tx_str("OK ZERO\r\n");
  }
  else if (!strcmp(argv[0], "STATUS"))
  {
    char p[48], f[40], r[20];
    ENC_Mode_t m = ENC_GetMode();
    fmt_pos(p, sizeof p, ENC_GetStates());
    if (m == ENC_IDLE)
    {
      snprintf(buf, sizeof buf, "STATUS IDLE pos=%s\r\n", p);
    }
    else
    {
      fmt_freq(f, sizeof f, ENC_GetHz());
      if (m == ENC_MOVE)
      {
        fmt_fixed(r, sizeof r, (double)ENC_GetRemainingStates() / ENC_STATES_PER_PULSE, 2);
        uint16_t sn = SCN_Running();
        if (sn) snprintf(buf, sizeof buf, "STATUS SCEN %u %c %s reste=%s imp\r\n",
                         (unsigned)sn, ENC_GetDir() > 0 ? '+' : '-', f, r);
        else    snprintf(buf, sizeof buf, "STATUS MOVE %c %s reste=%s imp\r\n",
                         ENC_GetDir() > 0 ? '+' : '-', f, r);
      }
      else
      {
        snprintf(buf, sizeof buf, "STATUS RUN %c %s\r\n", ENC_GetDir() > 0 ? '+' : '-', f);
      }
      tx_str(buf);
      snprintf(buf, sizeof buf, "       pos=%s\r\n", p);
    }
    tx_str(buf);
  }
  else if (!strcmp(argv[0], "HELP") || argv[0][0] == '\0')
  {
    cmd_help();
  }
  else
  {
    tx_str("ERR UNKNOWN (HELP pour l'aide)\r\n");
  }
}

/* ========================================================================== */
/* API                                                                        */
/* ========================================================================== */
/**
 * @brief  Affiche l'invite de saisie "> ".
 */
static void prompt(void) { tx_str("> "); }

/**
 * @brief  Initialise l'interpreteur : active l'interruption de reception
 *         USART2 (RXNE) puis affiche la banniere, la configuration, l'aide et
 *         l'invite. A appeler apres MX_USART2_UART_Init() et apres le chargement
 *         des reglages (pour afficher le PPCM memorise).
 */
void CMD_Init(void)
{
  USART2->CR1 |= USART_CR1_RXNEIE;
  tx_str("\r\n=== Simulateur codeur quadrature - NUCLEO-F411RE ===\r\n"
         "A=PA8 (D7)  B=PA9 (D8)  push-pull 3.3V\r\n");
  print_config("");
  cmd_help();
  prompt();
}

/**
 * @brief  Trace dans le terminal une action faite depuis l'ecran tactile
 *         ("[TACTILE] msg"), puis reaffiche l'invite et la saisie en cours.
 *         L'activite est marquee comme venant du tactile (affichage OLED).
 * @param  msg  Texte de l'action (ex. "STOP", "VA 300 2.5").
 */
void CMD_Notify(const char *msg)
{
  s_act_t = HAL_GetTick() | 1u;
  s_act_touch = true;
  tx_str("\r\n[TACTILE] ");
  tx_str(msg);
  tx_str("\r\n");
  prompt();
  for (uint16_t i = 0; i < s_len; i++) tx_char(s_line[i]);
}

/**
 * @brief  Instant de la derniere commande recue (terminal ou tactile).
 * @param  from_touch  Si non NULL, recoit true si elle venait du tactile.
 * @retval Valeur de HAL_GetTick() a la derniere commande, 0 si aucune.
 */
uint32_t CMD_LastActivity(bool *from_touch)
{
  if (from_touch) *from_touch = s_act_touch;
  return s_act_t;
}

/**
 * @brief  Tache de l'interpreteur, a appeler en boucle dans main() :
 *         - signale la fin d'un MOVE ("DONE pos=...") et reaffiche la saisie ;
 *         - allume la LED LD2 pendant un mouvement ;
 *         - traite les caracteres recus : echo, retour arriere, Echap (efface
 *           la ligne) et execution sur CR, LF ou CRLF.
 *         Non bloquant (hors emission des reponses).
 */
void CMD_Process(void)
{
  int c;

  /* Fin de MOVE : message asynchrone, puis on reaffiche la saisie en cours */
  if (ENC_TakeDone())
  {
    char p[48], buf[72];
    fmt_pos(p, sizeof p, ENC_GetStates());
    snprintf(buf, sizeof buf, "\r\nDONE pos=%s\r\n", p);
    tx_str(buf);
    prompt();
    for (uint16_t i = 0; i < s_len; i++) tx_char(s_line[i]);
  }

  /* LED verte allumee pendant un mouvement */
  GPIOA->BSRR = (ENC_GetMode() != ENC_IDLE) ? LD2_Pin : ((uint32_t)LD2_Pin << 16);

  while ((c = rx_get()) >= 0)
  {
    if (c == '\r' || c == '\n')
    {
      if (c == '\n' && s_last_cr) { s_last_cr = false; continue; }  /* CRLF */
      s_last_cr = (c == '\r');
      tx_str("\r\n");
      s_line[s_len] = '\0';
      execute(s_line);
      s_len = 0;
      prompt();
      continue;
    }
    s_last_cr = false;

    if (c == 0x08 || c == 0x7F)                     /* retour arriere */
    {
      if (s_len > 0) { s_len--; tx_str("\b \b"); }
    }
    else if (c == 0x1B)                             /* Echap : efface la ligne */
    {
      while (s_len > 0) { s_len--; tx_str("\b \b"); }
    }
    else if (c >= 0x20 && c < 0x7F && s_len < LINE_MAX - 1u)
    {
      s_line[s_len++] = (char)c;
      tx_char((char)c);
    }
  }
}
