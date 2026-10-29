/**
  ******************************************************************************
  * @file    touch.h
  * @brief   Dalle tactile resistive XPT2046 (SPI logiciel)
  *          PC0 T_CLK, PC1 T_CS, PC2 T_DO (MISO), PC3 T_DIN (MOSI), PC4 T_IRQ
  ******************************************************************************
  */
#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Calibration (mesuree sur la dalle : 4 coins + centre) --------------- */
#define TOUCH_RAW_X_MIN   484     /* brut (axe Y du XPT2046) au bord droit   */
#define TOUCH_RAW_X_MAX   3830    /* brut au bord gauche                     */
#define TOUCH_RAW_Y_MIN   267     /* brut (axe X du XPT2046) en bas          */
#define TOUCH_RAW_Y_MAX   3697    /* brut en haut                            */
#define TOUCH_SWAP_XY     1       /* 1 : l'axe X brut correspond a Y ecran   */
#define TOUCH_INVERT_X    1       /* 1 : inverse le sens gauche/droite       */
#define TOUCH_INVERT_Y    1       /* 1 : inverse le sens haut/bas            */

#define TOUCH_Z_MIN       100     /* seuil de pression pour valider un appui */

typedef struct
{
  bool     pressed;
  uint16_t raw_x, raw_y;   /* 0..4095 */
  uint16_t z;              /* pression (plus grand = plus appuye) */
  uint16_t x, y;           /* pixels ecran 320 x 240 (calibration ci-dessus) */
} TOUCH_State_t;

/* Calibration affine : x = a*rx + b*ry + c ; y = d*rx + e*ry + f
 * (gere permutation, inversion et legere rotation de la dalle) */
typedef struct { float a, b, c, d, e, f; } TOUCH_Cal_t;

void TOUCH_Init(void);
bool TOUCH_Read(TOUCH_State_t *t);              /* true si appui valide */
void TOUCH_SetCal(const TOUCH_Cal_t *cal);      /* NULL : calibration par defaut (#define) */
void TOUCH_DefaultCal(TOUCH_Cal_t *cal);
void TOUCH_Map(const TOUCH_Cal_t *cal, float rx, float ry, int16_t *x, int16_t *y);

/* Calcul par moindres carres sur n points (n >= 3). false si mal conditionne. */
bool TOUCH_ComputeCal(const float *rx, const float *ry, const float *sx, const float *sy,
                      int n, TOUCH_Cal_t *out);

#endif /* TOUCH_H */
