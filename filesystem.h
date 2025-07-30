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
#include "nrf_log_ctrl.h"
#include "variables.h"

// Macro para convertir bytes a words (32-bit words)
#define BYTES_TO_WORDS(bytes) (((bytes) + 3) / 4)

// Estructura de guardado de historiales
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

typedef struct
{
    uint16_t V1;       // Voltaje 1
    uint16_t V2;       // Voltaje 2
    uint32_t contador; // Contador de advertisings
} adc_values_t;

extern adc_values_t adc_values;

typedef enum
{
    TIEMPO_ENCENDIDO, // Tiempo de encendido
    TIEMPO_SLEEP      // Tiempo de apagado
} valor_type_t;

static uint8_t mac_address_from_flash[6] = {0};

// History functions
ret_code_t save_history_record_emisor(store_history const *p_history_data, uint16_t offset);
ret_code_t save_history_record(store_history const *p_history_data);
ret_code_t read_history_record_by_id(uint16_t record_id, store_history *p_history_data);
void       print_history_record(store_history const *p_record, const char *p_title);
ret_code_t read_last_history_record(store_history *p_history_data);

ret_code_t send_all_history_ble(void);
void       history_send_next_packet(void);
bool       history_send_is_active(void);
uint32_t   history_get_progress(void);

// Date and time functions
ret_code_t write_date_to_flash(const datetime_t *p_date);
datetime_t read_date_from_flash(void);
void       write_time_to_flash(valor_type_t valor_type, uint32_t valor);
uint32_t   read_time_from_flash(valor_type_t valor_type, uint32_t default_valor);
void       load_mac_from_flash(uint8_t *mac_out);
void       save_mac_to_flash(uint8_t *mac_addr);

// typedef struct
// {
//   uint8_t company[2];
//   uint8_t total_reset[4];
//   uint8_t total_adv[4];
//   bool Enable_Custom_mac;
//   uint8_t mac_custom[6];
//   uint8_t sleep_time[3];
//   uint8_t adv_time[2];
//   uint8_t mac_original[6];
//   uint8_t Type_resistor;
//   uint8_t Type_sensor;
//   uint8_t Type_battery;
//   uint8_t offset_plate_bolt; // distancia desde sensor al
//   uint8_t Version[3];
//   uint8_t Offset_sensor_cut;
//   uint8_t Offset_sensor_bolt;
//   uint8_t Offset_sensor_measure;
//   uint8_t Sending_Position;
//   uint8_t type_advertising;
//   uint8_t Sensibility;
//   uint8_t fecha[6];
//   uint8_t hora[6];
//   uint8_t reset[14];
//   uint8_t second;
//   uint8_t minute;
//   uint8_t hour;
//   uint8_t date;
//   uint8_t month;
//   uint16_t year;
//   uint16_t last_history;
// } store_flash;

// extern store_flash Flash_array;

#endif // FILESYSTEM_H