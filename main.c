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
#include "bsp.h"
#include "calendar.h"
#include "filesystem.h"
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

// store_flash Flash_array = {0};
adc_values_t adc_values = {0};

// Sistema de sincronización con emisor
typedef enum
{
    SYNC_STATE_NORMAL,         // Modo normal: lee tiempos desde flash
    SYNC_STATE_EXTENDED_SEARCH // Modo búsqueda: usa tiempo extendido fijo
} sync_state_t;

static sync_state_t   current_sync_state          = SYNC_STATE_NORMAL;
static uint32_t       cycles_without_emisor       = 0;
static const uint32_t MAX_FAILED_CYCLES           = 1;
static bool           emisor_connected_this_cycle = false;

// Funciones del sistema de sincronización
void sync_system_reset_connection_flag(void)
{
    emisor_connected_this_cycle = false;
}

void sync_system_mark_emisor_connected(void)
{
    emisor_connected_this_cycle = true;
    cycles_without_emisor       = 0; // Reset contador de fallos

    // Si estábamos en búsqueda extendida, volver a normal INMEDIATAMENTE
    if (current_sync_state == SYNC_STATE_EXTENDED_SEARCH)
    {
        current_sync_state = SYNC_STATE_NORMAL;
        NRF_LOG_RAW_INFO("\n[\033[1;32mSYNC\033[0m] Emisor encontrado - Cambiando INMEDIATAMENTE a "
                         "tiempo normal");

        // Reprogramar usando la función existente
        restart_on_rtc();
        NRF_LOG_RAW_INFO(
            "\n[\033[1;32mSYNC\033[0m] RTC reprogramado con tiempo normal desde flash");
    }
}

