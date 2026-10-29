/**
  ******************************************************************************
  * @file    touch.c
  * @brief   XPT2046 en SPI logiciel (mode 0, ~1 MHz, max controleur 2,5 MHz).
  *          Mesures differentielles, mise en veille entre conversions (PD=00)
  *          pour garder T_IRQ actif. 7 echantillons par axe, mediane.
  ******************************************************************************
  */
#include "touch.h"
#include "tft.h"
#include "main.h"

#define CMD_X    0xD0   /* S=1 A=101 MODE=0(12 bits) SER=0(diff) PD=00 */
#define CMD_Y    0x90   /* A=001 */
#define CMD_Z1   0xB0   /* A=011 */
#define CMD_Z2   0xC0   /* A=100 */

#define NSAMP    7

#define CLK_H()  (Touch_CLK_GPIO_Port->BSRR  = Touch_CLK_Pin)
#define CLK_L()  (Touch_CLK_GPIO_Port->BSRR  = (uint32_t)Touch_CLK_Pin << 16)
#define CS_H()   (Touch_CS_GPIO_Port->BSRR   = Touch_CS_Pin)
#define CS_L()   (Touch_CS_GPIO_Port->BSRR   = (uint32_t)Touch_CS_Pin << 16)
#define DIN_H()  (Touch_MOSI_GPIO_Port->BSRR = Touch_MOSI_Pin)
#define DIN_L()  (Touch_MOSI_GPIO_Port->BSRR = (uint32_t)Touch_MOSI_Pin << 16)
#define DOUT()   ((Touch_MISO_GPIO_Port->IDR & Touch_MISO_Pin) != 0u)
#define IRQ_LOW() ((Touch_IRQ_GPIO_Port->IDR & Touch_IRQ_Pin) == 0u)

static inline void dly(void)            /* ~0,4 us a 100 MHz */
{
  for (volatile int i = 0; i < 10; i++) { }
}

static uint8_t xfer(uint8_t out)
{
  uint8_t in = 0;
  for (int b = 7; b >= 0; b--)
  {
    if (out & (1u << b)) DIN_H(); else DIN_L();
    dly();
    CLK_H();                            /* le XPT2046 echantillonne DIN sur front montant */
    dly();
    in = (uint8_t)((in << 1) | (DOUT() ? 1u : 0u));
    CLK_L();
  }
  return in;
}

static uint16_t read12(uint8_t cmd)     /* CS doit etre bas */
{
  xfer(cmd);
  uint16_t hi = xfer(0x00);
  uint16_t lo = xfer(0x00);
  return (uint16_t)(((hi << 8) | lo) >> 3) & 0x0FFFu;
}

static uint16_t median(uint16_t *v, int n)
{
  for (int i = 1; i < n; i++)           /* tri insertion, n petit */
  {
    uint16_t k = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
    v[j + 1] = k;
  }
  return v[n / 2];
}

static TOUCH_Cal_t s_cal;

/* Calibration par defaut deduite des #define de touch.h */
void TOUCH_DefaultCal(TOUCH_Cal_t *c)
{
  float kx = (float)TFT_WIDTH  / (float)(TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN);
  float ky = (float)TFT_HEIGHT / (float)(TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN);
  float sx = TOUCH_INVERT_X ? -kx : kx, ox = TOUCH_INVERT_X ? (TFT_WIDTH - 1) + TOUCH_RAW_X_MIN * kx : -TOUCH_RAW_X_MIN * kx;
  float sy = TOUCH_INVERT_Y ? -ky : ky, oy = TOUCH_INVERT_Y ? (TFT_HEIGHT - 1) + TOUCH_RAW_Y_MIN * ky : -TOUCH_RAW_Y_MIN * ky;
#if TOUCH_SWAP_XY
  c->a = 0.0f; c->b = sx; c->c = ox;              /* x ecran <- y brut */
  c->d = sy;   c->e = 0.0f; c->f = oy;            /* y ecran <- x brut */
#else
  c->a = sx; c->b = 0.0f; c->c = ox;
  c->d = 0.0f; c->e = sy; c->f = oy;
#endif
}

