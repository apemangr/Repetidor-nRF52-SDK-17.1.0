#ifndef AD5593R_H
#define AD5593R_H

#include <stdint.h>
#include "nrf_drv_twi.h"

#define AD5593R_ADDR                  0x10

// Registros del AD5593R
#define AD5593R_REG_NOOP             0x00
#define AD5593R_REG_ADC_SEQ          0x02
#define AD5593R_REG_CTRL             0x03
#define AD5593R_REG_ADC_EN           0x04
#define AD5593R_REG_GPIO_EN          0x05
#define AD5593R_REG_GPIO_OUT_EN      0x06
#define AD5593R_REG_GPIO_SET         0x07
#define AD5593R_REG_GPIO_IN          0x08
#define AD5593R_REG_ADC_CONFIG       0x09
#define AD5593R_REG_ADC_READ         0x40

// Funciones de inicialización y control
ret_code_t ad5593r_init(nrf_drv_twi_t const * p_twi);
ret_code_t ad5593r_config_adc(uint8_t channel);
ret_code_t ad5593r_read_adc(uint8_t channel, uint16_t * p_value);

#endif // AD5593R_H
