/**
  ******************************************************************************
  * @file    encoder_sim.h
  * @brief   Simulateur de codeur incremental en quadrature (A/B, sans Z)
  *          sur TIM2 (32 bits) + PA8 (A) / PA9 (B) en push-pull 3,3 V.
  ******************************************************************************
  */
#ifndef ENCODER_SIM_H
#define ENCODER_SIM_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Parametres metier ---------------------------------------------------- */
#define ENC_DEFAULT_PPCM      50.0         /* resolution au demarrage (imp/cm)   */
#define ENC_PPCM_MIN          0.1          /* plage reglable par PPCM            */
#define ENC_PPCM_MAX          1000.0       /* -> Fmax 27.8 kHz, IT a 111 kHz     */
#define ENC_VMAX_MH           1000.0       /* vitesse max simulee (m/h)          */
#define ENC_STATES_PER_PULSE  4u           /* 4 etats de Gray par cycle          */
#define ENC_FMIN_HZ           0.01         /* limite basse (ARR 32 bits)         */
#define ENC_MAX_PULSES        100000000L   /* garde-fou sur MOVE                 */

/* 1 m/h = 100 cm / 3600 s * ppcm imp/cm  ->  Hz = m/h * ppcm / 36
 * Fmax (Hz) = ENC_VMAX_MH * ppcm / 36 : recalculee a chaque changement de ppcm */

/* Sens : 0 = x positif -> A en avance sur B. Mettre 1 pour inverser
 * (equivaut a croiser les fils A et B). */
#define ENC_DIR_INVERT        0

typedef enum
{
  ENC_IDLE = 0,
  ENC_MOVE,      /* nombre d'impulsions fixe */
  ENC_RUN        /* flux continu jusqu'a STOP */
} ENC_Mode_t;

typedef enum
{
  ENC_OK = 0,
  ENC_ERR_BUSY,
  ENC_ERR_FREQ,
  ENC_ERR_COUNT,
  ENC_ERR_NOT_RUNNING,
  ENC_ERR_PPCM,
  ENC_ERR_PROFILE
} ENC_Status_t;

void         ENC_Init(void);
ENC_Status_t ENC_Move(int32_t pulses, double hz);  /* signe de pulses = sens   */
ENC_Status_t ENC_Run(double hz_signed);            /* signe de hz = sens       */
ENC_Status_t ENC_SetSpeed(double hz);              /* change f a la volee      */
void         ENC_Stop(void);
void         ENC_Zero(void);

/* Mouvement a profil de vitesse : la distance est decoupee en nseg segments,
 * le segment i dure seg_states[i] etats a la periode seg_ticks[i] (ARR+1).
 * Les tables doivent rester valides jusqu'a la fin du mouvement. */
ENC_Status_t ENC_MoveProfile(int8_t dir, const uint32_t *seg_ticks,
                             const uint32_t *seg_states, uint16_t nseg);
bool         ENC_IsProfile(void);
uint32_t     ENC_HzToTicks(double hz);
bool         ENC_HzValid(double hz);

/* Resolution (impulsions / cm) et conversions qui en dependent */
ENC_Status_t ENC_SetPulsesPerCm(double ppcm);      /* refuse pendant un mouvement */
double       ENC_GetPulsesPerCm(void);
double       ENC_GetFmaxHz(void);
double       ENC_MhToHz(double mh);
double       ENC_HzToMh(double hz);

ENC_Mode_t   ENC_GetMode(void);
int8_t       ENC_GetDir(void);             /* +1 / -1 */
int32_t      ENC_GetStates(void);          /* position signee en etats (1/4 imp) */
uint32_t     ENC_GetRemainingStates(void); /* restant en mode MOVE               */
uint32_t     ENC_GetTotalStates(void);     /* total du MOVE/SCEN en cours        */
double       ENC_GetHz(void);              /* frequence reelle (apres arrondi ARR) */
bool         ENC_TakeDone(void);           /* true une fois a la fin d'un MOVE   */

void         ENC_TimerIRQ(void);           /* a appeler depuis TIM2_IRQHandler   */

#endif /* ENCODER_SIM_H */
