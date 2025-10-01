#ifndef LEDS_H
#define LEDS_H

#include "boards.h" 
#include "nrf_delay.h"

#define LED1_PIN NRF_GPIO_PIN_MAP(0, 18)
#define LED2_PIN NRF_GPIO_PIN_MAP(0, 13)
#define LED3_PIN NRF_GPIO_PIN_MAP(0, 11)



/**@brief Function for the LEDs initialization.
*
* @details Initializes all LEDs used by the application.
*/

static void leds_init(void)
{
    bsp_board_init(BSP_INIT_LEDS);

    nrf_gpio_cfg_output(LED1_PIN);
    nrf_gpio_cfg_output(LED2_PIN);
    nrf_gpio_cfg_output(LED3_PIN);

    nrf_gpio_pin_set(LED1_PIN);
}




#endif  // LEDS_H
