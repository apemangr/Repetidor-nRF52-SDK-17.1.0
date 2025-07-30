#include "app_nus_client.h"

#include "app_error.h"
#include "app_nus_server.h"
#include "ble_db_discovery.h"
#include "ble_nus_c.h"
#include "bsp_btn_ble.h"
#include "fds.h"
#include "nordic_common.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_scan.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "variables.h"

#define NUS_SERVICE_UUID_TYPE BLE_UUID_TYPE_VENDOR_BEGIN

#define APP_BLE_CONN_CFG_TAG  1
#define APP_BLE_OBSERVER_PRIO 3

BLE_NUS_C_DEF(m_ble_nus_c);
BLE_DB_DISCOVERY_DEF(m_db_disc);
NRF_BLE_GQ_DEF(m_ble_gatt_queue, NRF_SDH_BLE_CENTRAL_LINK_COUNT,
               NRF_BLE_GQ_QUEUE_SIZE);
NRF_BLE_SCAN_DEF(m_scan);

static app_nus_client_on_data_received_t m_on_data_received = 0;
static ble_gap_addr_t                    m_target_periph_addr;

static ble_uuid_t const                  m_nus_uuid = {.uuid = BLE_UUID_NUS_SERVICE,
                                                       .type = NUS_SERVICE_UUID_TYPE};

// Función para inicializar `m_target_periph_addr` con la MAC leída
static void target_periph_addr_init(void)
{
    // Carga la MAC desde la memoria flash
    // 80 --
    NRF_LOG_RAW_INFO("\n\n\033[1;31m>\033[0m Configurando filtrado...");
    nrf_delay_ms(20);
    load_mac_from_flash(m_target_periph_addr.addr);

    // Verifica si la MAC se ha cargado correctamente
    if (m_target_periph_addr.addr[0] == 0 && m_target_periph_addr.addr[1] == 0 &&
        m_target_periph_addr.addr[2] == 0 && m_target_periph_addr.addr[3] == 0 &&
        m_target_periph_addr.addr[4] == 0 && m_target_periph_addr.addr[5] == 0)
    {
        NRF_LOG_RAW_INFO("\n\t>> \033[0;31mError: No se pudo cargar la direccion "
                         "MAC desde la memoria flash.\033[0m\n");
        return;
    }
    // Configura la dirección del dispositivo objetivo
    m_target_periph_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    // memcpy(m_target_periph_addr.addr, m_target_periph_addr.addr,
    // sizeof(m_target_periph_addr.addr));

    NRF_LOG_RAW_INFO(
        "\n\t>> \033[0;32mFiltrado configurado correctamente.\033[0m\n");
}

static void nus_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

static void db_disc_handler(ble_db_discovery_evt_t *p_evt)
{
    ble_nus_c_on_db_disc_evt(&m_ble_nus_c, p_evt);
}

