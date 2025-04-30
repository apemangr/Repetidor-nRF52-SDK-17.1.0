#ifndef TIMERS_H
#define TIMERS_H

#include <stdint.h>

#include "app_error.h"  // Para APP_ERROR_CHECK
#include "app_timer.h"  // Necesario para todas las funciones de app_timer
#include "bsp.h"
#include "nrf_log.h"  // Para logging dentro de los handlers y la inicialización
#include "nrf_log_ctrl.h"  // Necesario para NRF_LOG_INFO, etc.
#include "sdk_errors.h"
#include "variables.h"

ret_code_t timers_app_init(void);
ret_code_t timers_start_cycle(void);
static bool in_sleep_mode = false;

#endif  // TIMERS_H