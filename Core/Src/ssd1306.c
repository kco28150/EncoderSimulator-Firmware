/**
  ******************************************************************************
  * @file    ssd1306.c
  * @brief   Pilote SSD1306 128x64 sur I2C1 + DMA (voir ssd1306.h).
  *
  *  Protocole I2C du SSD1306
  *  ------------------------
  *  Chaque trame commence par un octet de controle :
  *    0x00 -> les octets suivants sont des COMMANDES
  *    0x40 -> les octets suivants sont des DONNEES (pixels)
  *  On utilise donc HAL_I2C_Mem_Write(..., MemAddress = 0x00 ou 0x40, ...).
  *
  *  Memoire d'affichage : 8 pages de 8 lignes. Un octet = une colonne de
  *  8 pixels d'une page, bit 0 en haut.
  *
  *  Compatibilite SSD1306 / SH1106
  *  ------------------------------
  *  Beaucoup de modules vendus "SSD1306" embarquent en fait un SH1106 : RAM de
  *  132 colonnes (image visible decalee de 2), et PAS de mode d'adressage
  *  horizontal (commandes 0x20/0x21/0x22 inconnues). On utilise donc le mode
  *  d'adressage par page, commun aux deux : pour chaque page, 3 octets de
  *  commande (page, colonne basse, colonne haute) puis 128 octets de pixels.
  *  Les 8 pages sont envoyees une par une par DMA, enchainees par
  *  SSD1306_Task() depuis la boucle principale (jamais en interruption).
  ******************************************************************************
  */
#include "ssd1306.h"
#include "font5x7.h"
#include "i2c.h"
#include "main.h"
#include <string.h>

#define BUF_SIZE   (SSD1306_WIDTH * SSD1306_HEIGHT / 8)    /* 1024 octets */

static uint8_t s_fb[BUF_SIZE];      /* image de travail (dessin)                 */
static uint8_t s_tx[BUF_SIZE];      /* copie envoyee par DMA (ne pas modifier)   */
static uint8_t s_addr8 = 0;         /* adresse I2C decalee (format HAL), 0 = absent */
static bool    s_ok = false;
static int8_t  s_page = -1;        /* page en cours d'envoi, -1 = repos         */

/* ========================================================================== */
/* Bas niveau                                                                 */
/* ========================================================================== */

/* Envoie une suite de commandes (octet de controle 0x00), en mode bloquant */
static bool cmd(const uint8_t *c, uint16_t n)
{
  if (!s_addr8) return false;
  return HAL_I2C_Mem_Write(&hi2c1, s_addr8, 0x00, I2C_MEMADD_SIZE_8BIT,
                           (uint8_t *)c, n, SSD1306_I2C_TIMEOUT_MS) == HAL_OK;
}

static bool cmd1(uint8_t c) { return cmd(&c, 1); }

/* Deblocage du bus : si un esclave a ete interrompu au milieu d'un octet
 * (reset du micro, reflashage...), il peut maintenir SDA a 0 indefiniment.
 * On genere jusqu'a 9 impulsions sur SCL "a la main" puis une condition STOP,
 * ce qui lui fait terminer son octet et relacher la ligne. */
static void bus_recover(void)
{
  GPIO_InitTypeDef g = {0};

  HAL_I2C_DeInit(&hi2c1);                         /* rend les broches en GPIO */
  __HAL_RCC_GPIOB_CLK_ENABLE();
  g.Pin   = OLED_SCL_Pin | OLED_SDA_Pin;
  g.Mode  = GPIO_MODE_OUTPUT_OD;
  g.Pull  = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_WritePin(GPIOB, OLED_SCL_Pin | OLED_SDA_Pin, GPIO_PIN_SET);
  HAL_GPIO_Init(GPIOB, &g);

  for (int i = 0; i < 9 && HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin) == GPIO_PIN_RESET; i++)
  {
    HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_RESET);
    for (volatile int d = 0; d < 200; d++) { }  /* ~5 us */
    HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET);
    for (volatile int d = 0; d < 200; d++) { }
  }
  /* STOP : SDA monte pendant que SCL est haut */
  HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_RESET);
  for (volatile int d = 0; d < 200; d++) { }
  HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET);

  HAL_GPIO_DeInit(GPIOB, OLED_SCL_Pin | OLED_SDA_Pin);
  MX_I2C1_Init();                                 /* reconfigure I2C1 + DMA */
}

