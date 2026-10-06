# Architecture

## Vue d'ensemble

```
            core 0                                   core 1
 ┌──────────────────────────────┐        ┌──────────────────────────────┐
 │ tâche sdr (prio 2)           │        │ ISR vidéo FabGL (I2S1 + DMA) │
 │  espsdr_rx_capture ×8        │        │ ISR PS/2 (ULP)               │
 │  → sd_accumulate (FFT 1024)  │ trame  │ tâche ui (prio 3)            │
 │  → sd_finish_db (512 col.)   │──────► │  input_poll (PS/2)           │
 │ tâches Wi-Fi / PHY           │ mutex+ │  input_uart_poll (série)     │
 │ tâche primitives FabGL       │ sémaph.│  ui_draw_frame               │
 └──────────────────────────────┘        └──────────────────────────────┘
          ▲  réglages (version + mutex)  │
          └──────────────────────────────┘
```

| Unité | Rôle | Dépend de |
|---|---|---|
| `components/espsdr_rx` | Init radio (Wi-Fi NULL + promiscuous), accord, gain, capture I/Q brute dans la SRAM réservée | libphy (symboles privés), `third_party/esp-sdr/main/common` |
| `components/spectrum_dsp` | Hann, FFT radix-2 float, \|X\|² moyenné, 512 colonnes en dBFS, quantification hauteur/couleur | rien (C pur, testé sur PC) |
| `components/app_controls` | Réglages radio/affichage, commandes, décodeur terminal → commandes | rien (C pur, testé sur PC) |
| `main/sdr_task.c` | Boucle capture → DSP, publication de la dernière trame | espsdr_rx, spectrum_dsp |
| `main/ui_render.cpp` | Bandeau, spectre en barres, waterfall par écriture brute 4 bpp | FabGL |
| `main/input_ps2.cpp`, `main/input_uart.c` | Clavier PS/2 et terminal série → `app_cmd_t` | FabGL, driver UART |

## Flux d'une trame

1. `sdr` lit les réglages demandés (copie sous mutex) ; si la version a changé : retune (`espsdr_rx_tune`, avec recalibration PHY) puis gain.
2. 8 captures de 1024 échantillons ; chaque capture est consommée immédiatement (`words` est écrasé à la capture suivante). Échec → `capture_errors++`.
3. `sd_finish_db` produit 512 colonnes ; si aucune capture n'a réussi, pas de trame publiée.
4. `ui` attend une trame (50 ms max), applique les touches, puis : `scroll(0,1)` du waterfall, texte du bandeau, `waitCompletion()`, écritures brutes du spectre et de la nouvelle ligne de waterfall.

## Écran (VGA16, 512×384, 16 couleurs)

| Zone | Lignes | Contenu |
|---|---|---|
| Bandeau | 0–19 | Réglages, compteur d'erreurs, `KBD?` si pas de clavier ; bornes de fréquence |
| Spectre | 20–147 | Barres GRID, sommet TRACE, repère central |
| Waterfall | 152–383 | Région de scroll pleine largeur (permutation de pointeurs de lignes) |

Palette : 0 fond, 1 grille, 2 trace, 3 texte, 4–15 colormap.

## Budget mémoire (mesuré sur la carte, 2026-10-06)

La DRAM interne de l'ESP32 est découpée en régions ; la réserve de capture `0x3ffe8000–0x3fff8000` (64 KiB) coupe la région haute.

| Étape | Heap libre | Plus grand bloc |
|---|---|---|
| Boot | 164 196 o | 98 304 o |
| Après radio (Wi-Fi + PHY) | 133 068 o | 69 632 o |
| Après PS/2 + UART | 128 860 o | 65 536 o |
| Après VGA 512×384 | 5 352 o | 4 864 o |

Régions libres avant la VGA : 4 B @0x3ffae6e0, 14 656 o @0x3ffe0440, 15 120 o @0x3ffe4350, 32 032 o @0x3fff8000, 67 048 o @0x3ffc1e40.

Leviers déjà appliqués pour que le framebuffer (98 304 o) tienne :

- `FABGLIB_MINFREELARGESTBLOCK=8192` (patch `0002` + `CMakeLists.txt` racine) : FabGL laisse ce reliquat dans chaque bloc qu'il entame ; 40 000 par défaut rendait les petites régions inutilisables.
- `BitmappedDisplayController::queueSize = 64` (au lieu de 1024).
- `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4`, `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8`.
- Piles des tâches `sdr` et `ui` statiques (`.bss`), driver UART installé avant la VGA.
- Garde-fou : `app_main` vérifie `getViewPortHeight() == UI_HEIGHT` (FabGL réduit le viewport en silence quand la RAM manque) et `abort()` sinon.

**Marge restante : ~5 Ko.** Toute nouvelle allocation dynamique ou tout buffer statique ajouté se paie en lignes d'écran. Leviers restants si besoin : viewport `setResolution(VGA_512x384_60Hz, 512, 320)` (−16 Ko), FFT 512 points (−12 Ko de `.bss`).

## Tâches et cores

| Tâche | Core | Prio | Pile |
|---|---|---|---|
| ISR vidéo FabGL | 1 (`WIFI_TASK_CORE_ID ^ 1`) | niveau 1, IRAM | — |
| `ui` | 1 | 3 | 4096 o statique |
| `sdr` | 0 | 2 | 4096 o statique |
| primitives FabGL | 0 | 5 | FabGL |

`sdr` fait `vTaskDelay(1)` par trame pour la task watchdog du core 0 ; le core 1 n'est pas surveillé (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` désactivé, comme FabGL le recommande).

## Patches FabGL

| Patch | Raison |
|---|---|
| `0001-idf-6.2-compat.patch` | GCC esp-16 : `-Werror=stringop-truncation` sur deux `strncpy` (débordement réel) dans `netutils.cpp` |
| `0002-configurable-minfreelargestblock.patch` | `#ifndef` autour de `FABGLIB_MINFREELARGESTBLOCK` |
