/**
  ******************************************************************************
  * @file    scenario.h
  * @brief   Scenarios de vitesse enregistres dans le firmware
  ******************************************************************************
  */
#ifndef SCENARIO_H
#define SCENARIO_H

#include <stdint.h>
#include "encoder_sim.h"

typedef enum
{
  SCN_SINE = 0,     /* v(d) = vmin + (vmax-vmin) * (1 - cos(2*pi*param*d/D)) / 2      */
  SCN_STAIRS        /* param marches egales en distance, de vmin a vmax (incluses)   */
} SCN_Type_t;

typedef struct
{
  const char *name;
  SCN_Type_t  type;
  double      dist_cm;     /* profondeur parcourue (signe = sens)          */
  double      vmin_mh;     /* vitesse mini (m/h)                           */
  double      vmax_mh;     /* vitesse maxi (m/h)                           */
  double      param;       /* SINE : nb de periodes   STAIRS : nb de marches */
} SCN_Def_t;

uint16_t         SCN_Count(void);
const SCN_Def_t *SCN_Get(uint16_t idx);            /* idx a partir de 1 */
ENC_Status_t     SCN_Start(uint16_t idx);
uint16_t         SCN_Running(void);                /* 0 si aucun        */
double           SCN_SpeedAt(const SCN_Def_t *s, double d_cm); /* m/h  */
double           SCN_DurationS(const SCN_Def_t *s);           /* duree estimee (s) */

#endif /* SCENARIO_H */
