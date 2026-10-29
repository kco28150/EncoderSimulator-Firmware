# EncoderSimulator — Firmware

Firmware STM32 du **simulateur de codeur incrémental** : génère les signaux en
quadrature A/B d'un codeur de profondeur (déplacement d'une sonde à vitesse
donnée), pilotable depuis un PC (terminal série ou application WPF
[EncoderSimulator](https://github.com/kco28150/EncoderSimulator)), depuis un
écran tactile, et affiché sur un petit OLED.

- **Carte** : NUCLEO-F411RE (STM32F411RE, Cortex-M4 à 100 MHz)
- **Projet** : STM32CubeIDE + CubeMX (`Encoder SimulatorMX.ioc`), HAL ST

## Fonctions

- Signaux A/B en quadrature (sans top Z) sur PA8 / PA9, push-pull 3,3 V,
  générés par l'interruption de TIM2 (32 bits) : de 0,01 Hz à
  `Fmax = 1000 m/h × PPCM / 36` (1389 Hz à 50 imp/cm).
- Déplacements en impulsions ou en distance (`mm`, `cm`, `m`), vitesses en
  `Hz` ou en `m/h`, sens par le signe, position suivie en permanence.
- Résolution réglable : **PPCM** de 0,1 à 1000 imp/cm (50 par défaut).
- **Scénarios** de vitesse embarqués (sinus, escalier...).
- Commandes ASCII sur le port série virtuel du ST-Link (PuTTY ou appli PC).
- **IHM tactile** sur TFT 2,8" ILI9341 + XPT2046 : 6 pages, thème sombre / clair.
- **Tableau de bord OLED** 0,96" (facultatif, branchable à chaud).
- Réglages mémorisés en flash (PPCM, vitesses, thème, calibration tactile).
- LED verte LD2 allumée pendant un mouvement.

## Matériel et brochage

| Fonction | Broches | Périphérique |
|----------|---------|--------------|
| Codeur simulé A / B | PA8 (A), PA9 (B) | GPIO + TIM2 (IT) |
| Liaison PC (VCP ST-Link) | PA2 (TX), PA3 (RX) | USART2, 115200 8N1 |
| TFT ILI9341 320×240 | PB13 SCK, PB14 MISO, PB15 MOSI, PB1 DC, PB10 CS, PB12 RST | SPI2 |
| Tactile XPT2046 | PC0 CLK, PC1 CS, PC2 DO, PC3 DIN, PC4 IRQ | SPI logiciel |
| OLED SH1106 / SSD1306 128×64 | PB8 SCL, PB9 SDA | I2C1 400 kHz + DMA1 Stream 6 |
| LED d'activité | PA5 (LD2) | GPIO |

> Les sorties A/B sont en 3,3 V : prévoir une adaptation de niveau si
> l'équipement testé attend du 5 V, 12 V ou 24 V.

## Commandes série

Terminal à **115200 bauds, 8N1**, fin de ligne CR, LF ou CRLF. Réponses
`OK ...` ou `ERR ...`.

| Commande | Rôle |
|----------|------|
| `VA v y` | vitesse d'avance `v` (m/h) sur `y` (m), relatif ; `y < 0` = remontée |
| `MOVE x f` | `x` impulsions à la fréquence `f` (signe de `x` = sens) |
| `RUN f` | flux continu jusqu'à `STOP` (signe de `f` = sens) |
| `SPEED f` | change la fréquence pendant un mouvement |
| `STOP` | arrêt immédiat, sorties figées |
| `POS?` | position courante |
| `RST` / `RESET` / `ZERO` | remise à zéro de la position |
| `STATUS?` | état complet (IDLE, MOVE, RUN, SCEN) |
| `PPCM n` / `PPCM?` | résolution en imp/cm (à l'arrêt) |
| `SCEN n` / `SCEN?` | lance le scénario `n` / liste des scénarios |
| `HELP` / `?` | aide |

Unités : distance `x` en `imp` (défaut), `mm`, `cm`, `m` ; fréquence `f` en
`Hz` (défaut) ou `mh` (m/h).

```
VA 300 2.5          descend de 2,5 m à 300 m/h
VA 500 -50cm        remonte de 50 cm à 500 m/h
MOVE 120.5cm 800mh
RUN -500mh
STOP
```

## Interface tactile (TFT)

Splash au démarrage, puis 6 onglets :

| Page | Contenu |
|------|---------|
| ETAT | position, vitesse, mode en cours |
| VA | vitesse d'avance : descente / remontée d'une profondeur donnée |
| JOG | déplacements courts et marche continue (RUN) |
| GRAPH | vitesse sur les 30 dernières secondes |
| SCEN | lancement des scénarios |
| REGL | PPCM, remise à zéro de la position, thème sombre / clair, calibration tactile (5 croix) |

Les valeurs se saisissent sur un pavé numérique plein écran. Chaque action
tactile est aussi tracée dans le terminal (`[TACTILE] ...`).

## Écran OLED

Facultatif : s'il est absent ou débranché, une détection est retentée toutes
les 3 s. Splash de 5 s, puis tableau de bord :

- bande jaune : mode (`ARRET`, `MOVE`, `RUN`, `SCENn`), flèche de sens,
  indicateur `PC` / `TAC` pendant 1,5 s après une commande ;
- zone bleue : vitesse en m/h, profondeur en grand, barre de progression
  (MOVE / VA / SCEN) ou PPCM et fréquence de sortie.

Le pilote OLED est la librairie publique
[stm32-ssd1306](https://github.com/kco28150/stm32-ssd1306).

## Réglages en flash

Secteur 7 (`0x08060000`, 128 Ko), journal d'enregistrements de 64 octets :
le secteur n'est effacé qu'une fois plein (~2000 sauvegardes). Écriture
uniquement à l'arrêt. Contenu : PPCM, vitesse et profondeur VA, vitesse JOG,
thème, calibration tactile.

## Organisation du code (`Core/`)

| Module | Rôle |
|--------|------|
| `encoder_sim` | génération A/B, mouvements, position, PPCM |
| `scenario` | profils de vitesse embarqués |
| `cmd_parser` | réception USART2 et commandes ASCII |
| `settings` | réglages persistants en flash |
| `ui`, `gui`, `keypad`, `theme`, `splash` | IHM tactile (pages, widgets, pavé, palettes) |
| `tft`, `touch` | pilotes ILI9341 et XPT2046 |
| `oled_ui`, `ssd1306` | tableau de bord OLED et son pilote |

La boucle principale appelle `CMD_Process()` et `UI_Task()`. Tout le dessin se
fait hors interruption : la génération A/B (IT TIM2) et la réception série
(IT USART2) ne sont jamais retardées par l'affichage.

## Compilation et flash

1. Ouvrir le dossier dans **STM32CubeIDE** (*File → Import → Existing Projects
   into Workspace*).
2. *Project → Build* (configuration Debug).
3. Brancher la NUCLEO en USB et lancer `Encoder SimulatorMX.launch`
   (*Run* ou *Debug*).

Pour modifier le brochage ou les périphériques, ouvrir `Encoder SimulatorMX.ioc`
dans CubeMX ; le code utilisateur est conservé dans les blocs `USER CODE`.
