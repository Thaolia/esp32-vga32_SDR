# Matériel

## Carte de référence (validée)

- TTGO VGA32 — **ESP32-PICO-D4 revision v1.1**, quartz 40 MHz, flash 4 MB, pas de PSRAM
- USB-série **QinHeng CH9102** (`1a86:55d4`, pilote `cdc_acm`) → **`/dev/ttyACM0`**
  (lien stable : `/dev/serial/by-id/usb-1a86_USB_Single_Serial_5C8C006648-if00`)
- Ouvrir le port sans reset : `dtr = False`, `rts = False` **avant** `open()` (RTS pilote EN).

## Brochage utilisé (défauts FabGL)

| Fonction | GPIO |
|---|---|
| VGA R1 / R0 | 22 / 21 |
| VGA G1 / G0 | 19 / 18 |
| VGA B1 / B0 | 5 / 4 |
| HSync / VSync | 23 / 15 |
| PS/2 clavier CLK / DAT | 33 / 32 |
| PS/2 souris CLK / DAT (non utilisé) | 26 / 27 |
| Console UART0 TX / RX | 1 / 3 |

## Radio

- Capture I/Q via le chemin de dump MAC (`DUMP_CTRL 0x60033d90`), SRAM `0x3ffe8000–0x3fff8000` réservée
  (`SOC_RESERVE_MEMORY_REGION`) et prêtée au MAC pendant chaque capture.
- Horloges : 16 / 40 / 80 MS/s (sélecteurs 0 / 2 / 1). 1024 échantillons ≈ 27 µs à 40 MS/s.
- Échantillon : I signé bits 0–9, Q signé bits 10–19.
- `gain_max` lu sur cette carte : 72.
- Le Wi-Fi et le Bluetooth ne sont pas utilisables en parallèle.

## Clavier PS/2

FabGL attend un clavier qui parle le protocole PS/2. Un clavier USB derrière un **adaptateur passif**
USB→PS/2 ne fonctionne que s'il est « combo » USB/PS/2. Mesure faite le 2026-10-06 avec le clavier
USB + adaptateur de l'utilisateur : lignes au repos à 1, **0 front d'horloge** pendant la frappe, sur
les deux prises → clavier non compatible. Le contrôle par le terminal série (`idf.py monitor`) remplace
le clavier dans ce cas.

## Validation de bout en bout (2026-10-06, carte de référence)

| Vérification | Résultat |
|---|---|
| Boot sans clavier PS/2 compatible | OK, viewport 384 lignes, aucun log d'erreur pendant 30 s |
| Affichage bandeau / spectre / waterfall | OK |
| Contrôle série : Shift+→, `r`, `g` | Bandeau `2447 MHz 80 MS/s G40` |
| Compteur `err` après plusieurs minutes | 0 |
| Hotspot téléphone | Bosse visible dans le spectre et le waterfall |
| Contrôle PS/2 | Non validé : pas de clavier PS/2 compatible disponible |
