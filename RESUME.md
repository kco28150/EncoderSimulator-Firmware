# Résumé — Firmware STM32 « Encoder Simulator »

## Rôle

Simulateur de **codeur incrémental en quadrature** (signaux A/B, sans top Z) :
il reproduit les signaux d'un codeur de profondeur lors de la descente ou de la
remontée d'une sonde à une vitesse donnée.

## Plateforme

- Carte **NUCLEO-F411RE** (STM32F411RE, Cortex-M4 à 100 MHz, HSI + PLL)
- Projet **STM32CubeIDE + CubeMX** (`Encoder SimulatorMX.ioc`), HAL ST

## Génération des signaux

- Sorties A/B sur **PA8 / PA9**, push-pull 3,3 V
  (adaptation de niveau à prévoir pour 5 / 12 / 24 V)
- 4 états de Gray par impulsion, cadencés par l'IT de **TIM2 (32 bits)**
- Fréquence de **0,01 Hz** à `Fmax = 1000 m/h × PPCM / 36`
  (1389 Hz à 50 imp/cm)
- Résolution **PPCM** réglable de 0,1 à 1000 imp/cm (50 par défaut)
- Position suivie en permanence (en quarts d'impulsion)
- LED LD2 allumée pendant un mouvement

## Modes

| Mode | Description |
|------|-------------|
| **MOVE** | nombre d'impulsions ou distance fixe, à une fréquence donnée |
| **RUN** | flux continu jusqu'à `STOP` |
| **VA** | vitesse d'avance (m/h) sur une profondeur relative |
| **SCEN** | scénarios de vitesse embarqués (sinus, escalier) via des profils segmentés |

## Interfaces de pilotage

### Série (USART2, VCP ST-Link, 115200 8N1)

Commandes ASCII, réponses `OK ...` / `ERR ...`, `DONE` en fin de mouvement :

`VA`, `MOVE`, `RUN`, `SPEED`, `STOP`, `POS?`, `RST`/`RESET`/`ZERO`,
`STATUS?`, `PPCM` / `PPCM?`, `SCEN` / `SCEN?`, `HELP` / `?`

Unités : distances en `imp`, `mm`, `cm`, `m` ; fréquences en `Hz` ou `mh`.

### Écran tactile TFT 2,8" (ILI9341 + XPT2046)

- Splash au démarrage, puis 6 pages : **ETAT, VA, JOG, GRAPH, SCEN, REGL**
- Pavé numérique plein écran, thème sombre / clair
- Calibration tactile affine par moindres carrés (5 croix)
- Actions tracées dans le terminal (`[TACTILE] ...`)

### OLED 0,96" (SSD1306 / SH1106, facultatif)

- I2C1 400 kHz + DMA, détection à chaud (nouvel essai toutes les 3 s)
- Tableau de bord : mode, sens, indicateur PC / TAC, vitesse, profondeur,
  barre de progression ou PPCM / fréquence

## Persistance

Réglages en flash, secteur 7 (`0x08060000`) : journal d'enregistrements de
64 octets (~2000 sauvegardes avant effacement), écrits uniquement à l'arrêt.
Contenu : PPCM, vitesse / profondeur VA, vitesse JOG, thème, calibration tactile.

## Brochage

| Fonction | Broches | Périphérique |
|----------|---------|--------------|
| Codeur A / B | PA8, PA9 | GPIO + TIM2 (IT) |
| Liaison PC | PA2 TX, PA3 RX | USART2 |
| TFT ILI9341 | PB13 SCK, PB14 MISO, PB15 MOSI, PB1 DC, PB10 CS, PB12 RST | SPI2 |
| Tactile XPT2046 | PC0 CLK, PC1 CS, PC2 DO, PC3 DIN, PC4 IRQ | SPI logiciel |
| OLED | PB8 SCL, PB9 SDA | I2C1 + DMA1 Stream 6 |
| LED | PA5 (LD2) | GPIO |

## Organisation du code (`Core/`)

| Module | Rôle |
|--------|------|
| `encoder_sim` | génération A/B, mouvements, position, PPCM |
| `scenario` | profils de vitesse embarqués |
| `cmd_parser` | réception USART2 et commandes ASCII |
| `settings` | réglages persistants en flash |
| `ui`, `gui`, `keypad`, `theme`, `splash` | IHM tactile |
| `tft`, `touch` | pilotes ILI9341 et XPT2046 |
| `oled_ui`, `ssd1306` | tableau de bord OLED et son pilote |

## Architecture temps réel

```c
ENC_Init();  UI_Init();  CMD_Init();
while (1) { CMD_Process(); UI_Task(); }
```

Tout le dessin est fait dans la boucle principale : la génération A/B (IT TIM2)
et la réception série (IT USART2) ne sont jamais retardées par l'affichage.

## Lien avec l'application PC

L'application WPF `Desktop_EncoderSimulator` envoie les mêmes commandes ASCII
et décode les réponses `OK` / `ERR` / `DONE` / `STATUS` / `POS` / `PPCM` / `SCEN`.
