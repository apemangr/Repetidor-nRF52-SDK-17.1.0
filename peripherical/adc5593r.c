#include "ad5593r.h"
#include "nrf_log.h"

static const nrf_drv_twi_t * m_twi;

ret_code_t ad5593r_init(nrf_drv_twi_t const * p_twi)
{
    m_twi = p_twi;
    
    // Configuración inicial del ADC
    uint8_t reg_data[] = {AD5593R_REG_ADC_CONFIG, 0x00}; // Configuración básica
    return nrf_drv_twi_tx(m_twi, AD5593R_ADDR, reg_data, sizeof(reg_data), false);
}

ret_code_t ad5593r_config_adc(uint8_t channel)
{
    if (channel > 7) return NRF_ERROR_INVALID_PARAM;
    
    uint8_t reg_data[] = {
        AD5593R_REG_ADC_EN,
        (1 << channel)  // Habilitar canal específico
    };
    
    return nrf_drv_twi_tx(m_twi, AD5593R_ADDR, reg_data, sizeof(reg_data), false);
}

ret_code_t ad5593r_read_adc(uint8_t channel, uint16_t * p_value)
{
    if (channel > 7 || !p_value) return NRF_ERROR_INVALID_PARAM;
    
    uint8_t reg = AD5593R_REG_ADC_READ | channel;
    uint8_t data[2];
    ret_code_t err_code;
    
    // Enviar comando de lectura
    err_code = nrf_drv_twi_tx(m_twi, AD5593R_ADDR, &reg, 1, true);
    if (err_code != NRF_SUCCESS) return err_code;
    
    // Leer resultado
    err_code = nrf_drv_twi_rx(m_twi, AD5593R_ADDR, data, sizeof(data), false);
    if (err_code != NRF_SUCCESS) return err_code;
    
    *p_value = (data[0] << 8) | data[1];
    return NRF_SUCCESS;
}