/* ========================================================================== */
/* Controle                                                                   */
/* ========================================================================== */
bool SSD1306_Init(void)
{
  /* Sequence d'initialisation standard d'un module 128x64 avec pompe de charge
   * interne (alimentation 3,3 V ou 5 V via le regulateur du module). */
  static const uint8_t init_seq[] =
  {
    0xAE,             /* ecran eteint pendant la configuration          */
    0xD5, 0x80,       /* horloge interne : diviseur 1, frequence ~370 kHz */
    0xA8, 0x3F,       /* multiplexage : 64 lignes                       */
    0xD3, 0x00,       /* pas de decalage vertical                       */
    0x40,             /* ligne de depart = 0                            */
#if !SSD1306_SH1106
    0x8D, 0x14,       /* SSD1306 : pompe de charge activee              */
    0x20, 0x02,       /* SSD1306 : adressage par page (valeur au reset) */
#else
    0xAD, 0x8B,       /* SH1106 : convertisseur DC-DC interne active    */
#endif
#if SSD1306_ROTATE_180
    0xA0,             /* colonnes dans l'ordre                          */
    0xC0,             /* balayage lignes croissant                      */
#else
    0xA1,             /* colonnes inversees : x = 0 a gauche            */
    0xC8,             /* balayage lignes decroissant : y = 0 en haut    */
#endif
    0xDA, 0x12,       /* configuration des broches COM (128x64)         */
    0x81, 0xCF,       /* contraste                                      */
    0xD9, 0xF1,       /* pre-charge                                     */
    0xDB, 0x40,       /* niveau VCOMH                                   */
    0xA4,             /* affiche le contenu de la RAM                   */
    0xA6,             /* video normale (pas inversee)                   */
#if !SSD1306_SH1106
    0x2E,             /* SSD1306 : pas de defilement                    */
#endif
    0xAF,             /* ecran allume                                   */
  };

  s_ok = false;
  s_addr8 = 0;
  bus_recover();

  /* Detection automatique de l'adresse */
  if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(SSD1306_ADDR_1 << 1), 2, 5) == HAL_OK)
    s_addr8 = (uint8_t)(SSD1306_ADDR_1 << 1);
  else if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(SSD1306_ADDR_2 << 1), 2, 5) == HAL_OK)
    s_addr8 = (uint8_t)(SSD1306_ADDR_2 << 1);
  else
    return false;

  if (!cmd(init_seq, sizeof init_seq)) { s_addr8 = 0; return false; }

  s_ok = true;
  s_page = -1;
  SSD1306_Clear();
  return true;
}

bool    SSD1306_Present(void) { return s_ok; }
uint8_t SSD1306_Address(void) { return (uint8_t)(s_addr8 >> 1); }

bool SSD1306_Busy(void)
{
  return s_page >= 0 || HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY;
}

/* Envoie la page suivante si le bus est libre. Une page = 3 commandes en mode
 * bloquant (~80 us) puis 128 octets par DMA (~3 ms). */
static void send_next_page(void)
{
  if (s_page < 0 || HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) return;
  if (s_page >= (int8_t)(SSD1306_HEIGHT / 8)) { s_page = -1; return; }     /* image complete */

  uint8_t c[3] =
  {
    (uint8_t)(0xB0u | (uint8_t)s_page),                     /* numero de page      */
    (uint8_t)(0x00u | (SSD1306_COL_OFFSET & 0x0Fu)),         /* colonne, 4 bits bas */
    (uint8_t)(0x10u | (SSD1306_COL_OFFSET >> 4)),            /* colonne, 4 bits hauts */
  };
  if (!cmd(c, 3) ||
      HAL_I2C_Mem_Write_DMA(&hi2c1, s_addr8, 0x40, I2C_MEMADD_SIZE_8BIT,
                            &s_tx[s_page * SSD1306_WIDTH], SSD1306_WIDTH) != HAL_OK)
  {
    s_ok = false;
    s_page = -1;
    return;
  }
  s_page++;
}

void SSD1306_Task(void)
{
  if (s_ok) send_next_page();
}

bool SSD1306_Update(void)
{
  if (!s_ok) return false;
  if (SSD1306_Busy()) return false;            /* image precedente en cours d'envoi */

  memcpy(s_tx, s_fb, BUF_SIZE);                /* on peut redessiner pendant l'envoi */
  s_page = 0;
  send_next_page();
  return s_ok;
}

void SSD1306_DisplayOn(bool on)   { if (s_ok && !SSD1306_Busy()) cmd1(on ? 0xAF : 0xAE); }
void SSD1306_Invert(bool inv)     { if (s_ok && !SSD1306_Busy()) cmd1(inv ? 0xA7 : 0xA6); }
void SSD1306_Contrast(uint8_t v)
{
  uint8_t c[2] = { 0x81, v };
  if (s_ok && !SSD1306_Busy()) cmd(c, 2);
}

