/**
  ******************************************************************************
  * @file    splash.h
  * @brief   Ecran de demarrage APAGEO (logo centre + nom en bas)
  ******************************************************************************
  */
#ifndef SPLASH_H
#define SPLASH_H

#include <stdint.h>
#include <stdbool.h>

/* Choix du style :
 *   1 = Clair  : fond blanc, logo et nom bleu APAGEO
 *   2 = Bleu   : fond bleu APAGEO, logo et nom blancs
 *   3 = Sombre : fond bleu nuit, logo bleu, nom blanc, barre de progression */
#ifndef SPLASH_STYLE
#define SPLASH_STYLE   3
#endif

#define SPLASH_MS      5000u

void SPLASH_Draw(void);                 /* dessine l'ecran complet           */
void SPLASH_Progress(uint32_t elapsed); /* style 3 : barre (sans effet sinon) */

#endif /* SPLASH_H */
