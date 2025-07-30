#include "app_nus_server.h"

#include "app_timer.h"
#include "app_uart.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_nus.h"
#include "bsp_btn_ble.h"
#include "calendar.h"
#include "fds.h"
#include "filesystem.h"
#include "nrf.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "variables.h"

#define APP_BLE_CONN_CFG_TAG           1
#define DEVICE_NAME                    "Repetidor"
#define NUS_SERVICE_UUID_TYPE          BLE_UUID_TYPE_VENDOR_BEGIN
#define APP_BLE_OBSERVER_PRIO          3
#define APP_ADV_INTERVAL               64
#define APP_ADV_DURATION               18000
#define MIN_CONN_INTERVAL              MSEC_TO_UNITS(20, UNIT_1_25_MS)
#define MAX_CONN_INTERVAL              MSEC_TO_UNITS(75, UNIT_1_25_MS)
#define SLAVE_LATENCY                  0
#define CONN_SUP_TIMEOUT               MSEC_TO_UNITS(4000, UNIT_10_MS)
#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000)
#define NEXT_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(30000)
#define MAX_CONN_PARAMS_UPDATE_COUNT   3
#define DEAD_BEEF                      0xDEADBEEF

#define Largo_Advertising              0x18 // Largo_Advertising  10 son 16 y 18 son 24

uint8_t m_beacon_info[Largo_Advertising];
uint8_t adv_buffer[BLE_GAP_ADV_SET_DATA_SIZE_MAX];

BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);
NRF_BLE_QWR_DEF(m_qwr);
BLE_ADVERTISING_DEF(m_advertising);

static bool                              m_emisor_nus_ready     = false;
static app_nus_server_on_data_received_t m_on_data_received     = 0;
static uint16_t                          m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;
static uint16_t                          m_conn_handle          = BLE_CONN_HANDLE_INVALID;
static uint16_t                          m_emisor_conn_handle   = BLE_CONN_HANDLE_INVALID;
static uint8_t                           custom_mac_addr_[6]    = {0};
static ble_gap_addr_t                    m_target_periph_addr;

static ble_uuid_t                        m_adv_uuids[] = {
    {BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}};

// Función para realizar la recolección de basura
static void perform_garbage_collection(void)
{
    ret_code_t err_code = fds_gc();
    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\t>> Recoleccion de basura completada.");
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> Error en la recoleccion de basura: %d", err_code);
    }
}

