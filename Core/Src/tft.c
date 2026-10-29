/**
  ******************************************************************************
  * @file    tft.c
  * @brief   Driver ILI9341 sur SPI2 - meme sequence d'init que la classe C++
  *          ILI9341 du projet TFTpp, CS maintenu bas par transaction pour
  *          aller plus vite, et rendu de texte (police 5x7).
  ******************************************************************************
  */
#include "tft.h"
#include "font5x7.h"
#include "main.h"
#include "spi.h"

#define CS_LOW()   (Screen_CS_GPIO_Port->BSRR   = (uint32_t)Screen_CS_Pin << 16)
#define CS_HIGH()  (Screen_CS_GPIO_Port->BSRR   = Screen_CS_Pin)
#define DC_CMD()   (Screen_DCRS_GPIO_Port->BSRR = (uint32_t)Screen_DCRS_Pin << 16)
#define DC_DATA()  (Screen_DCRS_GPIO_Port->BSRR = Screen_DCRS_Pin)

#define BURST_PIXELS  256u                     /* tampon de remplissage */

static uint16_t s_w = TFT_WIDTH;
static uint16_t s_h = TFT_HEIGHT;
static uint8_t  s_buf[TFT_CHAR_W(4) * TFT_CHAR_H(4) * 2];   /* 1 caractere x4 = 1536 o */

/* -------------------------------------------------------------------------- */
static void spi_tx(const uint8_t *d, uint16_t n)
{
  HAL_SPI_Transmit(&hspi2, (uint8_t *)d, n, 50);
}

static void wr_cmd(uint8_t c)                 /* CS doit etre bas */
{
  DC_CMD();
  spi_tx(&c, 1);
  DC_DATA();
}

/* Commande + parametres dans une transaction CS */
static void cmd_params(uint8_t c, const uint8_t *p, uint8_t n)
{
  CS_LOW();
  wr_cmd(c);
  if (n) spi_tx(p, n);
  CS_HIGH();
}

