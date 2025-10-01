#include "button.h"
#include "app_button.h"
#include "app_error.h"
#include "app_timer.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_delay.h"
#include "variables.h"

// Configuracion del boton - usar constante directa para evitar problemas de dependencias
#define BUTTON_DETECTION_DELAY    50    // 50ms de debounce

// Array de configuracion de botones
static app_button_cfg_t m_buttons[] = 
{
    {LEDBUTTON_BUTTON_PIN, false, BUTTON_PULL, button_event_handler}
};

void button_event_handler(uint8_t pin_no, uint8_t button_action)
{
    // Solo procesar cuando el boton es presionado (no cuando se libera)
    if (button_action == APP_BUTTON_PUSH)
    {
        if (pin_no == LEDBUTTON_BUTTON_PIN)
        {
            // Mostrar mensaje de reinicio cuando se presiona el boton
            NRF_LOG_RAW_INFO("\n" LOG_INFO " Boton presionado - Reiniciando dispositivo...");
            NRF_LOG_FLUSH();
            
            // Esperar un momento para que se complete el log
            nrf_delay_ms(500);
            
            // Reiniciar el dispositivo
            NVIC_SystemReset();
        }
    }
}

void button_handler_init(void)
{
    ret_code_t err_code;
    
    // Inicializar el modulo de botones
    err_code = app_button_init(m_buttons, 
                              sizeof(m_buttons) / sizeof(m_buttons[0]), 
                              BUTTON_DETECTION_DELAY);
    APP_ERROR_CHECK(err_code);
    
    // Habilitar los botones
    err_code = app_button_enable();
    APP_ERROR_CHECK(err_code);
    
    NRF_LOG_RAW_INFO(LOG_OK " Manejador de botones inicializado correctamente");
    NRF_LOG_FLUSH();
}
