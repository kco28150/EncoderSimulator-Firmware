/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   Librairie portable pour ecrans OLED SSD1306 / SH1106 sur I2C (HAL STM32).
  *
  *  Points forts
  *  ------------
  *  - Toutes familles STM32 (F0/F1/F4/G0/G4/L4/H7...) : n'utilise que la HAL I2C.
  *  - Plusieurs ecrans possibles : chaque ecran est une variable SSD1306_t.
  *  - 128x64 ou 128x32, controleur SSD1306 ou SH1106, rotation 180 deg :
  *    tout se regle a l'execution, sans toucher a la librairie.
  *  - Envoi non bloquant par DMA si le handle I2C a un DMA TX lie
  *    (hi2c->hdmatx), sinon envoi bloquant automatique.
  *  - Double tampon : on peut redessiner pendant l'envoi, sans dechirure.
  *  - Detection automatique de l'adresse (0x3C / 0x3D) et deblocage du bus
  *    I2C apres un reset du micro au milieu d'un transfert (facultatif).
  *  - Ecran absent ou debranche : les fonctions ne font rien, pas de blocage.
  *
  *  Integration (CubeMX / STM32CubeIDE)
  *  -----------------------------------
  *  1. Activer un I2C (Fast Mode 400 kHz). Pour le mode DMA : ajouter un
  *     DMA "I2Cx_TX" et activer l'interruption "I2Cx event" (+ "error").
  *  2. Copier Inc/ et Src/ dans le projet (ou les ajouter aux chemins).
  *  3. Si "main.h" n'inclut pas la HAL, definir SSD1306_HAL_HEADER, par ex.
  *     -DSSD1306_HAL_HEADER=\"stm32g0xx_hal.h\"
  *  4. En mode DMA, appeler SSD1306_ErrorHandler() depuis
  *     HAL_I2C_ErrorCallback() (la librairie ne definit pas ce callback).
  *
  *  Utilisation type
  *  ----------------
  *    static SSD1306_t oled;
  *
  *    SSD1306_Config_t cfg;
  *    SSD1306_DefaultConfig(&cfg, &hi2c1);
  *    cfg.controller = SSD1306_CTRL_SH1106;           // si image decalee de 2 px
  *    if (SSD1306_Init(&oled, &cfg)) {
  *      SSD1306_Text(&oled, 0, 0, "Bonjour", 2, SSD1306_ON);
  *      SSD1306_Update(&oled);                        // non bloquant en DMA
  *    }
  *    while (1) { SSD1306_Task(&oled); ... }          // enchaine l'envoi des pages
  *
  *  Coordonnees : x = 0..largeur-1 de gauche a droite, y = 0..hauteur-1 de haut
  *  en bas. Les dessins hors ecran sont simplement ignores.
  ******************************************************************************
  */
#ifndef SSD1306_H
#define SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SSD1306_HAL_HEADER
#define SSD1306_HAL_HEADER "main.h"     /* inclut stm32xxxx_hal.h dans un projet CubeMX */
#endif
#include SSD1306_HAL_HEADER

#include <stdint.h>
#include <stdbool.h>

/* ---- Limites ---------------------------------------------------------------- */
#define SSD1306_MAX_WIDTH    128
#define SSD1306_MAX_HEIGHT   64
#define SSD1306_BUF_SIZE     (SSD1306_MAX_WIDTH * SSD1306_MAX_HEIGHT / 8)  /* 1024 octets */

/* ---- Adresses I2C 7 bits ------------------------------------------------------ */
#define SSD1306_ADDR_AUTO    0x00   /* essaie 0x3C puis 0x3D                   */
#define SSD1306_ADDR_3C      0x3C   /* "0x78" sur la serigraphie (8 bits)      */
#define SSD1306_ADDR_3D      0x3D   /* "0x7A" sur la serigraphie (8 bits)      */

/* Delai max des commandes courtes envoyees en mode bloquant */
#ifndef SSD1306_I2C_TIMEOUT_MS
#define SSD1306_I2C_TIMEOUT_MS  20
#endif