void sync_system_handle_cycle_end(void)
{
    if (!emisor_connected_this_cycle)
    {
        cycles_without_emisor++;
        NRF_LOG_RAW_INFO("\n[\033[1;32mSYNC\033[0m] Ciclo sin emisor: %d/%d", cycles_without_emisor,
                         MAX_FAILED_CYCLES);

        if (cycles_without_emisor >= MAX_FAILED_CYCLES && current_sync_state == SYNC_STATE_NORMAL)
        {
            current_sync_state = SYNC_STATE_EXTENDED_SEARCH;
            NRF_LOG_RAW_INFO("\n[\033[1;32mSYNC\033[0m] Cambiando a modo BUSQUEDA EXTENDIDA");
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\n[\033[1;32mSYNC\033[0m] Emisor conectado en este ciclo");
    }
}

/**
 * @brief Lee todos los registros de historial e imprime la hora de cada uno
 *
 * @param file_id       ID del archivo a leer
 * @return ret_code_t   Código de retorno
 */
ret_code_t fds_print_all_record_times(void)
{
    ret_code_t         err_code;
    fds_find_token_t   token          = {0};
    fds_record_desc_t  record_desc    = {0};
    fds_flash_record_t flash_record   = {0};
    uint32_t           record_count   = 0;
    uint16_t           expected_words = BYTES_TO_WORDS(sizeof(store_history));

    // Iterar a través de todos los registros del file_id
    while (fds_record_iterate(&record_desc, &token) == NRF_SUCCESS)
    {
        // Abrir el registro para lectura
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Error abriendo registro ID %d", record_desc.record_id);
            continue;
        }

        // Verificar que el tamaño sea correcto
        if (flash_record.p_header->length_words != expected_words)
        {
            NRF_LOG_WARNING("Registro ID %d: tamaño incorrecto (%d words)", record_desc.record_id,
                            flash_record.p_header->length_words);
            fds_record_close(&record_desc);
            continue;
        }

        // Obtener puntero a los datos del historial
        const store_history *p_history = (const store_history *)flash_record.p_data;

        record_count++;

        // Cerrar el registro
        fds_record_close(&record_desc);
    }

    NRF_LOG_INFO("=== Total de registros procesados: %d ===", record_count);
    NRF_LOG_FLUSH();
    nrf_delay_ms(1000);
    return NRF_SUCCESS;
}

NRF_BLE_GATT_DEF(m_gatt); /**< GATT module instance. */
nrfx_rtc_t           m_rtc             = NRFX_RTC_INSTANCE(2);
bool                 m_device_active   = true;
static uint16_t      m_conn_handle     = BLE_CONN_HANDLE_INVALID;
static volatile bool m_rtc_on_flag     = false;
static volatile bool m_rtc_sleep_flag  = false;
static uint16_t m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - OPCODE_LENGTH - HANDLE_LENGTH;

// Sistema de recuperación UART
static uint32_t       uart_error_count = 0;
static const uint32_t UART_MAX_ERRORS  = 5; // Reinicializar después de 5 errores consecutivos
static bool           uart_recovery_in_progress = false;

// Forward declaration del manejador de eventos UART
void uart_event_handler(app_uart_evt_t *p_event);

// Función para reinicializar el UART en caso de errores repetidos
static void uart_recovery_reinit(void)
{
    if (uart_recovery_in_progress)
    {
        return; // Evitar reentrada
    }

    uart_recovery_in_progress = true;

    NRF_LOG_RAW_INFO(
        "\n\x1b[1;33m[UART RECOVERY]\x1b[0m Reinicializando UART después de %d errores...",
        uart_error_count);

    // Cerrar UART actual
    app_uart_close();
    nrf_delay_ms(100); // Pequeña pausa para asegurar limpieza

    // Reinicializar UART
    ret_code_t                   err_code;
    app_uart_comm_params_t const comm_params = {.rx_pin_no    = RX_PIN_NUMBER,
                                                .tx_pin_no    = TX_PIN_NUMBER,
                                                .rts_pin_no   = RTS_PIN_NUMBER,
                                                .cts_pin_no   = CTS_PIN_NUMBER,
                                                .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
                                                .use_parity   = false,
                                                .baud_rate    = UART_BAUDRATE_BAUDRATE_Baud115200};

    APP_UART_FIFO_INIT(&comm_params, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, uart_event_handler,
                       APP_IRQ_PRIORITY_LOWEST, err_code);

    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;32m[UART RECOVERY]\x1b[0m UART reinicializado exitosamente");
        uart_error_count = 0; // Reset contador de errores
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[UART RECOVERY]\x1b[0m Error al reinicializar UART: 0x%X",
                         err_code);
    }

    uart_recovery_in_progress = false;
}

void uart_event_handler(app_uart_evt_t *p_event)
{

    static uint8_t  data_array[BLE_NUS_MAX_DATA_LEN];
    static uint16_t index = 0;
    uint32_t        ret_val;

    switch (p_event->evt_type)
    {

        /**@snippet [Handling data from UART] */

    case APP_UART_DATA_READY:
        // Reset contador de errores en comunicación exitosa
        uart_error_count = 0;

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
        uart_error_count++;
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[UART ERROR]\x1b[0m Error de comunicación #%d: 0x%x",
                         uart_error_count, p_event->data.error_communication);

        // Si se alcanza el límite de errores, reinicializar UART
        if (uart_error_count >= UART_MAX_ERRORS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;33m[UART ERROR]\x1b[0m Límite de errores alcanzado (%d), "
                             "iniciando recuperación...",
                             UART_MAX_ERRORS);
            uart_recovery_reinit();
        }
        break;

    case APP_UART_FIFO_ERROR:
        uart_error_count++;
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[UART ERROR]\x1b[0m Error en FIFO #%d: 0x%x",
                         uart_error_count, p_event->data.error_code);

        // También considerar errores FIFO para recuperación
        if (uart_error_count >= UART_MAX_ERRORS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;33m[UART ERROR]\x1b[0m Límite de errores alcanzado (%d), "
                             "iniciando recuperación...",
                             UART_MAX_ERRORS);
            uart_recovery_reinit();
        }
        break;

    default:
        break;
    }
}

static void uart_init(void)
{
    ret_code_t err_code;

    // Reset contador de errores al inicializar
    uart_error_count                         = 0;
    uart_recovery_in_progress                = false;

    app_uart_comm_params_t const comm_params = {.rx_pin_no    = RX_PIN_NUMBER,
                                                .tx_pin_no    = TX_PIN_NUMBER,
                                                .rts_pin_no   = RTS_PIN_NUMBER,
                                                .cts_pin_no   = CTS_PIN_NUMBER,
                                                .flow_control = APP_UART_FLOW_CONTROL_DISABLED,
                                                .use_parity   = false,
                                                .baud_rate    = UART_BAUDRATE_BAUDRATE_Baud115200};

    APP_UART_FIFO_INIT(&comm_params, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, uart_event_handler,
                       APP_IRQ_PRIORITY_LOWEST, err_code);

    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\033[1;31m>\033[0m UART inicializado correctamente");
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\033[1;31m>\033[0m Error al inicializar UART: 0x%X", err_code);
    }

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
    ret_code_t err_code;

    if (m_rtc_on_flag)
    {
        m_rtc_on_flag = false;

        if (m_device_active)
        {
            // Manejar el final del ciclo activo
            sync_system_handle_cycle_end();

            // Si hay un escaneo de paquetes activo, detenerlo antes del sleep
            if (packet_scan_mode_is_active())
            {
                NRF_LOG_RAW_INFO(
                    "\n\x1b[1;33m[SCAN MODE]\x1b[0m Deteniendo escaneo por transición a sleep");
                packet_scan_mode_stop();
            }

            NRF_LOG_RAW_INFO(
                "\n\n\033[1;31m--------->\033[0m Transicion a \033[1;36mMODO SLEEP\033[0m");
            disconnect_all_devices();
            advertising_stop();
            scan_stop();
            app_uart_close();
            m_device_active = false;

            // Decidir el tiempo de sleep según el estado de sincronización
            if (current_sync_state == SYNC_STATE_EXTENDED_SEARCH)
            {
                // En búsqueda extendida: usar sleep fijo de 10 segundos
                uint32_t current_counter  = nrfx_rtc_counter_get(&m_rtc);
                uint32_t sleep_time_fixed = 10000; // 10 segundos fijos
                uint32_t next_event = (current_counter + (sleep_time_fixed / 1000) * 8) & 0xFFFFFF;
                nrfx_rtc_cc_set(&m_rtc, 1, next_event, true);
                NRF_LOG_RAW_INFO("\n[\033[1;32mSYNC\033[0m] Sleep busqueda extendida: 10s fijos");
            }
            else
            {
                // En modo normal: usar tiempo desde flash
                restart_sleep_rtc();
            }
        }
    }

    if (m_rtc_sleep_flag)
    {
        m_rtc_sleep_flag = false;

        if (!m_device_active)
        {
            // Reset flag de conexión para el nuevo ciclo
            sync_system_reset_connection_flag();

            // Reset contador de errores UART al despertar (nuevo ciclo limpio)
            uart_error_count = 0;

            const char *mode_str =
                (current_sync_state == SYNC_STATE_NORMAL) ? "NORMAL" : "BUSQUEDA EXTENDIDA";
            NRF_LOG_RAW_INFO(
                "\n\n\033[1;31m--------->\033[0m Transicion a \033[1;32mMODO ACTIVO\033[0m (%s)",
                mode_str);

            // TRATAR DE HACER LOS PROCESOS DE MEMORIA ANTES DE
            // INICIAR EL ADVERTISING Y EL SCANEO

            // Modificar el advertising payload para mostrar
            // los valores de los ADC
            advertising_init();

            write_date_to_flash(&m_time);

            // Mostrar fecha y hora
            NRF_LOG_RAW_INFO("\n[GUARDADO] Fecha y hora actual: %04u-%02u-%02u, "
                             "%02u:%02u:%02u",
                             m_time.year, m_time.month, m_time.day, m_time.hour, m_time.minute,
                             m_time.second);

            // Actualizar el payload para mostrar los adc_values
            scan_start();
            advertising_start();
            uart_init();
            m_device_active = true;

            // Decidir qué función de RTC usar según el estado de sincronización
            if (current_sync_state == SYNC_STATE_NORMAL)
            {
                restart_on_rtc(); // Usa tiempo desde flash
            }
            else
            {
                restart_on_rtc_search_mode(); // Usa tiempo fijo para búsqueda del emisor
            }
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
    nrfx_rtc_cc_set(&m_rtc, 0,
                    (read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS) / 1000) * 8,
                    true);

    // Deshabilitar el evento de 20s inicialmente
    nrfx_rtc_cc_disable(&m_rtc, 1);

    // Habilitar interrupciones
    nrfx_rtc_int_enable(&m_rtc, NRF_RTC_INT_COMPARE0_MASK | NRF_RTC_INT_COMPARE1_MASK |
                                    NRF_RTC_INT_COMPARE2_MASK);
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
        err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.gap_evt.conn_handle,
                                               BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
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
    nrf_gpio_cfg_output(LED1_PIN);
    nrf_gpio_cfg_output(LED2_PIN);
    nrf_gpio_cfg_output(LED3_PIN);
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
    // // Output the data to the UART
    // NRF_LOG_RAW_INFO("\nServer data received: ");
    // for (int i = 0; i < data_length; i++)
    //     NRF_LOG_RAW_INFO("\n%c", data_ptr[i]);
    // NRF_LOG_RAW_INFO("\n");

    // Forward the data from the client to the server
    app_nus_client_send_data(data_ptr, data_length);
}

// Procesa la información recibida desde el emisor
void app_nus_client_on_data_received(const uint8_t *data_ptr, uint16_t data_length)
{
    uint16_t position = 0;

    // Se recibieron los datos de los ADC's y contador
    if (data_ptr[0] == 0x96 && data_length == 9)
    {
        // Marcar que el emisor se conectó en este ciclo
        sync_system_mark_emisor_connected();

        uint16_t pos  = 1;
        adc_values.V1 = (data_ptr[pos] << 8) | data_ptr[pos + 1];
        pos += 2;
        adc_values.V2 = (data_ptr[pos] << 8) | data_ptr[pos + 1];
        pos += 2;
        adc_values.contador = (data_ptr[pos] << 24) | (data_ptr[pos + 1] << 16) |
                              (data_ptr[pos + 2] << 8) | data_ptr[pos + 3];

        NRF_LOG_RAW_INFO("\nDatos cortos recibidos (0x96): ");
        NRF_LOG_RAW_INFO("V1: %u, V2: %u,", adc_values.V1, adc_values.V2);
        NRF_LOG_RAW_INFO(" Contador: %lu", adc_values.contador);
        NRF_LOG_FLUSH();
    }

    // Se recibio el 'ultimo' historial del emisor
    if (data_length > 20 && data_ptr[0] == 0x08)
    {
        // Marcar que el emisor se conectó en este ciclo
        sync_system_mark_emisor_connected();
        position = 1; // Comenzar después del primer byte
        // Desempaquetar datos
        uint8_t  day      = data_ptr[position++];
        uint8_t  month    = data_ptr[position++];
        uint16_t year     = (data_ptr[position++] << 8) | data_ptr[position++];
        uint8_t  hour     = data_ptr[position++];
        uint8_t  minute   = data_ptr[position++];
        uint8_t  second   = data_ptr[position++];
        uint32_t contador = (data_ptr[position++] << 24) | (data_ptr[position++] << 16) |
                            (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V1      = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V2      = (data_ptr[position++] << 8) | data_ptr[position++];
        uint8_t  battery = data_ptr[position++];
        position += 6; // MAC original
        position += 6; // MAC custom
        uint16_t V3            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V4            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V5            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V6            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V7            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint16_t V8            = (data_ptr[position++] << 8) | data_ptr[position++];
        uint8_t  temp          = data_ptr[position++]; // Temperatura del módulo
        uint16_t last_position = (data_ptr[position++] << 8) | data_ptr[position++];

        // Construir el registro temporal
        store_history nuevo_historial = {.year     = year,
                                         .month    = month,
                                         .day      = day,
                                         .hour     = hour,
                                         .minute   = minute,
                                         .second   = second,
                                         .contador = contador,
                                         .V1       = V1,
                                         .V2       = V2,
                                         .V3       = V3,
                                         .V4       = V4,
                                         .V5       = V5,
                                         .V6       = V6,
                                         .V7       = V7,
                                         .V8       = V8,
                                         .temp     = temp,
                                         .battery  = battery};

        // Guardar el historial recibido en la posición indicada
        ret_code_t ret = save_history_record_emisor(&nuevo_historial, last_position);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al guardar historial en posicion: %u (ret=%d)\n",
                             last_position, ret);
        }

        // Imprimir usando print_history_record con el número de historial en el
        // título
        char titulo[41];
        snprintf(titulo, sizeof(titulo), "Historial recibido \x1B[33m#%u\x1B[0m", last_position);
        print_history_record(&nuevo_historial, titulo);
        // fds_print_all_record_times();
        NRF_LOG_FLUSH();
    }
    app_nus_server_send_data(data_ptr, data_length);
}

static void idle_state_handle(void)
{
    if (NRF_LOG_PROCESS() == false)
    {
        // Actualizar el modo de escaneo de paquetes si está activo
        packet_scan_mode_update();

        if (!m_device_active)
        {
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

    Led_intro();

    NRF_LOG_RAW_INFO("\n\n\nYa hice la intro");
    power_management_init();

    ble_stack_init();
    gatt_init();

    // Inicializa los servicios de servidor y cliente NUS
    app_nus_server_init(app_nus_server_on_data_received);
    app_nus_client_init(app_nus_client_on_data_received);

    calendar_init();

    NRF_LOG_FLUSH();
    calendar_set_datetime();

    NRF_LOG_RAW_INFO("\n\033[1;31m>\033[0m Buscando emisor...\n");
    NRF_LOG_FLUSH();

    // Enter main loop.
    for (;;)
    {
        calendar_update();
        handle_rtc_events();
        idle_state_handle();
    }
}
