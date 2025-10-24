#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>
#include "app_button.h"
#include "nrf_gpio.h"
#include "bsp.h"
#include "nrf_log.h"

// Definir el pin del boton usando BSP_BUTTON_0
#define LEDBUTTON_BUTTON_PIN BSP_BUTTON_0


void button_handler_init(void);


void button_event_handler(uint8_t pin_no, uint8_t button_action);

#endif // BUTTON_H
