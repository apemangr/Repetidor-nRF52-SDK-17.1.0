#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_error.h"
#include "app_nus_client.h"
#include "app_nus_server.h"
#include "app_timer.h"
#include "app_uart.h"
#include "app_util.h"
#include "ble.h"
#include "ble_gap.h"
#include "ble_hci.h"
#include "ble_nus.h"
#include "bsp_btn_ble.h"
#include "calendar.h"
#include "leds.h"
#include "nordic_common.h"
#include "nrf_ble_gatt.h"
#include "nrf_drv_rtc.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "variables.h"

#define RTC_ON_TICKS    (100 * 8)
#define RTC_SLEEP_TICKS (10 * 8)
#define RTC_PRESCALER   4095

NRF_BLE_GATT_DEF(m_gatt); /**< GATT module instance. */
static uint16_t      m_conn_handle          = BLE_CONN_HANDLE_INVALID;
static uint16_t      m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - OPCODE_LENGTH - HANDLE_LENGTH;
nrfx_rtc_t           m_rtc                  = NRFX_RTC_INSTANCE(2);
static volatile bool m_device_active        = true;
static volatile bool m_rtc_on_flag          = false;
static volatile bool m_rtc_sleep_flag       = false;

//
void uart_event_handler(app_uart_evt_t *p_event)
{
    static uint8_t  data_array[BLE_NUS_MAX_DATA_LEN];
    static uint16_t index = 0;
    uint32_t        ret_val;

    switch (p_event->evt_type)
    {
    /**@snippet [Handling data from UART] */
    case APP_UART_DATA_READY:
        UNUSED_VARIABLE(app_uart_get(&data_array[index]));
        index++;

        if ((data_array[index - 1] == '\n') || (data_array[index - 1] == '\r') ||
            (index >= (m_ble_nus_max_data_len)))
        {
            NRF_LOG_DEBUG("Ready to send data over BLE NUS client and server");
            NRF_LOG_HEXDUMP_DEBUG(data_array, index);

            app_nus_client_send_data(data_array, index);

            app_nus_server_send_data(data_array, index);

            index = 0;
        }
        break;

    /**@snippet [Handling data from UART] */
    case APP_UART_COMMUNICATION_ERROR:
        NRF_LOG_ERROR("Communication error occurred while handling UART.");
        APP_ERROR_HANDLER(p_event->data.error_communication);
        break;

    case APP_UART_FIFO_ERROR:
        NRF_LOG_ERROR("Error occurred in FIFO module used by UART.");
        APP_ERROR_HANDLER(p_event->data.error_code);
        break;

    default:
        break;
    }
}

/**@brief Function for initializing the UART. */
static void uart_init(void)
{
    ret_code_t                   err_code;
    app_uart_comm_params_t const comm_params = {.rx_pin_no    = RX_PIN_NUMBER,
                                                .tx_pin_no    = TX_PIN_NUMBER,
                                                .rts_pin_no   = RTS_PIN_NUMBER,
                                                .cts_pin_no   = CTS_PIN_NUMBER,
                                                .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
                                                .use_parity   = false,
                                                .baud_rate    = UART_BAUDRATE_BAUDRATE_Baud115200};

    APP_UART_FIFO_INIT(&comm_params,
                       UART_RX_BUF_SIZE,
                       UART_TX_BUF_SIZE,
                       uart_event_handler,
                       APP_IRQ_PRIORITY_LOWEST,
                       err_code);

    APP_ERROR_CHECK(err_code);
}

void rtc_handler(nrfx_rtc_int_type_t int_type)
{
    if (int_type == NRFX_RTC_INT_COMPARE0)
    {
        m_rtc_on_flag = true;
    }
    else if (int_type == NRFX_RTC_INT_COMPARE1)
    {
        m_rtc_sleep_flag = true;
    }
    else if (int_type == NRFX_RTC_INT_COMPARE2)
    {
        calendar_rtc_handler();
    }
}