/* Erreur I2C pendant le DMA (ecran debranche...) : on marque l'ecran absent ;
 * le module d'affichage tentera une reinitialisation plus tard. */
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c->Instance == I2C1) { s_ok = false; s_page = -1; }
}

/* ========================================================================== */
/* Dessin                                                                     */
/* ========================================================================== */
uint8_t *SSD1306_Buffer(void) { return s_fb; }

void SSD1306_Clear(void) { memset(s_fb, 0x00, BUF_SIZE); }
void SSD1306_Fill(SSD1306_Color_t c) { memset(s_fb, (c == SSD1306_ON) ? 0xFF : 0x00, BUF_SIZE); }

void SSD1306_Pixel(int16_t x, int16_t y, SSD1306_Color_t c)
{
  if (x < 0 || y < 0 || x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;
  uint8_t *p = &s_fb[(y >> 3) * SSD1306_WIDTH + x];      /* page = y / 8 */
  uint8_t  m = (uint8_t)(1u << (y & 7));
  if (c == SSD1306_ON)       *p |= m;
  else if (c == SSD1306_OFF) *p &= (uint8_t)~m;
  else                       *p ^= m;
}

void SSD1306_HLine(int16_t x, int16_t y, int16_t w, SSD1306_Color_t c)
{
  for (int16_t i = 0; i < w; i++) SSD1306_Pixel((int16_t)(x + i), y, c);
}

void SSD1306_VLine(int16_t x, int16_t y, int16_t h, SSD1306_Color_t c)
{
  for (int16_t i = 0; i < h; i++) SSD1306_Pixel(x, (int16_t)(y + i), c);
}

void SSD1306_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c)
{
  for (int16_t j = 0; j < h; j++) SSD1306_HLine(x, (int16_t)(y + j), w, c);
}

void SSD1306_Rect(int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c)
{
  if (w <= 0 || h <= 0) return;
  SSD1306_HLine(x, y, w, c);
  SSD1306_HLine(x, (int16_t)(y + h - 1), w, c);
  SSD1306_VLine(x, (int16_t)(y + 1), (int16_t)(h - 2), c);
  SSD1306_VLine((int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), c);
}

void SSD1306_Triangle(int16_t cx, int16_t ytop, int16_t h, bool up, SSD1306_Color_t c)
{
  if (h < 2) return;
  int16_t hw = (int16_t)(h / 2);
  for (int16_t k = 0; k < h; k++)
  {
    int16_t row  = up ? k : (int16_t)(h - 1 - k);         /* row 0 = pointe */
    int16_t half = (int16_t)(row * hw / (h - 1));
    SSD1306_HLine((int16_t)(cx - half), (int16_t)(ytop + k), (int16_t)(2 * half + 1), c);
  }
}

uint16_t SSD1306_TextWidth(const char *s, uint8_t scale)
{
  return (uint16_t)(strlen(s) * 6u * (scale ? scale : 1u));
}

int16_t SSD1306_Text(int16_t x, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c)
{
  if (scale < 1u) scale = 1u;
  if (scale > 4u) scale = 4u;
  SSD1306_Color_t bg = (c == SSD1306_ON) ? SSD1306_OFF : (c == SSD1306_OFF) ? SSD1306_ON : SSD1306_INV;

  for (; *s; s++, x = (int16_t)(x + 6 * scale))
  {
    uint8_t ch = (uint8_t)*s;
    if (ch < FONT5X7_FIRST || ch > FONT5X7_LAST) ch = '?';
    const uint8_t *g = font5x7[ch - FONT5X7_FIRST];

    for (uint8_t col = 0; col < 6u; col++)              /* 5 colonnes + 1 d'espace */
    {
      uint8_t bits = (col < 5u) ? g[col] : 0u;
      for (uint8_t row = 0; row < 8u; row++)
      {
        bool on = (bits >> row) & 1u;
        if (!on && c == SSD1306_INV) continue;          /* INV : ne touche pas au fond */
        SSD1306_Color_t pc = on ? c : bg;
        if (scale == 1u)
          SSD1306_Pixel((int16_t)(x + col), (int16_t)(y + row), pc);
        else
          SSD1306_FillRect((int16_t)(x + col * scale), (int16_t)(y + row * scale), scale, scale, pc);
      }
    }
  }
  return x;
}

void SSD1306_Bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bits, SSD1306_Color_t c)
{
  int16_t stride = (int16_t)((w + 7) / 8);
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++)
      if (bits[j * stride + (i >> 3)] & (0x80u >> (i & 7)))
        SSD1306_Pixel((int16_t)(x + i), (int16_t)(y + j), c);
}
