#ifndef __APP_NUS_SERVER_H
#define __APP_NUS_SERVER_H

#include "ble.h"
#include "filesystem.h"
#include "nrf.h"
#include <stdint.h>

typedef void (*app_nus_server_on_data_received_t)(const uint8_t *data_ptr, uint16_t data_length);
uint32_t   app_nus_server_send_data(const uint8_t *data_array, uint16_t length);
void       app_nus_server_ble_evt_handler(ble_evt_t const *p_ble_evt);
void       app_nus_server_init(app_nus_server_on_data_received_t on_data_received);
ret_code_t send_configuration(config_t const *config_repetidor);
ret_code_t send_configuration_nus(config_t const *config_repetidor);
void       check_and_restart_advertising(void);
void       advertising_stop(void);
void       advertising_init(void);
void       advertising_update_data(void);
void       advertising_start(void);
void       diagnose_nus_connection(void);
void       disconnect_all_devices(void);

#endif
