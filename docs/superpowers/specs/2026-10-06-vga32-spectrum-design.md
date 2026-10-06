# VGA32 Spectrum Analyzer — Design

## Context

L'utilisateur veut une visualisation de spectre autonome sur TTGO FabGL-VGA32 avec la lib ESPARGOS/esp-sdr. Faits établis par exploration (clones dans le scratchpad) :

- **esp-sdr n'est pas une lib** : c'est un firmware complet (protocole ASCII sur UART, fonctions `static`). Sur ESP32 : captures *snapshot* uniquement, ≤ 16 380 échantillons, horloges matérielles 16/40/80 MS/s (sélecteur : 0=16, 1=80, 2=40), accord 100–6000 MHz (fiable surtout en 2,4 GHz). Format : mot 32 bits, I = `(int32_t)(w<<22)>>22`, Q = `(int32_t)(w<<12)>>22`. Réserve 64 KiB de DRAM (`SOC_RESERVE_MEMORY_REGION(0x3ffe8000, 0x3fff8000, …)`), `-Wl,--wrap=chip_v7_set_chan_ana`, garde linker `main/targets/esp32/sram_guard.ld`, symboles privés libphy → **épinglé sur IDF 6.2-dev**. L'utilisateur a validé « utilise IDF 6.2 ».
- esp-sdr est livré en `CONFIG_FREERTOS_UNICORE=y` ; le dual-core n'est pas validé → **spike Task 2**.
- **FabGL upstream** ne compile pas en IDF ≥ 5. Le fork **shaeberling/FabGL** (`3f21e97f`, 2026-10-03) est porté IDF 5/6 (testé 6.0.1), IDF pur sans Arduino, avec exemple `idf/NativeVGATerminal` et `idf/sdkconfig.fabgl`.
- FabGL VGA = I2S1 + APLL + DMA ; esp-sdr n'utilise ni I2S, ni APLL, ni interruptions → pas de conflit périphérique. Le risque principal est la **RAM interne** (framebuffer 512×384×4 bpp = 98 304 o + descripteurs DMA + `FABGLIB_MINFREELARGESTBLOCK` 40 000 o, après réserve de 64 KiB + Wi-Fi).

Décisions utilisateur : spectre + waterfall 2,4 GHz ; contrôle clavier PS/2 ; submodules + composants adaptés ; design approuvé ; IDF installé dans `~/esp/esp-idf-6.2` ; exécution via dispatching-parallel-agents.

## Design

**Écran** `VGA16Controller`, `VGA_512x384_60Hz`, 16 couleurs :

| Zone | Lignes y | Contenu |
|---|---|---|
| Bandeau | 0–19 | Ligne 1 (y=2) : fréquence, MS/s, gain, ref, plage, erreurs, état clavier. Ligne 2 (y=11) : fréquences gauche/centre/droite |
| Spectre | 20–147 (128 px) | Barres pleines (couleur GRID), sommet en TRACE, repère central pointillé |
| Waterfall | 152–383 (232 lignes) | `Canvas::setScrollingRegion(0,152,511,383)` + `scroll(0,1)` (permutation de pointeurs car région pleine largeur), nouvelle ligne écrite en y=152 |

Palette : 0 BG noir, 1 GRID, 2 TRACE, 3 TEXT, 4–15 colormap (12 teintes bleu→rouge→blanc). Écritures pixel brutes via `getScanline(y)` (nibble haut = pixel pair), **après** `canvas.waitCompletion()`.

**DSP** (C pur, testé sur PC) : N = 1024, fenêtre de Hann périodique, retrait de DC par moyenne, FFT radix-2 float maison (table de twiddles), fftshift, |X|² accumulé sur 8 captures, 512 colonnes = max des 2 bins, dBFS normalisés : `10·log10(p / (512·N·0.5)²)` → ton complexe d'amplitude 256 centré sur un bin = −6,02 dB ; plancher −200 dB.

**Contrôles PS/2** : ←/→ ±1 MHz, Shift+←/→ ±10 MHz (bornes 100–6000), R cycle 16→40→80 MS/s, G bascule AGC/manuel, PgUp/PgDn gain manuel (passe en manuel), ↑/↓ ref ±5 dB (−120…+20), +/− plage −/+10 dB (20…120). Défauts : 2437 MHz, 40 MS/s, AGC, gain 40 (borné à gain_max), ref 0 dB, plage 80 dB.

**Tâches** : `sdr` (core 0, prio 2, pile 4096) ; `ui` (core 1, prio 3, pile 4096) ; FabGL : ISR vidéo core 1, tâche primitives prio 5. Échanges via mutex + sémaphore binaire. Erreur de capture → compteur affiché ; trame sautée si les 8 captures échouent.

**Repli unicore** (si le spike Task 2 échoue en dual-core) : `CONFIG_FREERTOS_UNICORE=y`, patch FabGL `FABGLIB_VIDEO_CPUINTENSIVE_TASKS_CORE` = 0 sous `CONFIG_FREERTOS_UNICORE`, tous les cœurs d'app = 0.
**Repli RAM** (si DMA libre < 160 000 o après `espsdr_rx_init`) : dans l'ordre, `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=2` + `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=4`, puis viewport réduit `setResolution(VGA_512x384_60Hz, 512, 320)` avec waterfall 152–319.

