#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fds.h"
#include "nrf_log.h"
#include "variables.h"

// Enumeración para diferenciar entre tiempo de encendido y apagado
typedef enum
{
	TIEMPO_ENCENDIDO,  // Tiempo de encendido
	TIEMPO_SLEEP       // Tiempo de apagado
} valor_type_t;

#endif  // FILESYSTEM_H