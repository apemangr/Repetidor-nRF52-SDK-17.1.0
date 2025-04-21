#include "app_nus_client.h"
#include "app_nus_common.h"
#include "nordic_common.h"
#include "app_error.h"
#include "bsp_btn_ble.h"
#include "ble_db_discovery.h"
#include "nrf_ble_scan.h"
#include "nrf_ble_gatt.h"
#include "ble_nus_c.h"
#include "app_nus_server.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define NUS_SERVICE_UUID_TYPE BLE_UUID_TYPE_VENDOR_BEGIN /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_BLE_CONN_CFG_TAG 1  /**< Tag that refers to the BLE stack configuration set with @ref sd_ble_cfg_set. The default tag is @ref BLE_CONN_CFG_TAG_DEFAULT. */
#define APP_BLE_OBSERVER_PRIO 3 /**< BLE observer priority of the application. There is no need to modify this value. */

BLE_NUS_C_DEF(m_ble_nus_c);      /**< BLE Nordic UART Service (NUS) client instance. */
BLE_DB_DISCOVERY_DEF(m_db_disc); /**< Database discovery module instance. */
NRF_BLE_GQ_DEF(m_ble_gatt_queue, /**< BLE GATT Queue instance. */
               NRF_SDH_BLE_CENTRAL_LINK_COUNT,
               NRF_BLE_GQ_QUEUE_SIZE);
NRF_BLE_SCAN_DEF(m_scan); /**< Scanning Module instance. */
static app_nus_client_on_data_received_t m_on_data_received = 0;

//bool filtro_aplicado = false; /**< Flag to indicate if the filter is applied. */
//uint8_t mac_address_conectado[6] = {0};
static bool m_target_device_connected = false;
static uint16_t m_target_conn_handle = BLE_CONN_HANDLE_INVALID;

/**@brief NUS UUID. */
static ble_uuid_t const m_nus_uuid =
    {
        .uuid = BLE_UUID_NUS_SERVICE,
        .type = NUS_SERVICE_UUID_TYPE};

static ble_gap_addr_t const m_target_periph_addr =
    {
        .addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC,
        .addr = { 0xA6, 0x3E, 0x61, 0xA8, 0x0C, 0xF8 } // reverse order 0xF8, 0x0C, 0xA8, 0x61, 0x3E, 0xA6 } // convert to hex f80ca8613ea6}
};

/**@brief Function for handling the Nordic UART Service Client errors.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nus_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling database discovery events.
 *
 * @details This function is a callback function to handle events from the database discovery module.
 *          Depending on the UUIDs that are discovered, this function forwards the events
 *          to their respective services.
 *
 * @param[in] p_event  Pointer to the database discovery event.
 */
static void db_disc_handler(ble_db_discovery_evt_t *p_evt)
{
    ble_nus_c_on_db_disc_evt(&m_ble_nus_c, p_evt);
}