/* Controleur. Beaucoup de modules vendus "SSD1306" embarquent un SH1106 :
 * symptome d'un mauvais choix = image decalee de 2 pixels ou bande de "neige". */
typedef enum
{
  SSD1306_CTRL_SSD1306 = 0,
  SSD1306_CTRL_SH1106  = 1      /* RAM 132 colonnes, image decalee de 2 */
} SSD1306_Controller_t;

/* Couleurs (ecran monochrome) */
typedef enum
{
  SSD1306_OFF = 0,      /* pixel eteint              */
  SSD1306_ON  = 1,      /* pixel allume              */
  SSD1306_INV = 2       /* inverse l'etat du pixel   */
} SSD1306_Color_t;

/* Configuration, a remplir avant SSD1306_Init() (partir de SSD1306_DefaultConfig) */
typedef struct
{
  I2C_HandleTypeDef   *hi2c;        /* bus I2C deja initialise (MX_I2Cx_Init)       */
  uint8_t              address;     /* 7 bits, ou SSD1306_ADDR_AUTO                */
  SSD1306_Controller_t controller;
  uint8_t              height;      /* 64 ou 32                                    */
  bool                 rotate_180;  /* module monte tete en bas                    */
  uint8_t              contrast;    /* 0..255                                      */
  bool                 use_dma;     /* false : force l'envoi bloquant               */

  /* Deblocage du bus (facultatif) : broches SCL / SDA du bus. Laisser NULL
   * pour ne pas l'utiliser. Necessite que HAL_I2C_MspInit() configure tout
   * (broches, DMA, IRQ), ce que fait le code genere par CubeMX. */
  GPIO_TypeDef        *scl_port;
  uint16_t             scl_pin;
  GPIO_TypeDef        *sda_port;
  uint16_t             sda_pin;
} SSD1306_Config_t;

/**
  * @brief  Remplit cfg avec les valeurs par defaut : adresse auto, SSD1306,
  *         128x64, pas de rotation, contraste 0xCF, DMA si disponible,
  *         pas de deblocage de bus.
  */
void SSD1306_DefaultConfig(SSD1306_Config_t *cfg, I2C_HandleTypeDef *hi2c);

/* Etat d'un ecran (contenu prive : ne pas modifier directement) */
typedef struct
{
  SSD1306_Config_t cfg;
  uint8_t          addr8;                   /* adresse decalee (format HAL), 0 = absent */
  volatile bool    ok;
  bool             dma;                     /* envoi par DMA effectif                    */
  int8_t           page;                    /* page en cours d'envoi, -1 = repos         */
  uint8_t          fb[SSD1306_BUF_SIZE];    /* image de travail (dessin)                 */
  uint8_t          tx[SSD1306_BUF_SIZE];    /* copie envoyee a l'ecran                   */
} SSD1306_t;

/* ---- Controle ---------------------------------------------------------------- */

/**
  * @brief  Debloque le bus si besoin, detecte l'ecran, le configure et l'efface.
  * @retval true si un ecran a repondu. Sinon les autres fonctions ne font rien
  *         et l'on peut rappeler SSD1306_Init() plus tard (ecran branche a chaud).
  */
bool SSD1306_Init(SSD1306_t *d, const SSD1306_Config_t *cfg);

bool    SSD1306_Present(const SSD1306_t *d);      /* detecte et sans erreur       */
uint8_t SSD1306_Address(const SSD1306_t *d);      /* adresse 7 bits, 0 si absent  */
uint8_t SSD1306_Width(const SSD1306_t *d);
uint8_t SSD1306_Height(const SSD1306_t *d);
bool    SSD1306_Busy(const SSD1306_t *d);         /* envoi d'image en cours       */

/**
  * @brief  Envoie l'image a l'ecran. En DMA : rend la main tout de suite,
  *         SSD1306_Task() enchaine les pages. Sans DMA : bloquant (~25 ms).
  * @retval false si l'ecran est absent, si un envoi est deja en cours (rappeler
  *         plus tard) ou en cas d'erreur I2C (l'ecran est alors marque absent).
  */
