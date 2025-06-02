#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "calendar.h"
#include "fds.h"
#include "nrf_log.h"
#include "variables.h"

typedef enum
{
    TIEMPO_ENCENDIDO,  // Tiempo de encendido
    TIEMPO_SLEEP       // Tiempo de apagado
} valor_type_t;

ret_code_t write_date_to_flash(const char *fecha_str);
datetime_t read_date_from_flash();

#endif  // FILESYSTEM_H