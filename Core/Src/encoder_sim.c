/**
  ******************************************************************************
  * @file    encoder_sim.c
  * @brief   Generation A/B en quadrature : TIM2 tique a 4*f, chaque tick
  *          avance (ou recule) d'un etat de Gray. Sorties ecrites par BSRR.
  ******************************************************************************
  */
#include "encoder_sim.h"
#include "main.h"

/* Table BSRR des 4 etats (A en avance sur B pour dir = +1) :
 *   phase 0 : A=0 B=0
 *   phase 1 : A=1 B=0
 *   phase 2 : A=1 B=1
 *   phase 3 : A=0 B=1
 * Push-pull 3,3 V : bit a 1 = 3,3 V, bit a 0 = 0 V. */
#define A_SET   ((uint32_t)ENC_A_Pin)
#define A_RST   ((uint32_t)ENC_A_Pin << 16)
#define B_SET   ((uint32_t)ENC_B_Pin)
#define B_RST   ((uint32_t)ENC_B_Pin << 16)

static const uint32_t gray_bsrr[4] =
{
  A_RST | B_RST,
  A_SET | B_RST,
  A_SET | B_SET,
  A_RST | B_SET
};

static volatile ENC_Mode_t s_mode      = ENC_IDLE;
static volatile int8_t     s_dir       = 1;
static volatile uint8_t    s_phase     = 0;
static volatile int32_t    s_pos       = 0;     /* en etats */
static volatile uint32_t   s_remaining = 0;     /* en etats */
static uint32_t            s_total     = 0;     /* en etats, pour la progression */
static volatile bool       s_done      = false;
static uint32_t            s_timclk    = 100000000u;
static volatile uint32_t   s_ticks     = 0;     /* periode courante (ARR+1) */

/* Profil de vitesse (scenarios) */
static volatile bool       s_prof      = false;
static const uint32_t     *s_prof_ticks;
static const uint32_t     *s_prof_states;
static uint16_t            s_prof_nseg;
static volatile uint16_t   s_prof_seg;
static volatile uint32_t   s_prof_left;         /* etats restants dans le segment */
static double              s_ppcm      = ENC_DEFAULT_PPCM;

/* -------------------------------------------------------------------------- */
static uint32_t hz_to_ticks(double hz)
{
  double t = (double)s_timclk / (4.0 * hz) + 0.5;
  if (t > 4294967295.0) t = 4294967295.0;
  if (t < 2.0)          t = 2.0;
  return (uint32_t)t;
}

static bool hz_valid(double hz)
{
  return (hz >= ENC_FMIN_HZ) && (hz <= ENC_GetFmaxHz() * (1.0 + 1e-9));
}

static void timer_halt(void)
{
  TIM2->CR1  &= ~TIM_CR1_CEN;
  TIM2->DIER &= ~TIM_DIER_UIE;
  TIM2->SR    = 0;
  NVIC_ClearPendingIRQ(TIM2_IRQn);
}

static void timer_start(uint32_t ticks)
{
  timer_halt();
  TIM2->CR1 |= TIM_CR1_URS | TIM_CR1_ARPE;  /* UG ne declenche pas d'IT */
  TIM2->PSC  = 0;
  TIM2->ARR  = ticks - 1u;
  TIM2->CNT  = 0;
  TIM2->EGR  = TIM_EGR_UG;                  /* charge ARR/PSC dans les shadows */
  TIM2->SR   = 0;
  TIM2->DIER |= TIM_DIER_UIE;
  TIM2->CR1 |= TIM_CR1_CEN;
}

/* -------------------------------------------------------------------------- */
void ENC_Init(void)
{
  /* Horloge TIM2 = PCLK1, x2 si le prescaler APB1 est different de 1 */
  uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  s_timclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == 0u) ? pclk1 : 2u * pclk1;

  timer_halt();
  s_phase = 0;
  GPIOA->BSRR = gray_bsrr[0];
  s_mode = ENC_IDLE;
}

ENC_Status_t ENC_Move(int32_t pulses, double hz)
{
  if (s_mode != ENC_IDLE)                          return ENC_ERR_BUSY;
  if (pulses == 0 || pulses > ENC_MAX_PULSES || pulses < -ENC_MAX_PULSES)
                                                   return ENC_ERR_COUNT;
  if (!hz_valid(hz))                               return ENC_ERR_FREQ;

  s_prof      = false;
  s_dir       = (pulses > 0) ? 1 : -1;
  s_remaining = (uint32_t)((pulses > 0) ? pulses : -pulses) * ENC_STATES_PER_PULSE;
  s_total     = s_remaining;
  s_done      = false;
  s_ticks     = hz_to_ticks(hz);
  s_mode      = ENC_MOVE;
  timer_start(s_ticks);
  return ENC_OK;
}

ENC_Status_t ENC_Run(double hz_signed)
{
  double hz = (hz_signed < 0.0) ? -hz_signed : hz_signed;

  if (s_mode != ENC_IDLE) return ENC_ERR_BUSY;
  if (!hz_valid(hz))      return ENC_ERR_FREQ;

  s_prof      = false;
  s_dir       = (hz_signed < 0.0) ? -1 : 1;
  s_remaining = 0;
  s_done      = false;
  s_ticks     = hz_to_ticks(hz);
  s_mode      = ENC_RUN;
  timer_start(s_ticks);
  return ENC_OK;
}

