/**
  ******************************************************************************
  * @file    ssd1306.c
  * @brief   Librairie OLED SSD1306 / SH1106 sur I2C (voir ssd1306.h).
  *
  *  Protocole I2C du SSD1306
  *  ------------------------
  *  Chaque trame commence par un octet de controle :
  *    0x00 -> les octets suivants sont des COMMANDES
  *    0x40 -> les octets suivants sont des DONNEES (pixels)
  *  On utilise donc HAL_I2C_Mem_Write(..., MemAddress = 0x00 ou 0x40, ...).
  *
  *  Memoire d'affichage : pages de 8 lignes. Un octet = une colonne de
  *  8 pixels d'une page, bit 0 en haut.
  *
  *  Compatibilite SSD1306 / SH1106
  *  ------------------------------
  *  Le SH1106 a une RAM de 132 colonnes (image visible decalee de 2) et PAS de
  *  mode d'adressage horizontal (commandes 0x20/0x21/0x22 inconnues). On utilise
  *  donc le mode d'adressage par page, commun aux deux : pour chaque page,
  *  3 octets de commande (page, colonne basse, colonne haute) puis 128 octets
  *  de pixels. En mode DMA, les pages sont envoyees une par une, enchainees par
  *  SSD1306_Task() depuis la boucle principale (jamais en interruption).
  ******************************************************************************
  */
#include "ssd1306.h"
#include "ssd1306_font5x7.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define W   SSD1306_MAX_WIDTH                       /* largeur : toujours 128 */

/**
 * @brief  Nombre de pages de 8 lignes de l'ecran (4 ou 8).
 * @param  d  Ecran.
 * @retval Nombre de pages.
 */
static inline uint8_t pages(const SSD1306_t *d) { return (uint8_t)(d->cfg.height / 8u); }

/* ========================================================================== */
/* Bas niveau                                                                 */
/* ========================================================================== */

/**
 * @brief  Envoie une suite de commandes (octet de controle 0x00), en mode
 *         bloquant.
 * @param  d  Ecran.
 * @param  c  Commandes.
 * @param  n  Nombre d'octets.
 * @retval true si l'ecran a acquitte.
 */
static bool cmd(SSD1306_t *d, const uint8_t *c, uint16_t n)
{
  if (!d->addr8) return false;
  return HAL_I2C_Mem_Write(d->cfg.hi2c, d->addr8, 0x00, I2C_MEMADD_SIZE_8BIT,
                           (uint8_t *)c, n, SSD1306_I2C_TIMEOUT_MS) == HAL_OK;
}

/**
 * @brief  Envoie une commande d'un seul octet (bloquant).
 * @param  d  Ecran.
 * @param  c  Commande.
 * @retval true si l'ecran a acquitte.
 */
static bool cmd1(SSD1306_t *d, uint8_t c) { return cmd(d, &c, 1); }

/**
 * @brief  Positionne l'ecriture au debut de la page p (decalage de 2 colonnes
 *         pour le SH1106).
 * @param  d  Ecran.
 * @param  p  Numero de page.
 * @retval true si l'ecran a acquitte.
 */
static bool set_page(SSD1306_t *d, uint8_t p)
{
  uint8_t off = (d->cfg.controller == SSD1306_CTRL_SH1106) ? 2u : 0u;
  uint8_t c[3] =
  {
    (uint8_t)(0xB0u | p),                   /* numero de page        */
    (uint8_t)(0x00u | (off & 0x0Fu)),       /* colonne, 4 bits bas   */
    (uint8_t)(0x10u | (off >> 4)),          /* colonne, 4 bits hauts */
  };
  return cmd(d, c, 3);
}

/**
 * @brief  Envoie une page de d->tx en mode bloquant (~3 ms a 400 kHz).
 * @param  d  Ecran.
 * @param  p  Numero de page.
 * @retval true si l'ecran a acquitte.
 */
static bool write_page_blocking(SSD1306_t *d, uint8_t p)
{
  return set_page(d, p) &&
         HAL_I2C_Mem_Write(d->cfg.hi2c, d->addr8, 0x40, I2C_MEMADD_SIZE_8BIT,
                           &d->tx[p * W], W, SSD1306_I2C_TIMEOUT_MS + 10u) == HAL_OK;
}

