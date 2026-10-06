/* Décodeur d'octets terminal (idf.py monitor) → commandes app_cmd_t. C pur (testé sur PC). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "app_controls.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SERIAL_MAX_PARAMS 4u

typedef struct {
    uint8_t state;
    uint8_t len;
    char params[APP_SERIAL_MAX_PARAMS];
} app_serial_t;

void app_serial_init(app_serial_t *d);
/* true si l'octet termine une commande, écrite dans *cmd */
bool app_serial_feed(app_serial_t *d, uint8_t byte, app_cmd_t *cmd);

#ifdef __cplusplus
}
#endif
