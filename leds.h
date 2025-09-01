#ifndef LEDS_H
#define LEDS_H

#include "boards.h"
#include "nrf_delay.h"

#define LED1_PIN NRF_GPIO_PIN_MAP(0, 18)
#define LED2_PIN NRF_GPIO_PIN_MAP(0, 13)
#define LED3_PIN NRF_GPIO_PIN_MAP(0, 11)

void LED_Control(bool Estado, uint32_t led_, uint32_t tiempo_)
{
    if (Estado)
    {
        nrf_gpio_pin_set(led_);
        nrf_delay_ms(tiempo_);
    }
    else
    {
        nrf_gpio_pin_clear(led_);
        nrf_delay_ms(tiempo_);
    }
}

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
}

void Led_intro(void)
{

    nrf_gpio_pin_set(LED1_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED1_PIN);
    nrf_gpio_pin_set(LED2_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED2_PIN);
    nrf_gpio_pin_set(LED3_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED3_PIN);

    nrf_gpio_pin_set(LED1_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED1_PIN);
    nrf_gpio_pin_set(LED2_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED2_PIN);
    nrf_gpio_pin_set(LED3_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED3_PIN);

    nrf_gpio_pin_set(LED1_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED1_PIN);
    nrf_gpio_pin_set(LED2_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED2_PIN);
    nrf_gpio_pin_set(LED3_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED3_PIN);

    nrf_gpio_pin_set(LED1_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED1_PIN);
    nrf_gpio_pin_set(LED2_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED2_PIN);
    nrf_gpio_pin_set(LED3_PIN);
    nrf_delay_ms(100);

    nrf_gpio_pin_clear(LED3_PIN);
}

#endif // LEDS_H