/**
 * @brief  Attente d'environ 5 us quelle que soit l'horloge du coeur (boucle de
 *         ~4 cycles), pour la generation manuelle de SCL.
 */
static void delay_5us(void)
{
  for (volatile uint32_t i = SystemCoreClock / 800000u; i > 0u; i--) { }
}

/**
 * @brief  Deblocage du bus : si un esclave a ete interrompu au milieu d'un
 *         octet (reset du micro, reflashage...), il peut maintenir SDA a 0
 *         indefiniment. On genere jusqu'a 9 impulsions sur SCL "a la main" puis
 *         une condition STOP, ce qui lui fait terminer son octet et relacher la
 *         ligne. Le peripherique I2C est ensuite reinitialise. Sans effet si les
 *         broches SCL / SDA ne sont pas renseignees dans la configuration.
 * @param  d  Ecran.
 */
static void bus_recover(SSD1306_t *d)
{
  const SSD1306_Config_t *c = &d->cfg;
  GPIO_InitTypeDef g = {0};

  if (c->scl_port == NULL || c->sda_port == NULL) return;

  HAL_I2C_DeInit(c->hi2c);                        /* rend les broches en GPIO */
  HAL_GPIO_WritePin(c->scl_port, c->scl_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(c->sda_port, c->sda_pin, GPIO_PIN_SET);
  g.Mode  = GPIO_MODE_OUTPUT_OD;
  g.Pull  = GPIO_PULLUP;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Pin   = c->scl_pin; HAL_GPIO_Init(c->scl_port, &g);
  g.Pin   = c->sda_pin; HAL_GPIO_Init(c->sda_port, &g);

  for (int i = 0; i < 9 && HAL_GPIO_ReadPin(c->sda_port, c->sda_pin) == GPIO_PIN_RESET; i++)
  {
    HAL_GPIO_WritePin(c->scl_port, c->scl_pin, GPIO_PIN_RESET);
    delay_5us();
    HAL_GPIO_WritePin(c->scl_port, c->scl_pin, GPIO_PIN_SET);
    delay_5us();
  }
  /* STOP : SDA monte pendant que SCL est haut */
  HAL_GPIO_WritePin(c->sda_port, c->sda_pin, GPIO_PIN_RESET);
  delay_5us();
  HAL_GPIO_WritePin(c->sda_port, c->sda_pin, GPIO_PIN_SET);
  delay_5us();

  HAL_GPIO_DeInit(c->scl_port, c->scl_pin);
  HAL_GPIO_DeInit(c->sda_port, c->sda_pin);
  HAL_I2C_Init(c->hi2c);              /* rappelle HAL_I2C_MspInit : broches, DMA, IRQ */
}

/* ========================================================================== */
/* Controle                                                                   */
/* ========================================================================== */
/**
 * @brief  Remplit une configuration par defaut : adresse auto-detectee,
 *         SSD1306 128 x 64, contraste 0xCF, envoi par DMA si disponible.
 * @param  cfg   Configuration a remplir.
 * @param  hi2c  Handle I2C a utiliser.
 */
void SSD1306_DefaultConfig(SSD1306_Config_t *cfg, I2C_HandleTypeDef *hi2c)
{
  memset(cfg, 0, sizeof *cfg);
  cfg->hi2c       = hi2c;
  cfg->address    = SSD1306_ADDR_AUTO;
  cfg->controller = SSD1306_CTRL_SSD1306;
  cfg->height     = 64;
  cfg->rotate_180 = false;
  cfg->contrast   = 0xCF;
  cfg->use_dma    = true;
}

/**
 * @brief  Initialise l'ecran : deblocage du bus, detection de l'adresse (0x3C
 *         puis 0x3D, ou adresse imposee), sequence de configuration propre au
 *         controleur, effacement de la RAM puis allumage. Bloquant (quelques
 *         dizaines de ms). Peut etre rappelee pour une nouvelle detection.
 * @param  d    Ecran (etat interne).
 * @param  cfg  Configuration (copiee).
 * @retval true si l'ecran a repondu et est initialise.
 */
bool SSD1306_Init(SSD1306_t *d, const SSD1306_Config_t *cfg)
{
  if (d == NULL || cfg == NULL || cfg->hi2c == NULL) return false;

  d->cfg   = *cfg;
  d->ok    = false;
  d->addr8 = 0;
  d->page  = -1;
  if (d->cfg.height != 32u) d->cfg.height = 64u;
  memset(d->fb, 0, sizeof d->fb);

  bus_recover(d);

  /* Adresse : imposee ou detectee (0x3C puis 0x3D) */
  const uint8_t try_addr[2] = { SSD1306_ADDR_3C, SSD1306_ADDR_3D };
  for (int i = 0; i < 2 && !d->addr8; i++)
  {
    uint8_t a = (d->cfg.address != SSD1306_ADDR_AUTO) ? d->cfg.address : try_addr[i];
    if (HAL_I2C_IsDeviceReady(d->cfg.hi2c, (uint16_t)(a << 1), 2, 5) == HAL_OK)
      d->addr8 = (uint8_t)(a << 1);
    if (d->cfg.address != SSD1306_ADDR_AUTO) break;
  }
  if (!d->addr8) return false;

  d->dma = d->cfg.use_dma && d->cfg.hi2c->hdmatx != NULL;

  /* Sequence d'initialisation standard d'un module avec pompe de charge
   * interne (alimentation 3,3 V ou 5 V via le regulateur du module). */
  const bool sh = (d->cfg.controller == SSD1306_CTRL_SH1106);
  uint8_t s[32];
  uint8_t n = 0;
  s[n++] = 0xAE;                                /* ecran eteint pendant la configuration */
  s[n++] = 0xD5; s[n++] = 0x80;                 /* horloge interne                       */
  s[n++] = 0xA8; s[n++] = (uint8_t)(d->cfg.height - 1u);   /* multiplexage              */
  s[n++] = 0xD3; s[n++] = 0x00;                 /* pas de decalage vertical              */
  s[n++] = 0x40;                                /* ligne de depart = 0                   */
  if (sh)
  {
    s[n++] = 0xAD; s[n++] = 0x8B;               /* SH1106 : DC-DC interne active         */
  }
  else
  {
    s[n++] = 0x8D; s[n++] = 0x14;               /* SSD1306 : pompe de charge activee     */
    s[n++] = 0x20; s[n++] = 0x02;               /* SSD1306 : adressage par page          */
  }
  s[n++] = d->cfg.rotate_180 ? 0xA0 : 0xA1;     /* sens des colonnes                     */
  s[n++] = d->cfg.rotate_180 ? 0xC0 : 0xC8;     /* sens des lignes                       */
  s[n++] = 0xDA; s[n++] = (d->cfg.height == 64u) ? 0x12 : 0x02;  /* broches COM         */
  s[n++] = 0x81; s[n++] = d->cfg.contrast;      /* contraste                             */
  s[n++] = 0xD9; s[n++] = 0xF1;                 /* pre-charge                            */
  s[n++] = 0xDB; s[n++] = 0x40;                 /* niveau VCOMH                          */
  s[n++] = 0xA4;                                /* affiche le contenu de la RAM          */
  s[n++] = 0xA6;                                /* video normale                         */
  if (!sh) s[n++] = 0x2E;                       /* SSD1306 : pas de defilement           */

  if (!cmd(d, s, n)) { d->addr8 = 0; return false; }

  /* Efface la RAM de l'ecran avant de l'allumer (evite la "neige" au demarrage) */
  memset(d->tx, 0, sizeof d->tx);
  for (uint8_t p = 0; p < pages(d); p++)
    if (!write_page_blocking(d, p)) { d->addr8 = 0; return false; }

  if (!cmd1(d, 0xAF)) { d->addr8 = 0; return false; }   /* ecran allume */

  d->ok = true;
  return true;
}

/**
 * @brief  Indique si l'ecran est initialise et sans erreur I2C depuis.
 * @param  d  Ecran.
 * @retval true si l'ecran est utilisable.
 */
bool    SSD1306_Present(const SSD1306_t *d) { return d->ok; }
/**
 * @brief  Adresse I2C detectee.
 * @param  d  Ecran.
 * @retval Adresse 7 bits (0 si aucun ecran).
 */
uint8_t SSD1306_Address(const SSD1306_t *d) { return (uint8_t)(d->addr8 >> 1); }
/**
 * @brief  Largeur de l'ecran.
 * @param  d  Ecran (inutilise).
 * @retval Largeur en pixels (toujours 128).
 */
uint8_t SSD1306_Width(const SSD1306_t *d)   { (void)d; return W; }
/**
 * @brief  Hauteur de l'ecran.
 * @param  d  Ecran.
 * @retval Hauteur en pixels (32 ou 64).
 */
uint8_t SSD1306_Height(const SSD1306_t *d)  { return d->cfg.height; }

/**
 * @brief  Indique si une image est en cours d'envoi ou si le bus I2C est
 *         occupe.
 * @param  d  Ecran.
 * @retval true si occupe.
 */
bool SSD1306_Busy(const SSD1306_t *d)
{
  return d->page >= 0 || HAL_I2C_GetState(d->cfg.hi2c) != HAL_I2C_STATE_READY;
}

/**
 * @brief  Envoie la page suivante si le bus est libre : 3 commandes en mode
 *         bloquant (~80 us) puis 128 octets par DMA (~3 ms). Termine l'image
 *         apres la derniere page ; marque l'ecran absent en cas d'erreur.
 * @param  d  Ecran.
 */
static void send_next_page(SSD1306_t *d)
{
  if (d->page < 0 || HAL_I2C_GetState(d->cfg.hi2c) != HAL_I2C_STATE_READY) return;
  if (d->page >= (int8_t)pages(d)) { d->page = -1; return; }      /* image complete */

  if (!set_page(d, (uint8_t)d->page) ||
      HAL_I2C_Mem_Write_DMA(d->cfg.hi2c, d->addr8, 0x40, I2C_MEMADD_SIZE_8BIT,
                            &d->tx[d->page * W], W) != HAL_OK)
  {
    d->ok = false;
    d->page = -1;
    return;
  }
  d->page++;
}

/**
 * @brief  Enchaine l'envoi DMA des pages, a appeler souvent depuis la boucle
 *         principale (jamais en interruption). Sans effet en mode bloquant.
 * @param  d  Ecran.
 */
void SSD1306_Task(SSD1306_t *d)
{
  if (d->ok && d->dma) send_next_page(d);
}

/**
 * @brief  Envoie le tampon de dessin a l'ecran. Le tampon est d'abord copie
 *         dans le tampon d'emission : on peut redessiner pendant l'envoi.
 *         En DMA, l'envoi se poursuit dans SSD1306_Task() ; sinon il est
 *         bloquant (~25 ms pour 8 pages).
 * @param  d  Ecran.
 * @retval false si l'ecran est absent, occupe ou en erreur.
 */
bool SSD1306_Update(SSD1306_t *d)
{
  if (!d->ok) return false;
  if (SSD1306_Busy(d)) return false;              /* image precedente en cours d'envoi */

  memcpy(d->tx, d->fb, (size_t)pages(d) * W);     /* on peut redessiner pendant l'envoi */

  if (!d->dma)
  {
    for (uint8_t p = 0; p < pages(d); p++)
      if (!write_page_blocking(d, p)) { d->ok = false; return false; }
    return true;
  }

  d->page = 0;
  send_next_page(d);
  return d->ok;
}

/**
 * @brief  A appeler depuis HAL_I2C_ErrorCallback() : si l'erreur concerne le
 *         bus de cet ecran (debranche...), il est marque absent ; l'application
 *         peut rappeler SSD1306_Init() plus tard.
 * @param  d     Ecran.
 * @param  hi2c  Handle I2C en erreur.
 */
void SSD1306_ErrorHandler(SSD1306_t *d, I2C_HandleTypeDef *hi2c)
{
  /* Erreur I2C pendant le DMA (ecran debranche...) : l'ecran est marque absent,
   * l'application peut rappeler SSD1306_Init() plus tard. */
  if (d != NULL && hi2c == d->cfg.hi2c) { d->ok = false; d->page = -1; }
}

/**
 * @brief  Allume ou eteint l'affichage (la RAM est conservee). Ignore si
 *         l'ecran est occupe.
 * @param  d   Ecran.
 * @param  on  true pour allumer.
 */
void SSD1306_DisplayOn(SSD1306_t *d, bool on)  { if (d->ok && !SSD1306_Busy(d)) cmd1(d, on ? 0xAF : 0xAE); }
/**
 * @brief  Active ou desactive la video inverse. Ignore si l'ecran est occupe.
 * @param  d    Ecran.
 * @param  inv  true pour inverser.
 */
void SSD1306_Invert(SSD1306_t *d, bool inv)    { if (d->ok && !SSD1306_Busy(d)) cmd1(d, inv ? 0xA7 : 0xA6); }
/**
 * @brief  Regle le contraste (memorise meme si l'ecran est occupe, envoye
 *         seulement s'il est libre).
 * @param  d  Ecran.
 * @param  v  Contraste (0..255).
 */
void SSD1306_Contrast(SSD1306_t *d, uint8_t v)
{
  uint8_t c[2] = { 0x81, v };
  d->cfg.contrast = v;
  if (d->ok && !SSD1306_Busy(d)) cmd(d, c, 2);
}

/* ========================================================================== */
/* Dessin                                                                     */
/* ========================================================================== */
/**
 * @brief  Acces direct au tampon de dessin (pages de 8 lignes, bit 0 en haut).
 * @param  d  Ecran.
 * @retval Pointeur sur le tampon.
 */
uint8_t *SSD1306_Buffer(SSD1306_t *d) { return d->fb; }

/**
 * @brief  Efface le tampon de dessin (tous les pixels eteints).
 * @param  d  Ecran.
 */
void SSD1306_Clear(SSD1306_t *d) { memset(d->fb, 0x00, sizeof d->fb); }
/**
 * @brief  Remplit le tampon de dessin (allume, eteint ou inverse tout).
 * @param  d  Ecran.
 * @param  c  SSD1306_ON, SSD1306_OFF ou SSD1306_INV.
 */
void SSD1306_Fill(SSD1306_t *d, SSD1306_Color_t c)
{
  if (c == SSD1306_INV)
    for (size_t i = 0; i < sizeof d->fb; i++) d->fb[i] ^= 0xFFu;
  else
    memset(d->fb, (c == SSD1306_ON) ? 0xFF : 0x00, sizeof d->fb);
}

/**
 * @brief  Allume, eteint ou inverse un pixel (ignore hors de l'ecran).
 * @param  d     Ecran.
 * @param  x, y  Position (px).
 * @param  c     Couleur.
 */
void SSD1306_Pixel(SSD1306_t *d, int16_t x, int16_t y, SSD1306_Color_t c)
{
  if (x < 0 || y < 0 || x >= W || y >= d->cfg.height) return;
  uint8_t *p = &d->fb[(y >> 3) * W + x];      /* page = y / 8 */
  uint8_t  m = (uint8_t)(1u << (y & 7));
  if (c == SSD1306_ON)       *p |= m;
  else if (c == SSD1306_OFF) *p &= (uint8_t)~m;
  else                       *p ^= m;
}

/**
 * @brief  Lit un pixel du tampon de dessin.
 * @param  d     Ecran.
 * @param  x, y  Position (px).
 * @retval true si le pixel est allume (false hors de l'ecran).
 */
bool SSD1306_GetPixel(const SSD1306_t *d, int16_t x, int16_t y)
{
  if (x < 0 || y < 0 || x >= W || y >= d->cfg.height) return false;
  return (d->fb[(y >> 3) * W + x] >> (y & 7)) & 1u;
}

/**
 * @brief  Trace une ligne horizontale.
 * @param  d     Ecran.
 * @param  x, y  Point de depart (px).
 * @param  w     Longueur (px).
 * @param  c     Couleur.
 */
void SSD1306_HLine(SSD1306_t *d, int16_t x, int16_t y, int16_t w, SSD1306_Color_t c)
{
  for (int16_t i = 0; i < w; i++) SSD1306_Pixel(d, (int16_t)(x + i), y, c);
}

/**
 * @brief  Trace une ligne verticale.
 * @param  d     Ecran.
 * @param  x, y  Point de depart (px).
 * @param  h     Longueur (px).
 * @param  c     Couleur.
 */
void SSD1306_VLine(SSD1306_t *d, int16_t x, int16_t y, int16_t h, SSD1306_Color_t c)
{
  for (int16_t i = 0; i < h; i++) SSD1306_Pixel(d, x, (int16_t)(y + i), c);
}

/**
 * @brief  Trace un segment quelconque (algorithme de Bresenham).
 * @param  d       Ecran.
 * @param  x0, y0  Premiere extremite (px).
 * @param  x1, y1  Seconde extremite (px).
 * @param  c       Couleur.
 */
void SSD1306_Line(SSD1306_t *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1, SSD1306_Color_t c)
{
  int16_t dx = (int16_t)(x1 > x0 ? x1 - x0 : x0 - x1);
  int16_t dy = (int16_t)(y1 > y0 ? y0 - y1 : y1 - y0);     /* negatif */
  int16_t sx = (x0 < x1) ? 1 : -1;
  int16_t sy = (y0 < y1) ? 1 : -1;
  int16_t err = (int16_t)(dx + dy);

  for (;;)
  {
    SSD1306_Pixel(d, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = (int16_t)(2 * err);
    if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
    if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
  }
}

/**
 * @brief  Remplit un rectangle.
 * @param  d     Ecran.
 * @param  x, y  Coin haut gauche (px).
 * @param  w, h  Taille (px).
 * @param  c     Couleur.
 */
void SSD1306_FillRect(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c)
{
  for (int16_t j = 0; j < h; j++) SSD1306_HLine(d, x, (int16_t)(y + j), w, c);
}

/**
 * @brief  Trace le contour d'un rectangle (chaque pixel une seule fois, ce
 *         qui le rend compatible avec SSD1306_INV).
 * @param  d     Ecran.
 * @param  x, y  Coin haut gauche (px).
 * @param  w, h  Taille (px).
 * @param  c     Couleur.
 */
void SSD1306_Rect(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c)
{
  if (w <= 0 || h <= 0) return;
  SSD1306_HLine(d, x, y, w, c);
  if (h > 1) SSD1306_HLine(d, x, (int16_t)(y + h - 1), w, c);
  SSD1306_VLine(d, x, (int16_t)(y + 1), (int16_t)(h - 2), c);
  if (w > 1) SSD1306_VLine(d, (int16_t)(x + w - 1), (int16_t)(y + 1), (int16_t)(h - 2), c);
}

/**
 * @brief  Trace un cercle (algorithme du point milieu). En SSD1306_INV, aucun
 *         point n'est dessine deux fois.
 * @param  d       Ecran.
 * @param  cx, cy  Centre (px).
 * @param  r       Rayon (px).
 * @param  c       Couleur.
 */
void SSD1306_Circle(SSD1306_t *d, int16_t cx, int16_t cy, int16_t r, SSD1306_Color_t c)
{
  if (r < 0) return;
  if (r == 0) { SSD1306_Pixel(d, cx, cy, c); return; }
  int16_t x = r, y = 0, err = (int16_t)(1 - r);
  while (x >= y)
  {
    /* 8 octants ; en INV on evite de dessiner deux fois le meme point */
    SSD1306_Pixel(d, (int16_t)(cx + x), (int16_t)(cy + y), c);
    SSD1306_Pixel(d, (int16_t)(cx - x), (int16_t)(cy + y), c);
    if (y != 0)
    {
      SSD1306_Pixel(d, (int16_t)(cx + x), (int16_t)(cy - y), c);
      SSD1306_Pixel(d, (int16_t)(cx - x), (int16_t)(cy - y), c);
    }
    if (x != y)
    {
      SSD1306_Pixel(d, (int16_t)(cx + y), (int16_t)(cy + x), c);
      SSD1306_Pixel(d, (int16_t)(cx + y), (int16_t)(cy - x), c);
      if (y != 0)
      {
        SSD1306_Pixel(d, (int16_t)(cx - y), (int16_t)(cy + x), c);
        SSD1306_Pixel(d, (int16_t)(cx - y), (int16_t)(cy - x), c);
      }
    }
    y++;
    if (err < 0) err = (int16_t)(err + 2 * y + 1);
    else         { x--; err = (int16_t)(err + 2 * (y - x) + 1); }
  }
}

/**
 * @brief  Remplit un disque, ligne horizontale par ligne horizontale.
 * @param  d       Ecran.
 * @param  cx, cy  Centre (px).
 * @param  r       Rayon (px).
 * @param  c       Couleur.
 */
void SSD1306_FillCircle(SSD1306_t *d, int16_t cx, int16_t cy, int16_t r, SSD1306_Color_t c)
{
  if (r < 0) return;
  for (int16_t dy = (int16_t)-r; dy <= r; dy++)
  {
    /* demi-largeur de la corde : plus grand dx avec dx^2 + dy^2 <= r^2 + r */
    int16_t dx = 0;
    while ((int32_t)(dx + 1) * (dx + 1) + (int32_t)dy * dy <= (int32_t)r * r + r) dx++;
    SSD1306_HLine(d, (int16_t)(cx - dx), (int16_t)(cy + dy), (int16_t)(2 * dx + 1), c);
  }
}

/**
 * @brief  Dessine un triangle isocele plein (fleche).
 * @param  d     Ecran.
 * @param  cx    Abscisse de l'axe (px).
 * @param  ytop  Ordonnee du haut (px).
 * @param  h     Hauteur (>= 2).
 * @param  up    true : pointe en haut ; false : pointe en bas.
 * @param  c     Couleur.
 */
void SSD1306_Triangle(SSD1306_t *d, int16_t cx, int16_t ytop, int16_t h, bool up, SSD1306_Color_t c)
{
  if (h < 2) return;
  int16_t hw = (int16_t)(h / 2);
  for (int16_t k = 0; k < h; k++)
  {
    int16_t row  = up ? k : (int16_t)(h - 1 - k);         /* row 0 = pointe */
    int16_t half = (int16_t)(row * hw / (h - 1));
    SSD1306_HLine(d, (int16_t)(cx - half), (int16_t)(ytop + k), (int16_t)(2 * half + 1), c);
  }
}

/**
 * @brief  Barre de progression : contour, fond efface, remplissage
 *         proportionnel.
 * @param  d         Ecran.
 * @param  x, y      Coin haut gauche (px).
 * @param  w, h      Taille (px, >= 5).
 * @param  permille  Avancement en pour mille (0..1000).
 */
void SSD1306_ProgressBar(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t permille)
{
  if (w < 5 || h < 5) return;
  if (permille > 1000u) permille = 1000u;
  SSD1306_Rect(d, x, y, w, h, SSD1306_ON);
  SSD1306_FillRect(d, (int16_t)(x + 2), (int16_t)(y + 2), (int16_t)(w - 4), (int16_t)(h - 4), SSD1306_OFF);
  int16_t fill = (int16_t)((int32_t)(w - 4) * permille / 1000);
  SSD1306_FillRect(d, (int16_t)(x + 2), (int16_t)(y + 2), fill, (int16_t)(h - 4), SSD1306_ON);
}

/**
 * @brief  Largeur d'un texte (ligne la plus longue s'il contient des '\n').
 * @param  s      Texte.
 * @param  scale  Taille de police (1..4).
 * @retval Largeur en pixels (6 px par caractere x scale).
 */
uint16_t SSD1306_TextWidth(const char *s, uint8_t scale)
{
  uint16_t best = 0, cur = 0;
  if (scale < 1u) scale = 1u;
  if (scale > 4u) scale = 4u;
  for (; *s; s++)
  {
    if (*s == '\n') { cur = 0; continue; }
    cur++;
    if (cur > best) best = cur;
  }
  return (uint16_t)(best * 6u * scale);
}

/**
 * @brief  Ecrit un texte en police 5x7 (cellule 6 x 8 x scale). '\n' revient
 *         a la ligne ; un caractere hors police est remplace par '?'. En ON ou
 *         OFF le fond de la cellule est ecrit dans la couleur opposee ; en INV,
 *         seuls les pixels du caractere sont inverses.
 * @param  d      Ecran.
 * @param  x, y   Coin haut gauche (px).
 * @param  s      Texte.
 * @param  scale  Taille de police (1..4).
 * @param  c      Couleur.
 * @retval Abscisse a droite du dernier caractere ecrit.
 */
int16_t SSD1306_Text(SSD1306_t *d, int16_t x, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c)
{
  if (scale < 1u) scale = 1u;
  if (scale > 4u) scale = 4u;
  SSD1306_Color_t bg = (c == SSD1306_ON) ? SSD1306_OFF : (c == SSD1306_OFF) ? SSD1306_ON : SSD1306_INV;
  const int16_t x0 = x;

  for (; *s; s++)
  {
    if (*s == '\n') { x = x0; y = (int16_t)(y + 8 * scale); continue; }

    uint8_t ch = (uint8_t)*s;
    if (ch < SSD1306_FONT_FIRST || ch > SSD1306_FONT_LAST) ch = '?';
    const uint8_t *g = ssd1306_font5x7[ch - SSD1306_FONT_FIRST];

    for (uint8_t col = 0; col < 6u; col++)              /* 5 colonnes + 1 d'espace */
    {
      uint8_t bits = (col < 5u) ? g[col] : 0u;
      for (uint8_t row = 0; row < 8u; row++)
      {
        bool on = (bits >> row) & 1u;
        if (!on && c == SSD1306_INV) continue;          /* INV : ne touche pas au fond */
        SSD1306_Color_t pc = on ? c : bg;
        if (scale == 1u)
          SSD1306_Pixel(d, (int16_t)(x + col), (int16_t)(y + row), pc);
        else
          SSD1306_FillRect(d, (int16_t)(x + col * scale), (int16_t)(y + row * scale), scale, scale, pc);
      }
    }
    x = (int16_t)(x + 6 * scale);
  }
  return x;
}

/**
 * @brief  Ecrit un texte centre horizontalement sur l'ecran.
 * @param  d      Ecran.
 * @param  y      Ordonnee du haut (px).
 * @param  s      Texte.
 * @param  scale  Taille de police.
 * @param  c      Couleur.
 */
void SSD1306_TextCentered(SSD1306_t *d, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c)
{
  int16_t x = (int16_t)((W - (int16_t)SSD1306_TextWidth(s, scale)) / 2);
  SSD1306_Text(d, x < 0 ? 0 : x, y, s, scale, c);
}

/**
 * @brief  Ecrit un texte formate facon printf (64 caracteres max).
 * @param  d      Ecran.
 * @param  x, y   Coin haut gauche (px).
 * @param  scale  Taille de police.
 * @param  c      Couleur.
 * @param  fmt    Format printf, suivi des arguments.
 * @retval Abscisse a droite du dernier caractere ecrit.
 */
int16_t SSD1306_Printf(SSD1306_t *d, int16_t x, int16_t y, uint8_t scale, SSD1306_Color_t c,
                       const char *fmt, ...)
{
  char buf[65];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return SSD1306_Text(d, x, y, buf, scale, c);
}

/**
 * @brief  Dessine une image 1 bit par pixel (lignes de (w + 7) / 8 octets,
 *         bit de poids fort a gauche). Seuls les pixels a 1 sont dessines.
 * @param  d     Ecran.
 * @param  x, y  Coin haut gauche (px).
 * @param  w, h  Taille de l'image (px).
 * @param  bits  Donnees de l'image.
 * @param  c     Couleur des pixels a 1.
 */
void SSD1306_Bitmap(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h,
                    const uint8_t *bits, SSD1306_Color_t c)
{
  int16_t stride = (int16_t)((w + 7) / 8);
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = 0; i < w; i++)
      if (bits[j * stride + (i >> 3)] & (0x80u >> (i & 7)))
        SSD1306_Pixel(d, (int16_t)(x + i), (int16_t)(y + j), c);
}
