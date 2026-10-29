/**
  ******************************************************************************
  * @file    ui.h
  * @brief   Interface utilisateur sur ecran TFT ILI9341 320x240 + tactile XPT2046
  *
  *  Role
  *  ----
  *  Module "haut niveau" de l'IHM : splash APAGEO, 6 pages tactiles (ETAT, VA,
  *  JOG/RUN, GRAPH, SCEN, REGL), pave numerique, calibration tactile et
  *  sauvegarde des reglages. Il ne fait que piloter les autres modules :
  *    - encoder_sim : generation A/B (ENC_Move, ENC_Run, ENC_Stop...)
  *    - scenario    : profils de vitesse embarques
  *    - settings    : reglages persistants en flash (g_set)
  *    - tft / gui   : dessin bas niveau / widgets
  *    - touch       : lecture et calibration de la dalle
  *    - cmd_parser  : trace "[TACTILE] ..." dans le terminal (CMD_Notify)
  *    - oled_ui     : ecran OLED I2C SSD1306 (splash + tableau de bord)
  *
  *  Utilisation (main.c)
  *  --------------------
  *    MX_..._Init();          // GPIO, DMA, TIM2, USART2, SPI2, I2C1 deja initialises
  *    ENC_Init();
  *    UI_Init();              // avant CMD_Init : charge le PPCM memorise
  *    CMD_Init();
  *    while (1) { CMD_Process(); UI_Task(); }
  *
  *  Contraintes temps reel
  *  ----------------------
  *  - Tout le dessin se fait dans la boucle principale (jamais en IT) ; la
  *    generation A/B (IT TIM2) et la reception UART (IT USART2) ne sont donc
  *    jamais retardees par l'affichage.
  *  - UI_Task() peut bloquer la boucle quelques ms (jusqu'a ~60 ms lors d'un
  *    redessin complet de page) : sans consequence, l'UART est tamponnee.
  *  - UI_Init() dure ~0,4 s (reset + init de l'ILI9341).
  ******************************************************************************
  */
#ifndef UI_H
#define UI_H

/* Periode de rafraichissement des valeurs affichees (textes, barre, graphe).
 * La lecture du tactile est independante (toutes les 20 ms, voir ui.c). */
#define UI_REFRESH_MS   100u

/**
  * @brief  Initialise l'IHM : charge les reglages flash (PPCM, theme,
  *         calibration tactile...), initialise ecran + tactile et affiche le
  *         splash. Ne bloque pas pendant les 5 s du splash.
  */
void UI_Init(void);

/**
  * @brief  Tache IHM, a appeler en permanence dans le while(1).
  *         Gere le splash, le tactile, le rafraichissement de la page,
  *         l'echantillonnage du graphique et la sauvegarde differee en flash.
  *         Non bloquante (hors temps de dessin), sans attente active.
  */
void UI_Task(void);

#endif /* UI_H */