/** @brief Function for initializing the database discovery module. */
static void db_discovery_init(void)
{
    ble_db_discovery_init_t db_init;

    memset(&db_init, 0, sizeof(ble_db_discovery_init_t));

    db_init.evt_handler = db_disc_handler;
    db_init.p_gatt_queue = &m_ble_gatt_queue;

    ret_code_t err_code = ble_db_discovery_init(&db_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function to start scanning. */
static void scan_start(void)
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
    case NRF_BLE_SCAN_EVT_CONNECTING_ERROR:
    {
        err_code = p_scan_evt->params.connecting_err.err_code;
        APP_ERROR_CHECK(err_code);
    }
    break;

    case NRF_BLE_SCAN_EVT_CONNECTED:
    {
        ble_gap_evt_connected_t const *p_connected = p_scan_evt->params.connected.p_connected;
        bool mac_coincide = (p_connected->peer_addr.addr_type == m_target_periph_addr.addr_type) &&
                            (memcmp(p_connected->peer_addr.addr,
                                    m_target_periph_addr.addr,
                                    BLE_GAP_ADDR_LEN) == 0);

        // Scan is automatically stopped by the connection.
        if (!mac_coincide)
        {
            // Loggear MAC no autorizada y desconectar
            NRF_LOG_INFO("Dispositivo no autorizado desconectado. MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                        p_connected->peer_addr.addr[0],
                        p_connected->peer_addr.addr[1],
                        p_connected->peer_addr.addr[2],
                        p_connected->peer_addr.addr[3],
                        p_connected->peer_addr.addr[4],
                        p_connected->peer_addr.addr[5]);

            err_code = sd_ble_gap_disconnect(p_scan_evt->params.connected.conn_handle, 
                                            BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            if (err_code != NRF_SUCCESS)
            {
                NRF_LOG_ERROR("Error al desconectar: 0x%X", err_code);
            }
        }
        else
        {
            NRF_LOG_INFO("Conectado a dispositivo autorizado: %02x:%02x:%02x:%02x:%02x:%02x",
                        p_connected->peer_addr.addr[0],
                        p_connected->peer_addr.addr[1],
                        p_connected->peer_addr.addr[2],
                        p_connected->peer_addr.addr[3],
                        p_connected->peer_addr.addr[4],
                        p_connected->peer_addr.addr[5]);

            NRF_LOG_INFO("Filtro desactivado.");
            err_code = nrf_ble_scan_filters_disable(&m_scan);
            APP_ERROR_CHECK(err_code);

        }
    }
    break;

    case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT:
    {
        NRF_LOG_INFO("Scan timed out.");
        scan_start();
    }
    break;

    case NRF_BLE_SCAN_EVT_FILTER_MATCH:
        NRF_LOG_INFO("Los filtros matchean!!!");
        break;

    case NRF_BLE_SCAN_EVT_NOT_FOUND:
        //NRF_LOG_INFO("El dispositivo NO cumple con los filtros.");
        // Puedes ignorarlo o tomar alguna acción.
        break;

    default:
        break;
    }
}

/**@brief Function for initializing the scanning and setting the filters.
 */
static void scan_init(void)
{
    ret_code_t err_code;
    nrf_ble_scan_init_t init_scan;

    memset(&init_scan, 0, sizeof(init_scan));

    init_scan.connect_if_match = true;
    init_scan.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;

    err_code = nrf_ble_scan_init(&m_scan, &init_scan, scan_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filter_set(&m_scan, NRF_BLE_SCAN_ADDR_FILTER, m_target_periph_addr.addr);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_ALL_FILTER, false);
    APP_ERROR_CHECK(err_code);
}

/**@brief Callback handling Nordic UART Service (NUS) client events.
 *
 * @details This function is called to notify the application of NUS client events.
 *
 * @param[in]   p_ble_nus_c   NUS client handle. This identifies the NUS client.
 * @param[in]   p_ble_nus_evt Pointer to the NUS client event.
 */

/**@snippet [Handling events from the ble_nus_c module] */
static void ble_nus_c_evt_handler(ble_nus_c_t *p_ble_nus_c, ble_nus_c_evt_t const *p_ble_nus_evt)
{
    ret_code_t err_code;

    switch (p_ble_nus_evt->evt_type)
    {
    case BLE_NUS_C_EVT_DISCOVERY_COMPLETE:
        NRF_LOG_INFO("Discovery complete.");
        err_code = ble_nus_c_handles_assign(p_ble_nus_c, p_ble_nus_evt->conn_handle, &p_ble_nus_evt->handles);
        APP_ERROR_CHECK(err_code);

        err_code = ble_nus_c_tx_notif_enable(p_ble_nus_c);
        APP_ERROR_CHECK(err_code);
        NRF_LOG_INFO("Connected to device with Nordic UART Service.");
        break;

    case BLE_NUS_C_EVT_NUS_TX_EVT:
        if (m_on_data_received)
        {
            m_on_data_received(p_ble_nus_evt->p_data, p_ble_nus_evt->data_len);
        }
        break;

    case BLE_NUS_C_EVT_DISCONNECTED:
        NRF_LOG_INFO("Disconnected.");
        scan_start();
        break;
    }
}
/**@snippet [Handling events from the ble_nus_c module] */

/**@brief Function for initializing the Nordic UART Service (NUS) client. */
static void nus_c_init(void)
{
    ret_code_t err_code;
    ble_nus_c_init_t init;

    init.evt_handler = ble_nus_c_evt_handler;
    init.error_handler = nus_error_handler;
    init.p_gatt_queue = &m_ble_gatt_queue;

    err_code = ble_nus_c_init(&m_ble_nus_c, &init);
    APP_ERROR_CHECK(err_code);
}

uint32_t app_nus_client_send_data(const uint8_t *data_array, uint16_t length)
{
    return ble_nus_c_string_send(&m_ble_nus_c, (uint8_t *)data_array, length);
}

void app_nus_client_ble_evt_handler(ble_evt_t const *p_ble_evt)
{
    ret_code_t err_code;
    ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;
    uint16_t conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    uint8_t mac_address[6];

    switch (p_ble_evt->header.evt_id)
    {
    case BLE_GAP_EVT_CONNECTED:
        if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
        {
            memcpy(mac_address, p_ble_evt->evt.gap_evt.params.connected.peer_addr.addr, 6);

            m_target_device_connected = true;

            if (memcmp(mac_address, m_target_periph_addr.addr, 6) == 0 && m_target_device_connected)
            {
                // Quita el filtro
                err_code = nrf_ble_scan_filters_disable(&m_scan);
                APP_ERROR_CHECK(err_code);


                err_code = ble_nus_c_handles_assign(&m_ble_nus_c, p_ble_evt->evt.gap_evt.conn_handle, NULL);
                APP_ERROR_CHECK(err_code);

                err_code = bsp_indication_set(BSP_INDICATE_CONNECTED);
                APP_ERROR_CHECK(err_code);

                // start discovery of services. The NUS Client waits for a discovery result
                err_code = ble_db_discovery_start(&m_db_disc, p_ble_evt->evt.gap_evt.conn_handle);
                APP_ERROR_CHECK(err_code);
            }
            else
            {
                sd_ble_gap_disconnect(p_ble_evt->evt.gap_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            }
        }
        break;
    case BLE_GAP_EVT_DISCONNECTED:
        // Verificar si el dispositivo que se desconectó ERA nuestro objetivo
        if (conn_handle == m_target_conn_handle)
        {
            NRF_LOG_INFO("Dispositivo objetivo desconectado.");
            m_target_device_connected = false;
            m_target_conn_handle = BLE_CONN_HANDLE_INVALID;

            // **PASO CLAVE: Rehabilitar el filtro por dirección MAC**
            NRF_LOG_INFO("Habilitando filtro por MAC.");
            // Primero deshabilita todos por si acaso
            // err_code = nrf_ble_scan_filters_disable(&m_scan);
            // APP_ERROR_CHECK(err_code);
            // Habilita solo el de dirección
            err_code = nrf_ble_scan_filters_enable(&m_scan, NRF_BLE_SCAN_ADDR_FILTER, false);
            APP_ERROR_CHECK(err_code);

            // Volver a escanear
            scan_start();
        }
        // else: Se desconectó otro dispositivo (el celular?), manejado en on_ble_peripheral_evt
    }
}

void app_nus_client_init(app_nus_client_on_data_received_t on_data_received)
{
    m_on_data_received = on_data_received;
    db_discovery_init();
    nus_c_init();
    scan_init();
    scan_start();
}