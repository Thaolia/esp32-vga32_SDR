# Spike phase 0 — esp-sdr stock sur TTGO VGA32 (2026-10-06)

**Question :** la capture I/Q d'esp-sdr fonctionne-t-elle sur cette carte avec ESP-IDF 6.2, en unicore puis en dual-core ?

## Environnement

- ESP-IDF `25fe69f946311abdaf9ad56591f25fedbc20ac98` (`esp_idf_version.h` : 6.2.0 ; `git describe` : v6.1-dev-6376)
- esp-sdr `fb264f96226e6838ba323d97801ccd28c752e4e0` (sous-module esp-dsp initialisé : requis par le firmware stock uniquement)
- Carte : ESP32-PICO-D4 revision v1.1, quartz 40 MHz, USB-série CH9102 (`1a86:55d4`) → **`/dev/ttyACM0`**
- Builds hors arbre : `build/spike-uni`, `build/spike-dual` ; sous-module esp-sdr resté propre

## Résultats (`tools/spike_capture.py`, `FREQ 2437`, `CAP16 1024 1` = 40 MS/s)

| Config | Captures OK | En-têtes KO | CRC KO | Durée capture | RMS (8 bits) |
|---|---|---|---|---|---|
| Unicore (stock) | 50/50 | 0 | 0 | 26 µs | 4.8 |
| Dual-core (`# CONFIG_FREERTOS_UNICORE is not set`) | 200/200 | 0 | 0 | 27 µs | 18.6 |

Le RMS varie avec l'AGC et l'environnement RF ; il n'est pas comparable entre les deux runs.

## Décision

Design nominal **dual-core** retenu. Pas de repli unicore, pas de patch FabGL `0002`.

## Notes

- La convention CRC du protocole est `zlib.crc32` (identique à `third_party/esp-sdr/tools/*.py`).
- Avec `idf.py -C <projet>`, les chemins `-B` et `-D SDKCONFIG=` doivent être **absolus** : sinon ils sont résolus relativement au projet (risque d'écrire dans le sous-module).
