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

/**
 * @brief Inicializa el manejador de botones
 * 
 * Esta funcion configura el boton y registra el manejador de eventos
 */
void button_handler_init(void);

/**
 * @brief Funcion de callback que se ejecuta cuando se presiona el boton
 * 
 * @param pin_no Numero del pin que genero el evento
 * @param button_action Accion del boton (presionado/liberado)
 */
void button_event_handler(uint8_t pin_no, uint8_t button_action);

#endif // BUTTON_H