bool SSD1306_Update(SSD1306_t *d);

/** @brief A appeler en boucle (pas en interruption) : envoie la page suivante. */
void SSD1306_Task(SSD1306_t *d);

/** @brief A appeler depuis HAL_I2C_ErrorCallback() (mode DMA). */
void SSD1306_ErrorHandler(SSD1306_t *d, I2C_HandleTypeDef *hi2c);

void SSD1306_DisplayOn(SSD1306_t *d, bool on);    /* le contenu est conserve      */
void SSD1306_Contrast(SSD1306_t *d, uint8_t value);
void SSD1306_Invert(SSD1306_t *d, bool inv);      /* inversion video materielle   */

/* ---- Dessin dans l'image RAM -------------------------------------------------- */
void SSD1306_Clear(SSD1306_t *d);
void SSD1306_Fill(SSD1306_t *d, SSD1306_Color_t c);
void SSD1306_Pixel(SSD1306_t *d, int16_t x, int16_t y, SSD1306_Color_t c);
bool SSD1306_GetPixel(const SSD1306_t *d, int16_t x, int16_t y);
void SSD1306_HLine(SSD1306_t *d, int16_t x, int16_t y, int16_t w, SSD1306_Color_t c);
void SSD1306_VLine(SSD1306_t *d, int16_t x, int16_t y, int16_t h, SSD1306_Color_t c);
void SSD1306_Line(SSD1306_t *d, int16_t x0, int16_t y0, int16_t x1, int16_t y1, SSD1306_Color_t c);
void SSD1306_Rect(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c);
void SSD1306_FillRect(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c);
void SSD1306_Circle(SSD1306_t *d, int16_t cx, int16_t cy, int16_t r, SSD1306_Color_t c);
void SSD1306_FillCircle(SSD1306_t *d, int16_t cx, int16_t cy, int16_t r, SSD1306_Color_t c);

/* Triangle plein pointe en haut (up) ou en bas, de hauteur h, sommet centre en cx */
void SSD1306_Triangle(SSD1306_t *d, int16_t cx, int16_t ytop, int16_t h, bool up, SSD1306_Color_t c);

/* Barre de progression : contour + remplissage proportionnel (permille 0..1000) */
void SSD1306_ProgressBar(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t permille);

/**
  * @brief  Texte en police 5x7 (cellule 6x8), agrandie par 'scale' (1..4).
  *         En 128x64 : taille 1 = 21 car. x 8 lignes, taille 2 = 10 x 4.
  *         Le fond de chaque caractere est efface (couleur opposee) sauf en INV.
  *         '\n' passe a la ligne suivante (retour a l'abscisse de depart).
  * @retval abscisse juste apres le dernier caractere
  */
int16_t  SSD1306_Text(SSD1306_t *d, int16_t x, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c);
uint16_t SSD1306_TextWidth(const char *s, uint8_t scale);   /* plus longue ligne, en pixels */

/* Texte centre horizontalement sur la ligne y */
void SSD1306_TextCentered(SSD1306_t *d, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c);

/* Comme printf (64 caracteres max apres formatage) */
int16_t SSD1306_Printf(SSD1306_t *d, int16_t x, int16_t y, uint8_t scale, SSD1306_Color_t c,
                       const char *fmt, ...) __attribute__((format(printf, 6, 7)));

/**
  * @brief  Image 1 bit par pixel, ligne par ligne, poids fort a gauche,
  *         ((w + 7) / 8) octets par ligne (format des convertisseurs courants,
  *         ex. image2cpp "Horizontal - 1 bit per pixel"). Seuls les bits a 1
  *         sont dessines.
  */
void SSD1306_Bitmap(SSD1306_t *d, int16_t x, int16_t y, int16_t w, int16_t h,
                    const uint8_t *bits, SSD1306_Color_t c);

/** @brief Acces direct a l'image RAM (format natif : pages de 8 lignes, bit 0 en haut). */
uint8_t *SSD1306_Buffer(SSD1306_t *d);

#ifdef __cplusplus
}
#endif

#endif /* SSD1306_H */
