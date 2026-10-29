/**
  ******************************************************************************
  * @file    ssd1306.h
  * @brief   Pilote d'ecran OLED 128x64 a controleur SSD1306, liaison I2C.
  *
  *  Materiel
  *  --------
  *  - Module 0,96" 128x64 bicolore : lignes 0..15 jaunes, 16..63 bleues
  *    (couleurs fixes de la dalle, le controleur reste monochrome).
  *  - I2C1 : PB8 = SCL (D15), PB9 = SDA (D14), 400 kHz, emission par DMA
  *    (DMA1 Stream 6, canal 1).
  *  - Adresse 7 bits 0x3C ou 0x3D selon la resistance de selection du module :
  *    detectee automatiquement par SSD1306_Init().
  *
  *  Principe
  *  --------
  *  Tout le dessin se fait dans une image en RAM (1 Ko : 128 colonnes x 8 pages
  *  de 8 lignes, bit 0 = ligne du haut de la page, format natif du SSD1306).
  *  SSD1306_Update() recopie cette image dans un 2e tampon puis l'envoie par
  *  DMA, page par page (~25 ms a 400 kHz) : la fonction rend la main tout de
  *  suite et l'on peut continuer a dessiner pendant le transfert, sans
  *  dechirure d'image. SSD1306_Task() doit etre appelee en boucle pour
  *  enchainer les 8 pages.
  *
  *  Compatible SSD1306 et SH1106 (voir SSD1306_SH1106 ci-dessous).
  *
  *  Utilisation type
  *  ----------------
  *    if (SSD1306_Init()) {                  // apres MX_I2C1_Init()
  *      SSD1306_Clear();
  *      SSD1306_Text(0, 0, "Bonjour", 2, SSD1306_ON);
  *      SSD1306_Update();                    // non bloquant
  *    }
  *    while (1) { SSD1306_Task(); ... }      // enchaine l'envoi des pages
  *
  *  Coordonnees : x = 0..127 de gauche a droite, y = 0..63 de haut en bas.
  *  Les dessins hors ecran sont simplement ignores (pas de plantage).
  ******************************************************************************
  */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Geometrie ------------------------------------------------------------ */
#define SSD1306_WIDTH      128
#define SSD1306_HEIGHT     64
#define SSD1306_YELLOW_H   16      /* hauteur de la bande jaune (lignes 0..15) */

/* ---- Liaison I2C ------------------------------------------------------------
 * Adresses 7 bits essayees dans l'ordre par SSD1306_Init(). */
#define SSD1306_ADDR_1     0x3C    /* "0x78" sur la serigraphie (8 bits)  */
#define SSD1306_ADDR_2     0x3D    /* "0x7A" sur la serigraphie (8 bits)  */
#define SSD1306_I2C_TIMEOUT_MS  20 /* commandes courtes en mode bloquant  */

/* ---- Controleur ---------------------------------------------------------------
 * 1 : SH1106 (RAM 132 colonnes, image decalee de 2 colonnes) - cas de ce module.
 * 0 : vrai SSD1306.
 * Symptome d'un mauvais choix : image decalee de 2 pixels, ou bande de 2 pixels
 * de "neige" sur un bord. */
#define SSD1306_SH1106     1
#if SSD1306_SH1106
#define SSD1306_COL_OFFSET 2u
#else
#define SSD1306_COL_OFFSET 0u
#endif

/* ---- Orientation --------------------------------------------------------------
 * 1 : rotation de 180 deg (si le module est monte tete en bas). Attention :
 * la bande jaune se retrouve alors en bas. */
#define SSD1306_ROTATE_180 0

/* Couleurs (ecran monochrome) */
typedef enum
{
  SSD1306_OFF = 0,      /* pixel eteint                  */
  SSD1306_ON  = 1,      /* pixel allume (jaune ou bleu)  */
  SSD1306_INV = 2       /* inverse l'etat du pixel       */
} SSD1306_Color_t;

/* ---- Controle ---------------------------------------------------------------- */

/**
  * @brief  Detecte l'ecran (0x3C puis 0x3D), le configure et l'efface.
  *         Debloque d'abord le bus si un esclave retient SDA (cas d'un reset
  *         du micro au milieu d'un transfert).
  * @retval true si un ecran a repondu, false sinon (les autres fonctions ne
  *         font alors rien : le reste du firmware fonctionne sans ecran).
  */
bool SSD1306_Init(void);

/** @brief true si l'ecran a ete detecte et ne signale pas d'erreur. */
bool SSD1306_Present(void);

/** @brief Adresse 7 bits detectee (0 si absent). */
uint8_t SSD1306_Address(void);

/** @brief true tant qu'un transfert DMA de l'image est en cours. */
bool SSD1306_Busy(void);

/**
  * @brief  Envoie l'image RAM a l'ecran par DMA (non bloquant).
  * @retval true si le transfert est lance ; false si l'ecran est absent, si un
  *         transfert est deja en cours (l'image sera envoyee au prochain appel)
  *         ou en cas d'erreur I2C (l'ecran est alors marque absent).
  */
bool SSD1306_Update(void);

/** @brief A appeler en boucle : envoie la page suivante quand le bus est libre. */
void SSD1306_Task(void);

/** @brief Allume / eteint l'affichage (le contenu est conserve). */
void SSD1306_DisplayOn(bool on);

/** @brief Contraste 0..255 (0xCF par defaut). */
void SSD1306_Contrast(uint8_t value);

/** @brief Inversion video materielle (fond allume, texte eteint). */
void SSD1306_Invert(bool inv);

/* ---- Dessin dans l'image RAM -------------------------------------------------- */
void SSD1306_Clear(void);                                     /* tout eteint */
void SSD1306_Fill(SSD1306_Color_t c);                         /* tout ON/OFF */
void SSD1306_Pixel(int16_t x, int16_t y, SSD1306_Color_t c);
void SSD1306_HLine(int16_t x, int16_t y, int16_t w, SSD1306_Color_t c);
void SSD1306_VLine(int16_t x, int16_t y, int16_t h, SSD1306_Color_t c);
void SSD1306_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c);
void SSD1306_Rect(int16_t x, int16_t y, int16_t w, int16_t h, SSD1306_Color_t c);   /* contour */

/* Triangle plein pointe en haut (up) ou en bas, de hauteur h, sommet centre en cx */
void SSD1306_Triangle(int16_t cx, int16_t ytop, int16_t h, bool up, SSD1306_Color_t c);

/**
  * @brief  Texte en police 5x7 (cellule 6x8), agrandie par 'scale' (1..4).
  *         Taille 1 : 21 caracteres x 8 lignes ; taille 2 : 10 x 4 ; taille 3 : 7 x 2.
  *         Le fond de chaque caractere est efface (couleur opposee) sauf en INV.
  * @retval abscisse juste apres le dernier caractere
  */
int16_t  SSD1306_Text(int16_t x, int16_t y, const char *s, uint8_t scale, SSD1306_Color_t c);
uint16_t SSD1306_TextWidth(const char *s, uint8_t scale);     /* largeur en pixels */

/**
  * @brief  Image 1 bit par pixel, ligne par ligne, poids fort a gauche
  *         (format "XBM inverse" classique, ((w + 7) / 8) octets par ligne).
  *         Seuls les bits a 1 sont dessines.
  */
void SSD1306_Bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *bits, SSD1306_Color_t c);

/** @brief Acces direct a l'image RAM (1024 octets), pour usage avance. */
uint8_t *SSD1306_Buffer(void);

#endif /* SSD1306_H */
