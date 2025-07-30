#ifndef __APP_NUS_SERVER_H
#define __APP_NUS_SERVER_H

#include "ble.h"
#include "filesystem.h"
#include "nrf.h"
#include <stdint.h>

typedef void (*app_nus_server_on_data_received_t)(const uint8_t *data_ptr, uint16_t data_length);
uint32_t app_nus_server_send_data(const uint8_t *data_array, uint16_t length);
void     app_nus_server_ble_evt_handler(ble_evt_t const *p_ble_evt);
void     app_nus_server_init(app_nus_server_on_data_received_t on_data_received);
void     advertising_stop(void);
void     advertising_init(void);
void     advertising_start(void);
void     disconnect_all_devices(void);
uint16_t get_conn_handle(void);

#endif