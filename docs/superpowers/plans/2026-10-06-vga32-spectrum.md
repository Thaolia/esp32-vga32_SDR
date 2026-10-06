# VGA32 Spectrum Analyzer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:dispatching-parallel-agents for the parallel batch (Tasks 3/4/5); the rest is executed inline (superpowers:executing-plans). Steps use checkbox (`- [ ]`) syntax.

**Goal:** Afficher en temps réel un spectre + waterfall (bande 2,4 GHz, centre réglable) sur un écran VGA branché à une TTGO VGA32 (ESP32 classique), à partir des captures I/Q brutes de la radio Wi-Fi (technique ESPARGOS/esp-sdr), piloté au clavier PS/2.

**Architecture:** Projet ESP-IDF 6.2 (commit épinglé `25fe69f946311abdaf9ad56591f25fedbc20ac98`, identique à esp-sdr pour l'ESP32). FabGL (fork shaeberling, déjà porté IDF 5/6) en submodule pour la VGA 16 couleurs + PS/2 ; le backend RX ESP32 d'esp-sdr est extrait dans un composant `espsdr_rx` à API publique. Core 1 : ISR vidéo FabGL + tâche UI ; core 0 : tâche SDR (capture → FFT → moyennage) + Wi-Fi.

**Tech Stack:** ESP-IDF 6.2-dev, C11 / C++ (FabGL), FreeRTOS, CMake/ctest pour les tests host, Python (pyserial de l'env IDF) pour le spike.

**Spec:** sections « Context » et « Design » ci-dessous ; copiées dans `docs/superpowers/specs/2026-10-06-vga32-spectrum-design.md` (Task 1).

---

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

## Global Constraints

- ESP-IDF : `~/esp/esp-idf-6.2` au commit `25fe69f946311abdaf9ad56591f25fedbc20ac98`, cible `esp32`, flash 4 MB.
- FabGL : submodule `third_party/FabGL` = `https://github.com/shaeberling/FabGL` @ `3f21e97ff276e778d2940b4e32f1f54a8a6193d2`. Nom de composant IDF = `FabGL` (nom du dossier).
- esp-sdr : submodule `third_party/esp-sdr` = `https://github.com/ESPARGOS/esp-sdr` @ `fb264f96226e6838ba323d97801ccd28c752e4e0` (sans `--recursive` : esp-dsp inutile).
- Ne jamais modifier les submodules en place de façon permanente : correctifs = `patches/fabgl/*.patch` appliqués par `tools/apply_patches.sh`.
- Licence du projet : GPL-3.0-or-later (FabGL et esp-sdr sont GPL-3). Garder les en-têtes de copyright d'origine dans le code dérivé.
- Commentaires de code en français, termes techniques en anglais ; pas de `.md` à la racine hors README/CLAUDE.
- Aucun `sudo`. Les agents parallèles ne commitent pas : le coordinateur relit et commite.
- `.data`/`.bss` doivent rester sous `0x3ffe8000` (garde linker) : buffers DSP ≤ ~40 Ko en statique.

## Review Focus

1. **Fréquence aux bornes (100 / 6000 MHz)** → borne sans crash ni retune invalide. Test : `test_freq_clamps` (Task 5).
2. **Clavier absent ou débranché au boot** → l'app tourne, le bandeau affiche `KBD?`. Vérification hardware (Task 7, étape 4).
3. **Échecs de capture (timeout / mémoire)** → compteur incrémenté, pas de trame vide affichée. Test : `test_finish_without_frames` (Task 5), compteur visible (Task 7).
4. **Valeurs dB extrêmes (NaN, ±inf, entrée nulle, saturation)** → hauteur/couleur bornées. Tests : `test_db_to_height`, `test_db_to_color`, `test_dc_removed` (Task 5).
5. **Heap insuffisant pour le framebuffer après init Wi-Fi** → log explicite + abort avant `setResolution` au lieu d'un crash FabGL. Mesure : Task 4 étape 5 ; garde : Task 6 (`APP_MIN_DMA_FREE`).

---

## File Structure

```
CMakeLists.txt                 # projet IDF racine (EXTRA_COMPONENT_DIRS FabGL)
sdkconfig.defaults             # fusion esp-sdr (esp32) + FabGL
CLAUDE.md  README.md  .gitignore  .gitmodules
main/
  CMakeLists.txt
  app_main.cpp                 # init radio → PS/2 → VGA, crée les tâches
  sdr_task.h / sdr_task.c      # capture + DSP sur core 0, échange de trames
  ui_render.h / ui_render.cpp  # rendu FabGL (bandeau, spectre, waterfall)
  input_ps2.h / input_ps2.cpp  # VirtualKey → app_cmd_t
components/
  espsdr_rx/                   # backend RX ESP32 extrait d'esp-sdr
    CMakeLists.txt  include/espsdr_rx.h  espsdr_rx.c
  spectrum_dsp/                # C pur, testable host
    CMakeLists.txt  include/spectrum_dsp.h  spectrum_dsp.c
  app_controls/                # C pur : réglages + commandes clavier
    CMakeLists.txt  include/app_controls.h  app_controls.c
third_party/FabGL  third_party/esp-sdr      # submodules
patches/fabgl/                 # correctifs IDF 6.2 (si nécessaires)
tools/apply_patches.sh  tools/spike_capture.py
tests/host/                    # CMake + ctest : test_spectrum_dsp.c, test_app_controls.c, test_util.h
tests/target/rx_smoke/  tests/target/fabgl_smoke/   # mini-projets IDF de validation
tests/spike/dualcore.defaults
docs/ARCHITECTURE.md  docs/HARDWARE.md  docs/spike-phase0.md
docs/superpowers/specs/…  docs/superpowers/plans/…
```

---

### Task 1: Scaffolding, submodules, toolchain IDF 6.2 (coordinateur, séquentiel)

**Files:** Create `CMakeLists.txt`, `sdkconfig.defaults`, `.gitignore`, `.gitmodules`, `CLAUDE.md`, `README.md`, `tools/apply_patches.sh`, `docs/superpowers/specs/2026-10-06-vga32-spectrum-design.md`, `docs/superpowers/plans/2026-10-06-vga32-spectrum.md`, dossiers `main/ components/ patches/fabgl/ tests/ docs/`.

**Interfaces:** Produces : chemins submodules, `$IDF_PATH`=`~/esp/esp-idf-6.2`, `sdkconfig.defaults` racine réutilisé par les smoke tests.

- [ ] **Step 1: Installer IDF en arrière-plan** (long ; lancer avec `run_in_background`)
```bash
mkdir -p ~/esp && git clone https://github.com/espressif/esp-idf.git ~/esp/esp-idf-6.2 \
 && cd ~/esp/esp-idf-6.2 && git checkout 25fe69f946311abdaf9ad56591f25fedbc20ac98 \
 && git submodule update --init --recursive && ./install.sh esp32
```
- [ ] **Step 2: Submodules**
```bash
cd /home/james/dev/claude/esp32-vga32_SDR
git submodule add https://github.com/shaeberling/FabGL third_party/FabGL
git -C third_party/FabGL checkout 3f21e97ff276e778d2940b4e32f1f54a8a6193d2
git submodule add https://github.com/ESPARGOS/esp-sdr third_party/esp-sdr
git -C third_party/esp-sdr checkout fb264f96226e6838ba323d97801ccd28c752e4e0
```
- [ ] **Step 3: `CMakeLists.txt` racine**
```cmake
cmake_minimum_required(VERSION 3.16)
# FabGL est consommé directement depuis le submodule (composant "FabGL")
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/third_party/FabGL")
set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vga32_sdr)
```
- [ ] **Step 4: `sdkconfig.defaults`**
```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
# esp-sdr : accès PHY debug
CONFIG_ESP_PHY_ENABLE_CERT_TEST=y
CONFIG_ESP_PHY_DEBUG=y
# CONFIG_ESP_WIFI_IRAM_OPT is not set
# CONFIG_ESP_WIFI_RX_IRAM_OPT is not set
# CONFIG_ESP_PHY_IRAM_OPT is not set
# CONFIG_ESP_WIFI_AMPDU_TX_ENABLED is not set
# CONFIG_ESP_WIFI_AMPDU_RX_ENABLED is not set
# FabGL : PS/2 via ULP FSM, ISR vidéo hors cache
CONFIG_ULP_COPROC_ENABLED=y
CONFIG_ULP_COPROC_TYPE_FSM=y
CONFIG_ULP_COPROC_RESERVE_MEM=4096
CONFIG_FREERTOS_IN_IRAM=y
CONFIG_ESP_IPC_TASK_STACK_SIZE=2048
# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is not set
```
- [ ] **Step 5: `.gitignore`**
```
build/
sdkconfig
sdkconfig.old
tests/target/*/sdkconfig
tests/target/*/sdkconfig.old
__pycache__/
```
- [ ] **Step 6: `tools/apply_patches.sh`** (idempotent)
```bash
#!/usr/bin/env bash
# Applique patches/fabgl/*.patch sur le submodule FabGL ; ignore ceux déjà appliqués.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
shopt -s nullglob
for p in "$root"/patches/fabgl/*.patch; do
  if git -C "$root/third_party/FabGL" apply --reverse --check "$p" 2>/dev/null; then
    echo "déjà appliqué : $(basename "$p")"
  else
    git -C "$root/third_party/FabGL" apply "$p" && echo "appliqué : $(basename "$p")"
  fi
done
```
`chmod +x tools/apply_patches.sh`
- [ ] **Step 7: Docs** — copier les sections Context + Design de ce plan dans `docs/superpowers/specs/2026-10-06-vga32-spectrum-design.md`, le plan complet dans `docs/superpowers/plans/2026-10-06-vga32-spectrum.md`. `README.md` : but, matériel, `git submodule update --init` (non récursif), `tools/apply_patches.sh`, `. ~/esp/esp-idf-6.2/export.sh`, `idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig set-target esp32` puis `build flash monitor` avec les mêmes options, touches clavier, licence GPL-3. `CLAUDE.md` (instructions opérationnelles en anglais) : règles des Global Constraints, commandes de vérification (host tests, build), règle « no permanent edits in third_party », ordre d'init radio→PS/2→VGA, `_DEBUG`-style logging (ESP_LOGD hors chemin critique), mise à jour docs dans la même session.
- [ ] **Step 8: Vérifier IDF** (après fin du Step 1) : `. ~/esp/esp-idf-6.2/export.sh && idf.py --version` → doit afficher `v6.2-dev-…`.
- [ ] **Step 9: Commit** `git add -A && git commit -m "chore: scaffold projet, submodules FabGL/esp-sdr, config IDF 6.2"`

---

### Task 2: Spike phase 0 — esp-sdr stock en unicore puis dual-core (coordinateur + carte)

**Question :** la capture I/Q fonctionne-t-elle sur la VGA32 avec IDF 6.2, et toujours en dual-core ? **Gate** pour la suite.

**Files:** Create `tools/spike_capture.py`, `tests/spike/dualcore.defaults`, `docs/spike-phase0.md`.

- [ ] **Step 1: Demander à l'utilisateur** le port série (défaut `/dev/ttyUSB0`) et de brancher la carte.
- [ ] **Step 2: `tests/spike/dualcore.defaults`**
```
# CONFIG_FREERTOS_UNICORE is not set
```
- [ ] **Step 3: `tools/spike_capture.py`**
```python
#!/usr/bin/env python3
"""Spike phase 0 : valide les captures CAP16 du firmware esp-sdr stock."""
import argparse
import sys
import zlib

import serial


def command(port, line):
    port.write((line + "\n").encode())
    return port.readline().decode(errors="replace").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--count", type=int, default=50)
    a = ap.parse_args()
    ok = bad_header = bad_crc = 0
    us, rms = 0, 0.0
    with serial.Serial(a.port, 2000000, timeout=2) as p:
        p.reset_input_buffer()
        print(command(p, "INFO"))
        print(command(p, "FREQ 2437"))
        for _ in range(a.count):
            p.write(b"CAP16 1024 1\n")  # rate 1 → horloge 40 MS/s
            hdr = p.readline().decode(errors="replace").split()
            if len(hdr) != 4 or hdr[0] != "DATA":
                bad_header += 1
                print("header:", hdr)
                continue
            n, crc, us = int(hdr[1]), int(hdr[2], 16), int(hdr[3])
            data = p.read(n * 2)
            if len(data) != n * 2 or (zlib.crc32(data) & 0xFFFFFFFF) != crc:
                bad_crc += 1
                continue
            iq = [b - 256 if b > 127 else b for b in data]
            rms = (sum(v * v for v in iq) / len(iq)) ** 0.5
            ok += 1
    print(f"ok={ok}/{a.count} bad_header={bad_header} bad_crc={bad_crc} last_us={us} rms={rms:.1f}")
    return 0 if ok == a.count else 1


if __name__ == "__main__":
    sys.exit(main())
```
Si `bad_crc == count` mais en-têtes OK : vérifier la convention CRC dans `third_party/esp-sdr/tools/` avant de conclure.
- [ ] **Step 4: Build + flash unicore (stock)**
```bash
. ~/esp/esp-idf-6.2/export.sh
idf.py -C third_party/esp-sdr -B build/spike-uni -D SDKCONFIG=build/spike-uni/sdkconfig set-target esp32
idf.py -C third_party/esp-sdr -B build/spike-uni -D SDKCONFIG=build/spike-uni/sdkconfig -p /dev/ttyUSB0 build flash
python tools/spike_capture.py --port /dev/ttyUSB0
```
Attendu : `ESP32SDR 6 burst 16380`, `OK`, `ok=50/50`, `last_us` ≈ 26–40.
- [ ] **Step 5: Build + flash dual-core**
```bash
D=$PWD/third_party/esp-sdr
idf.py -C third_party/esp-sdr -B build/spike-dual -D SDKCONFIG=build/spike-dual/sdkconfig \
  -D SDKCONFIG_DEFAULTS="$D/sdkconfig.defaults;$PWD/tests/spike/dualcore.defaults" set-target esp32
grep -c "^CONFIG_FREERTOS_UNICORE=y" build/spike-dual/sdkconfig   # attendu : 0
idf.py -C third_party/esp-sdr -B build/spike-dual -D SDKCONFIG=build/spike-dual/sdkconfig -p /dev/ttyUSB0 build flash
python tools/spike_capture.py --port /dev/ttyUSB0
```
(IDF charge `sdkconfig.defaults.esp32` automatiquement après `sdkconfig.defaults`.) Vérifier que `git -C third_party/esp-sdr status --porcelain` est vide.
- [ ] **Step 6: Consigner** dans `docs/spike-phase0.md` : version IDF, résultats uni/dual, `last_us`, rms. **Décision** : dual OK → design nominal ; dual KO / uni OK → appliquer le *Repli unicore* (Design) dans Tasks 3/6 ; uni KO → STOP, rapport à l'utilisateur.
- [ ] **Step 7: Commit** `git add tools/spike_capture.py tests/spike docs/spike-phase0.md && git commit -m "test: spike phase 0 esp-sdr stock (uni/dual-core)"`

---

## Batch parallèle — Tasks 3, 4, 5 (superpowers:dispatching-parallel-agents)

Un agent `general-purpose` par tâche, lancés **dans un même message**. Chaque prompt contient : le texte intégral de sa tâche + Global Constraints + résultat du spike. Domaines disjoints (A : `third_party/FabGL` via patches + `tests/target/fabgl_smoke` ; B : `components/espsdr_rx` + `tests/target/rx_smoke` ; C : `components/spectrum_dsp`, `components/app_controls`, `tests/host`). Règles : ne pas commiter, ne pas flasher (une seule carte), builds dans `build/<nom>`. Retour attendu : fichiers créés, sortie de build/tests, écarts au plan. Le coordinateur relit chaque diff, lance les vérifs, commite, puis fait les validations matérielles A et B en séquence avec l'utilisateur.

### Task 3 (Agent A): FabGL sur IDF 6.2 + smoke test VGA/PS/2

**Files:** Create `tests/target/fabgl_smoke/CMakeLists.txt`, `tests/target/fabgl_smoke/main/CMakeLists.txt`, `tests/target/fabgl_smoke/main/main.cpp` ; éventuellement `patches/fabgl/0001-idf-6.2-compat.patch`, `patches/fabgl/0002-unicore-video-core.patch` (si repli unicore).

**Interfaces:** Produces : composant `FabGL` compilable en IDF 6.2 ; API validée utilisée par Task 6 : `VGA16Controller::begin()`, `setResolution(VGA_512x384_60Hz)`, `setPaletteItem(int, RGB888)`, `getScanline(int)`, `Canvas::setScrollingRegion/scroll/drawText/selectFont/waitCompletion`, `PS2Controller::begin(PS2Preset::KeyboardPort0)`, `keyboard()->getNextVirtualKey(VirtualKeyItem*, int)`, `isKeyboardAvailable()`, noms `VK_LEFT VK_RIGHT VK_UP VK_DOWN VK_PAGEUP VK_PAGEDOWN VK_r VK_R VK_g VK_G VK_PLUS VK_KP_PLUS VK_EQUALS VK_MINUS VK_KP_MINUS`.

- [ ] **Step 1: `tests/target/fabgl_smoke/CMakeLists.txt`**
```cmake
cmake_minimum_required(VERSION 3.16)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${ROOT}/third_party/FabGL")
set(SDKCONFIG_DEFAULTS "${ROOT}/sdkconfig.defaults")
set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(fabgl_smoke)
```
`main/CMakeLists.txt` : `idf_component_register(SRCS "main.cpp" REQUIRES FabGL)`
- [ ] **Step 2: `main.cpp`** — même technique de rendu que l'app finale :
```cpp
// Smoke test FabGL IDF 6.2 : barres 16 couleurs en écriture brute, texte, VScroll, touches PS/2
#include <cstdio>
#include "esp_heap_caps.h"
#include "fabgl.h"

static fabgl::VGA16Controller s_vga;
static fabgl::PS2Controller s_ps2;

static inline void put4(uint8_t *row, int x, uint8_t c)
{
  uint8_t &b = row[x >> 1];
  b = (x & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
}

extern "C" void app_main()
{
  printf("DMA libre avant VGA : %u\n", (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA));
  s_ps2.begin(PS2Preset::KeyboardPort0);
  s_vga.begin();
  s_vga.setResolution(VGA_512x384_60Hz);
  for (int i = 0; i < 16; ++i)
    s_vga.setPaletteItem(i, RGB888((i & 1) ? 255 : 0, (i & 2) ? 255 : 0, (i & 4) ? 255 : (i * 16)));
  fabgl::Canvas canvas(&s_vga);
  canvas.waitCompletion();
  for (int y = 0; y < 384; ++y) {
    uint8_t *row = s_vga.getScanline(y);
    for (int x = 0; x < 512; ++x) put4(row, x, (uint8_t)(x / 32));
  }
  canvas.selectFont(&fabgl::FONT_8x8);
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
  canvas.setScrollingRegion(0, 200, 511, 383);
  canvas.drawText(4, 4, "FabGL smoke IDF 6.2");
  printf("DMA libre après VGA : %u\n", (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA));
  for (;;) {
    fabgl::VirtualKeyItem item;
    auto *kbd = s_ps2.keyboard();
    if (kbd && kbd->getNextVirtualKey(&item, 100) && item.down)
      canvas.drawText(4, 20, kbd->virtualKeyToString(item.vk));
    canvas.scroll(0, 1);
  }
}
```
- [ ] **Step 3: Build**
```bash
. ~/esp/esp-idf-6.2/export.sh && tools/apply_patches.sh
idf.py -C tests/target/fabgl_smoke -B build/fabgl_smoke -D SDKCONFIG=build/fabgl_smoke/sdkconfig set-target esp32
idf.py -C tests/target/fabgl_smoke -B build/fabgl_smoke -D SDKCONFIG=build/fabgl_smoke/sdkconfig build
```
- [ ] **Step 4: Si erreurs de compilation IDF 6.2** — corriger dans `third_party/FabGL` (minimal, garder les branches de version existantes du fork, `#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6,2,0)` si besoin), puis :
```bash
git -C third_party/FabGL diff > patches/fabgl/0001-idf-6.2-compat.patch
git -C third_party/FabGL checkout -- . && tools/apply_patches.sh
```
Rebuild jusqu'à succès, **zéro warning nouveau introduit par nos patches**. Si le spike impose l'unicore : `0002-unicore-video-core.patch` sur `src/fabglconf.h:142` :
```c
#if CONFIG_FREERTOS_UNICORE
#define FABGLIB_VIDEO_CPUINTENSIVE_TASKS_CORE 0
#else
#define FABGLIB_VIDEO_CPUINTENSIVE_TASKS_CORE (WIFI_TASK_CORE_ID ^ 1) // using the same core of WiFi may cause flickering
#endif
```
- [ ] **Step 5: Vérifier les noms de VirtualKey** listés dans *Interfaces* (`grep -n "VK_EQUALS\|VK_KP_MINUS\|VK_R,\|VK_G," third_party/FabGL/src/fabutils.h`) et rapporter tout nom absent.
- [ ] **Step 6 (coordinateur, carte) :** `idf.py … -p PORT flash monitor` → attendu : 16 barres verticales, texte, zone basse qui défile, nom de touche affiché ; noter les deux valeurs « DMA libre ».

### Task 4 (Agent B): composant `espsdr_rx` + smoke test RX

**Files:** Create `components/espsdr_rx/CMakeLists.txt`, `components/espsdr_rx/include/espsdr_rx.h`, `components/espsdr_rx/espsdr_rx.c`, `tests/target/rx_smoke/CMakeLists.txt`, `tests/target/rx_smoke/main/CMakeLists.txt`, `tests/target/rx_smoke/main/main.c`.

**Interfaces:** Produces (consommé par Task 6) :
```c
typedef enum { ESPSDR_RATE_16M = 0, ESPSDR_RATE_80M = 1, ESPSDR_RATE_40M = 2 } espsdr_rate_t;
esp_err_t espsdr_rx_init(void);
esp_err_t espsdr_rx_tune(unsigned mhz);
void espsdr_rx_set_gain(bool hardware_agc, unsigned manual_index);
unsigned espsdr_rx_gain_max(void);
esp_err_t espsdr_rx_capture(unsigned n, espsdr_rate_t rate, const uint32_t **words, unsigned *elapsed_us);
```

- [ ] **Step 1: `include/espsdr_rx.h`**
```c
/* Récepteur I/Q brut ESP32 dérivé d'ESPARGOS/esp-sdr (GPL-3.0-or-later).
 * Non thread-safe : un seul appelant (la tâche SDR). Le Wi-Fi n'est plus utilisable. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Valeurs = sélecteurs d'horloge matériels du dump MAC */
typedef enum { ESPSDR_RATE_16M = 0, ESPSDR_RATE_80M = 1, ESPSDR_RATE_40M = 2 } espsdr_rate_t;

#define ESPSDR_MIN_SAMPLES 256u
#define ESPSDR_MAX_SAMPLES 16380u

esp_err_t espsdr_rx_init(void);
/* ESP_ERR_INVALID_ARG hors 100..6000 MHz */
esp_err_t espsdr_rx_tune(unsigned mhz);
/* manual_index borné à espsdr_rx_gain_max() */
void espsdr_rx_set_gain(bool hardware_agc, unsigned manual_index);
unsigned espsdr_rx_gain_max(void);
/* *words pointe dans la SRAM réservée, valide jusqu'à la capture suivante.
 * Mot : I = bits 0-9, Q = bits 10-19 (signés). ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE, ESP_ERR_INVALID_ARG. */
esp_err_t espsdr_rx_capture(unsigned n, espsdr_rate_t rate, const uint32_t **words, unsigned *elapsed_us);

#ifdef __cplusplus
}
#endif
```
- [ ] **Step 2: `espsdr_rx.c`** — reprise fidèle de `third_party/esp-sdr/main/targets/esp32/receiver.c` (`prepare_rx`, `tune_rx`, `apply_gain`, `acquire_iq`, init Wi-Fi d'`app_main`), protocole série et filtre LPF retirés :
```c
/* Backend RX ESP32 extrait d'ESPARGOS/esp-sdr main/targets/esp32/receiver.c (GPL-3.0-or-later).
 * Séquence registres et réserve SRAM inchangées ; protocole série retiré. */
#include "sdkconfig.h" /* rx_lo.h teste CONFIG_IDF_TARGET_ESP32 sans l'inclure */
#include "espsdr_rx.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "heap_memory_layout.h"
#include "nvs_flash.h"
#include "soc/dport_access.h"
#include "soc/dport_reg.h"
#include "soc/rtc.h"
#include "soc/soc.h"
#include "rx_lo.h"
#include "rx_recalibration.h"
#include "rx_tuning.h"

/* Les deux bancs SRAM servent d'ouverture de capture au MAC (mode 3 = 64 KiB) */
SOC_RESERVE_MEMORY_REGION(0x3ffe8000, 0x3fff8000, esp32_rf_dump);

#define DUMP_CTRL 0x60033d90u
#define DUMP_STATUS 0x60033d94u
#define DUMP_BYTES 0x60033dc4u
#define RX_GAIN 0x3ff5c02cu
#define CAPACITY 16384u
#define SENTINEL 0xa55aa55au

static const char *TAG = "espsdr_rx";
static uint32_t *const samples = (void *)0x3ffe8000;
static unsigned s_gain_max;
static unsigned s_gain_code = 40;
static bool s_hardware_agc = true;
static unsigned s_frequency_mhz = 2412;

extern void rom_pbus_workmode(void);
extern void rom_pbus_xpd_rx_on(int);
extern void rom_pbus_xpd_tx_off(void);
extern void rom_set_rxclk_en(int);
extern void set_chanfreq(unsigned mhz, unsigned mode);
extern void rom_set_rf_freq_offset(unsigned crystal, unsigned mhz, int offset);

static void prepare_rx(void)
{
    rom_pbus_workmode();
    rom_pbus_xpd_tx_off();
    rom_pbus_xpd_rx_on(1);
    rom_set_rxclk_en(1);
    if (rx_lo_select(rx_lo_plan(s_frequency_mhz).alternate)) esp_rom_delay_us(3000);
}

static void apply_gain(void)
{
    uint32_t gain = REG_READ(RX_GAIN);
    if (s_hardware_agc) REG_CLR_BIT(RX_GAIN, BIT(23));
    else REG_WRITE(RX_GAIN, (gain & 0x7fffff) | (s_gain_code << 24) | BIT(23));
}

static void tune_rx(unsigned mhz)
{
    static unsigned calibrated_mhz;
    if (calibrated_mhz != mhz) {
        rx_recalibrate(mhz);
        calibrated_mhz = mhz;
    }
    rx_lo_plan_t plan = rx_lo_plan(mhz);
    rx_lo_select(false);
    bool channel = (mhz >= 2412 && mhz <= 2472 && (mhz - 2412) % 5 == 0) || mhz == 2484;
    /* Calibrer sur un vrai canal avant de contourner le mapping des canaux */
    set_chanfreq(channel ? mhz : 2412, 0);
    if (!channel) {
        unsigned xtal = rtc_clk_xtal_freq_get();
        /* Libérer le code capacité figé par le canal avant le retune ROM */
        unsigned cap = ram_chip_i2c_readReg(0x62, 1, 0);
        ram_chip_i2c_writeReg(0x62, 1, 0, cap & ~0x80u);
        /* Cette ROM utilise des offsets en 1/1024 MHz */
        int offset = (plan.offset_khz * 1024 + 500) / 1000;
        rom_set_rf_freq_offset(xtal == 26 ? 1 : xtal == 24 ? 2 : 0, plan.mhz, offset);
    }
}

esp_err_t espsdr_rx_init(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
        e = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(e, TAG, "nvs init");
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_NULL), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "ps");
    ESP_RETURN_ON_ERROR(esp_wifi_set_promiscuous(true), TAG, "promiscuous");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE), TAG, "channel");
    prepare_rx();
    s_gain_max = (REG_READ(RX_GAIN) >> 8) & 127;
    if (s_gain_code > s_gain_max) s_gain_code = s_gain_max;
    REG_CLR_BIT(RX_GAIN, BIT(23)); /* AGC matériel par défaut */
    return ESP_OK;
}

esp_err_t espsdr_rx_tune(unsigned mhz)
{
    if (!rx_frequency_valid(mhz)) return ESP_ERR_INVALID_ARG;
    s_frequency_mhz = mhz;
    tune_rx(mhz);
    prepare_rx();
    apply_gain();
    return ESP_OK;
}

void espsdr_rx_set_gain(bool hardware_agc, unsigned manual_index)
{
    s_hardware_agc = hardware_agc;
    s_gain_code = manual_index > s_gain_max ? s_gain_max : manual_index;
    apply_gain();
}

unsigned espsdr_rx_gain_max(void)
{
    return s_gain_max;
}

esp_err_t espsdr_rx_capture(unsigned n, espsdr_rate_t rate, const uint32_t **words, unsigned *elapsed_us)
{
    unsigned clock = (unsigned)rate;
    if (n < ESPSDR_MIN_SAMPLES || n > ESPSDR_MAX_SAMPLES || clock > 2) return ESP_ERR_INVALID_ARG;
    REG_WRITE(DUMP_CTRL, 0);
    for (unsigned j = 0; j < CAPACITY; j++) samples[j] = SENTINEL;
    uint32_t owner = DPORT_REG_READ(DPORT_IRAM_DRAM_AHB_SEL_REG);
    uint32_t byte_select = REG_READ(DUMP_BYTES);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG,
        (owner & ~DPORT_MAC_DUMP_MODE_M) | (3 << DPORT_MAC_DUMP_MODE_S));
    REG_WRITE(DUMP_BYTES, 0x03020100); /* Quatre octets distincts, comme mac_init */
    uint32_t ctrl = n | ((clock == 0) << 16) | ((clock == 2) << 15);
    REG_WRITE(DUMP_CTRL, ctrl | BIT(31));
    int64_t start = esp_timer_get_time();
    REG_SET_BIT(DUMP_CTRL, BIT(19));
    REG_CLR_BIT(DUMP_CTRL, BIT(19));
    while (!(REG_READ(DUMP_CTRL) & BIT(18)) && esp_timer_get_time() - start < 20000) {}
    uint32_t result = REG_READ(DUMP_CTRL), status = REG_READ(DUMP_STATUS);
    unsigned elapsed = (unsigned)(esp_timer_get_time() - start);
    REG_WRITE(DUMP_CTRL, 0);
    esp_rom_delay_us(10);
    DPORT_REG_WRITE(DPORT_IRAM_DRAM_AHB_SEL_REG, owner);
    REG_WRITE(DUMP_BYTES, byte_select);
    if (!(result & BIT(18)) || (status & 0x7fff) != n) return ESP_ERR_TIMEOUT;
    for (unsigned j = 0; j < CAPACITY; j++) {
        if ((j < n && samples[j] == SENTINEL) || (j >= n && samples[j] != SENTINEL))
            return ESP_ERR_INVALID_RESPONSE;
    }
    *words = samples;
    *elapsed_us = elapsed;
    return ESP_OK;
}
```
- [ ] **Step 3: `components/espsdr_rx/CMakeLists.txt`**
```cmake
set(ESPSDR_MAIN "${CMAKE_CURRENT_LIST_DIR}/../../third_party/esp-sdr/main")
idf_component_register(SRCS "espsdr_rx.c" "${ESPSDR_MAIN}/common/rx_recalibration.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${ESPSDR_MAIN}/common"
                       REQUIRES esp_common
                       PRIV_REQUIRES esp_phy esp_wifi esp_event nvs_flash esp_timer
                                     esp_rom esp_hw_support heap soc)
# Garde linker de l'ouverture SRAM + hook de recalibration PHY (cf. esp-sdr main/CMakeLists.txt)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${ESPSDR_MAIN}/targets/esp32/sram_guard.ld"
    "-Wl,--wrap=chip_v7_set_chan_ana")
```
- [ ] **Step 4: smoke `tests/target/rx_smoke/`** — `CMakeLists.txt` :
```cmake
cmake_minimum_required(VERSION 3.16)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${ROOT}/components/espsdr_rx")
set(SDKCONFIG_DEFAULTS "${ROOT}/sdkconfig.defaults")
set(ENV{IDF_COMPONENT_MANAGER} "0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(rx_smoke)
```
`main/CMakeLists.txt` : `idf_component_register(SRCS "main.c" REQUIRES espsdr_rx)` ; `main/main.c` :
```c
// Smoke test RX : 100 captures/s à 2437 MHz, 40 MS/s ; log du heap pour dimensionner la VGA
#include <math.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espsdr_rx.h"

static const char *TAG = "rx_smoke";

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "%s : DMA libre %u (plus grand bloc %u)", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void app_main(void)
{
    log_heap("boot");
    ESP_ERROR_CHECK(espsdr_rx_init());
    log_heap("après espsdr_rx_init");
    ESP_ERROR_CHECK(espsdr_rx_tune(2437));
    for (;;) {
        unsigned ok = 0, fail = 0, us = 0;
        double acc = 0;
        for (int k = 0; k < 100; k++) {
            const uint32_t *w;
            if (espsdr_rx_capture(1024, ESPSDR_RATE_40M, &w, &us) != ESP_OK) { fail++; continue; }
            for (int j = 0; j < 1024; j++) {
                int32_t i = (int32_t)(w[j] << 22) >> 22, q = (int32_t)(w[j] << 12) >> 22;
                acc += (double)(i * i + q * q);
            }
            ok++;
        }
        ESP_LOGI(TAG, "ok=%u fail=%u last_us=%u rms=%.1f gain_max=%u", ok, fail, us,
                 ok ? sqrt(acc / (ok * 2048.0)) : 0.0, espsdr_rx_gain_max());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```
- [ ] **Step 5: Build** `idf.py -C tests/target/rx_smoke -B build/rx_smoke -D SDKCONFIG=build/rx_smoke/sdkconfig set-target esp32 && idf.py -C tests/target/rx_smoke -B build/rx_smoke -D SDKCONFIG=build/rx_smoke/sdkconfig build` → succès, garde `sram_guard.ld` respectée. Vérifier le lien du wrap : `xtensa-esp32-elf-nm build/rx_smoke/rx_smoke.elf | grep chip_v7_set_chan_ana` → `__wrap_` et `__real_` présents.
- [ ] **Step 6 (coordinateur, carte) :** flash + monitor → attendu `ok=100 fail=0`, `last_us` 25–40, rms > 0 ; hotspot téléphone sur canal 6 → rms augmente. Noter « DMA libre après espsdr_rx_init » : si < 160 000 o → appliquer le *Repli RAM* (Design) avant Task 6.

### Task 5 (Agent C): `spectrum_dsp` + `app_controls` en TDD host

**Files:** Create `components/spectrum_dsp/{CMakeLists.txt,include/spectrum_dsp.h,spectrum_dsp.c}`, `components/app_controls/{CMakeLists.txt,include/app_controls.h,app_controls.c}`, `tests/host/{CMakeLists.txt,test_util.h,test_spectrum_dsp.c,test_app_controls.c}`.

**Interfaces:** Produces :
```c
#define SD_FFT_N 1024u
#define SD_COLUMNS 512u
#define SD_FLOOR_DB (-200.0f)
typedef struct { float re[SD_FFT_N], im[SD_FFT_N], window[SD_FFT_N], power_acc[SD_FFT_N];
                 float tw_re[SD_FFT_N / 2], tw_im[SD_FFT_N / 2]; unsigned frames; } sd_ctx_t;
static inline int32_t sd_iq10_i(uint32_t w); static inline int32_t sd_iq10_q(uint32_t w);
void sd_init(sd_ctx_t *ctx);
void sd_fft(const sd_ctx_t *ctx, float *re, float *im);
void sd_accumulate(sd_ctx_t *ctx, const uint32_t *words);   /* SD_FFT_N mots */
bool sd_finish_db(sd_ctx_t *ctx, float *db_cols);            /* SD_COLUMNS ; false si 0 trame */
unsigned sd_db_to_height(float db, float ref_db, float range_db, unsigned height);
uint8_t sd_db_to_color(float db, float ref_db, float range_db, uint8_t first, uint8_t count);

typedef enum { APP_RATE_16M, APP_RATE_40M, APP_RATE_80M, APP_RATE_COUNT } app_rate_t;
typedef struct { unsigned center_mhz; app_rate_t rate; bool hardware_agc; unsigned gain_index; } app_settings_t;
typedef struct { float ref_db; float range_db; } app_view_t;
typedef enum { APP_CMD_FREQ_UP_1, APP_CMD_FREQ_DOWN_1, APP_CMD_FREQ_UP_10, APP_CMD_FREQ_DOWN_10,
               APP_CMD_RATE_CYCLE, APP_CMD_AGC_TOGGLE, APP_CMD_GAIN_UP, APP_CMD_GAIN_DOWN,
               APP_CMD_REF_UP, APP_CMD_REF_DOWN, APP_CMD_RANGE_DOWN, APP_CMD_RANGE_UP } app_cmd_t;
typedef enum { APP_CHANGE_NONE, APP_CHANGE_VIEW, APP_CHANGE_RADIO } app_change_t;
void app_defaults(app_settings_t *s, app_view_t *v, unsigned gain_max);
app_change_t app_apply_cmd(app_cmd_t cmd, app_settings_t *s, app_view_t *v, unsigned gain_max);
unsigned app_rate_msps(app_rate_t rate);
```
Mémoire `sd_ctx_t` ≈ 24 Ko (statique dans la tâche SDR, sous la garde linker).

- [ ] **Step 1: `tests/host/test_util.h` + `CMakeLists.txt`**
```c
#pragma once
#include <math.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #c); return 1; } } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(fabsf((float)(a) - (float)(b)) <= (tol))
#define RUN(t) do { if (t()) { fprintf(stderr, "FAIL %s\n", #t); failures++; } else printf("ok %s\n", #t); } while (0)
```
```cmake
cmake_minimum_required(VERSION 3.16)
project(host_tests C)
set(CMAKE_C_STANDARD 11)
add_compile_options(-Wall -Wextra -Werror)
set(COMP ${CMAKE_CURRENT_LIST_DIR}/../../components)
enable_testing()
add_executable(test_spectrum_dsp test_spectrum_dsp.c ${COMP}/spectrum_dsp/spectrum_dsp.c)
target_include_directories(test_spectrum_dsp PRIVATE ${COMP}/spectrum_dsp/include)
target_link_libraries(test_spectrum_dsp m)
add_test(NAME spectrum_dsp COMMAND test_spectrum_dsp)
add_executable(test_app_controls test_app_controls.c ${COMP}/app_controls/app_controls.c)
target_include_directories(test_app_controls PRIVATE ${COMP}/app_controls/include)
add_test(NAME app_controls COMMAND test_app_controls)
```
- [ ] **Step 2: Écrire `test_spectrum_dsp.c` (échoue : pas d'implémentation)**
```c
#include <stdint.h>
#include <stdlib.h>
#include "spectrum_dsp.h"
#include "test_util.h"

#define TWO_PI 6.28318530717958647692f
static sd_ctx_t ctx;
static uint32_t words[SD_FFT_N];
static float db[SD_COLUMNS];

static uint32_t pack(int32_t i, int32_t q) { return ((uint32_t)i & 0x3ffu) | (((uint32_t)q & 0x3ffu) << 10); }

static void make_tone(int bin, float amp)
{
    for (unsigned n = 0; n < SD_FFT_N; n++) {
        float ph = TWO_PI * (float)bin * (float)n / (float)SD_FFT_N;
        words[n] = pack((int32_t)lroundf(amp * cosf(ph)), (int32_t)lroundf(amp * sinf(ph)));
    }
}

static int test_iq10_sign(void)
{
    uint32_t w = pack(-1, 511);
    CHECK(sd_iq10_i(w) == -1 && sd_iq10_q(w) == 511);
    w = pack(-512, -512) | (0x3fu << 20); /* bits hauts (gain) ignorés */
    CHECK(sd_iq10_i(w) == -512 && sd_iq10_q(w) == -512);
    return 0;
}

static int test_fft_impulse(void)
{
    static float re[SD_FFT_N], im[SD_FFT_N];
    for (unsigned k = 0; k < SD_FFT_N; k++) re[k] = im[k] = 0.0f;
    re[0] = 1.0f;
    sd_fft(&ctx, re, im);
    for (unsigned k = 0; k < SD_FFT_N; k++) { CHECK_NEAR(re[k], 1.0f, 1e-5f); CHECK_NEAR(im[k], 0.0f, 1e-5f); }
    return 0;
}

static int test_fft_tone_bin37(void)
{
    static float re[SD_FFT_N], im[SD_FFT_N];
    for (unsigned n = 0; n < SD_FFT_N; n++) {
        re[n] = cosf(TWO_PI * 37.0f * (float)n / (float)SD_FFT_N);
        im[n] = sinf(TWO_PI * 37.0f * (float)n / (float)SD_FFT_N);
    }
    sd_fft(&ctx, re, im);
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        float mag = sqrtf(re[k] * re[k] + im[k] * im[k]);
        if (k == 37) CHECK_NEAR(mag, 1024.0f, 0.05f);
        else CHECK(mag < 0.05f);
    }
    return 0;
}

static int check_peak(unsigned col)
{
    CHECK_NEAR(db[col], -6.02f, 0.2f);
    for (unsigned c = 0; c < SD_COLUMNS; c++)
        if (abs((int)c - (int)col) > 1) CHECK(db[c] < -60.0f);
    return 0;
}

static int test_tone_positive(void)
{
    make_tone(256, 256.0f);          /* +fs/4 → index décalé 768 → colonne 384 */
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    return check_peak(384);
}

static int test_tone_negative(void)
{
    make_tone(-256, 256.0f);         /* −fs/4 → index décalé 256 → colonne 128 */
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    return check_peak(128);
}

static int test_average_equals_single(void)
{
    make_tone(256, 256.0f);
    sd_accumulate(&ctx, words);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    CHECK_NEAR(db[384], -6.02f, 0.2f);
    return 0;
}

static int test_dc_removed(void)
{
    for (unsigned n = 0; n < SD_FFT_N; n++) words[n] = pack(100, -50);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    for (unsigned c = 0; c < SD_COLUMNS; c++) CHECK(db[c] <= -150.0f);
    return 0;
}

static int test_finish_without_frames(void)
{
    CHECK(!sd_finish_db(&ctx, db));
    make_tone(10, 100.0f);
    sd_accumulate(&ctx, words);
    CHECK(sd_finish_db(&ctx, db));
    CHECK(!sd_finish_db(&ctx, db));  /* accumulateur remis à zéro */
    return 0;
}

static int test_db_to_height(void)
{
    CHECK(sd_db_to_height(0.0f, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-80.0f, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(-40.0f, 0.0f, 80.0f, 128) == 64);
    CHECK(sd_db_to_height(10.0f, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-100.0f, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(NAN, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(-INFINITY, 0.0f, 80.0f, 128) == 0);
    CHECK(sd_db_to_height(INFINITY, 0.0f, 80.0f, 128) == 128);
    CHECK(sd_db_to_height(-10.0f, 0.0f, 0.0f, 128) == 0);
    return 0;
}

static int test_db_to_color(void)
{
    CHECK(sd_db_to_color(-80.0f, 0.0f, 80.0f, 4, 12) == 4);
    CHECK(sd_db_to_color(0.0f, 0.0f, 80.0f, 4, 12) == 15);
    CHECK(sd_db_to_color(-40.0f, 0.0f, 80.0f, 4, 12) == 10);
    CHECK(sd_db_to_color(NAN, 0.0f, 80.0f, 4, 12) == 4);
    CHECK(sd_db_to_color(INFINITY, 0.0f, 80.0f, 4, 12) == 15);
    CHECK(sd_db_to_color(-10.0f, 0.0f, 80.0f, 4, 0) == 4);
    return 0;
}

int main(void)
{
    int failures = 0;
    sd_init(&ctx);
    RUN(test_iq10_sign);
    RUN(test_fft_impulse);
    RUN(test_fft_tone_bin37);
    RUN(test_tone_positive);
    RUN(test_tone_negative);
    RUN(test_average_equals_single);
    RUN(test_dc_removed);
    RUN(test_finish_without_frames);
    RUN(test_db_to_height);
    RUN(test_db_to_color);
    return failures ? 1 : 0;
}
```
- [ ] **Step 3: Vérifier l'échec** — `cmake -S tests/host -B build/host && cmake --build build/host` → échec (en-tête/symboles absents).
- [ ] **Step 4: `include/spectrum_dsp.h`**
```c
/* DSP spectre : FFT complexe 1024 points, moyennage, dBFS, quantification écran. C pur (testé sur PC). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_FFT_N 1024u
#define SD_COLUMNS 512u
#define SD_FLOOR_DB (-200.0f)

typedef struct {
    float re[SD_FFT_N];
    float im[SD_FFT_N];
    float window[SD_FFT_N];
    float power_acc[SD_FFT_N]; /* ordre fftshift : index 0 = −fs/2 */
    float tw_re[SD_FFT_N / 2];
    float tw_im[SD_FFT_N / 2];
    unsigned frames;
} sd_ctx_t;

/* Mot esp-sdr : I signé bits 0-9, Q signé bits 10-19 */
static inline int32_t sd_iq10_i(uint32_t w) { return (int32_t)(w << 22) >> 22; }
static inline int32_t sd_iq10_q(uint32_t w) { return (int32_t)(w << 12) >> 22; }

void sd_init(sd_ctx_t *ctx);
void sd_fft(const sd_ctx_t *ctx, float *re, float *im);
void sd_accumulate(sd_ctx_t *ctx, const uint32_t *words);
bool sd_finish_db(sd_ctx_t *ctx, float *db_cols);
unsigned sd_db_to_height(float db, float ref_db, float range_db, unsigned height);
uint8_t sd_db_to_color(float db, float ref_db, float range_db, uint8_t first, uint8_t count);

#ifdef __cplusplus
}
#endif
```
- [ ] **Step 5: `spectrum_dsp.c`**
```c
#include "spectrum_dsp.h"
#include <math.h>
#include <string.h>

#define SD_PI 3.14159265358979323846f
/* Ton complexe pleine échelle (|I|,|Q| = 512) centré sur un bin, Hann (gain cohérent 0,5) = 0 dBFS */
#define SD_NORM (512.0f * (float)SD_FFT_N * 0.5f)

void sd_init(sd_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    for (unsigned k = 0; k < SD_FFT_N; k++)
        ctx->window[k] = 0.5f - 0.5f * cosf(2.0f * SD_PI * (float)k / (float)SD_FFT_N);
    for (unsigned k = 0; k < SD_FFT_N / 2; k++) {
        ctx->tw_re[k] = cosf(-2.0f * SD_PI * (float)k / (float)SD_FFT_N);
        ctx->tw_im[k] = sinf(-2.0f * SD_PI * (float)k / (float)SD_FFT_N);
    }
}

void sd_fft(const sd_ctx_t *ctx, float *re, float *im)
{
    const unsigned n = SD_FFT_N;
    for (unsigned i = 1, j = 0; i < n; i++) {
        unsigned bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (unsigned len = 2; len <= n; len <<= 1) {
        unsigned half = len >> 1, step = n / len;
        for (unsigned i = 0; i < n; i += len) {
            for (unsigned k = 0; k < half; k++) {
                float wr = ctx->tw_re[k * step], wi = ctx->tw_im[k * step];
                unsigned a = i + k, b = a + half;
                float tr = re[b] * wr - im[b] * wi;
                float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

void sd_accumulate(sd_ctx_t *ctx, const uint32_t *words)
{
    float mean_i = 0.0f, mean_q = 0.0f;
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        ctx->re[k] = (float)sd_iq10_i(words[k]);
        ctx->im[k] = (float)sd_iq10_q(words[k]);
        mean_i += ctx->re[k];
        mean_q += ctx->im[k];
    }
    mean_i /= (float)SD_FFT_N;
    mean_q /= (float)SD_FFT_N;
    for (unsigned k = 0; k < SD_FFT_N; k++) {
        ctx->re[k] = (ctx->re[k] - mean_i) * ctx->window[k];
        ctx->im[k] = (ctx->im[k] - mean_q) * ctx->window[k];
    }
    sd_fft(ctx, ctx->re, ctx->im);
    for (unsigned s = 0; s < SD_FFT_N; s++) {
        unsigned k = (s + SD_FFT_N / 2) % SD_FFT_N;
        ctx->power_acc[s] += ctx->re[k] * ctx->re[k] + ctx->im[k] * ctx->im[k];
    }
    ctx->frames++;
}

bool sd_finish_db(sd_ctx_t *ctx, float *db_cols)
{
    if (!ctx->frames) return false;
    const float scale = 1.0f / (SD_NORM * SD_NORM * (float)ctx->frames);
    for (unsigned c = 0; c < SD_COLUMNS; c++) {
        float p = fmaxf(ctx->power_acc[2 * c], ctx->power_acc[2 * c + 1]) * scale;
        db_cols[c] = p > 1e-20f ? 10.0f * log10f(p) : SD_FLOOR_DB;
    }
    memset(ctx->power_acc, 0, sizeof(ctx->power_acc));
    ctx->frames = 0;
    return true;
}

/* Position relative de db dans [ref − range, ref] ; NaN → −1 */
static float sd_ratio(float db, float ref_db, float range_db)
{
    if (!(range_db > 0.0f) || isnan(db)) return -1.0f;
    return (db - (ref_db - range_db)) / range_db;
}

unsigned sd_db_to_height(float db, float ref_db, float range_db, unsigned height)
{
    float t = sd_ratio(db, ref_db, range_db);
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return height;
    return (unsigned)(t * (float)height + 0.5f);
}

uint8_t sd_db_to_color(float db, float ref_db, float range_db, uint8_t first, uint8_t count)
{
    if (!count) return first;
    float t = sd_ratio(db, ref_db, range_db);
    if (t <= 0.0f) return first;
    if (t >= 1.0f) return (uint8_t)(first + count - 1);
    unsigned i = (unsigned)(t * (float)count);
    return (uint8_t)(first + (i < count ? i : count - 1u));
}
```
- [ ] **Step 6: Lancer** `cmake --build build/host && ctest --test-dir build/host -R spectrum_dsp --output-on-failure` → tous `ok`.
- [ ] **Step 7: Écrire `test_app_controls.c` (échoue)**
```c
#include "app_controls.h"
#include "test_util.h"

static app_settings_t s;
static app_view_t v;

static int test_defaults(void)
{
    app_defaults(&s, &v, 30);
    CHECK(s.center_mhz == 2437 && s.rate == APP_RATE_40M && s.hardware_agc && s.gain_index == 30);
    CHECK_NEAR(v.ref_db, 0.0f, 0.0f);
    CHECK_NEAR(v.range_db, 80.0f, 0.0f);
    app_defaults(&s, &v, 100);
    CHECK(s.gain_index == 40);
    return 0;
}

static int test_freq_steps(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_1, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2438);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2448);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2438);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_1, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 2437);
    return 0;
}

static int test_freq_clamps(void)
{
    app_defaults(&s, &v, 100);
    s.center_mhz = 5995;
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 6000);
    CHECK(app_apply_cmd(APP_CMD_FREQ_UP_1, &s, &v, 100) == APP_CHANGE_NONE && s.center_mhz == 6000);
    s.center_mhz = 105;
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_10, &s, &v, 100) == APP_CHANGE_RADIO && s.center_mhz == 100);
    CHECK(app_apply_cmd(APP_CMD_FREQ_DOWN_1, &s, &v, 100) == APP_CHANGE_NONE && s.center_mhz == 100);
    return 0;
}

static int test_rate_cycle(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100) == APP_CHANGE_RADIO && s.rate == APP_RATE_80M);
    app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100);
    CHECK(s.rate == APP_RATE_16M);
    app_apply_cmd(APP_CMD_RATE_CYCLE, &s, &v, 100);
    CHECK(s.rate == APP_RATE_40M);
    CHECK(app_rate_msps(APP_RATE_16M) == 16 && app_rate_msps(APP_RATE_40M) == 40 && app_rate_msps(APP_RATE_80M) == 80);
    return 0;
}

static int test_gain(void)
{
    app_defaults(&s, &v, 41);
    CHECK(app_apply_cmd(APP_CMD_AGC_TOGGLE, &s, &v, 41) == APP_CHANGE_RADIO && !s.hardware_agc);
    CHECK(app_apply_cmd(APP_CMD_AGC_TOGGLE, &s, &v, 41) == APP_CHANGE_RADIO && s.hardware_agc);
    CHECK(app_apply_cmd(APP_CMD_GAIN_UP, &s, &v, 41) == APP_CHANGE_RADIO && !s.hardware_agc && s.gain_index == 41);
    CHECK(app_apply_cmd(APP_CMD_GAIN_UP, &s, &v, 41) == APP_CHANGE_NONE && s.gain_index == 41);
    s.gain_index = 0;
    CHECK(app_apply_cmd(APP_CMD_GAIN_DOWN, &s, &v, 41) == APP_CHANGE_NONE && s.gain_index == 0);
    return 0;
}

static int test_view(void)
{
    app_defaults(&s, &v, 100);
    CHECK(app_apply_cmd(APP_CMD_REF_UP, &s, &v, 100) == APP_CHANGE_VIEW);
    CHECK_NEAR(v.ref_db, 5.0f, 0.0f);
    v.ref_db = 20.0f;
    CHECK(app_apply_cmd(APP_CMD_REF_UP, &s, &v, 100) == APP_CHANGE_NONE);
    v.ref_db = -120.0f;
    CHECK(app_apply_cmd(APP_CMD_REF_DOWN, &s, &v, 100) == APP_CHANGE_NONE);
    CHECK(app_apply_cmd(APP_CMD_RANGE_DOWN, &s, &v, 100) == APP_CHANGE_VIEW);
    CHECK_NEAR(v.range_db, 70.0f, 0.0f);
    v.range_db = 20.0f;
    CHECK(app_apply_cmd(APP_CMD_RANGE_DOWN, &s, &v, 100) == APP_CHANGE_NONE);
    v.range_db = 120.0f;
    CHECK(app_apply_cmd(APP_CMD_RANGE_UP, &s, &v, 100) == APP_CHANGE_NONE);
    return 0;
}

int main(void)
{
    int failures = 0;
    RUN(test_defaults);
    RUN(test_freq_steps);
    RUN(test_freq_clamps);
    RUN(test_rate_cycle);
    RUN(test_gain);
    RUN(test_view);
    return failures ? 1 : 0;
}
```
- [ ] **Step 8: Vérifier l'échec** (build KO), puis **`include/app_controls.h`**
```c
/* Réglages radio/affichage et commandes clavier. C pur (testé sur PC). */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FREQ_MIN_MHZ 100u
#define APP_FREQ_MAX_MHZ 6000u
#define APP_REF_MIN_DB (-120.0f)
#define APP_REF_MAX_DB 20.0f
#define APP_REF_STEP_DB 5.0f
#define APP_RANGE_MIN_DB 20.0f
#define APP_RANGE_MAX_DB 120.0f
#define APP_RANGE_STEP_DB 10.0f

typedef enum { APP_RATE_16M, APP_RATE_40M, APP_RATE_80M, APP_RATE_COUNT } app_rate_t;
typedef struct { unsigned center_mhz; app_rate_t rate; bool hardware_agc; unsigned gain_index; } app_settings_t;
typedef struct { float ref_db; float range_db; } app_view_t;
typedef enum {
    APP_CMD_FREQ_UP_1, APP_CMD_FREQ_DOWN_1, APP_CMD_FREQ_UP_10, APP_CMD_FREQ_DOWN_10,
    APP_CMD_RATE_CYCLE, APP_CMD_AGC_TOGGLE, APP_CMD_GAIN_UP, APP_CMD_GAIN_DOWN,
    APP_CMD_REF_UP, APP_CMD_REF_DOWN, APP_CMD_RANGE_DOWN, APP_CMD_RANGE_UP
} app_cmd_t;
typedef enum { APP_CHANGE_NONE, APP_CHANGE_VIEW, APP_CHANGE_RADIO } app_change_t;

void app_defaults(app_settings_t *s, app_view_t *v, unsigned gain_max);
app_change_t app_apply_cmd(app_cmd_t cmd, app_settings_t *s, app_view_t *v, unsigned gain_max);
unsigned app_rate_msps(app_rate_t rate);

#ifdef __cplusplus
}
#endif
```
- [ ] **Step 9: `app_controls.c`**
```c
#include "app_controls.h"

void app_defaults(app_settings_t *s, app_view_t *v, unsigned gain_max)
{
    s->center_mhz = 2437;
    s->rate = APP_RATE_40M;
    s->hardware_agc = true;
    s->gain_index = gain_max < 40u ? gain_max : 40u;
    v->ref_db = 0.0f;
    v->range_db = 80.0f;
}

static app_change_t step_freq(app_settings_t *s, int delta)
{
    int f = (int)s->center_mhz + delta;
    if (f < (int)APP_FREQ_MIN_MHZ) f = (int)APP_FREQ_MIN_MHZ;
    if (f > (int)APP_FREQ_MAX_MHZ) f = (int)APP_FREQ_MAX_MHZ;
    if ((unsigned)f == s->center_mhz) return APP_CHANGE_NONE;
    s->center_mhz = (unsigned)f;
    return APP_CHANGE_RADIO;
}

static app_change_t step_view(float *value, float delta, float lo, float hi)
{
    float next = *value + delta;
    if (next < lo) next = lo;
    if (next > hi) next = hi;
    if (next == *value) return APP_CHANGE_NONE;
    *value = next;
    return APP_CHANGE_VIEW;
}

/* Toute action sur le gain passe en manuel */
static app_change_t step_gain(app_settings_t *s, int delta, unsigned gain_max)
{
    int g = (int)s->gain_index + delta;
    if (g < 0) g = 0;
    if (g > (int)gain_max) g = (int)gain_max;
    if ((unsigned)g == s->gain_index && !s->hardware_agc) return APP_CHANGE_NONE;
    s->gain_index = (unsigned)g;
    s->hardware_agc = false;
    return APP_CHANGE_RADIO;
}

app_change_t app_apply_cmd(app_cmd_t cmd, app_settings_t *s, app_view_t *v, unsigned gain_max)
{
    switch (cmd) {
    case APP_CMD_FREQ_UP_1: return step_freq(s, 1);
    case APP_CMD_FREQ_DOWN_1: return step_freq(s, -1);
    case APP_CMD_FREQ_UP_10: return step_freq(s, 10);
    case APP_CMD_FREQ_DOWN_10: return step_freq(s, -10);
    case APP_CMD_RATE_CYCLE:
        s->rate = s->rate == APP_RATE_16M ? APP_RATE_40M : s->rate == APP_RATE_40M ? APP_RATE_80M : APP_RATE_16M;
        return APP_CHANGE_RADIO;
    case APP_CMD_AGC_TOGGLE:
        s->hardware_agc = !s->hardware_agc;
        return APP_CHANGE_RADIO;
    case APP_CMD_GAIN_UP: return step_gain(s, 1, gain_max);
    case APP_CMD_GAIN_DOWN: return step_gain(s, -1, gain_max);
    case APP_CMD_REF_UP: return step_view(&v->ref_db, APP_REF_STEP_DB, APP_REF_MIN_DB, APP_REF_MAX_DB);
    case APP_CMD_REF_DOWN: return step_view(&v->ref_db, -APP_REF_STEP_DB, APP_REF_MIN_DB, APP_REF_MAX_DB);
    case APP_CMD_RANGE_DOWN: return step_view(&v->range_db, -APP_RANGE_STEP_DB, APP_RANGE_MIN_DB, APP_RANGE_MAX_DB);
    case APP_CMD_RANGE_UP: return step_view(&v->range_db, APP_RANGE_STEP_DB, APP_RANGE_MIN_DB, APP_RANGE_MAX_DB);
    }
    return APP_CHANGE_NONE;
}

unsigned app_rate_msps(app_rate_t rate)
{
    return rate == APP_RATE_16M ? 16u : rate == APP_RATE_80M ? 80u : 40u;
}
```
Note `test_gain` : 1er `GAIN_UP` depuis AGC avec gain 40 → 41 (manuel) ; 2e → borné 41 et déjà manuel → `NONE`. `GAIN_DOWN` à 0 en manuel → `NONE`.
- [ ] **Step 10: Lancer tous les tests** `cmake --build build/host && ctest --test-dir build/host --output-on-failure` → 2/2 passent.
- [ ] **Step 11: CMake IDF des composants**
`components/spectrum_dsp/CMakeLists.txt` :
```cmake
idf_component_register(SRCS "spectrum_dsp.c" INCLUDE_DIRS "include")
```
`components/app_controls/CMakeLists.txt` :
```cmake
idf_component_register(SRCS "app_controls.c" INCLUDE_DIRS "include")
```

**Fin du batch (coordinateur)** : relire les 3 diffs, relancer les vérifs (host tests, 2 builds IDF), validations carte Task 3 Step 6 puis Task 4 Step 6, puis commits séparés :
`feat(fabgl): smoke test VGA16 512x384 + patches IDF 6.2` · `feat(espsdr_rx): backend RX ESP32 extrait d'esp-sdr + smoke test` · `feat(dsp): spectrum_dsp et app_controls testés sur PC`.

---

### Task 6: Intégration de l'application (coordinateur, séquentiel)

**Files:** Create `main/CMakeLists.txt`, `main/sdr_task.h`, `main/sdr_task.c`, `main/ui_render.h`, `main/ui_render.cpp`, `main/input_ps2.h`, `main/input_ps2.cpp`, `main/app_main.cpp`.

**Interfaces:** Consumes Tasks 3/4/5. Produces le firmware `build/vga32_sdr.bin`.

- [ ] **Step 1: `main/CMakeLists.txt`**
```cmake
idf_component_register(SRCS "app_main.cpp" "sdr_task.c" "ui_render.cpp" "input_ps2.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES FabGL espsdr_rx spectrum_dsp app_controls)
```
- [ ] **Step 2: `sdr_task.h`**
```c
/* Tâche SDR (core 0) : capture → FFT → moyennage ; publie la dernière trame */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_controls.h"
#include "spectrum_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDR_AVERAGES 8u

typedef struct {
    float db[SD_COLUMNS];
    app_settings_t settings;   /* réglages effectifs pour cette trame */
    uint32_t seq;
    uint32_t capture_errors;   /* cumulé depuis le boot */
    unsigned capture_us;       /* durée de la dernière capture réussie */
} spectrum_frame_t;

void sdr_task_start(const app_settings_t *initial);
void sdr_task_request(const app_settings_t *settings);
bool sdr_task_get_frame(spectrum_frame_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
```
- [ ] **Step 3: `sdr_task.c`**
```c
#include "sdr_task.h"
#include "esp_log.h"
#include "espsdr_rx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SDR_CORE 0 /* core 1 réservé à l'ISR vidéo FabGL et à l'UI (aussi valable en unicore) */

static const char *TAG = "sdr_task";
static sd_ctx_t s_dsp;
static spectrum_frame_t s_work, s_shared;
static app_settings_t s_requested;
static uint32_t s_requested_version;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_frame_ready;

static espsdr_rate_t to_hw_rate(app_rate_t rate)
{
    return rate == APP_RATE_16M ? ESPSDR_RATE_16M : rate == APP_RATE_80M ? ESPSDR_RATE_80M : ESPSDR_RATE_40M;
}

static void apply_settings(const app_settings_t *want, const app_settings_t *active, bool first)
{
    if (first || want->center_mhz != active->center_mhz) {
        esp_err_t e = espsdr_rx_tune(want->center_mhz);
        if (e != ESP_OK) ESP_LOGE(TAG, "tune %u MHz : %s", want->center_mhz, esp_err_to_name(e));
    }
    espsdr_rx_set_gain(want->hardware_agc, want->gain_index);
}

static void sdr_task(void *arg)
{
    (void)arg;
    app_settings_t active = {0};
    uint32_t applied_version = 0, errors = 0, seq = 0;
    bool first = true;
    sd_init(&s_dsp);
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        app_settings_t want = s_requested;
        uint32_t version = s_requested_version;
        xSemaphoreGive(s_lock);
        if (first || version != applied_version) {
            apply_settings(&want, &active, first);
            active = want;
            applied_version = version;
            first = false;
        }
        unsigned us = 0;
        for (unsigned k = 0; k < SDR_AVERAGES; k++) {
            const uint32_t *words;
            unsigned elapsed;
            if (espsdr_rx_capture(SD_FFT_N, to_hw_rate(active.rate), &words, &elapsed) == ESP_OK) {
                sd_accumulate(&s_dsp, words);  /* avant la capture suivante : words est écrasé */
                us = elapsed;
            } else {
                errors++;
            }
        }
        if (sd_finish_db(&s_dsp, s_work.db)) {
            s_work.settings = active;
            s_work.seq = ++seq;
            s_work.capture_errors = errors;
            s_work.capture_us = us;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_shared = s_work;
            xSemaphoreGive(s_lock);
            xSemaphoreGive(s_frame_ready);
        }
        vTaskDelay(1); /* laisse tourner l'idle task du core 0 (task watchdog) */
    }
}

void sdr_task_start(const app_settings_t *initial)
{
    s_lock = xSemaphoreCreateMutex();
    s_frame_ready = xSemaphoreCreateBinary();
    configASSERT(s_lock && s_frame_ready);
    s_requested = *initial;
    BaseType_t ok = xTaskCreatePinnedToCore(sdr_task, "sdr", 4096, NULL, 2, NULL, SDR_CORE);
    configASSERT(ok == pdPASS);
}

void sdr_task_request(const app_settings_t *settings)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_requested = *settings;
    s_requested_version++;
    xSemaphoreGive(s_lock);
}

bool sdr_task_get_frame(spectrum_frame_t *out, uint32_t timeout_ms)
{
    if (xSemaphoreTake(s_frame_ready, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_shared;
    xSemaphoreGive(s_lock);
    return true;
}
```
- [ ] **Step 4: `ui_render.h`**
```cpp
// Rendu FabGL : bandeau texte, spectre en barres, waterfall défilant
#pragma once
#include "fabgl.h"
#include "app_controls.h"
#include "sdr_task.h"

void ui_begin(fabgl::VGA16Controller &vga, fabgl::Canvas &canvas);
void ui_draw_frame(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok);
```
- [ ] **Step 5: `ui_render.cpp`**
```cpp
#include "ui_render.h"
#include <cstdio>
#include <cstring>

namespace {

constexpr int UI_W = 512;
constexpr int UI_H = 384;
constexpr int SPEC_Y0 = 20;
constexpr int SPEC_H = 128;
constexpr int WF_Y0 = 152;
constexpr uint8_t PAL_BG = 0, PAL_GRID = 1, PAL_TRACE = 2, PAL_TEXT = 3;
constexpr uint8_t PAL_WF_FIRST = 4, PAL_WF_COUNT = 12;

const RGB888 kColormap[PAL_WF_COUNT] = {
  {0, 0, 32}, {0, 0, 96}, {0, 0, 170}, {0, 64, 255}, {0, 160, 255}, {0, 255, 200},
  {0, 255, 64}, {160, 255, 0}, {255, 255, 0}, {255, 160, 0}, {255, 64, 0}, {255, 255, 255},
};

fabgl::VGA16Controller *s_vga;
fabgl::Canvas *s_canvas;

// VGA16 : nibble haut = pixel pair
inline void put4(uint8_t *row, int x, uint8_t c)
{
  uint8_t &b = row[x >> 1];
  b = (x & 1) ? ((b & 0xF0) | c) : ((b & 0x0F) | (c << 4));
}

void draw_header(const spectrum_frame_t &f, const app_view_t &v, bool keyboard_ok)
{
  char line[72];
  const app_settings_t &s = f.settings;
  if (s.hardware_agc)
    snprintf(line, sizeof line, "%4u MHz %2u MS/s AGC     ref %+4d rng %3d err %-6lu %s",
             s.center_mhz, app_rate_msps(s.rate), (int) v.ref_db, (int) v.range_db,
             (unsigned long) f.capture_errors, keyboard_ok ? "    " : "KBD?");
  else
    snprintf(line, sizeof line, "%4u MHz %2u MS/s G%-3u    ref %+4d rng %3d err %-6lu %s",
             s.center_mhz, app_rate_msps(s.rate), s.gain_index, (int) v.ref_db, (int) v.range_db,
             (unsigned long) f.capture_errors, keyboard_ok ? "    " : "KBD?");
  s_canvas->drawText(0, 2, line);
  const float half = app_rate_msps(s.rate) / 2.0f;
  char edge[12];
  snprintf(edge, sizeof edge, "%-7.1f", s.center_mhz - half);
  s_canvas->drawText(0, 11, edge);
  snprintf(edge, sizeof edge, "%4u", s.center_mhz);
  s_canvas->drawText(UI_W / 2 - 16, 11, edge);
  snprintf(edge, sizeof edge, "%7.1f", s.center_mhz + half);
  s_canvas->drawText(UI_W - 56, 11, edge);
}

void draw_spectrum(const spectrum_frame_t &f, const app_view_t &v)
{
  constexpr uint8_t bg = (PAL_BG << 4) | PAL_BG;
  for (int y = SPEC_Y0; y < SPEC_Y0 + SPEC_H; ++y)
    memset(s_vga->getScanline(y), bg, UI_W / 2);
  for (int x = 0; x < UI_W; ++x) {
    unsigned h = sd_db_to_height(f.db[x], v.ref_db, v.range_db, SPEC_H);
    for (unsigned k = 0; k < h; ++k)
      put4(s_vga->getScanline(SPEC_Y0 + SPEC_H - 1 - (int) k), x, k + 1 == h ? PAL_TRACE : PAL_GRID);
  }
  for (int y = SPEC_Y0; y < SPEC_Y0 + SPEC_H; y += 4)
    put4(s_vga->getScanline(y), UI_W / 2, PAL_TEXT);
}

void draw_waterfall_line(const spectrum_frame_t &f, const app_view_t &v)
{
  uint8_t *row = s_vga->getScanline(WF_Y0);
  for (int x = 0; x < UI_W; ++x)
    put4(row, x, sd_db_to_color(f.db[x], v.ref_db, v.range_db, PAL_WF_FIRST, PAL_WF_COUNT));
}

} // namespace

void ui_begin(fabgl::VGA16Controller &vga, fabgl::Canvas &canvas)
{
  s_vga = &vga;
  s_canvas = &canvas;
  vga.setPaletteItem(PAL_BG, RGB888(0, 0, 0));
  vga.setPaletteItem(PAL_GRID, RGB888(0, 96, 96));
  vga.setPaletteItem(PAL_TRACE, RGB888(255, 255, 0));
  vga.setPaletteItem(PAL_TEXT, RGB888(255, 255, 255));
  for (int i = 0; i < PAL_WF_COUNT; ++i)
    vga.setPaletteItem(PAL_WF_FIRST + i, kColormap[i]);
  canvas.setBrushColor(RGB888(0, 0, 0));
  canvas.setPenColor(RGB888(255, 255, 255));
  canvas.clear();
  canvas.selectFont(&fabgl::FONT_8x8);
  canvas.setGlyphOptions(GlyphOptions().FillBackground(true));
  canvas.setScrollingRegion(0, WF_Y0, UI_W - 1, UI_H - 1);
  canvas.waitCompletion();
}

void ui_draw_frame(const spectrum_frame_t &frame, const app_view_t &view, bool keyboard_ok)
{
  s_canvas->scroll(0, 1);            // waterfall vers le bas (permutation de pointeurs de lignes)
  draw_header(frame, view, keyboard_ok);
  s_canvas->waitCompletion();        // scroll + texte exécutés avant les écritures brutes
  draw_spectrum(frame, view);
  draw_waterfall_line(frame, view);
}
```
Rappel : `getScanline(y)` est relu à chaque trame car `scroll` permute les pointeurs.
- [ ] **Step 6: `input_ps2.h` / `input_ps2.cpp`**
```cpp
// Clavier PS/2 → commandes app_controls (non bloquant)
#pragma once
#include "fabgl.h"
#include "app_controls.h"

struct input_result_t {
  bool radio_changed;
};

input_result_t input_poll(fabgl::Keyboard *kbd, app_settings_t *s, app_view_t *v, unsigned gain_max);
```
```cpp
#include "input_ps2.h"

namespace {

bool map_key(const fabgl::VirtualKeyItem &k, app_cmd_t *cmd)
{
  switch (k.vk) {
    case fabgl::VK_RIGHT:    *cmd = k.SHIFT ? APP_CMD_FREQ_UP_10 : APP_CMD_FREQ_UP_1; return true;
    case fabgl::VK_LEFT:     *cmd = k.SHIFT ? APP_CMD_FREQ_DOWN_10 : APP_CMD_FREQ_DOWN_1; return true;
    case fabgl::VK_UP:       *cmd = APP_CMD_REF_UP; return true;
    case fabgl::VK_DOWN:     *cmd = APP_CMD_REF_DOWN; return true;
    case fabgl::VK_PAGEUP:   *cmd = APP_CMD_GAIN_UP; return true;
    case fabgl::VK_PAGEDOWN: *cmd = APP_CMD_GAIN_DOWN; return true;
    case fabgl::VK_r:
    case fabgl::VK_R:        *cmd = APP_CMD_RATE_CYCLE; return true;
    case fabgl::VK_g:
    case fabgl::VK_G:        *cmd = APP_CMD_AGC_TOGGLE; return true;
    case fabgl::VK_PLUS:
    case fabgl::VK_KP_PLUS:
    case fabgl::VK_EQUALS:   *cmd = APP_CMD_RANGE_DOWN; return true;
    case fabgl::VK_MINUS:
    case fabgl::VK_KP_MINUS: *cmd = APP_CMD_RANGE_UP; return true;
    default:                 return false;
  }
}

} // namespace

input_result_t input_poll(fabgl::Keyboard *kbd, app_settings_t *s, app_view_t *v, unsigned gain_max)
{
  input_result_t r = {false};
  if (!kbd)
    return r;
  fabgl::VirtualKeyItem item;
  while (kbd->getNextVirtualKey(&item, 0)) {
    app_cmd_t cmd;
    if (!item.down || !map_key(item, &cmd))
      continue;
    if (app_apply_cmd(cmd, s, v, gain_max) == APP_CHANGE_RADIO)
      r.radio_changed = true;
  }
  return r;
}
```
(Retirer de `map_key` tout nom VK signalé absent par l'Agent A, Task 3 Step 5.)
- [ ] **Step 7: `app_main.cpp`**
```cpp
// Analyseur de spectre VGA32 : radio (esp-sdr) → DSP → FabGL VGA16 512x384, clavier PS/2
#include <cstdlib>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "fabgl.h"
#include "espsdr_rx.h"
#include "input_ps2.h"
#include "sdr_task.h"
#include "ui_render.h"

#if CONFIG_FREERTOS_UNICORE
#define UI_CORE 0
#else
#define UI_CORE 1 // même core que l'ISR vidéo, le core 0 calcule la DSP
#endif

namespace {

constexpr const char *TAG = "app";
// Framebuffer 98 304 o + descripteurs DMA (~12 Ko) + FABGLIB_MINFREELARGESTBLOCK (40 000 o)
constexpr size_t APP_MIN_DMA_FREE = 160000;

fabgl::PS2Controller s_ps2;
fabgl::VGA16Controller s_vga;
spectrum_frame_t s_frame;

void log_heap(const char *stage)
{
  ESP_LOGI(TAG, "%s : DMA libre %u (plus grand bloc %u)", stage,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void ui_task(void *)
{
  static fabgl::Canvas canvas(&s_vga);
  ui_begin(s_vga, canvas);
  const unsigned gain_max = espsdr_rx_gain_max();
  app_settings_t settings;
  app_view_t view;
  app_defaults(&settings, &view, gain_max);
  sdr_task_start(&settings);
  for (;;) {
    fabgl::Keyboard *kbd = s_ps2.keyboard();
    if (input_poll(kbd, &settings, &view, gain_max).radio_changed)
      sdr_task_request(&settings);
    if (sdr_task_get_frame(&s_frame, 50))
      ui_draw_frame(s_frame, view, kbd && kbd->isKeyboardAvailable());
  }
}

} // namespace

extern "C" void app_main()
{
  log_heap("boot");
  ESP_ERROR_CHECK(espsdr_rx_init()); // avant FabGL : le Wi-Fi alloue ses buffers internes en premier
  log_heap("après radio");
  s_ps2.begin(PS2Preset::KeyboardPort0);
  if (heap_caps_get_free_size(MALLOC_CAP_DMA) < APP_MIN_DMA_FREE) {
    ESP_LOGE(TAG, "RAM DMA insuffisante pour la VGA 512x384 (voir docs/ARCHITECTURE.md, repli RAM)");
    abort();
  }
  s_vga.begin();
  s_vga.setResolution(VGA_512x384_60Hz);
  log_heap("après VGA");
  BaseType_t ok = xTaskCreatePinnedToCore(ui_task, "ui", 4096, nullptr, 3, nullptr, UI_CORE);
  configASSERT(ok == pdPASS);
}
```
(Si le *Repli RAM* a été activé en Task 4 : `setResolution(VGA_512x384_60Hz, 512, 320)`, `UI_H = 320` et ajuster `APP_MIN_DMA_FREE` au besoin.)
- [ ] **Step 8: Build**
```bash
. ~/esp/esp-idf-6.2/export.sh && tools/apply_patches.sh
idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig set-target esp32
idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig build
```
(`build/app` et non `build/` : `build/` contient aussi `host/`, `rx_smoke/`, `fabgl_smoke/`, `spike-*`.) Attendu : succès, aucun warning dans `main/` et `components/`, garde `sram_guard.ld` OK. `idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig size` → noter la DRAM utilisée.
- [ ] **Step 9: Host tests toujours verts** `cmake --build build/host && ctest --test-dir build/host --output-on-failure`.
- [ ] **Step 10: Commit** `git add main && git commit -m "feat: app spectre + waterfall VGA16 512x384 avec contrôle PS/2"`

---

### Task 7: Validation sur la carte + documentation (coordinateur + utilisateur)

**Files:** Create `docs/ARCHITECTURE.md`, `docs/HARDWARE.md` ; Modify `README.md`, `CLAUDE.md`, `docs/spike-phase0.md` (résultats finaux).

- [ ] **Step 1: Flash** `idf.py -B build/app -D SDKCONFIG=build/app/sdkconfig -p /dev/ttyUSB0 flash monitor` → logs `boot`, `après radio`, `après VGA` sans abort.
- [ ] **Step 2: Signal connu** — hotspot téléphone forcé canal 6 (2437 MHz), clavier et écran branchés : bosse d'environ 20 MHz centrée sur le repère, visible dans le waterfall ; `err` reste à 0 pendant 5 minutes ; image stable, sans décrochage.
- [ ] **Step 3: Contrôles** — ←/→, Shift+←/→ : la bosse se décale de 1/10 MHz ; R : la largeur de bande affichée change (16/40/80) ; G / PgUp / PgDn : `AGC` ↔ `Gnn` ; ↑/↓ et +/− modifient le niveau et le contraste.
- [ ] **Step 4: Review Focus** — boot sans clavier → l'app tourne et le bandeau affiche `KBD?` ; Shift+→ maintenu jusqu'à 6000 → bornes respectées, pas de crash (hors 2,4 GHz : expérimental).
- [ ] **Step 5: Docs** — `docs/ARCHITECTURE.md` : schéma des tâches/cores, flux de données, budget RAM mesuré, replis. `docs/HARDWARE.md` : pinout VGA32 (R 21/22, G 18/19, B 4/5, HSync 23, VSync 15, PS/2 kbd 32/33, UART0 1/3), version de carte, réserve SRAM 0x3ffe8000–0x3fff8000. Mettre à jour `README.md` (touches, mesures) et `CLAUDE.md` (pointeurs `@docs/ARCHITECTURE.md`, `@docs/HARDWARE.md`).
- [ ] **Step 6: Revue finale** — superpowers:requesting-code-review sur toute la branche, puis commit `docs: architecture, hardware, résultats de validation`.

---

## Vérification end-to-end (résumé)

1. `ctest --test-dir build/host --output-on-failure` → spectrum_dsp + app_controls verts.
2. `idf.py build` (app) + builds `tests/target/{rx_smoke,fabgl_smoke}` sans erreur.
3. Spike (Task 2) : `ok=50/50` unicore et dual-core (ou repli documenté).
4. Carte : rx_smoke `ok=100 fail=0` ; fabgl_smoke affiche barres + défilement + touches ; app : bosse Wi-Fi canal 6 à 2437 MHz, `err 0` pendant 5 min, contrôles opérationnels, `KBD?` sans clavier.