/** @brief Function for initializing the database discovery module. */
static void db_discovery_init(void)
{
    ble_db_discovery_init_t db_init;

    memset(&db_init, 0, sizeof(ble_db_discovery_init_t));

    db_init.evt_handler  = db_disc_handler;
    db_init.p_gatt_queue = &m_ble_gatt_queue;

    ret_code_t err_code  = ble_db_discovery_init(&db_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function to start scanning. */
void scan_start(void)
{
    ret_code_t ret;

    ret = nrf_ble_scan_start(&m_scan);
    APP_ERROR_CHECK(ret);

    ret = bsp_indication_set(BSP_INDICATE_SCANNING);
    APP_ERROR_CHECK(ret);
}

/**@brief Function for handling Scanning Module events.
 */
static void scan_evt_handler(scan_evt_t const *p_scan_evt)
{
    ret_code_t err_code;

    switch (p_scan_evt->scan_evt_id)
    {
    case NRF_BLE_SCAN_EVT_CONNECTING_ERROR: {
        err_code = p_scan_evt->params.connecting_err.err_code;
        APP_ERROR_CHECK(err_code);
    }
    break;

    case NRF_BLE_SCAN_EVT_CONNECTED: {
        ble_gap_evt_connected_t const *p_connected =
            p_scan_evt->params.connected.p_connected;

        NRF_LOG_RAW_INFO(
            "\n\nConectado a dispositivo autorizado: "
            "%02x:%02x:%02x:%02x:%02x:%02x",
            p_connected->peer_addr.addr[0], p_connected->peer_addr.addr[1],
            p_connected->peer_addr.addr[2], p_connected->peer_addr.addr[3],
            p_connected->peer_addr.addr[4], p_connected->peer_addr.addr[5]);
    }
    break;

    case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT: {
        NRF_LOG_INFO("Scan timed out.");
        scan_start();
    }
    break;

    case NRF_BLE_SCAN_EVT_FILTER_MATCH:
        break;

    case NRF_BLE_SCAN_EVT_NOT_FOUND:
        break;
    }
}

static void scan_init(void)
{
    ret_code_t          err_code;
    nrf_ble_scan_init_t init_scan;

    memset(&init_scan, 0, sizeof(init_scan));

    init_scan.connect_if_match = true;
    init_scan.conn_cfg_tag     = APP_BLE_CONN_CFG_TAG;

    err_code                   = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filter_set(&m_scan, NRF_BLE_SCAN_ADDR_FILTER,
                                       m_target_periph_addr.addr);
    APP_ERROR_CHECK(err_code);

    err_code =
        nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_ALL_FILTER, false);
    APP_ERROR_CHECK(err_code);
}
void string_to_command(const char *input, uint8_t *output,
                       uint16_t output_size)
{
    uint16_t input_len = strlen(input);
    if (input_len > output_size)
    {
        input_len = output_size;
    }
    memcpy(output, input, input_len);
}

static void ble_nus_c_evt_handler(ble_nus_c_t           *p_ble_nus_c,
                                  ble_nus_c_evt_t const *p_ble_nus_evt)
{
    ret_code_t err_code;

    switch (p_ble_nus_evt->evt_type)
    {
    case BLE_NUS_C_EVT_DISCOVERY_COMPLETE:
        // NRF_LOG_INFO("Discovery complete.");
        err_code = ble_nus_c_handles_assign(p_ble_nus_c, p_ble_nus_evt->conn_handle,
                                            &p_ble_nus_evt->handles);
        APP_ERROR_CHECK(err_code);

        err_code = ble_nus_c_tx_notif_enable(p_ble_nus_c);
        APP_ERROR_CHECK(err_code);

        // Comando para solicitar el ultimo registro
        // uint8_t cmd_buffer[32];
        // string_to_command("08", cmd_buffer, sizeof(cmd_buffer));
        // err_code = app_nus_client_send_data(cmd_buffer, strlen((const char *)cmd_buffer));
        // if (err_code != NRF_SUCCESS)
        // {
        //     NRF_LOG_ERROR("Failed to send command: %d", err_code);
        // }

        // char cmd_enviar_hora_a_emisor[18] = {0};
        // snprintf(cmd_enviar_hora_a_emisor, sizeof(cmd_enviar_hora_a_emisor),
        //          "06%04u%02u%02u%02u%02u%02u", m_time.year, m_time.month, m_time.day,
        //          m_time.hour, m_time.minute, m_time.second);
        // //NRF_LOG_RAW_INFO("\nEnviando comando de hora actual: %s\n", cmd_enviar_hora_a_emisor);
        // app_nus_client_send_data((uint8_t *)cmd_enviar_hora_a_emisor, strlen((const char *)cmd_enviar_hora_a_emisor));

        // Comando para solicitar la configuracion del emisor con 99
        // uint8_t cmd_config[32];
        // string_to_command("99", cmd_config, sizeof(cmd_config));

        //==============================
        // COMANDOS
        //==============================
        uint8_t cmd_id[2];

        // Enviar la hora actual del repetidor al emisor
        char cmd_enviar_hora_a_emisor[24] = {0};
        snprintf(cmd_enviar_hora_a_emisor, sizeof(cmd_enviar_hora_a_emisor),
                 "060%04u.%02u.%02u %02u.%02u.%02u", m_time.year, m_time.month, m_time.day,
                 m_time.hour, m_time.minute, m_time.second);
        app_nus_client_send_data((uint8_t *)cmd_enviar_hora_a_emisor, strlen((const char *)cmd_enviar_hora_a_emisor));

        // Solicitar los valores de los ADC y contador
        cmd_id[0] = '9';
        cmd_id[1] = '6';

        err_code      = app_nus_client_send_data(cmd_id, 2);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nFallo al solicitar datos de ADC y contador: %d", err_code);
        }
    
        // Solicitar el ultimo historial del emisor
        cmd_id[0] = '0';
        cmd_id[1] = '8';

        err_code      = app_nus_client_send_data(cmd_id, 2);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nFallo al solicitar el ultimo historial: %d", err_code);
        }

        break;

    case BLE_NUS_C_EVT_NUS_TX_EVT:
        if (m_on_data_received)
        {

            m_on_data_received(p_ble_nus_evt->p_data, p_ble_nus_evt->data_len);
        }
        // Imprime los datos recibidos
        // NRF_LOG_RAW_INFO("\nClient data received: ");
        // for (uint32_t i = 0; i < p_ble_nus_evt->data_len; i++)
        // {
        //     NRF_LOG_RAW_INFO("%c", p_ble_nus_evt->p_data[i]);
        // }
        // NRF_LOG_RAW_INFO("\n");
        break;

    case BLE_NUS_C_EVT_DISCONNECTED:
        // NRF_LOG_INFO("Emisor desconectado.");
        // scan_start();
        break;
    }
}
/**@snippet [Handling events from the ble_nus_c module] */

