/**
  ******************************************************************************
  * @file    oled_ui.h
  * @brief   Ecran OLED SSD1306 128x64 bicolore : tableau de bord du simulateur.
  *
  *  Disposition
  *  -----------
  *   Bande jaune (lignes 0..15)
  *     | ARRET / MOVE / RUN / SCENn  + fleche de sens      [PC] ou [TAC] |
  *       ([PC] / [TAC] : allume 1,5 s apres une commande PuTTY / tactile)
  *   Zone bleue (lignes 16..63)
  *     | PROF (m)                                         vitesse m/h     |
  *     |                 profondeur en gros (taille 3)                   |
  *     | barre de progression + % (MOVE / VA / SCEN)                      |
  *     |   ou  PPCM n          frequence de sortie (RUN / arret)         |
  *
  *  Au demarrage : splash APAGEO de 5 s (nom dans le jaune, logo dans le bleu).
  *
  *  L'ecran est facultatif : s'il est absent ou debranche, le module retente
  *  une detection toutes les OLED_RETRY_MS sans gener le reste du firmware.
  *
  *  Utilisation : OLED_UI_Init() une fois (apres MX_I2C1_Init), puis
  *  OLED_UI_Task() en boucle (appeles par UI_Init / UI_Task dans ui.c).
  ******************************************************************************
  */
#ifndef OLED_UI_H
#define OLED_UI_H

#define OLED_REFRESH_MS   100u    /* periode de rafraichissement (1 image = ~25 ms DMA) */
#define OLED_SPLASH_MS    5000u   /* duree du splash                                    */
#define OLED_RETRY_MS     3000u   /* nouvelle tentative de detection si ecran absent    */
#define OLED_ACTIVITY_MS  1500u   /* duree d'affichage de l'indicateur PC / TAC         */

/** @brief Detecte l'ecran et affiche le splash (non bloquant pendant les 5 s). */
void OLED_UI_Init(void);

/** @brief A appeler en permanence dans la boucle principale (non bloquant). */
void OLED_UI_Task(void);

#endif /* OLED_UI_H */
