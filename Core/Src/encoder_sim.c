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
/**
 * @brief  Convertit une frequence d'impulsions en periode de TIM2 :
 *         ticks = horloge TIM2 / (4 x hz), car chaque impulsion compte 4 etats.
 *         Arrondi au plus proche, borne a [2, 2^32 - 1].
 * @param  hz  Frequence en Hz (> 0).
 * @retval Periode en cycles d'horloge TIM2 (= ARR + 1).
 */
static uint32_t hz_to_ticks(double hz)
{
  double t = (double)s_timclk / (4.0 * hz) + 0.5;
  if (t > 4294967295.0) t = 4294967295.0;
  if (t < 2.0)          t = 2.0;
  return (uint32_t)t;
}

/**
 * @brief  Verifie que ENC_FMIN_HZ <= hz <= Fmax (tolerance relative 1e-9 sur
 *         Fmax pour absorber les arrondis de conversion m/h -> Hz).
 * @param  hz  Frequence en Hz.
 * @retval true si la frequence est utilisable.
 */
static bool hz_valid(double hz)
{
  return (hz >= ENC_FMIN_HZ) && (hz <= ENC_GetFmaxHz() * (1.0 + 1e-9));
}

/**
 * @brief  Arrete TIM2, masque son interruption de mise a jour et efface tout
 *         drapeau / interruption en attente. Les sorties A/B restent figees
 *         dans leur etat courant.
 */
static void timer_halt(void)
{
  TIM2->CR1  &= ~TIM_CR1_CEN;
  TIM2->DIER &= ~TIM_DIER_UIE;
  TIM2->SR    = 0;
  NVIC_ClearPendingIRQ(TIM2_IRQn);
}

/**
 * @brief  (Re)demarre TIM2 avec une periode de 'ticks' cycles et active son
 *         interruption de mise a jour. URS evite que la generation de l'UG
 *         (chargement des registres ARR / PSC) declenche une interruption.
 * @param  ticks  Periode en cycles d'horloge TIM2 (>= 2).
 */
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
/**
 * @brief  Initialise le simulateur : calcule l'horloge de TIM2 (PCLK1, x2 si
 *         le prescaler APB1 est different de 1), arrete le timer, met les
 *         sorties a A = B = 0 et passe en mode IDLE.
 *         A appeler apres MX_GPIO_Init() et MX_TIM2_Init().
 */
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

/**
 * @brief  Lance un deplacement de 'pulses' impulsions a frequence constante.
 *         La fin est signalee par ENC_TakeDone().
 * @param  pulses  Nombre d'impulsions signe (signe = sens), non nul,
 *                 |pulses| <= ENC_MAX_PULSES.
 * @param  hz      Frequence en Hz (ENC_FMIN_HZ..Fmax).
 * @retval ENC_OK, ENC_ERR_BUSY (mouvement en cours), ENC_ERR_COUNT ou
 *         ENC_ERR_FREQ.
 */
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

/**
 * @brief  Lance un defilement continu, jusqu'a ENC_Stop().
 * @param  hz_signed  Frequence en Hz ; le signe donne le sens.
 * @retval ENC_OK, ENC_ERR_BUSY ou ENC_ERR_FREQ.
 */
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

/**
 * @brief  Change la frequence du mouvement en cours (MOVE ou RUN). Grace au
 *         preload de ARR, la nouvelle periode s'applique a la fin de l'etat en
 *         cours, sans glitch. Refuse pendant un scenario (profil impose).
 * @param  hz  Nouvelle frequence en Hz.
 * @retval ENC_OK, ENC_ERR_NOT_RUNNING, ENC_ERR_PROFILE ou ENC_ERR_FREQ.
 */
ENC_Status_t ENC_SetSpeed(double hz)
{
  if (s_mode == ENC_IDLE) return ENC_ERR_NOT_RUNNING;
  if (s_prof)             return ENC_ERR_PROFILE;
  if (!hz_valid(hz))      return ENC_ERR_FREQ;

  s_ticks   = hz_to_ticks(hz);
  TIM2->ARR = s_ticks - 1u;   /* preload : pris en compte a la fin du tick en cours */
  return ENC_OK;
}

/**
 * @brief  Lance un deplacement a vitesse variable, decoupe en segments :
 *         le segment i dure seg_states[i] etats a la periode seg_ticks[i].
 *         Les tableaux ne sont pas copies : ils doivent rester valides jusqu'a
 *         la fin du mouvement.
 * @param  dir         Sens (+1 ou -1).
 * @param  seg_ticks   Periode TIM2 de chaque segment (>= 2).
 * @param  seg_states  Nombre d'etats de chaque segment (> 0).
 * @param  nseg        Nombre de segments (> 0).
 * @retval ENC_OK, ENC_ERR_BUSY ou ENC_ERR_COUNT.
 */
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

/**
 * @brief  Indique si un mouvement par profil (scenario) est en cours.
 * @retval true pendant un ENC_MoveProfile() non termine.
 */