/* Fenetre d'ecriture ; laisse CS bas et DC en donnees, pret pour les pixels */
static void set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  uint8_t ca[4] = { (uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2 };
  uint8_t ra[4] = { (uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2 };
  CS_LOW();
  wr_cmd(0x2A); spi_tx(ca, 4);
  wr_cmd(0x2B); spi_tx(ra, 4);
  wr_cmd(0x2C);
}

/* -------------------------------------------------------------------------- */
void TFT_Init(void)
{
  /* Reset materiel (RST bas puis haut) */
  CS_HIGH();
  Screen_RST_GPIO_Port->BSRR = (uint32_t)Screen_RST_Pin << 16;
  HAL_Delay(20);
  Screen_RST_GPIO_Port->BSRR = Screen_RST_Pin;
  HAL_Delay(120);

  cmd_params(0x01, 0, 0);                                      /* SOFTWARE RESET */
  HAL_Delay(150);

  /* Sequence reprise telle quelle de ILI9341::Init() (TFTpp) */
  { static const uint8_t p[] = {0x39, 0x2C, 0x00, 0x34, 0x02}; cmd_params(0xCB, p, sizeof p); }
  { static const uint8_t p[] = {0x00, 0xC1, 0x30};             cmd_params(0xCF, p, sizeof p); }
  { static const uint8_t p[] = {0x85, 0x00, 0x78};             cmd_params(0xE8, p, sizeof p); }
  { static const uint8_t p[] = {0x00, 0x00};                   cmd_params(0xEA, p, sizeof p); }
  { static const uint8_t p[] = {0x64, 0x03, 0x12, 0x81};       cmd_params(0xED, p, sizeof p); }
  { static const uint8_t p[] = {0x20};                         cmd_params(0xF7, p, sizeof p); }
  { static const uint8_t p[] = {0x23};                         cmd_params(0xC0, p, sizeof p); }
  { static const uint8_t p[] = {0x10};                         cmd_params(0xC1, p, sizeof p); }
  { static const uint8_t p[] = {0x3E, 0x28};                   cmd_params(0xC5, p, sizeof p); }
  { static const uint8_t p[] = {0x86};                         cmd_params(0xC7, p, sizeof p); }
  { static const uint8_t p[] = {0x48};                         cmd_params(0x36, p, sizeof p); }
  { static const uint8_t p[] = {0x55};                         cmd_params(0x3A, p, sizeof p); }  /* RGB565 */
  { static const uint8_t p[] = {0x00, 0x18};                   cmd_params(0xB1, p, sizeof p); }
  { static const uint8_t p[] = {0x08, 0x82, 0x27};             cmd_params(0xB6, p, sizeof p); }
  { static const uint8_t p[] = {0x00};                         cmd_params(0xF2, p, sizeof p); }
  { static const uint8_t p[] = {0x01};                         cmd_params(0x26, p, sizeof p); }
  { static const uint8_t p[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                                0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
                                                               cmd_params(0xE0, p, sizeof p); }
  { static const uint8_t p[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                                0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
                                                               cmd_params(0xE1, p, sizeof p); }

  cmd_params(0x11, 0, 0);                                      /* EXIT SLEEP */
  HAL_Delay(120);
  cmd_params(0x29, 0, 0);                                      /* DISPLAY ON */

  TFT_SetRotation(SCREEN_HORIZONTAL_1);
}

void TFT_SetRotation(uint8_t rotation)
{
  uint8_t madctl;
  switch (rotation)
  {
    case SCREEN_VERTICAL_1:   madctl = 0x40 | 0x08;               s_w = 240; s_h = 320; break;
    case SCREEN_VERTICAL_2:   madctl = 0x80 | 0x08;               s_w = 240; s_h = 320; break;
    case SCREEN_HORIZONTAL_2: madctl = 0x40 | 0x80 | 0x20 | 0x08; s_w = 320; s_h = 240; break;
    case SCREEN_HORIZONTAL_1:
    default:                  madctl = 0x20 | 0x08;               s_w = 320; s_h = 240; break;
  }
  cmd_params(0x36, &madctl, 1);
}

void TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (x >= s_w || y >= s_h || w == 0u || h == 0u) return;
  if (x + w > s_w) w = (uint16_t)(s_w - x);
  if (y + h > s_h) h = (uint16_t)(s_h - y);

  uint32_t n = (uint32_t)w * h;
  uint32_t fill = (n < BURST_PIXELS) ? n : BURST_PIXELS;
  for (uint32_t i = 0; i < fill; i++)
  {
    s_buf[2 * i]     = (uint8_t)(color >> 8);
    s_buf[2 * i + 1] = (uint8_t)color;
  }

  set_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
  while (n)
  {
    uint32_t k = (n < BURST_PIXELS) ? n : BURST_PIXELS;
    spi_tx(s_buf, (uint16_t)(k * 2u));
    n -= k;
  }
  CS_HIGH();
}

void TFT_FillScreen(uint16_t color)            { TFT_FillRect(0, 0, s_w, s_h, color); }
void TFT_HLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color) { TFT_FillRect(x, y, w, 1, color); }

void TFT_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
  if (scale < 1u) scale = 1u;
  if (scale > 4u) scale = 4u;
  if ((uint8_t)c < FONT5X7_FIRST || (uint8_t)c > FONT5X7_LAST) c = '?';

  const uint8_t *g = font5x7[(uint8_t)c - FONT5X7_FIRST];
  uint16_t cw = (uint16_t)TFT_CHAR_W(scale), ch = (uint16_t)TFT_CHAR_H(scale);
  if (x + cw > s_w || y + ch > s_h) return;

  /* Rendu complet de la cellule (fond compris) dans le tampon puis une seule rafale */
  uint32_t i = 0;
  for (uint16_t py = 0; py < ch; py++)
  {
    uint8_t row = (uint8_t)(py / scale);
    for (uint16_t px = 0; px < cw; px++)
    {
      uint8_t col = (uint8_t)(px / scale);
      uint16_t color = (col < 5u && (g[col] >> row) & 1u) ? fg : bg;
      s_buf[i++] = (uint8_t)(color >> 8);
      s_buf[i++] = (uint8_t)color;
    }
  }
  set_window(x, y, (uint16_t)(x + cw - 1u), (uint16_t)(y + ch - 1u));
  spi_tx(s_buf, (uint16_t)i);
  CS_HIGH();
}

void TFT_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
  while (*s)
  {
    TFT_DrawChar(x, y, *s++, fg, bg, scale);
    x = (uint16_t)(x + TFT_CHAR_W(scale));
  }
}

/* Melange lineaire RGB565 : t = 0 -> bg, t = 15 -> fg */
static uint16_t blend565(uint16_t fg, uint16_t bg, uint8_t t)
{
  uint16_t r = (uint16_t)((((fg >> 11) & 31u) * t + ((bg >> 11) & 31u) * (15u - t) + 7u) / 15u);
  uint16_t g = (uint16_t)((((fg >> 5)  & 63u) * t + ((bg >> 5)  & 63u) * (15u - t) + 7u) / 15u);
  uint16_t b = (uint16_t)(((fg & 31u)        * t + (bg & 31u)        * (15u - t) + 7u) / 15u);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void TFT_DrawAlpha4(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                    const uint8_t *a4, uint16_t fg, uint16_t bg)
{
  if (w == 0u || h == 0u || x + w > s_w || y + h > s_h || w * 2u > sizeof s_buf) return;

  uint16_t pal[16];
  for (uint8_t t = 0; t < 16u; t++) pal[t] = blend565(fg, bg, t);

  uint16_t stride = (uint16_t)((w + 1u) / 2u);
  set_window(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
  for (uint16_t row = 0; row < h; row++)
  {
    const uint8_t *src = a4 + (uint32_t)row * stride;
    for (uint16_t px = 0; px < w; px++)
    {
      uint8_t  v = src[px >> 1];
      uint16_t c = pal[(px & 1u) ? (v & 0x0Fu) : (v >> 4)];
      s_buf[2u * px]      = (uint8_t)(c >> 8);
      s_buf[2u * px + 1u] = (uint8_t)c;
    }
    spi_tx(s_buf, (uint16_t)(w * 2u));
  }
  CS_HIGH();
}
