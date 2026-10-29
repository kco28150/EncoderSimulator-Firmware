/**
  ******************************************************************************
  * @file    settings.h
  * @brief   Reglages persistants en flash (secteur 7, 0x08060000, 128 Ko).
  *          Journal d'enregistrements de 64 octets : un secteur efface ne l'est
  *          qu'une fois plein (~2000 sauvegardes), ecriture seulement a l'arret.
  ******************************************************************************
  */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
  float   ppcm;          /* imp/cm                 */
  float   va_speed_mh;   /* page VA                */
  float   va_depth_m;    /* page VA (relative)     */
  float   jog_speed_mh;  /* page JOG / RUN         */
  uint8_t theme;         /* 0 sombre, 1 clair      */
  uint8_t tcal_ok;       /* 1 : calibration tactile memorisee */
  float   tcal[6];       /* a b c d e f (TOUCH_Cal_t)         */
} Settings_t;

extern Settings_t g_set;        /* valeurs courantes (modifiables directement) */

void SETTINGS_Load(void);       /* charge le dernier enregistrement valide ou les defauts */
void SETTINGS_Task(void);       /* sauvegarde differee si g_set a change et codeur a l'arret */

#endif /* SETTINGS_H */
