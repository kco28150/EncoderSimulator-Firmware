/**
  ******************************************************************************
  * @file    scenario.c
  * @brief   Scenarios de vitesse : la distance est decoupee en SCN_NSEG
  *          segments, chacun a vitesse constante (milieu du segment).
  *          Les tables sont recalculees a chaque lancement avec la
  *          resolution courante (PPCM).
  ******************************************************************************
  */
#include "scenario.h"

#define SCN_NSEG   500u            /* 1 m -> un palier tous les 2 mm */
#define SCN_PI     3.14159265358979323846

/* ---- Scenarios enregistres ------------------------------------------------ */
static const SCN_Def_t s_scn[] =
{
  /* name                                       type        dist   vmin   vmax  param */
  { "Sinus 1 m, 2 periodes, 50 a 500 m/h",      SCN_SINE,   100.0, 50.0, 500.0, 2.0  },
  { "Escalier 1 m, 10 marches de 10 cm, 50 a 500 m/h",
                                                SCN_STAIRS, 100.0, 50.0, 500.0, 10.0 },
};
#define SCN_N  (sizeof(s_scn) / sizeof(s_scn[0]))

static uint32_t s_ticks[SCN_NSEG];
static uint32_t s_states[SCN_NSEG];
static uint16_t s_running = 0;

/* cos() sans libm : reduction a [-pi, pi] puis serie de Taylor (err < 1e-6) */
static double scn_cos(double x)
{
  while (x >  SCN_PI) x -= 2.0 * SCN_PI;
  while (x < -SCN_PI) x += 2.0 * SCN_PI;
  double x2 = x * x, term = 1.0, sum = 1.0;
  for (int n = 1; n <= 10; n++)
  {
    term *= -x2 / (double)((2 * n - 1) * (2 * n));
    sum  += term;
  }
  return sum;
}

double SCN_SpeedAt(const SCN_Def_t *s, double d_cm)
{
  double D = (s->dist_cm < 0.0) ? -s->dist_cm : s->dist_cm;
  switch (s->type)
  {
    case SCN_STAIRS:
    {
      int n = (int)(s->param + 0.5);
      if (n < 2) return s->vmin_mh;
      int k = (int)(d_cm / D * n);               /* marche courante 0..n-1 */
      if (k > n - 1) k = n - 1;
      if (k < 0)     k = 0;
      return s->vmin_mh + (s->vmax_mh - s->vmin_mh) * (double)k / (double)(n - 1);
    }

    case SCN_SINE:
    default:
      return s->vmin_mh + (s->vmax_mh - s->vmin_mh)
             * (1.0 - scn_cos(2.0 * SCN_PI * s->param * d_cm / D)) * 0.5;
  }
}

double SCN_DurationS(const SCN_Def_t *s)
{
  double D = (s->dist_cm < 0.0) ? -s->dist_cm : s->dist_cm;
  double t_h = 0.0;
  const int n = 400;
  for (int i = 0; i < n; i++)
    t_h += (D / n / 100.0) / SCN_SpeedAt(s, D * ((double)i + 0.5) / n);   /* m / (m/h) */
  return t_h * 3600.0;
}

uint16_t SCN_Count(void) { return (uint16_t)SCN_N; }

const SCN_Def_t *SCN_Get(uint16_t idx)
{
  return (idx >= 1u && idx <= SCN_N) ? &s_scn[idx - 1u] : 0;
}

uint16_t SCN_Running(void)
{
  if (!ENC_IsProfile()) s_running = 0;
  return s_running;
}

ENC_Status_t SCN_Start(uint16_t idx)
{
  const SCN_Def_t *s = SCN_Get(idx);
  if (s == 0)                    return ENC_ERR_COUNT;
  if (ENC_GetMode() != ENC_IDLE) return ENC_ERR_BUSY;

  double   D      = (s->dist_cm < 0.0) ? -s->dist_cm : s->dist_cm;
  double   p      = D * ENC_GetPulsesPerCm();
  uint32_t total  = (uint32_t)(p + 0.5) * ENC_STATES_PER_PULSE;
  uint16_t nseg   = (total < SCN_NSEG) ? (uint16_t)total : (uint16_t)SCN_NSEG;
  if (nseg == 0u) return ENC_ERR_COUNT;

  /* Decoupage en etats sans perte (somme exacte = total) */
  uint32_t prev = 0;
  for (uint16_t i = 0; i < nseg; i++)
  {
    uint32_t end = (uint32_t)(((uint64_t)total * (i + 1u)) / nseg);
    s_states[i]  = end - prev;
    prev         = end;

    double d_mid = D * ((double)i + 0.5) / (double)nseg;     /* cm */
    double hz    = ENC_MhToHz(SCN_SpeedAt(s, d_mid));
    if (!ENC_HzValid(hz)) return ENC_ERR_FREQ;
    s_ticks[i]   = ENC_HzToTicks(hz);
  }

  ENC_Status_t st = ENC_MoveProfile((s->dist_cm < 0.0) ? -1 : 1, s_ticks, s_states, nseg);
  if (st == ENC_OK) s_running = idx;
  return st;
}
