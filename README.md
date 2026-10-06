# VGA32 Spectrum Analyzer

Analyseur de spectre autonome pour la carte **TTGO VGA32** (ESP32 classique) : la radio Wi-Fi capture des
rafales I/Q brutes (technique [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr)), l'ESP32 calcule
une FFT et affiche un **spectre + waterfall** sur un écran VGA via [FabGL](https://github.com/shaeberling/FabGL).
Pilotage au clavier PS/2.

- Bande cible : 2,4 GHz (accord logiciel 100–6000 MHz, hors 2,4 GHz = expérimental)
- Span : 16, 40 ou 80 MS/s (horloges matérielles), FFT 1024 points, moyenne de 8 rafales
- Affichage : 512×384, 16 couleurs

## Prérequis

- ESP-IDF **6.2-dev** au commit `25fe69f946311abdaf9ad56591f25fedbc20ac98` (même commit qu'esp-sdr pour l'ESP32 : le code RX utilise des symboles privés de la libphy)
- TTGO VGA32, écran VGA, clavier PS/2

```bash
git clone https://github.com/espressif/esp-idf.git ~/esp/esp-idf-6.2
cd ~/esp/esp-idf-6.2 && git checkout 25fe69f946311abdaf9ad56591f25fedbc20ac98
git submodule update --init --recursive && ./install.sh esp32
```

## Build

```bash
git submodule update --init          # non récursif : esp-dsp d'esp-sdr est inutile
tools/apply_patches.sh               # correctifs FabGL pour IDF 6.2 (idempotent)
. ~/esp/esp-idf-6.2/export.sh
idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig set-target esp32
idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig -p /dev/ttyACM0 build flash monitor
```

Tests sur PC (DSP et commandes) :

```bash
cmake -S tests/host -B build/host && cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

Le port dépend de l'adaptateur USB-série : `/dev/ttyACM0` (CH9102) sur la carte de référence, parfois `/dev/ttyUSB0`.

## Touches

Clavier PS/2 natif (ou combo USB/PS/2) sur la prise clavier, **ou** terminal série (`idf.py monitor`) :

| PS/2 | Terminal série | Action |
|---|---|---|
| ← / → | ← / → ou `a` / `d` | Fréquence centrale ±1 MHz |
| Shift + ← / → | Shift + ← / → ou `A` / `D` | ±10 MHz |
| R | `r` | Span 16 → 40 → 80 MS/s |
| G | `g` | AGC matériel ↔ gain manuel |
| PgUp / PgDn | PgUp / PgDn ou `]` / `[` | Gain manuel ± (passe en manuel) |
| ↑ / ↓ | ↑ / ↓ ou `w` / `s` | Niveau de référence ±5 dB |
| + / − | `+` / `-` | Contraste (plage dB −/+10) |

Un clavier USB derrière un adaptateur passif USB→PS/2 ne marche que s'il est « combo » USB/PS/2 (voir `docs/HARDWARE.md`).

## Limites

- Captures en rafales (snapshots), pas de flux continu : le spectre n'est pas « sans trou ».
- Le Wi-Fi et le Bluetooth ne sont pas utilisables : la radio est détournée pour la capture.
- 64 KiB de SRAM (0x3ffe8000–0x3fff8000) sont réservés à l'ouverture de capture ; il reste ~5 Ko de heap une fois la VGA allouée (voir `docs/ARCHITECTURE.md`).
- Gain et niveaux non calibrés : les dB sont relatifs à la pleine échelle de l'ADC (dBFS).

## Licence

GPL-3.0-or-later. FabGL © Fabrizio Di Vittorio (GPL-3) ; le backend RX dérive d'ESPARGOS/esp-sdr (GPL-3.0-or-later).
