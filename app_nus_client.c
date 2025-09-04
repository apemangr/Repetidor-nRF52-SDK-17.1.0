#include "app_nus_client.h"

#include "app_error.h"
#include "app_nus_server.h"
#include "app_timer.h"
#include "ble_db_discovery.h"
#include "ble_nus_c.h"
#include "bsp_btn_ble.h"
#include "fds.h"
#include "filesystem.h"
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

// Parámetros de escaneo
#define SCAN_INTERVAL                                                                              \
    0x00A0 // Intervalo de escaneo en unidades de 0.625 ms (0x00A0 = 160 * 0.625 = 100 ms)
#define SCAN_WINDOW                                                                                \
    0x0050              // Ventana de escaneo en unidades de 0.625 ms (0x0050 = 80 * 0.625 = 50 ms)
#define SCAN_DURATION 0 // Duración del escaneo en unidades de 10 ms, 0 = sin límite de tiempo

BLE_NUS_C_DEF(m_ble_nus_c);
BLE_DB_DISCOVERY_DEF(m_db_disc);
NRF_BLE_GQ_DEF(m_ble_gatt_queue, NRF_SDH_BLE_CENTRAL_LINK_COUNT, NRF_BLE_GQ_QUEUE_SIZE);
NRF_BLE_SCAN_DEF(m_scan);

static app_nus_client_on_data_received_t m_on_data_received = 0;
static ble_gap_addr_t                    m_target_periph_addr;
static bool                              m_rssi_requested = false;

static ble_uuid_t const m_nus_uuid = {.uuid = BLE_UUID_NUS_SERVICE, .type = NUS_SERVICE_UUID_TYPE};

// Variables del modo de escaneo de paquetes
static bool     m_packet_scan_mode_active      = false;
static uint32_t m_packet_scan_count            = 0;
static uint8_t  m_scan_target_mac[6]           = {0};
static bool     m_first_packet_detected        = false;
static uint32_t m_packet_scan_start_time       = 0;
static uint32_t m_packet_scan_last_packet_time = 0;

// Declaraciones de funciones para el modo de escaneo
static uint32_t get_rtc_time_ms(void);
static bool     is_target_mac_packet(const ble_gap_evt_adv_report_t *p_adv_report);

// Función para inicializar `m_target_periph_addr` con la MAC leída
static void target_periph_addr_init(void)
{
    // Carga la MAC desde la memoria flash
    // 80 --
    NRF_LOG_RAW_INFO("\n\n\033[1;31m>\033[0m Configurando filtrado...");
    nrf_delay_ms(20);
    load_mac_from_flash(m_target_periph_addr.addr, MAC_FILTRADO);

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

    NRF_LOG_RAW_INFO("\n\t>> \033[0;32mFiltrado configurado correctamente.\033[0m\n");
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
        ble_gap_evt_connected_t const *p_connected = p_scan_evt->params.connected.p_connected;

        NRF_LOG_RAW_INFO("\n\n\033[1;32mConectado a dispositivo autorizado:\033[0m "
                         "\033[1;36m%02x:%02x:%02x:%02x:%02x:%02x\033[0m",
                         p_connected->peer_addr.addr[5], p_connected->peer_addr.addr[4],
                         p_connected->peer_addr.addr[3], p_connected->peer_addr.addr[2],
                         p_connected->peer_addr.addr[1], p_connected->peer_addr.addr[0]);
    }
    break;

    case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT: {
        NRF_LOG_INFO("Scan timed out.");
        scan_start();
    }
    break;

    case NRF_BLE_SCAN_EVT_FILTER_MATCH:
        // Dispositivo que coincide con nuestro filtro (emisor)
        break;

    case NRF_BLE_SCAN_EVT_NOT_FOUND:
        // No mostrar información de dispositivos que no coinciden con el filtro
        break;
    }
}

static void scan_init(void)
{
    ret_code_t                   err_code;
    nrf_ble_scan_init_t          init_scan;
    static ble_gap_scan_params_t scan_params;

    memset(&init_scan, 0, sizeof(init_scan));
    memset(&scan_params, 0, sizeof(scan_params));

    // Configurar parámetros de escaneo compatibles
    scan_params.active         = 1; // Escaneo activo para mejor recepción
    scan_params.interval       = SCAN_INTERVAL;
    scan_params.window         = SCAN_WINDOW;
    scan_params.timeout        = SCAN_DURATION;
    scan_params.scan_phys      = BLE_GAP_PHY_1MBPS;

    init_scan.connect_if_match = true;
    init_scan.conn_cfg_tag     = APP_BLE_CONN_CFG_TAG;
    init_scan.p_scan_param     = &scan_params;

    err_code                   = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code =
        nrf_ble_scan_filter_set(&m_scan, NRF_BLE_SCAN_ADDR_FILTER, m_target_periph_addr.addr);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_ALL_FILTER, false);
    APP_ERROR_CHECK(err_code);
}
void string_to_command(const char *input, uint8_t *output, uint16_t output_size)
{
    uint16_t input_len = strlen(input);
    if (input_len > output_size)
    {
        input_len = output_size;
    }
    memcpy(output, input, input_len);
}