static void fds_evt_handler(fds_evt_t const *p_evt)
{
    if (p_evt->id == FDS_EVT_INIT)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\033[1;31m>\033[0m Iniciando el modulo de almacenamiento...\n");

            fds_stat_t stat = {0};
            fds_stat(&stat);
            NRF_LOG_RAW_INFO("\t>> Se encontraron %d registros validos.\n",
                             stat.valid_records);
            NRF_LOG_RAW_INFO("\t>> Se encontraron %d registros no validos.",
                             stat.dirty_records);

            if (stat.dirty_records > 0)
            {
                // Realiza la recolección de basura
                NRF_LOG_RAW_INFO("\n\t>> Limpiando registros no validos...");
                perform_garbage_collection();
            }
            NRF_LOG_RAW_INFO(
                "\n\t>> \033[0;32mModulo inicializado correctamente.\033[0m");
        }
        else
        {
            NRF_LOG_RAW_INFO("\nError al inicializar FDS: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_WRITE)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;32m>> Registro escrito correctamente!\x1b[0m");
        }
        else
        {
            NRF_LOG_ERROR("Error al escribir el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_UPDATE)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            // NRF_LOG_RAW_INFO(
            //     "\n\n\x1b[1;32m>>\x1b[0m Registro actualizado correctamente!");
        }
        else
        {
            NRF_LOG_ERROR("Error al actualizar el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_DEL_RECORD)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nRegistro eliminado correctamente.");
        }
        else
        {
            NRF_LOG_ERROR("Error al eliminar el registro: %d", p_evt->result);
        }
    }
}

static void fds_initialize(void)
{
    ret_code_t err_code;

    // Registra el manejador de eventos
    err_code = fds_register(fds_evt_handler);
    APP_ERROR_CHECK(err_code);

    // Inicializa el módulo FDS
    err_code = fds_init();
    APP_ERROR_CHECK(err_code);
}
/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may
 * need to inform the application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went
 * wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for handling the data from the Nordic UART Service.
 *
 * @details This function will process the data received from the Nordic UART
 * BLE Service and send it to the UART module.
 *
 * @param[in] p_evt       Nordic UART Service event.
 */
/**@snippet [Handling the data received over BLE] */
static void nus_data_handler(ble_nus_evt_t *p_evt)
{
    if (p_evt->type == BLE_NUS_EVT_RX_DATA)
    {
        uint32_t err_code;

        NRF_LOG_DEBUG("Received data from BLE NUS. Writing data on UART.");
        NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data,
                              p_evt->params.rx_data.length);

        // Asegúrate de que el mensaje sea tratado como una cadena de texto
        char message[BLE_NUS_MAX_DATA_LEN]; // +1 para el carácter nulo
        if (p_evt->params.rx_data.length < sizeof(message))
        {
            memcpy(message, p_evt->params.rx_data.p_data,
                   p_evt->params.rx_data.length);

            message[p_evt->params.rx_data.length] = '\0'; // Agregar terminador nulo

            // Imprime el mensaje recibido
            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Mensaje recibido: \x1b[0m%s",
                             message);

            // Verifica si el mensaje comienza con "111" par interpretar el comando
            if (p_evt->params.rx_data.length >= 5 && message[0] == '1' &&
                message[1] == '1' && message[2] == '1')
            {
                // Extrae el comando (los dos caracteres después de "111")
                char command[3] = {message[3], message[4],
                                   '\0'}; // Comando de 2 caracteres

                // Manejo de comandos con un switch-case
                switch (atoi(command)) // Convierte el comando a entero
                {
                case 1: // Comando 01: Guardar MAC
                {
                    size_t mac_length = p_evt->params.rx_data.length - 5;
                    if (mac_length == 12) // Verifica que la longitud sea válida
                    {
                        for (size_t i = 0; i < 6; i++)
                        {
                            char byte_str[3]    = {message[5 + i * 2], message[6 + i * 2], '\0'};
                            custom_mac_addr_[i] = (uint8_t)strtol(byte_str, NULL, 16);
                        }
                        NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 01 recibido: "
                                         "Cambiar MAC \x1b[0m");
                        NRF_LOG_RAW_INFO("\n> MAC recibida: "
                                         "%02X:%02X:%02X:%02X:%02X:%02X",
                                         custom_mac_addr_[0], custom_mac_addr_[1],
                                         custom_mac_addr_[2], custom_mac_addr_[3],
                                         custom_mac_addr_[4], custom_mac_addr_[5]);

                        // Guarda la MAC en la memoria flash y reinicia el
                        // dispositivo
                        save_mac_to_flash(custom_mac_addr_);
                    }
                    else
                    {
                        NRF_LOG_WARNING("Longitud de MAC inválida: %d", mac_length);
                    }
                    break;
                }
                case 2: // Comando 02: Muestra la MAC custom guardada en la
                    // memoria flash
                    {
                        uint8_t mac_print[6];
                        // Carga la MAC desde la memoria flash
                        NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 02 recibido: Mostrando "
                                         "MAC "
                                         "guardada \x1b[0m");
                        load_mac_from_flash(mac_print);
                        // muestra la MAC
                    }

                    break;

                case 3: // Comando 03: Reiniciar el dispositivo
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 03 recibido: "
                                     "Reiniciando "
                                     "dispositivo...\n\n\n\n");
                    nrf_delay_ms(1000);
                    NVIC_SystemReset();

                    break;

                case 4: // Guardar tiempo de encendido
                {
                    if (p_evt->params.rx_data.length >=
                        6) // Verifica que haya datos suficientes
                    {
                        char     time_str[4] = {message[5], message[6], message[7], '\0'};
                        uint32_t time_in_seconds __attribute__((aligned(4))) =
                            atoi(time_str) * 1000;
                        if (time_in_seconds <= 666000)
                        {
                            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 04 recibido: "
                                             "Cambiar tiempo de encendido \x1b[0m");
                            write_time_to_flash(TIEMPO_ENCENDIDO, time_in_seconds);
                        }
                        else
                        {
                            NRF_LOG_WARNING("El tiempo de encendido excede el máximo "
                                            "permitido (666 segundos).");
                        }
                    }
                    else
                    {
                        NRF_LOG_WARNING("Comando 04 recibido con datos insuficientes.");
                    }
                    break;
                }

                case 5: // Comando 05: Leer tiempo de encendido desde la
                    // memoria flash
                    {
                        NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 05 recibido: "
                                         "Leer tiempo de encendido \x1b[0m");
                        uint32_t sleep_time_ms = read_time_from_flash(
                            TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS);

                        break;
                    }

                case 6: // Comando 06: Guardar tiempo de apagado
                {
                    if (p_evt->params.rx_data.length >=
                        6) // Verifica que haya datos suficientes
                    {
                        char     time_str[4] = {message[5], message[6], message[7], '\0'};
                        uint32_t time_in_seconds __attribute__((aligned(4))) =
                            atoi(time_str) * 1000;
                        if (time_in_seconds <= 6666000) // Verifica que no exceda el máximo
                        // permitido
                        {
                            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 06 recibido: "
                                             "Cambiar tiempo de dormido \x1b[0m");
                            write_time_to_flash(TIEMPO_SLEEP, time_in_seconds);
                        }
                        else
                        {
                            NRF_LOG_WARNING("El tiempo de dormido excede el máximo "
                                            "permitido (6666 segundos).");
                        }
                    }
                    else
                    {
                        NRF_LOG_WARNING("Comando 06 recibido con datos insuficientes.");
                    }
                    break;
                }

                case 7: // Comando 07: Leer tiempo de apagado desde la memoria flash
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 07 recibido: "
                                     "Leer tiempo de dormido\x1b[0m");
                    uint32_t sleep_time_ms = read_time_from_flash(
                        TIEMPO_SLEEP, DEFAULT_DEVICE_SLEEP_TIME_MS);

                    break;
                }

                case 8: // Comando 08: Escribe en la memoria flash la fecha, hora, formato YYYYMMDDHHMMSS
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 08 recibido: Guardar "
                                     "fecha y hora\x1b[0m");

                    if (p_evt->params.rx_data.length >=
                        19) // 5 de comando + 14 de fecha/hora
                    {
                        const char *fecha_iso =
                            &message[5]; // Apunta al inicio de la fecha/hora

                        datetime_t dt;
                        if (sscanf(fecha_iso, "%4hu%2hhu%2hhu%2hhu%2hhu%2hhu", &dt.year,
                                   &dt.month, &dt.day, &dt.hour, &dt.minute,
                                   &dt.second) == 6)
                        {
                            NRF_LOG_RAW_INFO("\nFecha recibida: "
                                             "%04u-%02u-%02u %02u:%02u:%02u",
                                             dt.year, dt.month, dt.day, dt.hour, dt.minute,
                                             dt.second);

                            // Llamar a la función modificada
                            err_code = write_date_to_flash(&dt);

                            if (err_code != NRF_SUCCESS)
                            {
                                NRF_LOG_ERROR("Error guardando: 0x%X", err_code);
                            }
                        }
                        else
                        {
                            NRF_LOG_WARNING("Formato inválido. Se esperaba "
                                            "YYYYMMDDHHMMSS.");
                        }
                    }
                    else
                    {
                        NRF_LOG_WARNING("Datos insuficientes. Se esperaban 14 dígitos");
                    }
                    break;
                }
                case 9: // Comando 09: Leer fecha y hora almacenada en flash
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 09 recibido: Leer fecha "
                                     "y hora\x1b[0m");

                    // Leer fecha de la memoria flash
                    datetime_t dt = read_date_from_flash();

                    // Mostrar fecha leída
                    NRF_LOG_RAW_INFO(
                        "\n>> Fecha almacenada: %04u-%02u-%02u %02u:%02u:%02u", dt.year,
                        dt.month, dt.day, dt.hour, dt.minute, dt.second);

                    break;
                }
                case 10: // Comando para solicitar el último historial al periférico
                {
                    // Work in progress
                    break;
                }
                case 11: // Solicitar un registro de historial por ID
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 11 recibido: Solicitar registro de historial por ID\x1b[0m");
                    if (p_evt->params.rx_data.length > 5) // Verifica que haya datos suficientes
                    {
                        // Extrae el ID del registro como string (todo lo que sigue después de "11111")
                        size_t id_len    = p_evt->params.rx_data.length - 5;
                        char   id_str[8] = {0}; // Soporta hasta 7 dígitos, ajusta si necesitas más
                        if (id_len < sizeof(id_str))
                        {
                            memcpy(id_str, &message[5], id_len);
                            id_str[id_len]       = '\0';
                            uint16_t registro_id = (uint16_t)atoi(id_str);
                            // Llama a la función para solicitar el registro por ID
                            store_history registro_historial;
                            err_code = read_history_record_by_id(registro_id, &registro_historial);
                            if (err_code == NRF_SUCCESS)
                            {
                                NRF_LOG_RAW_INFO("\nSolicitud de registro %u enviada correctamente.", registro_id);
                                // Imprime el registro de historial
                                char titulo[41];
                                snprintf(titulo, sizeof(titulo), "Historial recibido \x1B[33m#%u\x1B[0m", registro_id);
                                print_history_record(&registro_historial, titulo);
                                NRF_LOG_FLUSH();
                            }
                            else
                            {
                                NRF_LOG_RAW_INFO("\nError al solicitar el registro %u: 0x%X", registro_id, err_code);
                            }
                        }
                        else
                        {
                            NRF_LOG_WARNING("ID de registro demasiado largo.");
                        }
                    }
                    break;
                }
                case 12: // Guardar un nuevo historial con valores inventados

                    // Descartar funcionalidad
                    break;

                case 13: // Pide una lectura de todos los historiales
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 13 : Solicitud del historial completo\x1b[0m");
                    // Llama a la función para solicitar el historial completo
                    send_all_history_ble();

                    break;
                }
                default: // Comando desconocido
                    NRF_LOG_WARNING("Comando desconocido: %s", command);
                    break;
                }
            }
            else
            {
                // Reenvía el mensaje al emisor o lo maneja normalmente
                if (m_on_data_received)
                {
                    m_on_data_received((uint8_t *)message, p_evt->params.rx_data.length);
                }
            }
        }
        else
        {
            NRF_LOG_WARNING("Mensaje demasiado largo para procesar.");
        }
    }
    else if (p_evt->type == BLE_NUS_EVT_TX_RDY)
    {
        // El buffer de transmisión está listo - enviar siguiente paquete del comando 15/16 si está activo
        // También manejar el envío asíncrono de historial
        history_send_next_packet();
    }
}

