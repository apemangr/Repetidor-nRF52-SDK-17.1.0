#ifndef TIMERS_H
#define TIMERS_H

#include <stdint.h>
#include <stdlib.h>

#include "app_error.h"  // Para APP_ERROR_CHECK
#include "app_timer.h"  // Necesario para todas las funciones de app_timer
#include "bsp.h"
#include "app_nus_server.h"
#include "nrf_log.h"  // Para logging dentro de los handlers y la inicialización
#include "nrf_log_ctrl.h"  // Necesario para NRF_LOG_INFO, etc.
#include "sdk_errors.h"
#include "variables.h"
#include "filesystem.h"

ret_code_t timers_app_init(void);
ret_code_t timers_start_cycle(void);

extern volatile bool device_on;

void load_timers_from_flash(void);
// static uint32_t on_duration_ticks;
// static uint32_t sleep_duration_ticks;



#endif  // TIMERS_H