static void ble_nus_c_evt_handler(ble_nus_c_t *p_ble_nus_c, ble_nus_c_evt_t const *p_ble_nus_evt)
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

        //============================================================
        //                         COMANDOS
        //============================================================

        uint8_t cmd_id[2];

        //
        // Enviar la hora actual del repetidor al emisor
        //
        char cmd_enviar_hora_a_emisor[24] = {0};
        snprintf(cmd_enviar_hora_a_emisor, sizeof(cmd_enviar_hora_a_emisor),
                 "060%04u.%02u.%02u %02u.%02u.%02u", m_time.year, m_time.month, m_time.day,
                 m_time.hour, m_time.minute, m_time.second);
        app_nus_client_send_data((uint8_t *)cmd_enviar_hora_a_emisor,
                                 strlen((const char *)cmd_enviar_hora_a_emisor));

        //
        // Solicitar los valores de los ADC y contador
        //
        cmd_id[0] = '9';
        cmd_id[1] = '6';

        err_code  = app_nus_client_send_data(cmd_id, 2);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nFallo al solicitar datos de ADC y contador: %d", err_code);
        }

        //
        // Solicitar el ultimo historial del emisor entre las 23:00 y 00:00
        //
        // if (m_time.hour >= 23 || m_time.hour == 0)
        // {
        cmd_id[0] = '0';
        cmd_id[1] = '8';

        err_code  = app_nus_client_send_data(cmd_id, 2);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nFallo al solicitar el ultimo historial: %d", err_code);
        }
        // }

        //============================================================
        //                  AQUI TERMINAN LOS COMANDOS
        //============================================================

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
    case BLE_GAP_EVT_ADV_REPORT:
        // Procesar paquetes advertising en modo de escaneo
        if (m_packet_scan_mode_active)
        {
            const ble_gap_evt_adv_report_t *p_adv_report = &p_gap_evt->params.adv_report;

            // Verificar si el paquete es del emisor objetivo
            if (is_target_mac_packet(p_adv_report))
            {
                m_packet_scan_count++;
                m_packet_scan_last_packet_time = get_rtc_time_ms();

                if (!m_first_packet_detected)
                {
                    m_first_packet_detected = true;
                    NRF_LOG_RAW_INFO("\n\x1b[1;32m[SCAN MODE]\x1b[0m Primer paquete detectado!");
                }

                NRF_LOG_RAW_INFO(
                    "\n\x1b[1;36m[SCAN MODE]\x1b[0m Paquete #%lu detectado (RSSI: %d dBm)",
                    m_packet_scan_count, p_adv_report->rssi);
            }
        }
        break;

    case BLE_GAP_EVT_CONNECTED:
        if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
        {
            // Solicitar RSSI una sola vez después de establecer la conexión
            if (!m_rssi_requested)
            {
                ret_code_t rssi_err = sd_ble_gap_rssi_start(conn_handle, 0, 0);
                if (rssi_err == NRF_SUCCESS)
                {
                    m_rssi_requested = true;
                    NRF_LOG_RAW_INFO("Solicitando RSSI...");
                }
            }

            err_code =
                ble_nus_c_handles_assign(&m_ble_nus_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
            APP_ERROR_CHECK(err_code);

            err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
            APP_ERROR_CHECK(err_code);

            // start discovery of services. The NUS Client waits for a
            // discovery result
            err_code = ble_db_discovery_start(&m_db_disc, p_ble_evt->evt.gap_evt.conn_handle);
            APP_ERROR_CHECK(err_code);
        }
        break;
    case BLE_GAP_EVT_RSSI_CHANGED: {
        // Mostrar RSSI cuando se recibe la actualización
        int8_t rssi = p_gap_evt->params.rssi_changed.rssi;
        NRF_LOG_RAW_INFO("\n[RSSI] Potencia de senal del emisor: %d dBm", rssi);
        // Detener la monitorización de RSSI después de la primera lectura
        sd_ble_gap_rssi_stop(conn_handle);
    }
    break;
    case BLE_GAP_EVT_DISCONNECTED:
        // Resetear el flag de RSSI solicitado para la próxima conexión
        m_rssi_requested = false;
        // NRF_LOG_RAW_INFO("\nBuscando emisor...");
        // scan_start();
        break;
    }
}

void scan_stop(void)
{
    nrf_ble_scan_stop();
    NRF_LOG_RAW_INFO("\n\x1b[1;33m[SCAN]\x1b[0m Scan detenido");
}