ENC_Status_t ENC_SetSpeed(double hz)
{
  if (s_mode == ENC_IDLE) return ENC_ERR_NOT_RUNNING;
  if (s_prof)             return ENC_ERR_PROFILE;
  if (!hz_valid(hz))      return ENC_ERR_FREQ;

  s_ticks   = hz_to_ticks(hz);
  TIM2->ARR = s_ticks - 1u;   /* preload : pris en compte a la fin du tick en cours */
  return ENC_OK;
}

ENC_Status_t ENC_MoveProfile(int8_t dir, const uint32_t *seg_ticks,
                             const uint32_t *seg_states, uint16_t nseg)
{
  uint32_t total = 0;

  if (s_mode != ENC_IDLE) return ENC_ERR_BUSY;
  if (nseg == 0u)         return ENC_ERR_COUNT;
  for (uint16_t i = 0; i < nseg; i++)
  {
    if (seg_states[i] == 0u || seg_ticks[i] < 2u) return ENC_ERR_COUNT;
    total += seg_states[i];
  }

  s_prof_ticks  = seg_ticks;
  s_prof_states = seg_states;
  s_prof_nseg   = nseg;
  s_prof_seg    = 0;
  s_prof_left   = seg_states[0];
  s_prof        = true;

  s_dir       = (dir < 0) ? -1 : 1;
  s_remaining = total;
  s_total     = total;
  s_done      = false;
  s_ticks     = seg_ticks[0];
  s_mode      = ENC_MOVE;
  timer_start(s_ticks);
  return ENC_OK;
}

bool     ENC_IsProfile(void)        { return s_prof && (s_mode != ENC_IDLE); }
uint32_t ENC_HzToTicks(double hz)   { return hz_to_ticks(hz); }
bool     ENC_HzValid(double hz)     { return hz_valid(hz); }

void ENC_Stop(void)
{
  timer_halt();               /* sorties figees dans l'etat courant */
  s_remaining = 0;
  s_mode      = ENC_IDLE;
  s_prof      = false;
}

void ENC_Zero(void)
{
  __disable_irq();
  s_pos = 0;
  __enable_irq();
}

ENC_Status_t ENC_SetPulsesPerCm(double ppcm)
{
  if (s_mode != ENC_IDLE)                          return ENC_ERR_BUSY;
  if (ppcm < ENC_PPCM_MIN || ppcm > ENC_PPCM_MAX)  return ENC_ERR_PPCM;
  s_ppcm = ppcm;
  return ENC_OK;
}

double ENC_GetPulsesPerCm(void)  { return s_ppcm; }
double ENC_GetFmaxHz(void)       { return ENC_VMAX_MH * s_ppcm / 36.0; }
double ENC_MhToHz(double mh)     { return mh * s_ppcm / 36.0; }
double ENC_HzToMh(double hz)     { return hz * 36.0 / s_ppcm; }

ENC_Mode_t ENC_GetMode(void)             { return s_mode; }
int8_t     ENC_GetDir(void)              { return s_dir; }
int32_t    ENC_GetStates(void)           { return s_pos; }
uint32_t   ENC_GetRemainingStates(void)  { return s_remaining; }
uint32_t   ENC_GetTotalStates(void)      { return s_total; }

double ENC_GetHz(void)
{
  if (s_mode == ENC_IDLE || s_ticks == 0u) return 0.0;
  return (double)s_timclk / (4.0 * (double)s_ticks);
}

bool ENC_TakeDone(void)
{
  bool d;
  __disable_irq();
  d = s_done;
  s_done = false;
  __enable_irq();
  return d;
}

/* -------------------------------------------------------------------------- */
/* Appelee a chaque debordement de TIM2 (4*f par seconde). Pas de float ici.   */
void ENC_TimerIRQ(void)
{
  if ((TIM2->SR & TIM_SR_UIF) == 0u) return;
  TIM2->SR = ~TIM_SR_UIF;

#if ENC_DIR_INVERT
  int8_t d = (int8_t)(-s_dir);
#else
  int8_t d = s_dir;
#endif

  uint8_t ph = (uint8_t)((s_phase + (uint8_t)d) & 3u);
  s_phase     = ph;
  GPIOA->BSRR = gray_bsrr[ph];
  s_pos      += s_dir;

  if (s_mode == ENC_MOVE)
  {
    if (--s_remaining == 0u)
    {
      TIM2->CR1  &= ~TIM_CR1_CEN;
      TIM2->DIER &= ~TIM_DIER_UIE;
      s_mode = ENC_IDLE;
      s_prof = false;
      s_done = true;
    }
    else if (s_prof && --s_prof_left == 0u)
    {
      uint16_t seg = (uint16_t)(s_prof_seg + 1u);   /* segment suivant */
      s_prof_seg  = seg;
      s_prof_left = s_prof_states[seg];
      s_ticks     = s_prof_ticks[seg];
      TIM2->ARR   = s_ticks - 1u;                   /* preload */
    }
  }
}