static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)DEVICE_NAME,
                                          strlen(DEVICE_NAME));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code                          = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing services that will be used by the
 * application.
 */
static void services_init(void)
{
    uint32_t           err_code;
    ble_nus_init_t     nus_init;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code               = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    // Initialize NUS.
    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;

    err_code              = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);
}

static void on_conn_params_evt(ble_conn_params_evt_t *p_evt)
{
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle,
                                         BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}

static void conn_params_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}

static void conn_params_init(void)
{
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = true;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;
    err_code                               = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    uint32_t err_code;

    switch (ble_adv_evt)
    {
    case BLE_ADV_EVT_FAST:
        err_code = bsp_indication_set(BSP_INDICATE_ADVERTISING);
        APP_ERROR_CHECK(err_code);
        break;
    case BLE_ADV_EVT_IDLE:
        // sleep_mode_enter();
        break;
    default:
        break;
    }
}

void app_nus_server_ble_evt_handler(ble_evt_t const *p_ble_evt)
{
    uint32_t             err_code;
    ble_gap_evt_t const *p_gap_evt = &p_ble_evt->evt.gap_evt;

    switch (p_ble_evt->header.evt_id)
    {

    case BLE_GAP_EVT_CONNECTED:
        if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_PERIPH)
        {
            NRF_LOG_RAW_INFO("\nCelular conectado");
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            restart_on_rtc();
        }
        else if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
        {
            NRF_LOG_RAW_INFO("\nEmisor conectado");
            m_emisor_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        }
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        if (p_gap_evt->conn_handle == m_conn_handle)
        {
            ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
            NRF_LOG_RAW_INFO("\nCelular desconectado\n");
            m_conn_handle = BLE_CONN_HANDLE_INVALID; // Invalida el handle del celular
        }
        else if (p_gap_evt->conn_handle == m_emisor_conn_handle)
        {
            NRF_LOG_RAW_INFO("\nEmisor desconectado");
            NRF_LOG_RAW_INFO("\n\n\033[1;31m>\033[0m Buscando emisor...\n");

            m_emisor_conn_handle =
                BLE_CONN_HANDLE_INVALID; // Invalida el handle del emisor
            scan_start();
        }
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

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
        // Pairing not supported
        err_code = sd_ble_gap_sec_params_reply(
            m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
        // No system attributes have been stored.
        err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTC_EVT_TIMEOUT:
        // Disconnect on GATT Client timeout event.
        err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_GATTS_EVT_TIMEOUT:
        // Disconnect on GATT Server timeout event.
        err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        break;

    default:
        // No implementation needed.
        break;
    }
}