bool     ENC_IsProfile(void)        { return s_prof && (s_mode != ENC_IDLE); }
/**
 * @brief  Version publique de hz_to_ticks() (utilisee par scenario.c).
 * @param  hz  Frequence en Hz.
 * @retval Periode en cycles d'horloge TIM2.
 */
uint32_t ENC_HzToTicks(double hz)   { return hz_to_ticks(hz); }
/**
 * @brief  Version publique de hz_valid() (utilisee par scenario.c).
 * @param  hz  Frequence en Hz.
 * @retval true si la frequence est dans la plage autorisee.
 */
bool     ENC_HzValid(double hz)     { return hz_valid(hz); }

/**
 * @brief  Arret immediat : le timer est stoppe, les sorties restent figees
 *         dans l'etat courant et la position est conservee. Ne leve pas le
 *         drapeau de fin (pas de "DONE").
 */
void ENC_Stop(void)
{
  timer_halt();               /* sorties figees dans l'etat courant */
  s_remaining = 0;
  s_mode      = ENC_IDLE;
  s_prof      = false;
}

/**
 * @brief  Remet la position a 0. Section critique car s_pos est modifiee
 *         dans l'interruption TIM2.
 */
void ENC_Zero(void)
{
  __disable_irq();
  s_pos = 0;
  __enable_irq();
}

/**
 * @brief  Change la resolution du codeur simule, uniquement a l'arret.
 * @param  ppcm  Impulsions par cm (ENC_PPCM_MIN..ENC_PPCM_MAX).
 * @retval ENC_OK, ENC_ERR_BUSY ou ENC_ERR_PPCM.
 */
ENC_Status_t ENC_SetPulsesPerCm(double ppcm)
{
  if (s_mode != ENC_IDLE)                          return ENC_ERR_BUSY;
  if (ppcm < ENC_PPCM_MIN || ppcm > ENC_PPCM_MAX)  return ENC_ERR_PPCM;
  s_ppcm = ppcm;
  return ENC_OK;
}

/**
 * @brief  Resolution courante.
 * @retval Impulsions par cm.
 */
double ENC_GetPulsesPerCm(void)  { return s_ppcm; }
/**
 * @brief  Frequence maximale : ENC_VMAX_MH convertie en Hz avec la resolution
 *         courante (1 m/h = ppcm x 100 / 3600 = ppcm / 36 Hz).
 * @retval Frequence maximale en Hz.
 */
double ENC_GetFmaxHz(void)       { return ENC_VMAX_MH * s_ppcm / 36.0; }
/**
 * @brief  Convertit une vitesse en frequence : hz = mh x ppcm / 36.
 * @param  mh  Vitesse en m/h.
 * @retval Frequence en Hz.
 */
double ENC_MhToHz(double mh)     { return mh * s_ppcm / 36.0; }
/**
 * @brief  Convertit une frequence en vitesse : mh = hz x 36 / ppcm.
 * @param  hz  Frequence en Hz.
 * @retval Vitesse en m/h.
 */
double ENC_HzToMh(double hz)     { return hz * 36.0 / s_ppcm; }

/**
 * @brief  Mode courant.
 * @retval ENC_IDLE, ENC_MOVE ou ENC_RUN.
 */
ENC_Mode_t ENC_GetMode(void)             { return s_mode; }
/**
 * @brief  Sens logique du dernier mouvement lance.
 * @retval +1 ou -1.
 */
int8_t     ENC_GetDir(void)              { return s_dir; }
/**
 * @brief  Position courante.
 * @retval Position signee en etats de Gray (4 etats par impulsion).
 */
int32_t    ENC_GetStates(void)           { return s_pos; }
/**
 * @brief  Etats restant a produire pour le MOVE en cours.
 * @retval Nombre d'etats (0 en RUN ou a l'arret).
 */
uint32_t   ENC_GetRemainingStates(void)  { return s_remaining; }
/**
 * @brief  Nombre total d'etats du dernier MOVE (pour la progression).
 * @retval Nombre d'etats.
 */
uint32_t   ENC_GetTotalStates(void)      { return s_total; }

/**
 * @brief  Frequence courante, recalculee depuis la periode du timer.
 * @retval Frequence en Hz, 0 a l'arret.
 */
double ENC_GetHz(void)
{
  if (s_mode == ENC_IDLE || s_ticks == 0u) return 0.0;
  return (double)s_timclk / (4.0 * (double)s_ticks);
}

/**
 * @brief  Lit et efface, de facon atomique, le drapeau de fin de MOVE.
 * @retval true une seule fois apres chaque MOVE (ou scenario) termine
 *         normalement.
 */
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
/**
 * @brief  Appelee a chaque debordement de TIM2 (4 x f fois par seconde), depuis
 *         TIM2_IRQHandler. Pas de calcul flottant ici.
 *         Avance d'une phase de Gray dans le sens courant (inverse si
 *         ENC_DIR_INVERT), ecrit A et B en une seule ecriture BSRR et met a jour
 *         la position. En MOVE : decremente le reste, arrete le timer et leve
 *         le drapeau de fin a 0 ; en profil, charge la periode du segment
 *         suivant (preload ARR).
 */
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