void handle_rtc_events(void)
{
    uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);

    if (m_rtc_on_flag)
    {
        m_rtc_on_flag = false;

        if (m_device_active)
        {
            NRF_LOG_RAW_INFO("\n======> Transicion a MODO SLEEP");
            datetime_t now;
            calendar_get_time(&now);
            NRF_LOG_INFO("Fecha: %04u-%02u-%02u Hora: %02u:%02u:%02u",
                         now.year,
                         now.month,
                         now.day,
                         now.hour,
                         now.minute,
                         now.second);
            disconnect_all_connections();
            advertising_stop();
            scan_stop();
            app_uart_close();

            m_device_active = false;

            // Reprogramar solo el evento de 20s (activación)
            uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
            uint32_t next_event      = (current_counter + RTC_SLEEP_TICKS) & 0xFFFFFF;
            nrfx_rtc_cc_set(&m_rtc, 1, next_event, true);
        }
    }

    if (m_rtc_sleep_flag)
    {
        m_rtc_sleep_flag = false;

        if (!m_device_active)
        {
            NRF_LOG_RAW_INFO("\n======> Retomando MODO ACTIVO");
            datetime_t now;
            calendar_get_time(&now);
            NRF_LOG_INFO("Fecha: %04u-%02u-%02u Hora: %02u:%02u:%02u",
                         now.year,
                         now.month,
                         now.day,
                         now.hour,
                         now.minute,
                         now.second);
            scan_start();
            advertising_start();
            uart_init();

            m_device_active = true;

            // Reprogramar solo el evento de 15s (sueño)
            uint32_t current_counter = nrfx_rtc_counter_get(&m_rtc);
            uint32_t next_event      = (current_counter + RTC_ON_TICKS) & 0xFFFFFF;
            nrfx_rtc_cc_set(&m_rtc, 0, next_event, true);
        }
    }
}

void rtc_init(void)
{
    // Configuración LFCLK mejorada
    nrf_drv_clock_init();
    nrf_drv_clock_lfclk_request(NULL);

    // Esperar a que el reloj esté estable
    while (!nrf_drv_clock_lfclk_is_running())
    {
    }

    // Configurar RTC con prescaler CORRECTO
    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
    config.prescaler         = RTC_PRESCALER;

    ret_code_t err_code      = nrfx_rtc_init(&m_rtc, &config, rtc_handler);
    APP_ERROR_CHECK(err_code);

    // Limpiar contador y comenzar desde cero
    nrfx_rtc_counter_clear(&m_rtc);

    // Configurar comparadores iniciales
    // Solo programar el primer evento de 15s
    nrfx_rtc_cc_set(&m_rtc, 0, RTC_ON_TICKS, true);

    // Deshabilitar el evento de 20s inicialmente
    nrfx_rtc_cc_disable(&m_rtc, 1);

    // Habilitar interrupciones
    nrfx_rtc_int_enable(
        &m_rtc, NRF_RTC_INT_COMPARE0_MASK | NRF_RTC_INT_COMPARE1_MASK | NRF_RTC_INT_COMPARE2_MASK);
    nrfx_rtc_enable(&m_rtc);
}

void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name)
{
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

static void base_timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}

static void ble_nus_chars_received_uart_print(uint8_t *p_data, uint16_t data_len)
{
    ret_code_t ret_val;

    NRF_LOG_DEBUG("Receiving data.");
    NRF_LOG_HEXDUMP_DEBUG(p_data, data_len);

    for (uint32_t i = 0; i < data_len; i++)
    {
        do
        {
            ret_val = app_uart_put(p_data[i]);
            if ((ret_val != NRF_SUCCESS) && (ret_val != NRF_ERROR_BUSY))
            {
                NRF_LOG_ERROR("app_uart_put failed for index 0x%04x.", i);
                APP_ERROR_CHECK(ret_val);
            }
        } while (ret_val == NRF_ERROR_BUSY);
    }
    if (p_data[data_len - 1] == '\r')
    {
        while (app_uart_put('\n') == NRF_ERROR_BUSY)
            ;
    }
    if (ECHOBACK_BLE_UART_DATA)
    {
        // Send data back to the peripheral.
        do
        {
            /*ret_val = ble_nus_c_string_send(&m_ble_nus_c, p_data,
        data_len); if ((ret_val != NRF_SUCCESS) && (ret_val !=
        NRF_ERROR_BUSY))
        {
            NRF_LOG_ERROR("Failed sending NUS message. Error 0x%x.
        ", ret_val); APP_ERROR_CHECK(ret_val);
        }*/
        } while (ret_val == NRF_ERROR_BUSY);
    }
}

static bool shutdown_handler(nrf_pwr_mgmt_evt_t event)
{
    ret_code_t err_code;

    err_code = bsp_indication_set(BSP_INDICATE_IDLE);
    APP_ERROR_CHECK(err_code);

    switch (event)
    {
    case NRF_PWR_MGMT_EVT_PREPARE_WAKEUP:
        // Prepare wakeup buttons.
        err_code = bsp_btn_ble_sleep_mode_prepare();
        APP_ERROR_CHECK(err_code);
        break;

    default:
        break;
    }

    return true;
}

NRF_PWR_MGMT_HANDLER_REGISTER(shutdown_handler, APP_SHUTDOWN_HANDLER_PRIORITY);