uint32_t app_nus_server_send_data(const uint8_t *data_array, uint16_t length)
{
    return ble_nus_data_send(&m_nus, (uint8_t *)data_array, &length,
                             m_conn_handle);
}

/**@brief Function for initializing the Advertising functionality.
 */

void advertising_init(void)
{
    uint32_t                 err_code;
    ble_advertising_init_t   init;
    ble_advdata_manuf_data_t manuf_specific_data;

    memset(&m_beacon_info, 0, sizeof(m_beacon_info));
    // Se modifica para tener un numero de advertising mayor a 0
    // y poder ser procesado por la app
    m_beacon_info[1] = MSB_16(adc_values.contador);
    m_beacon_info[2] = LSB_16(adc_values.contador);
    m_beacon_info[3] = MSB_16(adc_values.V1);
    m_beacon_info[4] = LSB_16(adc_values.V1);
    m_beacon_info[5] = MSB_16(adc_values.V2);
    m_beacon_info[6] = LSB_16(adc_values.V2);

    // Indentificador
    manuf_specific_data.company_identifier = 0x1133;
    manuf_specific_data.data.p_data        = (uint8_t *)m_beacon_info;
    manuf_specific_data.data.size          = sizeof(m_beacon_info);

    memset(&init, 0, sizeof(init));

    init.advdata.name_type                     = BLE_ADVDATA_NO_NAME; // BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance            = false;
    init.advdata.flags                         = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    init.config.ble_adv_on_disconnect_disabled = true;
    init.advdata.p_manuf_specific_data         = &manuf_specific_data;

    init.srdata.uuids_complete.uuid_cnt =
        sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids = m_adv_uuids;

    init.config.ble_adv_fast_enabled   = true;
    init.config.ble_adv_fast_interval  = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout   = APP_ADV_DURATION;
    init.evt_handler                   = on_adv_evt;

    err_code                           = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Function for starting advertising.
 */
void advertising_start(void)
{
    uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\nble_advertising_start error: 0x%08X\n", err_code);
    }
    // Posible crash
    APP_ERROR_CHECK(err_code);
}

void advertising_stop(void)
{
    sd_ble_gap_adv_stop(m_advertising.adv_handle);
}

void disconnect_all_devices(void)
{
    ret_code_t err_code;

    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        err_code = sd_ble_gap_disconnect(m_conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
        NRF_LOG_RAW_INFO("\nCelular desconectado.");
    }

    if (m_emisor_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        err_code = sd_ble_gap_disconnect(m_emisor_conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        APP_ERROR_CHECK(err_code);
        m_emisor_conn_handle = BLE_CONN_HANDLE_INVALID;
        NRF_LOG_RAW_INFO("\nEmisor desconectado.");
    }
}

void app_nus_server_init(app_nus_server_on_data_received_t on_data_received)
{
    fds_initialize();
    m_on_data_received = on_data_received;
    gap_params_init();
    services_init();
    advertising_init();
    conn_params_init();
    advertising_start();
}