// Función para obtener el tiempo actual del RTC en milisegundos
static uint32_t get_rtc_time_ms(void)
{
    // El RTC cuenta a 8Hz (cada tick = 125ms)
    // nrfx_rtc_counter_get devuelve el contador actual
    extern nrfx_rtc_t m_rtc;
    uint32_t          rtc_ticks = nrfx_rtc_counter_get(&m_rtc);
    return rtc_ticks * 125; // Convertir a milisegundos
}

// Función para verificar timeouts del modo de escaneo
static void packet_scan_check_timeouts(void)
{
    if (!m_packet_scan_mode_active)
    {
        return;
    }

    uint32_t current_time = get_rtc_time_ms();

    // Verificar timeout de tiempo máximo (5 minutos)
    if ((current_time - m_packet_scan_start_time) >= MAX_DETECTION_TIME_MS)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;33m[SCAN MODE]\x1b[0m Tiempo máximo alcanzado - Terminando escaneo");
        NRF_LOG_RAW_INFO("\n\x1b[1;36m[SCAN MODE]\x1b[0m Total de paquetes detectados: %lu",
                         m_packet_scan_count);
        packet_scan_mode_stop();
        return;
    }

    // Verificar timeout de inactividad (30 segundos) solo después del primer paquete
    if (m_first_packet_detected &&
        (current_time - m_packet_scan_last_packet_time) >= INACTIVITY_TIMEOUT_MS)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;33m[SCAN MODE]\x1b[0m Timeout por inactividad - Terminando escaneo");
        NRF_LOG_RAW_INFO("\n\x1b[1;36m[SCAN MODE]\x1b[0m Total de paquetes detectados: %lu",
                         m_packet_scan_count);
        packet_scan_mode_stop();
    }
}

// Función para verificar si un paquete advertising coincide con la MAC objetivo
static bool is_target_mac_packet(const ble_gap_evt_adv_report_t *p_adv_report)
{
    // Comparar la dirección MAC del paquete con la MAC objetivo
    return (memcmp(p_adv_report->peer_addr.addr, m_scan_target_mac, 6) == 0);
}

// Funciones públicas del modo de escaneo
void packet_scan_mode_start(void)
{
    ret_code_t err_code;

    if (m_packet_scan_mode_active)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[SCAN MODE]\x1b[0m Modo de escaneo ya está activo");
        return;
    }

    // Cargar MAC objetivo desde la memoria flash
    load_mac_from_flash(m_scan_target_mac, MAC_ESCANEO);

    // Verificar si la MAC se cargó correctamente
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++)
    {
        if (m_scan_target_mac[i] != 0)
        {
            mac_is_zero = false;
            break;
        }
    }

    if (mac_is_zero)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[SCAN MODE]\x1b[0m Error: MAC objetivo no configurada");
        return;
    }

    // Inicializar variables
    m_packet_scan_mode_active      = true;
    m_packet_scan_count            = 0;
    m_first_packet_detected        = false;
    m_packet_scan_start_time       = get_rtc_time_ms();
    m_packet_scan_last_packet_time = m_packet_scan_start_time;

    NRF_LOG_RAW_INFO("\n\x1b[1;32m[SCAN MODE]\x1b[0m Iniciando modo de escaneo de paquetes");
    NRF_LOG_RAW_INFO("\n\x1b[1;36m[SCAN MODE]\x1b[0m MAC objetivo: %02X:%02X:%02X:%02X:%02X:%02X",
                     m_scan_target_mac[5], m_scan_target_mac[4], m_scan_target_mac[3],
                     m_scan_target_mac[2], m_scan_target_mac[1], m_scan_target_mac[0]);

    // Iniciar escaneo sin filtros para capturar todos los paquetes
    err_code = nrf_ble_scan_filters_disable(&m_scan);
    APP_ERROR_CHECK(err_code);

    scan_start();

    NRF_LOG_RAW_INFO("\n\x1b[1;32m[SCAN MODE]\x1b[0m Esperando primer paquete del emisor...");
}

void packet_scan_mode_stop(void)
{
    if (!m_packet_scan_mode_active)
    {
        return;
    }

    m_packet_scan_mode_active = false;

    // Restaurar filtros de escaneo normales
    ret_code_t err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_ALL_FILTER, false);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_RAW_INFO("\n\x1b[1;32m[SCAN MODE]\x1b[0m Modo de escaneo terminado");
    NRF_LOG_RAW_INFO("\n\x1b[1;36m[SCAN MODE]\x1b[0m Resultado final: %lu paquetes detectados",
                     m_packet_scan_count);
}

bool packet_scan_mode_is_active(void)
{
    return m_packet_scan_mode_active;
}

uint32_t packet_scan_mode_get_count(void)
{
    return m_packet_scan_count;
}

// Función para ser llamada periódicamente desde el main loop o RTC handler
void packet_scan_mode_update(void)
{
    packet_scan_check_timeouts();
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
