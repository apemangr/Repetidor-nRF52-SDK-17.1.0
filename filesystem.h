#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "calendar.h"
#include "fds.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "variables.h"

// Estructura de guardado
typedef struct
{
    uint16_t magic;
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint32_t contador;
    uint16_t V1;
    uint16_t V2;
    uint16_t V3;
    uint16_t V4;
    uint16_t V5;
    uint16_t V6;
    uint16_t V7;
    uint16_t V8;
    uint8_t  temp;
    uint8_t  battery;
} store_history;

typedef enum
{
    TIEMPO_ENCENDIDO, // Tiempo de encendido
    TIEMPO_SLEEP      // Tiempo de apagado
} valor_type_t;

static uint8_t mac_address_from_flash[6] = {0};
ret_code_t     save_history_record(store_history const *p_history_data);
ret_code_t     read_history_record_by_id(uint16_t record_id, store_history *p_history_data);
void           print_history_record(store_history const *p_record, const char *p_title);
ret_code_t     read_last_history_record(store_history *p_history_data);
void           fds_history_example_run(void);
ret_code_t     write_date_to_flash(const datetime_t *p_date);
datetime_t     read_date_from_flash(void);
void           write_time_to_flash(valor_type_t valor_type, uint32_t valor);
uint32_t       read_time_from_flash(valor_type_t valor_type, uint32_t default_valor);
void           load_mac_from_flash(uint8_t *mac_out);
void           save_mac_to_flash(uint8_t *mac_addr);

#endif // FILESYSTEM_H