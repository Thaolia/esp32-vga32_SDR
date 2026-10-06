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
4. `ui` applique d'abord les touches (PS/2 puis série), puis attend une trame (50 ms max). Avec une trame : `scroll(0,1)` du waterfall, bandeau (redessiné seulement s'il change), écritures brutes du spectre (une passe, sans effacement) et de la nouvelle ligne de waterfall. Sans trame (toutes les captures échouent) : le bandeau est rafraîchi avec `sdr_task_capture_errors()`.
5. Les primitives du Canvas sont exécutées **en synchrone** dans la tâche `ui` (`enableBackgroundPrimitiveExecution(false)`) : aucune course entre la tâche primitives de FabGL et les écritures brutes via `getScanline()`.

## Écran (VGA16, 512×384, 16 couleurs)

| Zone | Lignes | Contenu |
|---|---|---|
| Bandeau | 0–19 | Réglages, compteur d'erreurs, `KBD?` si pas de clavier ; bornes de fréquence |
| Spectre | 20–147 | Barres GRID, sommet TRACE, repère central |
| Waterfall | 152–383 | Région de scroll pleine largeur (permutation de pointeurs de lignes) |

Palette : 0 fond, 1 grille, 2 trace, 3–14 colormap, 15 texte. Le Canvas convertit ses couleurs RGB888 avec une LUT calculée sur la palette **par défaut** (`setPaletteItem` ne la met pas à jour) : le noir doit rester en 0 et le blanc en 15.

## Budget mémoire (mesuré sur la carte, 2026-10-06)

La DRAM interne de l'ESP32 est découpée en régions ; la réserve de capture `0x3ffe8000–0x3fff8000` (64 KiB) coupe la région haute.

| Étape | Heap libre | Plus grand bloc |
|---|---|---|
| Boot | 164 196 o | 98 304 o |
| Après radio (Wi-Fi + PHY) | 133 068 o | 69 632 o |
| Après PS/2 + UART | 128 860 o | 65 536 o |
| Après VGA 512×384 | 5 352 o | 4 864 o |

Régime établi (après retour d'`app_main`, ~7 s de fonctionnement) : heap libre 14 276 o, plus grand bloc 8 704 o.

Piles (high-water mark, mesuré sur retunes 2437, 2436 hors canal, 1996 LO alterné) : `sdr` 2 560 o libres sur 4 096, `ui` 2 976 o libres sur 4 096. Cadence mesurée : ~40 trames/s.

Régions libres avant la VGA : 4 B @0x3ffae6e0, 14 656 o @0x3ffe0440, 15 120 o @0x3ffe4350, 32 032 o @0x3fff8000, 67 048 o @0x3ffc1e40.

Leviers déjà appliqués pour que le framebuffer (98 304 o) tienne :

- `FABGLIB_MINFREELARGESTBLOCK=8192` (patch `0002` + `CMakeLists.txt` racine) : FabGL laisse ce reliquat dans chaque bloc qu'il entame ; 40 000 par défaut rendait les petites régions inutilisables.
- `BitmappedDisplayController::queueSize = 8` (au lieu de 1024) : exécution synchrone, la file ne sert presque pas.
- `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4`, `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8`.
- Piles des tâches `sdr` et `ui` statiques (`.bss`), driver UART installé avant la VGA.
- Garde-fou : `app_main` vérifie `getViewPortHeight() == UI_HEIGHT` (FabGL réduit le viewport en silence quand la RAM manque) et `abort()` sinon.

**Marge restante : ~5 Ko au moment de l'allocation VGA**, davantage en régime établi (la pile de la tâche main, 8 Ko, est libérée au retour d'`app_main` ; mesure ci-dessous). Toute nouvelle allocation dynamique ou tout buffer statique ajouté se paie en lignes d'écran. Leviers restants si besoin : viewport `setResolution(VGA_512x384_60Hz, 512, 320)` (−16 Ko), FFT 512 points (`sd_ctx_t` 20 480 → ~10 240 o), tables fenêtre/twiddles en `const` flash (~8 Ko), `db[]` en `int16` (~3 Ko).

## Tâches et cores

| Tâche | Core | Prio | Pile |
|---|---|---|---|
| ISR vidéo FabGL | 1 (`WIFI_TASK_CORE_ID ^ 1`) | niveau 1, IRAM | — |
| `ui` (+ primitives Canvas, synchrones) | 1 | 3 | 4096 o statique |
| `sdr` | 0 | 2 | 4096 o statique |
| primitives FabGL | 0 | 5 | FabGL — inactive (exécution synchrone) |

`sdr` fait `vTaskDelay(1)` par trame pour la task watchdog du core 0 ; le core 1 n'est pas surveillé (`CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` désactivé, comme FabGL le recommande).

## Patches FabGL

| Patch | Raison |
|---|---|
| `0001-idf-6.2-compat.patch` | GCC esp-16 : `-Werror=stringop-truncation` sur deux `strncpy` (débordement réel) dans `netutils.cpp` |
| `0002-configurable-minfreelargestblock.patch` | `#ifndef` autour de `FABGLIB_MINFREELARGESTBLOCK` |
