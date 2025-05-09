#ifndef LEDS_H
#define LEDS_H

#include "boards.h" 
#include "nrf_delay.h"

void LED_Control(bool Estado, uint32_t led_,uint32_t tiempo_)
{
  if (Estado)
  {
    bsp_board_led_on(led_);
    nrf_delay_ms(tiempo_); 
  }
  else 
  {
    bsp_board_led_off(led_);
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
}

void Led_intro()
  {
  LED_Control(1,0,75);
  LED_Control(0,0,75);
  LED_Control(1,1,75);
  LED_Control(0,1,75);  
  LED_Control(1,2,75);
  LED_Control(0,2,75);
  
  LED_Control(1,0,75);
  LED_Control(0,0,75);
  LED_Control(1,1,75);
  LED_Control(0,1,75);  
  LED_Control(1,2,75);
  LED_Control(0,2,75);
  
  LED_Control(1,0,75);
  LED_Control(0,0,75);
  LED_Control(1,1,75);
  LED_Control(0,1,75);  
  LED_Control(1,2,75);
  LED_Control(0,2,75);
}

#endif  // LEDS_H