void TOUCH_SetCal(const TOUCH_Cal_t *cal)
{
  if (cal) s_cal = *cal; else TOUCH_DefaultCal(&s_cal);
}

void TOUCH_Map(const TOUCH_Cal_t *c, float rx, float ry, int16_t *x, int16_t *y)
{
  float fx = c->a * rx + c->b * ry + c->c;
  float fy = c->d * rx + c->e * ry + c->f;
  *x = (int16_t)(fx + (fx >= 0 ? 0.5f : -0.5f));
  *y = (int16_t)(fy + (fy >= 0 ? 0.5f : -0.5f));
}

/* Resolution 3x3 (Cramer) */
static bool solve3(double m[3][3], const double v[3], double r[3])
{
  double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  if (det > -1e-9 && det < 1e-9) return false;
  for (int k = 0; k < 3; k++)
  {
    double t[3][3];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t[i][j] = (j == k) ? v[i] : m[i][j];
    r[k] = (t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1])
          - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0])
          + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])) / det;
  }
  return true;
}

bool TOUCH_ComputeCal(const float *rx, const float *ry, const float *sx, const float *sy,
                      int n, TOUCH_Cal_t *out)
{
  if (n < 3) return false;
  /* bruts normalises sur [0,1] pour le conditionnement */
  double m[3][3] = {{0}}, vx[3] = {0}, vy[3] = {0}, px[3], py[3];
  for (int i = 0; i < n; i++)
  {
    double u = rx[i] / 4096.0, w = ry[i] / 4096.0, q[3] = { u, w, 1.0 };
    for (int r = 0; r < 3; r++)
    {
      for (int c = 0; c < 3; c++) m[r][c] += q[r] * q[c];
      vx[r] += q[r] * sx[i];
      vy[r] += q[r] * sy[i];
    }
  }
  if (!solve3(m, vx, px) || !solve3(m, vy, py)) return false;
  out->a = (float)(px[0] / 4096.0); out->b = (float)(px[1] / 4096.0); out->c = (float)px[2];
  out->d = (float)(py[0] / 4096.0); out->e = (float)(py[1] / 4096.0); out->f = (float)py[2];
  return true;
}

/* -------------------------------------------------------------------------- */
void TOUCH_Init(void)
{
  if (s_cal.a == 0.0f && s_cal.b == 0.0f) TOUCH_DefaultCal(&s_cal);
  CS_H(); CLK_L(); DIN_L();
  CS_L();
  (void)read12(CMD_X);                  /* premiere conversion : arme T_IRQ (PD=00) */
  CS_H();
}

bool TOUCH_Read(TOUCH_State_t *t)
{
  uint16_t sx[NSAMP], sy[NSAMP];

  t->pressed = false;
  if (!IRQ_LOW()) return false;         /* pas d'appui */

  CS_L();
  uint16_t z1 = read12(CMD_Z1);
  uint16_t z2 = read12(CMD_Z2);
  for (int i = 0; i < NSAMP; i++)
  {
    sx[i] = read12(CMD_X);
    sy[i] = read12(CMD_Y);
  }
  (void)read12(CMD_X);                  /* termine en PD=00 -> T_IRQ reactive */
  CS_H();

  t->raw_x = median(sx, NSAMP);
  t->raw_y = median(sy, NSAMP);
  t->z     = (uint16_t)((int32_t)z1 + 4095 - (int32_t)z2);   /* indicateur de pression */
  if (z1 < 10u || t->z < TOUCH_Z_MIN) return false;

  int16_t x, y;
  TOUCH_Map(&s_cal, (float)t->raw_x, (float)t->raw_y, &x, &y);
  if (x < 0) x = 0;
  if (x > TFT_WIDTH - 1) x = TFT_WIDTH - 1;
  if (y < 0) y = 0;
  if (y > TFT_HEIGHT - 1) y = TFT_HEIGHT - 1;
  t->x = (uint16_t)x;
  t->y = (uint16_t)y;
  t->pressed = true;
  return true;
}