/**@brief Function for initializing the Nordic UART Service (NUS) client. */
static void nus_c_init(void)
{
    ret_code_t       err_code;
    ble_nus_c_init_t init;

    init.evt_handler   = ble_nus_c_evt_handler;
    init.error_handler = nus_error_handler;
    init.p_gatt_queue  = &m_ble_gatt_queue;

    err_code           = ble_nus_c_init(&m_ble_nus_c, &init);
    APP_ERROR_CHECK(err_code);
}

uint32_t app_nus_client_send_data(const uint8_t *data_array, uint16_t length)
{
    return ble_nus_c_string_send(&m_ble_nus_c, (uint8_t *)data_array, length);
}

void app_nus_client_ble_evt_handler(ble_evt_t const *p_ble_evt)
{
    ret_code_t           err_code;
    ble_gap_evt_t const *p_gap_evt   = &p_ble_evt->evt.gap_evt;
    uint16_t             conn_handle = p_ble_evt->evt.gap_evt.conn_handle;

    switch (p_ble_evt->header.evt_id)
    {
    case BLE_GAP_EVT_CONNECTED:
        if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
        {
            err_code = ble_nus_c_handles_assign(
                &m_ble_nus_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
            APP_ERROR_CHECK(err_code);

            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);

            // start discovery of services. The NUS Client waits for a
            // discovery result
            err_code = ble_db_discovery_start(&m_db_disc,
                                              p_ble_evt->evt.gap_evt.conn_handle);
            APP_ERROR_CHECK(err_code);
        }
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        // NRF_LOG_RAW_INFO("\nBuscando emisor...");
        // scan_start();
        break;
    }
}

void scan_stop(void)
{
    nrf_ble_scan_stop();
}

void app_nus_client_init(app_nus_client_on_data_received_t on_data_received)
{
    m_on_data_received = on_data_received;
    target_periph_addr_init();
    db_discovery_init();
    nus_c_init();
    scan_init();
    scan_start();
}