static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context)
{
    ret_code_t           err_code;
    ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;

    switch (p_ble_evt->header.evt_id)
    {
    case BLE_GAP_EVT_TIMEOUT:
        if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN)
        {
            NRF_LOG_INFO("Connection Request timed out.");
        }
        break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        // Pairing not supported.
        err_code = sd_ble_gap_sec_params_reply(
            p_ble_evt->evt.gap_evt.conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
        // Accepting parameters requested by peer.
        err_code = sd_ble_gap_conn_param_update(
            p_gap_evt->conn_handle, &p_gap_evt->params.conn_param_update_request.conn_params);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
        NRF_LOG_DEBUG("PHY update request.");
        ble_gap_phys_t const phys = {
            .rx_phys = BLE_GAP_PHY_AUTO,
            .tx_phys = BLE_GAP_PHY_AUTO,
        };
        err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
        APP_ERROR_CHECK(err_code);
    }
    break;

    case BLE_GATTC_EVT_TIMEOUT:
        // Disconnect on GATT Client timeout event.
        NRF_LOG_DEBUG("GATT Client Timeout.");
        err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTS_EVT_TIMEOUT:
        // Disconnect on GATT Server timeout event.
        NRF_LOG_DEBUG("GATT Server Timeout.");
        err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        break;

    default:
        break;
    }

    // Forward BLE events to the app NUS server module
    app_nus_server_ble_evt_handler(p_ble_evt);

    // Forward BLE events to the app NUS client module
    app_nus_client_ble_evt_handler(p_ble_evt);
}

static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code           = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

void gatt_evt_handler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const *p_evt)
{
    if (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)
    {
        // NRF_LOG_INFO("ATT MTU exchange completed.");

        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
        // NRF_LOG_INFO("Ble NUS max data length set to 0x%X(%d)",
        // m_ble_nus_max_data_len, m_ble_nus_max_data_len);
    }
}

/**@brief Function for initializing the GATT library. */
void gatt_init(void)
{
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_central_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);
}

void bsp_event_handler(bsp_event_t event)
{
    ret_code_t err_code;

    switch (event)
    {
    case BSP_EVENT_SLEEP:
        nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_SYSOFF);
        break;

    case BSP_EVENT_DISCONNECT:
        break;

    default:
        break;
    }
}

static void buttons_leds_init(void)
{
    ret_code_t  err_code;
    bsp_event_t startup_event;

    err_code = bsp_init(BSP_INIT_LEDS, bsp_event_handler);
    APP_ERROR_CHECK(err_code);

    err_code = bsp_btn_ble_init(NULL, &startup_event);
    APP_ERROR_CHECK(err_code);
}

static void timer_init(void)
{
    ret_code_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);
}

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

static void power_management_init(void)
{
    ret_code_t err_code;
    err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}

void app_nus_server_on_data_received(const uint8_t *data_ptr, uint16_t data_length)
{
    // Output the data to the UART
    printf("Server data received: ");
    for (int i = 0; i < data_length; i++)
        printf("%c", data_ptr[i]);
    printf("\r\n");

    // Forward the data from the client to the server
    app_nus_client_send_data(data_ptr, data_length);
}

void app_nus_client_on_data_received(const uint8_t *data_ptr, uint16_t data_length)
{
    // Output the data to the  UART
    printf("Client data received: ");
    for (int i = 0; i < data_length; i++)
        printf("%c", data_ptr[i]);
    printf("\r\n");

    // Forward the data from the client to the server
    app_nus_server_send_data(data_ptr, data_length);
}

static void idle_state_handle(void)
{
    if (NRF_LOG_PROCESS() == false)
    {
        if (!m_device_active)
        {
            // Entrar en bajo consumo solo cuando esta inactivo
            nrf_pwr_mgmt_run();
        }
        else
        {
            sd_app_evt_wait();
        }
    }
}

int main(void)
{
    ret_code_t err_code;
    log_init();
    NRF_LOG_RAW_INFO("\n\033[1;36m====================\033[0m "
                     "\033[1;33mINICIO DEL SISTEMA\033[0m"
                     " \033[1;36m====================\033[0m\n");
    NRF_LOG_RAW_INFO("\t\t Firmware 0.0.1 por\033[0m "
                     "\033[1;90mCrea\033[1;31mLab\033[0m\n\n");

    base_timer_init();
    rtc_init();
    uart_init();
    buttons_leds_init();
    power_management_init();
    ble_stack_init();
    gatt_init();

    // Inicializa los servicios de servidor y cliente NUS
    app_nus_server_init(app_nus_server_on_data_received);
    app_nus_client_init(app_nus_client_on_data_received);

    calendar_init();
    calendar_set_datetime();

    nrf_delay_ms(10);

    NRF_LOG_RAW_INFO("\n> Buscando emisor...\n");

    // Enter main loop.
    for (;;)
    {
        calendar_update();
        handle_rtc_events();
        idle_state_handle();
    }
}