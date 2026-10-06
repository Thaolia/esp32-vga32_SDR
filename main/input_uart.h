/* Contrôle par le port série de la console (idf.py monitor), en parallèle du PS/2 */
#pragma once
#include <stdbool.h>
#include "app_controls.h"

#ifdef __cplusplus
extern "C" {
#endif

void input_uart_begin(void);
/* Non bloquant ; true si un réglage radio a changé */
bool input_uart_poll(app_settings_t *s, app_view_t *v, unsigned gain_max);

#ifdef __cplusplus
}
#endif
