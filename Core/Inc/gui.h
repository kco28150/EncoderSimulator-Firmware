/**
  ******************************************************************************
  * @file    gui.h
  * @brief   Widgets simples pour l'ecran tactile : boutons, champs, texte
  *          rafraichi par difference, triangles (fleches haut/bas).
  ******************************************************************************
  */
#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>

typedef struct { uint16_t x, y, w, h; } GRect;

typedef enum
{
  GST_NORMAL = 0,   /* carte + bordure            */
  GST_ACCENT,       /* fond accent (action)       */
  GST_DANGER,       /* fond rouge (STOP)          */
  GST_FIELD,        /* champ de saisie            */
  GST_TAB,          /* onglet inactif             */
  GST_TAB_ON,       /* onglet actif               */
  GST_SELECTED      /* choix selectionne          */
} GStyle_t;

typedef enum { GIC_NONE = 0, GIC_UP, GIC_DOWN } GIcon_t;

bool     GUI_Hit(const GRect *r, uint16_t x, uint16_t y);
uint16_t GUI_TextW(const char *s, uint8_t scale);
void     GUI_Text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
void     GUI_TextCenter(const GRect *r, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
void     GUI_Triangle(uint16_t cx, uint16_t ytop, uint8_t h, bool up, uint16_t color);
void     GUI_Frame(const GRect *r, uint16_t color);

/* Bouton : libelle centre, icone triangle optionnelle a gauche du texte */
void GUI_Button(const GRect *r, const char *label, uint8_t scale,
                GStyle_t style, GIcon_t icon, bool pressed);

/* Champ : valeur (scale) alignee a droite avant l'unite (scale 2) */
void GUI_Field(const GRect *r, const char *value, uint8_t scale, const char *unit, bool pressed);

/* Zone de texte a largeur fixe, seuls les caracteres modifies sont redessines */
#define GTEXT_MAX 52
typedef struct
{
  uint16_t x, y;
  uint8_t  scale, width;
  uint16_t fg, bg;
  char     cur[GTEXT_MAX + 1];
} GText_t;

void GUI_TextSet(GText_t *f, const char *txt, uint16_t fg, uint16_t bg);
void GUI_TextReset(GText_t *f);

#endif /* GUI_H */
