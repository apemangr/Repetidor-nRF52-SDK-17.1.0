#include "app_nus_server.h"
#include "app_nus_client.h"
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

#define LED1_PIN                       NRF_GPIO_PIN_MAP(0, 18)
#define LED2_PIN                       NRF_GPIO_PIN_MAP(0, 13)
#define LED3_PIN                       NRF_GPIO_PIN_MAP(0, 11)

#define APP_BLE_CONN_CFG_TAG           1
#define DEVICE_NAME                    "Repetidor"
#define NUS_SERVICE_UUID_TYPE          BLE_UUID_TYPE_VENDOR_BEGIN
#define APP_BLE_OBSERVER_PRIO          3
#define APP_ADV_INTERVAL               64
#define APP_ADV_DURATION               18000 // 0 = advertising continuo (sin timeout)
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

static bool m_emisor_nus_ready        = false;
static bool m_advertising_active      = false; // Rastrear estado del advertising
static bool m_advertising_initialized = false; // Rastrear si el advertising ya fue inicializado
static app_nus_server_on_data_received_t m_on_data_received     = 0;
static uint16_t                          m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;
static uint16_t                          m_conn_handle          = BLE_CONN_HANDLE_INVALID;
static uint16_t                          m_emisor_conn_handle   = BLE_CONN_HANDLE_INVALID;
static uint8_t                           custom_mac_addr_[6]    = {0};
static uint8_t                           custom_mac_repeater_addr_[6] = {0};
static uint8_t                           custom_mac_scan_addr_[6]     = {0};
static ble_gap_addr_t                    m_target_periph_addr;

static ble_uuid_t m_adv_uuids[] = {{BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}};

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

ret_code_t change_mac_repeater(void)
{
    ret_code_t            ret;
    static ble_gap_addr_t mac_custom_repeater;
    mac_custom_repeater.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;

    uint8_t dummy_mac_repeater[6];

    load_mac_from_flash(mac_custom_repeater.addr, MAC_REPEATER);

    // for (int i = 0; i < 6; i++)
    //     mac_custom_repeater.addr[i] = dummy_mac_repeater[i];
    //
    NRF_LOG_RAW_INFO("\n> MAC personalizada: %02X:%02X:%02X:%02X:%02X:%02X",
                     mac_custom_repeater.addr[5], mac_custom_repeater.addr[4],
                     mac_custom_repeater.addr[3], mac_custom_repeater.addr[2],
                     mac_custom_repeater.addr[1], mac_custom_repeater.addr[0]);

    ret = sd_ble_gap_addr_set(&mac_custom_repeater);
    if (ret == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\nMAC personalizada del repetidor cargada!");
    }
    else
    {
        NRF_LOG_RAW_INFO("\nERROR al cargar MAC personalizada del repetidor");
    }

    return ret;
}

ret_code_t send_configuration(config_t const *config_repetidor)
{
    if (config_repetidor == NULL)
    {
        NRF_LOG_ERROR("send_configuration: config_repetidor is NULL");
        return NRF_ERROR_NULL;
    }

    // Verificar que hay conexión NUS activa
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_WARNING("send_configuration: No hay conexión NUS activa");
        return NRF_ERROR_INVALID_STATE;
    }

    ret_code_t err_code = NRF_SUCCESS;
    char       response[64]; // Buffer para mensajes de respuesta

    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m=== Enviando configuracion completa del repetidor ===\x1b[0m");

    // 0. Enviar mensaje de inicio
    const char *start_msg = "CONFIG_START";
    err_code              = app_nus_server_send_data((uint8_t *)start_msg, strlen(start_msg));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando mensaje de inicio: 0x%X", err_code);
        return err_code;
    }

    // 1. Enviar MAC del repetidor
    snprintf(response, sizeof(response), "CONFIG_MAC_REP:%02X%02X%02X%02X%02X%02X",
             config_repetidor->mac_repetidor_config[5], config_repetidor->mac_repetidor_config[4],
             config_repetidor->mac_repetidor_config[3], config_repetidor->mac_repetidor_config[2],
             config_repetidor->mac_repetidor_config[1], config_repetidor->mac_repetidor_config[0]);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando MAC repetidor: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> MAC Repetidor enviada: %s", response);
    nrf_delay_ms(50); // Pequeña pausa entre envíos

    // 2. Enviar MAC del emisor
    snprintf(response, sizeof(response), "CONFIG_MAC_EMI:%02X%02X%02X%02X%02X%02X",
             config_repetidor->mac_emisor_config[5], config_repetidor->mac_emisor_config[4],
             config_repetidor->mac_emisor_config[3], config_repetidor->mac_emisor_config[2],
             config_repetidor->mac_emisor_config[1], config_repetidor->mac_emisor_config[0]);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando MAC emisor: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> MAC Emisor enviada: %s", response);
    nrf_delay_ms(50);

    // 3. Enviar MAC de escaneo
    snprintf(response, sizeof(response), "CONFIG_MAC_SCAN:%02X%02X%02X%02X%02X%02X",
             config_repetidor->mac_escaneo_config[5], config_repetidor->mac_escaneo_config[4],
             config_repetidor->mac_escaneo_config[3], config_repetidor->mac_escaneo_config[2],
             config_repetidor->mac_escaneo_config[1], config_repetidor->mac_escaneo_config[0]);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando MAC escaneo: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> MAC Escaneo enviada: %s", response);
    nrf_delay_ms(50);

    // 4. Enviar tiempo de encendido (en segundos)
    snprintf(response, sizeof(response), "CONFIG_TIME_ON:%lu",
             config_repetidor->tiempo_encendido_config / 1000);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando tiempo encendido: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> Tiempo Encendido enviado: %s", response);
    nrf_delay_ms(50);

    // 5. Enviar tiempo de dormido (en segundos)
    snprintf(response, sizeof(response), "CONFIG_TIME_SLEEP:%lu",
             config_repetidor->tiempo_dormido_config / 1000);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando tiempo dormido: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> Tiempo Dormido enviado: %s", response);
    nrf_delay_ms(50);

    // 6. Enviar tiempo de búsqueda (en segundos)
    snprintf(response, sizeof(response), "CONFIG_TIME_SEARCH:%lu",
             config_repetidor->tiempo_busqueda_config / 1000);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando tiempo busqueda: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> Tiempo Busqueda enviado: %s", response);
    nrf_delay_ms(50);

    // 7. Enviar la version del firmware del repetidor
    snprintf(response, sizeof(response), "FIRM_VERSION:%u.%u.%u", config_repetidor->version[0],
             config_repetidor->version[1], config_repetidor->version[2]);

    err_code = app_nus_server_send_data((uint8_t *)response, strlen(response));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando version del firmware: 0x%X", err_code);
        return err_code;
    }
    NRF_LOG_RAW_INFO("\n> Version del firmware enviado: %s", response);
    nrf_delay_ms(50);

    // 8. Enviar mensaje de finalización
    const char *end_msg = "CONFIG_END";
    err_code            = app_nus_server_send_data((uint8_t *)end_msg, strlen(end_msg));
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error enviando mensaje de fin: 0x%X", err_code);
        return err_code;
    }

    NRF_LOG_RAW_INFO("\n\x1b[1;32m=== Configuracion enviada exitosamente ===\x1b[0m");
    return NRF_SUCCESS;
}

/**
 * @brief Envía la configuración completa del repetidor en formato binario optimizado
 * @details Esta función optimizada envía todos los datos de configuración en un solo paquete
 *          en formato binario para procesamiento más rápido en el receptor.
 *
 *          Formato binario: identificador + datos binarios (NO texto hexadecimal)
 *          Estructura de datos (en orden):
 *          - Identificador: 2 bytes (0xCC, 0xAA) - BYTES BINARIOS
 *          - MAC Repetidor: 6 bytes (little endian)
 *          - MAC Emisor: 6 bytes (little endian)
 *          - MAC Escaneo: 6 bytes (little endian)
 *          - Tiempo Encendido: 4 bytes (uint32_t, little endian)
 *          - Tiempo Dormido: 4 bytes (uint32_t, little endian)
 *          - Tiempo Búsqueda: 4 bytes (uint32_t, little endian)
 *          - Versión firmware: 3 bytes (v1.v2.v3)
 *
 *          Total: 35 bytes binarios (NO caracteres de texto)
 *
 * @param config_repetidor Puntero a la estructura de configuración
 * @return ret_code_t Código de resultado de la operación
 */
ret_code_t send_configuration_nus(config_t const *config_repetidor)
{
    if (config_repetidor == NULL)
    {
        NRF_LOG_ERROR("send_configuration_nus: config_repetidor is NULL");
        return NRF_ERROR_NULL;
    }

    // Verificar que hay conexión NUS activa
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_WARNING("send_configuration_nus: No hay conexión NUS activa");
        return NRF_ERROR_INVALID_STATE;
    }

    // Buffer binario para los datos
    // 2 bytes identificador + 33 bytes datos = 35 bytes total
    uint8_t  binary_data[35];
    uint8_t *ptr = binary_data;

    NRF_LOG_RAW_INFO(
        "\n\n\x1b[1;36m=== Enviando configuracion en formato binario optimizado ===\x1b[0m");

    // Añadir bytes identificadores 0xCC, 0xAA al inicio
    *ptr++ = 0xCC;
    *ptr++ = 0xAA;

    // 1. MAC del repetidor (6 bytes) - en orden inverso para formato little endian
    for (int i = 5; i >= 0; i--)
    {
        *ptr++ = config_repetidor->mac_repetidor_config[i];
    }

    // 2. MAC del emisor (6 bytes) - en orden inverso para formato little endian
    for (int i = 5; i >= 0; i--)
    {
        *ptr++ = config_repetidor->mac_emisor_config[i];
    }

    // 3. MAC de escaneo (6 bytes) - en orden inverso para formato little endian
    for (int i = 5; i >= 0; i--)
    {
        *ptr++ = config_repetidor->mac_escaneo_config[i];
    }

    // 4. Tiempo de encendido (4 bytes, little endian)
    uint32_t tiempo_on = config_repetidor->tiempo_encendido_config;
    *ptr++             = (uint8_t)(tiempo_on & 0xFF);
    *ptr++             = (uint8_t)((tiempo_on >> 8) & 0xFF);
    *ptr++             = (uint8_t)((tiempo_on >> 16) & 0xFF);
    *ptr++             = (uint8_t)((tiempo_on >> 24) & 0xFF);

    // 5. Tiempo de dormido (4 bytes, little endian)
    uint32_t tiempo_sleep = config_repetidor->tiempo_dormido_config;
    *ptr++                = (uint8_t)(tiempo_sleep & 0xFF);
    *ptr++                = (uint8_t)((tiempo_sleep >> 8) & 0xFF);
    *ptr++                = (uint8_t)((tiempo_sleep >> 16) & 0xFF);
    *ptr++                = (uint8_t)((tiempo_sleep >> 24) & 0xFF);

    // 6. Tiempo de búsqueda (4 bytes, little endian)
    uint32_t tiempo_search = config_repetidor->tiempo_busqueda_config;
    *ptr++                 = (uint8_t)(tiempo_search & 0xFF);
    *ptr++                 = (uint8_t)((tiempo_search >> 8) & 0xFF);
    *ptr++                 = (uint8_t)((tiempo_search >> 16) & 0xFF);
    *ptr++                 = (uint8_t)((tiempo_search >> 24) & 0xFF);

    // 7. Versión del firmware (3 bytes)
    *ptr++ = config_repetidor->version[0];
    *ptr++ = config_repetidor->version[1];
    *ptr++ = config_repetidor->version[2];

    // Enviar los datos binarios
    ret_code_t err_code = app_nus_server_send_data(binary_data, sizeof(binary_data));
    if (err_code != NRF_SUCCESS)
    {
        const char *error_msg;
        switch (err_code)
        {
        case NRF_ERROR_INVALID_STATE:
            error_msg = "Estado inválido (sin conexión o notificaciones deshabilitadas)";
            break;
        case NRF_ERROR_RESOURCES:
            error_msg = "Buffer de transmisión lleno";
            break;
        case NRF_ERROR_INVALID_PARAM:
            error_msg = "Parámetros inválidos";
            break;
        case NRF_ERROR_BUSY:
            error_msg = "Servicio ocupado";
            break;
        default:
            error_msg = "Error desconocido";
            break;
        }

        NRF_LOG_ERROR("Error enviando configuración binaria: 0x%X (%s)", err_code, error_msg);
        NRF_LOG_RAW_INFO("\n> Estado conexión: handle=0x%04X, activa=%s", m_conn_handle,
                         (m_conn_handle != BLE_CONN_HANDLE_INVALID) ? "SI" : "NO");

        // Ejecutar diagnóstico completo cuando hay error
        diagnose_nus_connection();

        return err_code;
    }

    // Log de información enviada (mostrar en hexadecimal para debugging)
    NRF_LOG_RAW_INFO("\n> Configuración binaria enviada (%d bytes):", sizeof(binary_data));
    NRF_LOG_RAW_INFO("\n> Datos hex: ");
    for (int i = 0; i < sizeof(binary_data); i++)
    {
        NRF_LOG_RAW_INFO("%02X", binary_data[i]);
    }

    NRF_LOG_RAW_INFO("\n> Decodificación:");
    NRF_LOG_RAW_INFO("\n  - Identificador: 0x%02X 0x%02X", binary_data[0], binary_data[1]);
    NRF_LOG_RAW_INFO(
        "\n  - MAC Repetidor: %02X:%02X:%02X:%02X:%02X:%02X",
        config_repetidor->mac_repetidor_config[5], config_repetidor->mac_repetidor_config[4],
        config_repetidor->mac_repetidor_config[3], config_repetidor->mac_repetidor_config[2],
        config_repetidor->mac_repetidor_config[1], config_repetidor->mac_repetidor_config[0]);
    NRF_LOG_RAW_INFO("\n  - MAC Emisor: %02X:%02X:%02X:%02X:%02X:%02X",
                     config_repetidor->mac_emisor_config[5], config_repetidor->mac_emisor_config[4],
                     config_repetidor->mac_emisor_config[3], config_repetidor->mac_emisor_config[2],
                     config_repetidor->mac_emisor_config[1],
                     config_repetidor->mac_emisor_config[0]);
    NRF_LOG_RAW_INFO(
        "\n  - MAC Escaneo: %02X:%02X:%02X:%02X:%02X:%02X", config_repetidor->mac_escaneo_config[5],
        config_repetidor->mac_escaneo_config[4], config_repetidor->mac_escaneo_config[3],
        config_repetidor->mac_escaneo_config[2], config_repetidor->mac_escaneo_config[1],
        config_repetidor->mac_escaneo_config[0]);
    NRF_LOG_RAW_INFO("\n  - Tiempo ON: %lu ms", config_repetidor->tiempo_encendido_config);
    NRF_LOG_RAW_INFO("\n  - Tiempo SLEEP: %lu ms", config_repetidor->tiempo_dormido_config);
    NRF_LOG_RAW_INFO("\n  - Tiempo SEARCH: %lu ms", config_repetidor->tiempo_busqueda_config);
    NRF_LOG_RAW_INFO("\n  - Versión FW: %u.%u.%u", config_repetidor->version[0],
                     config_repetidor->version[1], config_repetidor->version[2]);

    NRF_LOG_RAW_INFO("\n\x1b[1;32m=== Configuración binaria enviada exitosamente ===\x1b[0m");
    return NRF_SUCCESS;
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
            NRF_LOG_RAW_INFO("\t>> Se encontraron %d registros validos.\n", stat.valid_records);
            NRF_LOG_RAW_INFO("\t>> Se encontraron %d registros no validos.", stat.dirty_records);

            if (stat.dirty_records > 0)
            {
                // Realiza la recolección de basura
                NRF_LOG_RAW_INFO("\n\t>> Limpiando registros no validos...");
                perform_garbage_collection();
            }
            NRF_LOG_RAW_INFO("\n\t>> \033[0;32mModulo inicializado correctamente.\033[0m");
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
            NRF_LOG_RAW_INFO("\nError al escribir el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_UPDATE)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\n\x1b[1;32m>>\x1b[0m Registro actualizado correctamente!");
        }
        else
        {
            NRF_LOG_RAW_INFO("\nError al actualizar el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_DEL_RECORD)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;32mRegistro eliminado correctamente.\x1b[0m");
        }
        else
        {
            NRF_LOG_RAW_INFO("\nError al eliminar el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_DEL_FILE)
    {
        if (p_evt->result == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nArchivo eliminado correctamente y todos los registros asociados.");
        }
        else
        {
            NRF_LOG_RAW_INFO("\nError al eliminar el archivo: %d", p_evt->result);
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
        NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);

        // Asegúrate de que el mensaje sea tratado como una cadena de texto
        char message[BLE_NUS_MAX_DATA_LEN]; // +1 para el carácter nulo
        if (p_evt->params.rx_data.length < sizeof(message))
        {
            memcpy(message, p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);

            message[p_evt->params.rx_data.length] = '\0'; // Agregar terminador nulo

            // Imprime el mensaje recibido
            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Mensaje recibido: \x1b[0m%s", message);

            // Verifica si el mensaje comienza con "111" par interpretar el comando
            if (p_evt->params.rx_data.length >= 5 && message[0] == '1' && message[1] == '1' &&
                message[2] == '1')
            {
                // Extrae el comando (los dos caracteres después de "111")
                char command[3] = {message[3], message[4], '\0'}; // Comando de 2 caracteres

                // Manejo de comandos con un switch-case
                switch (atoi(command)) // Convierte el comando a entero
                {

                    //================================================================================================
                    // COMANDOS REPETIDOR
                    //================================================================================================

                case 1: // Comando 01: Guardar MAC
                {
                    size_t mac_length = p_evt->params.rx_data.length - 5;
                    if (mac_length == 12) // Verifica que la longitud sea válida
                    {
                        for (size_t i = 0; i < 6; i++)
                        {
                            char byte_str[3] = {message[5 + i * 2], message[6 + i * 2], '\0'};
                            custom_mac_addr_[5 - i] = (uint8_t)strtol(byte_str, NULL, 16);
                        }
                        NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 01 recibido: "
                                         "Cambiar MAC \x1b[0m");
                        NRF_LOG_RAW_INFO("\n> MAC recibida: "
                                         "%02X:%02X:%02X:%02X:%02X:%02X",
                                         custom_mac_addr_[5], custom_mac_addr_[4],
                                         custom_mac_addr_[3], custom_mac_addr_[2],
                                         custom_mac_addr_[1], custom_mac_addr_[0]);

                        // Guarda la MAC en la memoria flash y reinicia el
                        // dispositivo
                        save_mac_to_flash(custom_mac_addr_, MAC_FILTRADO);
                    }
                    else
                    {
                        NRF_LOG_WARNING("Longitud de MAC invalida: %d", mac_length);
                    }
                    break;
                }

                case 2: // Comando 02: Muestra la MAC custom guardada en memoria
                {
                    uint8_t mac_print[6];
                    // Carga la MAC desde la memoria flash
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 02 recibido: Mostrando MAC guardada \x1b[0m");
                    load_mac_from_flash(mac_print, MAC_FILTRADO);
                }
                break;

                case 3: // Comando 03: Reiniciar el dispositivo
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 03 recibido: Reiniciando "
                                     "dispositivo...\n\n\n\n");
                    nrf_delay_ms(1000);
                    NVIC_SystemReset();

                    break;

                case 4: // Guardar tiempo de encendido
                {
                    if (p_evt->params.rx_data.length >= 6) // Verifica que haya datos suficientes
                    {
                        char time_str[5] = {message[5], message[6], message[7], message[8], '\0'};
                        uint32_t time_in_seconds __attribute__((aligned(4))) =
                            atoi(time_str) * 1000;
                        if (time_in_seconds <= 999000)
                        {
                            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 04 recibido: "
                                             "Cambiar tiempo de encendido \x1b[0m");
                            write_time_to_flash(TIEMPO_ENCENDIDO, time_in_seconds);
                        }
                        else
                        {
                            NRF_LOG_RAW_INFO("\nEl tiempo de encendido excede el maximo "
                                             "permitido (999 segundos).");
                        }
                    }
                    else
                    {
                        NRF_LOG_RAW_INFO("\nComando 04 recibido con datos insuficientes.");
                    }
                    break;
                }

                case 5: // Comando 05: Leer tiempo de encendido desde la memoria
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 05 recibido: Leer tiempo de encendido \x1b[0m");
                    uint32_t sleep_time_ms =
                        read_time_from_flash(TIEMPO_ENCENDIDO, DEFAULT_DEVICE_ON_TIME_MS);

                    break;
                }

                case 6: // Comando 06: Guardar tiempo de apagado
                {
                    if (p_evt->params.rx_data.length >= 6)
                    {
                        // Calcular la longitud del número (hasta 5 caracteres máximo)
                        size_t time_length = p_evt->params.rx_data.length - 5; // Restar "11106"
                        if (time_length > 5)
                            time_length = 5; // Máximo 5 caracteres

                        char time_str[6] = {0}; // Buffer para hasta 5 dígitos + terminador nulo
                        memcpy(time_str, &message[5], time_length);
                        time_str[time_length] = '\0';

                        uint32_t time_in_seconds __attribute__((aligned(4))) =
                            atoi(time_str) * 1000;
                        if (time_in_seconds <= 82500000) // Máximo 82500 segundos
                        {
                            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 06 recibido: "
                                             "Cambiar tiempo de dormido \x1b[0m");
                            NRF_LOG_RAW_INFO("\n> Tiempo configurado: %lu segundos (%s)",
                                             time_in_seconds / 1000, time_str);
                            write_time_to_flash(TIEMPO_SLEEP, time_in_seconds);
                        }
                        else
                        {
                            NRF_LOG_RAW_INFO("\nEl tiempo de dormido excede el maximo "
                                             "permitido (82500 segundos).");
                        }
                    }
                    else
                    {
                        NRF_LOG_RAW_INFO("\nComando 06 recibido con datos insuficientes.");
                    }
                    break;
                }

                case 7: // Comando 07: Leer tiempo de dormido desde la memoria flash
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 07 recibido: Leer tiempo de dormido\x1b[0m");
                    uint32_t sleep_time_ms =
                        read_time_from_flash(TIEMPO_SLEEP, DEFAULT_DEVICE_SLEEP_TIME_MS);
                    // Imprime el tiempo de dormido
                    NRF_LOG_RAW_INFO("\n> Tiempo de dormido configurado: %lu segundos",
                                     sleep_time_ms / 1000);
                    break;
                }

                case 8: // Comando 08: Escribe en la memoria flash la fecha, hora, formato
                        // YYYYMMDDHHMMSS
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 08 recibido: Guardar fecha y hora "
                                     "- YYYYMMDDHHMMSS\x1b[0m");

                    if (p_evt->params.rx_data.length >= 19) // 5 de comando + 14 de fecha/hora
                    {
                        // Crear buffer local para garantizar terminador nulo
                        char fecha_buffer[15] = {0}; // 14 caracteres + terminador nulo

                        // Copiar exactamente 14 caracteres de fecha/hora
                        memcpy(fecha_buffer, &message[5], 14);
                        fecha_buffer[14] = '\0'; // Garantizar terminador nulo

                        // Debug: mostrar cadena recibida
                        NRF_LOG_RAW_INFO("\n> Cadena de fecha recibida: '%s' (longitud: %d)",
                                         fecha_buffer, strlen(fecha_buffer));

                        // Crear estructura de fecha local
                        datetime_t dt = {0};

                        // Parsing con validación mejorada
                        int parsed = sscanf(fecha_buffer, "%4hu%2hhu%2hhu%2hhu%2hhu%2hhu", &dt.year,
                                            &dt.month, &dt.day, &dt.hour, &dt.minute, &dt.second);

                        if (parsed == 6)
                        {
                            // Validación básica de rangos
                            if (dt.year >= 2000 && dt.year <= 2099 && dt.month >= 1 &&
                                dt.month <= 12 && dt.day >= 1 && dt.day <= 31 && dt.hour <= 23 &&
                                dt.minute <= 59 && dt.second <= 59)
                            {
                                NRF_LOG_RAW_INFO("\n> Fecha parseada correctamente: "
                                                 "%04u-%02u-%02u %02u:%02u:%02u",
                                                 dt.year, dt.month, dt.day, dt.hour, dt.minute,
                                                 dt.second);

                                // Guardar en flash
                                err_code = write_date_to_flash(&dt);

                                if (err_code == NRF_SUCCESS)
                                {
                                    NRF_LOG_RAW_INFO("\n\x1b[1;32m> Fecha guardada exitosamente en "
                                                     "flash\x1b[0m");
                                }
                                else
                                {
                                    NRF_LOG_ERROR(
                                        "\n\x1b[1;31m> Error guardando fecha: 0x%X\x1b[0m",
                                        err_code);
                                }
                            }
                            else
                            {
                                NRF_LOG_WARNING(
                                    "\n\x1b[1;33m> Valores de fecha fuera de rango\x1b[0m");
                                NRF_LOG_RAW_INFO("\n  Ano: %u, Mes: %u, Dia: %u", dt.year, dt.month,
                                                 dt.day);
                                NRF_LOG_RAW_INFO("\n  Hora: %u, Min: %u, Seg: %u", dt.hour,
                                                 dt.minute, dt.second);
                            }
                        }
                        else
                        {
                            NRF_LOG_WARNING(
                                "\n\x1b[1;33m> Error en parsing. Elementos parseados: %d/6\x1b[0m",
                                parsed);
                            NRF_LOG_RAW_INFO("\n> Formato esperado: YYYYMMDDHHMMSS");
                            NRF_LOG_RAW_INFO("\n> Ejemplo: 20250804143025 (2025-08-04 14:30:25)");
                        }
                    }
                    else
                    {
                        NRF_LOG_WARNING("\n\x1b[1;33m> Datos insuficientes. Longitud recibida: %d, "
                                        "esperada: 19+\x1b[0m",
                                        p_evt->params.rx_data.length);
                        NRF_LOG_RAW_INFO("\n> Formato: 11108YYYYMMDDHHMMSS");
                    }
                    break;
                }

                case 9: // Comando 09: Leer fecha y hora almacenada en flash
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 09 recibido: Leer fecha y hora\x1b[0m");

                    datetime_t dt = read_date_from_flash();

                    NRF_LOG_RAW_INFO("\n>> Fecha almacenada: %04u-%02u-%02u %02u:%02u:%02u",
                                     dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

                    break;
                }

                case 10: // Guarda en la memoria el tiempo de encendido extendido
                {
                    if (p_evt->params.rx_data.length >= 6)
                    {
                        char     time_str[4] = {message[5], message[6], message[7], '\0'};
                        uint32_t time_in_seconds_extended __attribute__((aligned(4))) =
                            atoi(time_str) * 1000;
                        if (time_in_seconds_extended <= 666000)
                        {
                            NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 10 recibido: "
                                             "Cambiar tiempo de encendido extendido\x1b[0m");
                            write_time_to_flash(TIEMPO_ENCENDIDO_EXTENDED,
                                                time_in_seconds_extended);
                        }
                        else
                        {
                            NRF_LOG_RAW_INFO("\nEl tiempo de encendido extendido excede el maximo "
                                             "permitido (666 segundos).");
                        }
                    }
                    else
                    {
                        NRF_LOG_RAW_INFO("\nComando 10 recibido con datos insuficientes.");
                    }
                    break;
                }
                case 11: // Solicitar tiempo de encendido extendido
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 11 recibido: Solicitar tiempo de "
                                     "encendido extendido\x1b[0m");
                    uint32_t encendido_extendido_ms = read_time_from_flash(
                        TIEMPO_ENCENDIDO_EXTENDED, DEFAULT_DEVICE_ON_TIME_EXTENDED_MS);
                    NRF_LOG_RAW_INFO("\n> Tiempo de encendido extendido configurado: %lu segundos",
                                     encendido_extendido_ms / 1000);

                    break;
                }
                case 12: // Solicitar un registro de historial por ID
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 12 recibido: Solicitar registro de "
                                     "historial por ID\x1b[0m");
                    if (p_evt->params.rx_data.length > 5) // Verifica que haya datos suficientes
                    {
                        // Extrae el ID del registro como string (todo lo que sigue después de
                        // "11112")
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
                                NRF_LOG_RAW_INFO(
                                    "\nRegistro %u leido correctamente, enviando por NUS...",
                                    registro_id);

                                // Preparar array de datos en formato hex (igual que
                                // send_all_history)
                                uint8_t  data_array[244];
                                uint16_t position = 0;

                                // Byte 0: Magic
                                data_array[position++] = 0x08;

                                // Bytes 1-7: Fecha y hora
                                data_array[position++] = registro_historial.day;
                                data_array[position++] = registro_historial.month;
                                data_array[position++] = (registro_historial.year >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.year & 0xFF);
                                data_array[position++] = registro_historial.hour;
                                data_array[position++] = registro_historial.minute;
                                data_array[position++] = registro_historial.second;

                                // Bytes 8-11: Contador (4 bytes) - convertir a big-endian
                                data_array[position++] = (registro_historial.contador >> 24) & 0xFF;
                                data_array[position++] = (registro_historial.contador >> 16) & 0xFF;
                                data_array[position++] = (registro_historial.contador >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.contador & 0xFF);

                                // Bytes 12-15: V1, V2 (2 bytes cada uno) - convertir a big-endian
                                data_array[position++] = (registro_historial.V1 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V1 & 0xFF);
                                data_array[position++] = (registro_historial.V2 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V2 & 0xFF);

                                // Byte 16: Battery
                                data_array[position++] = registro_historial.battery;

                                // Bytes 17-28: MACs (rellenar con ceros)
                                for (int j = 0; j < 12; j++)
                                {
                                    data_array[position++] = 0x00;
                                }

                                // Bytes 29-40: V3-V8 (2 bytes cada uno) - convertir a big-endian
                                data_array[position++] = (registro_historial.V3 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V3 & 0xFF);
                                data_array[position++] = (registro_historial.V4 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V4 & 0xFF);
                                data_array[position++] = (registro_historial.V5 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V5 & 0xFF);
                                data_array[position++] = (registro_historial.V6 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V6 & 0xFF);
                                data_array[position++] = (registro_historial.V7 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V7 & 0xFF);
                                data_array[position++] = (registro_historial.V8 >> 8) & 0xFF;
                                data_array[position++] = (registro_historial.V8 & 0xFF);

                                // Byte 41: Temperatura
                                data_array[position++] = registro_historial.temp;

                                // Byte 42-43: ID del registro solicitado (en lugar de
                                // last_position)
                                data_array[position++] = (registro_id >> 8) & 0xFF;
                                data_array[position++] = (registro_id & 0xFF);

                                // Enviar por NUS al celular
                                ret_code_t send_result =
                                    app_nus_server_send_data(data_array, position);
                                if (send_result == NRF_SUCCESS)
                                {
                                    NRF_LOG_RAW_INFO("\n\x1b[1;32m> Registro #%u enviado por NUS "
                                                     "exitosamente\x1b[0m",
                                                     registro_id);
                                }
                                else
                                {
                                    NRF_LOG_RAW_INFO("\n\x1b[1;31m> Error enviando registro #%u "
                                                     "por NUS: 0x%X\x1b[0m",
                                                     registro_id, send_result);
                                }

                                // También imprimir en consola para depuración
                                char titulo[41];
                                snprintf(titulo, sizeof(titulo),
                                         "Historial enviado \x1B[33m#%u\x1B[0m", registro_id);
                                print_history_record(&registro_historial, titulo);
                                NRF_LOG_FLUSH();
                            }
                            else
                            {
                                NRF_LOG_RAW_INFO(
                                    "\n\x1b[1;31m> Error al leer el registro %u: 0x%X\x1b[0m",
                                    registro_id, err_code);
                            }
                        }
                        else
                        {
                            NRF_LOG_WARNING("ID de registro demasiado largo.");
                        }
                    }
                    break;
                }
                case 13: // Comando para borrar un historial segun el historial
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 13 recibido: Solicitar el borrado "
                                     "de un historial por ID\x1b[0m");

                    if (p_evt->params.rx_data.length > 5)
                    {
                        size_t id_len    = p_evt->params.rx_data.length - 5;
                        char   id_str[8] = {0};

                        if (id_len < sizeof(id_str))
                        {
                            memcpy(id_str, &message[5], id_len);
                            id_str[id_len]       = '\0';
                            uint16_t registro_id = (uint16_t)atoi(id_str);

                            NRF_LOG_RAW_INFO("\n> ID a borrar: %u (%s)", registro_id, id_str);

                            err_code = delete_history_record_by_id(registro_id);
                        }
                        else
                        {
                            NRF_LOG_WARNING("ID de registro demasiado largo.");
                        }
                    }
                    else
                    {
                        NRF_LOG_WARNING("Comando 13 recibido con datos insuficientes.");
                    }

                    break;
                }

                case 14: // Pide una lectura de todos los historiales
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 14: Solicitud del historial completo\x1b[0m");
                    // Llama a la función para solicitar el historial completo
                    send_all_history();

                    break;
                }
                case 15: // Comando para iniciar modo de escaneo de paquetes
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 15 recibido: Iniciar modo de "
                                     "escaneo de paquetes\x1b[0m");

                    if (packet_scan_mode_is_active())
                    {
                        NRF_LOG_RAW_INFO("\n\x1b[1;33m> Modo de escaneo ya está activo\x1b[0m");
                        // Enviar estado actual
                        char response[50];
                        snprintf(response, sizeof(response), "SCAN_ACTIVE");
                        app_nus_server_send_data((uint8_t *)response, strlen(response));
                    }
                    else
                    {
                        packet_scan_mode_start();
                        if (packet_scan_mode_is_active())
                        {
                            app_nus_server_send_data((uint8_t *)"SCAN_STARTED", 12);
                        }
                        else
                        {
                            app_nus_server_send_data((uint8_t *)"SCAN_ERROR", 10);
                        }
                    }
                    break;
                }
                case 16: // Comando para detener modo de escaneo y obtener resultados
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 16 recibido: Detener modo de escaneo\x1b[0m");

                    if (packet_scan_mode_is_active())
                    {
                        uint32_t final_count = packet_scan_mode_get_count();
                        packet_scan_mode_stop();

                        // Enviar resultado final
                        char response[50];
                        snprintf(response, sizeof(response), "SCAN_RESULT:%lu", final_count);
                        app_nus_server_send_data((uint8_t *)response, strlen(response));

                        NRF_LOG_RAW_INFO(
                            "\n\x1b[1;32m> Escaneo detenido. Total de paquetes: %lu\x1b[0m",
                            final_count);
                    }
                    else
                    {
                        app_nus_server_send_data((uint8_t *)"SCAN_NOT_ACTIVE", 15);
                        NRF_LOG_RAW_INFO("\n\x1b[1;33m> Modo de escaneo no está activo\x1b[0m");
                    }
                    break;
                }
                case 17: // Comando para consultar estado del modo de escaneo
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 17 recibido: Consultar estado del "
                                     "escaneo\x1b[0m");

                    if (packet_scan_mode_is_active())
                    {
                        uint32_t current_count = packet_scan_mode_get_count();
                        char     response[50];
                        snprintf(response, sizeof(response), "SCAN_STATUS:ACTIVE:%lu",
                                 current_count);
                        ret_code_t send_result =
                            app_nus_server_send_data((uint8_t *)response, strlen(response));

                        if (send_result != NRF_SUCCESS)
                        {
                            NRF_LOG_RAW_INFO(
                                "\n\x1b[1;31m> Error enviando SCAN_STATUS: 0x%X\x1b[0m",
                                send_result);
                        }
                        else
                        {
                            NRF_LOG_RAW_INFO(
                                "\n\x1b[1;32m> SCAN_STATUS:ACTIVE:%lu enviado correctamente\x1b[0m",
                                current_count);
                        }

                        NRF_LOG_RAW_INFO(
                            "\n\x1b[1;36m> Estado: Activo, Paquetes detectados: %lu\x1b[0m",
                            current_count);
                    }
                    else
                    {
                        // Cuando está inactivo, también incluir el último conteo disponible
                        uint32_t last_count = packet_scan_mode_get_count();
                        char     response[50];
                        snprintf(response, sizeof(response), "SCAN_STATUS:INACTIVE:%lu",
                                 last_count);
                        ret_code_t send_result =
                            app_nus_server_send_data((uint8_t *)response, strlen(response));

                        if (send_result != NRF_SUCCESS)
                        {
                            NRF_LOG_RAW_INFO(
                                "\n\x1b[1;31m> Error enviando SCAN_STATUS: 0x%X\x1b[0m",
                                send_result);
                        }
                        else
                        {
                            NRF_LOG_RAW_INFO("\n\x1b[1;32m> SCAN_STATUS:INACTIVE:%lu enviado "
                                             "correctamente\x1b[0m",
                                             last_count);
                        }

                        NRF_LOG_RAW_INFO(
                            "\n\x1b[1;36m> Estado: Inactivo, Último conteo: %lu\x1b[0m",
                            last_count);
                    }
                    break;
                }
                case 18: // Comando para guardar MAC de escaneo
                {
                    size_t mac_length = p_evt->params.rx_data.length - 5;
                    if (mac_length == 12)
                    {
                        for (size_t i = 0; i < 6; i++)
                        {
                            char byte_str[3] = {message[5 + i * 2], message[6 + i * 2], '\0'};
                            custom_mac_scan_addr_[5 - i] = (uint8_t)strtol(byte_str, NULL, 16);
                        }

                        NRF_LOG_RAW_INFO(
                            "\n\n\x1b[1;36m--- Comando 18 recibido: Guardar MAC de escaneo\x1b[0m");
                        NRF_LOG_RAW_INFO("\n> MAC de escaneo recibida (parte 1): %02X:%02X:%02X",
                                         custom_mac_scan_addr_[5], custom_mac_scan_addr_[4],
                                         custom_mac_scan_addr_[3]);
                        NRF_LOG_RAW_INFO("\n> MAC de escaneo recibida (parte 2): %02X:%02X:%02X",
                                         custom_mac_scan_addr_[2], custom_mac_scan_addr_[1],
                                         custom_mac_scan_addr_[0]);

                        // Guardar la MAC de escaneo en la memoria flash
                        save_mac_to_flash(custom_mac_scan_addr_, MAC_ESCANEO);
                    }
                    break;
                }

                case 19: // Comando para leer MAC de escaneo guardada
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 19 recibido: Leer MAC de escaneo\x1b[0m");

                    uint8_t scan_mac[6];
                    load_mac_from_flash(scan_mac, MAC_ESCANEO);
                }
                break;

                case 20: // Guarda MAC custom del repetidor
                {
                    size_t mac_length = p_evt->params.rx_data.length - 5;
                    if (mac_length == 12)
                    {
                        for (size_t i = 0; i < 6; i++)
                        {
                            char byte_str[3] = {message[5 + i * 2], message[6 + i * 2], '\0'};
                            custom_mac_repeater_addr_[5 - i] = (uint8_t)strtol(byte_str, NULL, 16);
                        }

                        NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 20 recibido: Guardar MAC del "
                                         "repetidor\x1b[0m");
                        NRF_LOG_RAW_INFO(
                            "\n> MAC del repetidor recibida: %02X:%02X:%02X:%02X:%02X:%02X",
                            custom_mac_repeater_addr_[5], custom_mac_repeater_addr_[4],
                            custom_mac_repeater_addr_[3], custom_mac_repeater_addr_[2],
                            custom_mac_repeater_addr_[1], custom_mac_repeater_addr_[0]);

                        // Guardar la MAC del repetidor en la memoria flash
                        save_mac_to_flash(custom_mac_repeater_addr_, MAC_REPEATER);
                    }
                    break;
                }
                case 21: // Leer MAC custom del repetidor
                {
                    NRF_LOG_RAW_INFO(
                        "\n\n\x1b[1;36m--- Comando 21 recibido: Leer MAC del repetidor\x1b[0m");

                    uint8_t repeater_mac[6];
                    load_mac_from_flash(repeater_mac, MAC_REPEATER);

                    // Verificar si hay una MAC válida guardada
                    bool mac_is_zero = true;
                    for (int i = 0; i < 6; i++)
                    {
                        if (repeater_mac[i] != 0)
                        {
                            mac_is_zero = false;
                            break;
                        }
                    }

                    if (!mac_is_zero)
                    {
                        char response[26];
                        snprintf(response, sizeof(response),
                                 "REPEATER_MAC:%02X%02X%02X%02X%02X%02X", repeater_mac[5],
                                 repeater_mac[4], repeater_mac[3], repeater_mac[2], repeater_mac[1],
                                 repeater_mac[0]);
                        app_nus_server_send_data((uint8_t *)response, strlen(response));
                    }
                    else
                    {
                        NRF_LOG_RAW_INFO("\n> No hay MAC del repetidor configurada");
                        app_nus_server_send_data((uint8_t *)"REPEATER_MAC_NONE", 17);
                    }
                    break;
                }
                case 22: // Envia toda la configuración del repetidor por NUS
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 22 recibido: Envia la configuracion"
                                     " del repetidor\x1b[0m");
                    send_configuration_nus(&config_repetidor);
                    break;
                }
                case 23: // Envia toda la configuración del repetidor por NUS en formato hexadecimal
                         // optimizado
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 23 recibido: Envia la configuracion"
                                     " del repetidor (formato hex optimizado)\x1b[0m");
                    send_configuration_nus(&config_repetidor);
                    break;
                }
                case 24: // Diagnóstico de MAC del repetidor
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 24 recibido: Diagnóstico de MAC "
                                     "del repetidor\x1b[0m");
                    diagnose_mac_repeater_storage();
                    break;
                }
                case 25: // Comando para probar guardado de MAC del repetidor
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 25 recibido: Test guardado MAC del "
                                     "repetidor\x1b[0m");

                    // MAC de prueba: AA:BB:CC:DD:EE:FF
                    uint8_t test_mac[6] = {0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA};

                    NRF_LOG_RAW_INFO("\n> Guardando MAC de prueba: %02X:%02X:%02X:%02X:%02X:%02X",
                                     test_mac[5], test_mac[4], test_mac[3], test_mac[2],
                                     test_mac[1], test_mac[0]);

                    save_mac_to_flash(test_mac, MAC_REPEATER);

                    // Verificar inmediatamente
                    nrf_delay_ms(500);
                    diagnose_mac_repeater_storage();

                    break;
                }
                case 99: // Comando para borrar todos los historiales
                {
                    NRF_LOG_RAW_INFO("\n\n\x1b[1;36m--- Comando 99 recibido: Borrar todos los "
                                     "historiales\x1b[0m");

                    delete_all_history();
                    break;
                }

                    //================================================================================================
                    // AQUI TERMINAN LOS COMANDOS REPETIDOR
                    //================================================================================================

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
        // El buffer de transmisión está listo - enviar siguiente paquete del comando 15/16 si está
        // activo También manejar el envío asíncrono de historial
        history_send_next_packet();
    }
}

static void gap_params_init(void)
{
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code =
        sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *)DEVICE_NAME, strlen(DEVICE_NAME));
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
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
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
    ret_code_t err_code;

    switch (ble_adv_evt)
    {
    case BLE_ADV_EVT_FAST:
        NRF_LOG_RAW_INFO("\n\x1b[1;32m[ADV EVENT]\x1b[0m Advertising FAST iniciado");
        m_advertising_active = true; // Actualizar estado
        err_code             = bsp_indication_set(BSP_INDICATE_ADVERTISING);
        APP_ERROR_CHECK(err_code);
        break;

    case BLE_ADV_EVT_IDLE:
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[ADV EVENT]\x1b[0m Advertising entró en IDLE");
        m_advertising_active = false; // Actualizar estado

        // Solo reiniciar advertising si el dispositivo está activo y no hay conexión
        if (m_device_active && m_conn_handle == BLE_CONN_HANDLE_INVALID)
        {
            NRF_LOG_RAW_INFO(
                "\n\x1b[1;36m[ADV EVENT]\x1b[0m Reiniciando advertising desde IDLE...");
            advertising_start();
        }
        break;

    case BLE_ADV_EVT_FAST_WHITELIST:
        NRF_LOG_RAW_INFO("\n\x1b[1;32m[ADV EVENT]\x1b[0m Advertising FAST WHITELIST iniciado");
        m_advertising_active = true;
        break;

    default:
        NRF_LOG_RAW_INFO("\n\x1b[1;35m[ADV EVENT]\x1b[0m Evento de advertising desconocido: %d",
                         ble_adv_evt);
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
            NRF_LOG_RAW_INFO("\n\x1b[1;32m[BLE EVENT]\x1b[0m Celular conectado (handle: %d)",
                             p_ble_evt->evt.gap_evt.conn_handle);
            m_conn_handle        = p_ble_evt->evt.gap_evt.conn_handle;
            m_advertising_active = false; // Advertising se detiene automáticamente al conectar
            restart_on_rtc_extended();

            nrf_gpio_pin_set(LED3_PIN);
        }
        else if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
        {
            NRF_LOG_RAW_INFO(
                "\n\x1b[1;32m[BLE EVENT]\x1b[0m Emisor conectado como central (handle: %d)",
                p_ble_evt->evt.gap_evt.conn_handle);

            nrf_gpio_pin_set(LED1_PIN);
            NRF_LOG_RAW_INFO("\nEmisor conectado\n");
            m_emisor_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
        }
        break;

    case BLE_GAP_EVT_DISCONNECTED:
        if (p_gap_evt->conn_handle == m_conn_handle)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;33m[BLE EVENT]\x1b[0m Celular desconectado (handle: %d)",
                             m_conn_handle);
            m_conn_handle = BLE_CONN_HANDLE_INVALID; // Invalida el handle del celular

            // El advertising debería reiniciarse automáticamente, pero lo forzamos manualmente
            // también
            NRF_LOG_RAW_INFO("\n\x1b[1;36m[RESTART ADV]\x1b[0m Reiniciando advertising tras "
                             "desconexión del celular...");
            advertising_start();
            nrf_gpio_pin_clear(LED3_PIN);
        }
        else if (p_gap_evt->conn_handle == m_emisor_conn_handle)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;33m[BLE EVENT]\x1b[0m Emisor desconectado (handle: %d)",
                             m_emisor_conn_handle);
            NRF_LOG_RAW_INFO("\n\n\033[1;31m>\033[0m Buscando emisor...\n");
            m_emisor_conn_handle = BLE_CONN_HANDLE_INVALID;
            scan_start();
            nrf_gpio_pin_clear(LED1_PIN);
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
        err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,
                                               NULL, NULL);
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
    if (data_array == NULL || length == 0)
    {
        NRF_LOG_ERROR("app_nus_server_send_data: Parámetros inválidos");
        return NRF_ERROR_INVALID_PARAM;
    }
    //
    // if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
    // {
    //     NRF_LOG_ERROR("app_nus_server_send_data: Sin conexión BLE activa");
    //     return NRF_ERROR_INVALID_STATE;
    // }

    NRF_LOG_RAW_INFO("Enviando %d bytes por NUS, handle=0x%04X", length, m_conn_handle);

    uint16_t   actual_length = length;
    ret_code_t err_code =
        ble_nus_data_send(&m_nus, (uint8_t *)data_array, &actual_length, m_conn_handle);

    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\nble_nus_data_send fallo: 0x%X, intentaba enviar %d bytes", err_code, length);
    }
    else
    {
        NRF_LOG_RAW_INFO("\nble_nus_data_send exitoso: %d bytes enviados", actual_length);
    }

    return err_code;
}

/**@brief Function for diagnosing NUS connection status
 */
void diagnose_nus_connection(void)
{
    NRF_LOG_RAW_INFO("\n\x1b[1;36m=== Diagnóstico de conexión NUS ===\x1b[0m");

    // Estado de conexión
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_RAW_INFO("\n❌ Sin conexión BLE activa");
    }
    else
    {
        NRF_LOG_RAW_INFO("\n✅ Conexión BLE activa, handle: 0x%04X", m_conn_handle);
    }

    // Estado de notificaciones (verificación simplificada)
    NRF_LOG_RAW_INFO("\n⚠️  Estado de notificaciones: No verificable directamente");
    NRF_LOG_RAW_INFO("\n   (Las notificaciones se verificarán durante el envío)");

    // MTU actual
    uint16_t current_mtu = m_ble_nus_max_data_len + 3; // +3 por overhead ATT
    NRF_LOG_RAW_INFO("\n📏 MTU actual: %d bytes (datos útiles: %d bytes)", current_mtu,
                     m_ble_nus_max_data_len);

    // Estado del servicio
    NRF_LOG_RAW_INFO("\n📡 Estado del servicio NUS: %s",
                     (m_conn_handle != BLE_CONN_HANDLE_INVALID) ? "Conectado" : "Desconectado");

    NRF_LOG_RAW_INFO("\n\x1b[1;36m================================\x1b[0m");
}

/**@brief Function for initializing the Advertising functionality.
 */

void advertising_init(void)
{
    // Verificar si ya fue inicializado para evitar NRF_ERROR_INVALID_STATE
    if (m_advertising_initialized)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[ADVERTISING]\x1b[0m Advertising ya inicializado, omitiendo "
                         "inicialización");
        return;
    }

    uint32_t                 err_code;
    ble_advertising_init_t   init;
    ble_advdata_manuf_data_t manuf_specific_data;

    memset(&m_beacon_info, 0, sizeof(m_beacon_info));
    // Se modifica para tener un numero de advertising mayor a 0
    // y poder ser procesado por la app
    if (adc_values.contador < 1)
        adc_values.contador = 1;

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

    init.advdata.name_type          = BLE_ADVDATA_NO_NAME; // BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = false;
    init.advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    init.config.ble_adv_on_disconnect_disabled =
        false; // PERMITIR reinicio automático del advertising
    init.advdata.p_manuf_specific_data  = &manuf_specific_data;

    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

    init.config.ble_adv_fast_enabled    = true;
    init.config.ble_adv_fast_interval   = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout    = APP_ADV_DURATION;
    init.evt_handler                    = on_adv_evt;

    err_code                            = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);

    // Marcar como inicializado después de éxito
    m_advertising_initialized = true;
    NRF_LOG_RAW_INFO("\n\x1b[1;32m[ADVERTISING]\x1b[0m Advertising inicializado exitosamente");
}

/**@brief Function for updating advertising data without reinitializing.
 */
void advertising_update_data(void)
{
    if (!m_advertising_initialized)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[ADVERTISING]\x1b[0m Advertising no inicializado, no se "
                         "pueden actualizar datos");
        return;
    }

    uint32_t                 err_code;
    ble_advdata_t            advdata;
    ble_advdata_manuf_data_t manuf_specific_data;

    memset(&m_beacon_info, 0, sizeof(m_beacon_info));
    // Se modifica para tener un numero de advertising mayor a 0
    // y poder ser procesado por la app
    if (adc_values.contador < 1)
        adc_values.contador = 1;

    m_beacon_info[1] = MSB_16(adc_values.contador);
    m_beacon_info[2] = LSB_16(adc_values.contador);
    m_beacon_info[3] = MSB_16(adc_values.V1);
    m_beacon_info[4] = LSB_16(adc_values.V1);
    m_beacon_info[5] = MSB_16(adc_values.V2);
    m_beacon_info[6] = LSB_16(adc_values.V2);

    // Configurar datos del manufacturer
    manuf_specific_data.company_identifier = 0x1133;
    manuf_specific_data.data.p_data        = (uint8_t *)m_beacon_info;
    manuf_specific_data.data.size          = sizeof(m_beacon_info);

    // Configurar advertising data
    memset(&advdata, 0, sizeof(advdata));
    advdata.name_type             = BLE_ADVDATA_NO_NAME;
    advdata.include_appearance    = false;
    advdata.flags                 = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;
    advdata.p_manuf_specific_data = &manuf_specific_data;

    // Actualizar advertising data
    err_code = ble_advertising_advdata_update(&m_advertising, &advdata, NULL);
    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;32m[ADVERTISING]\x1b[0m Datos de advertising actualizados exitosamente");
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;31m[ADVERTISING]\x1b[0m Error actualizando datos: %d", err_code);
    }
}

/**@brief Function for starting advertising.
 */
void advertising_start(void)
{
    if (m_advertising_active)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;33m[ADVERTISING]\x1b[0m Advertising ya está activo, omitiendo inicio");
        return;
    }

    uint32_t err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;31m[ADVERTISING ERROR]\x1b[0m Error al iniciar advertising: 0x%08X",
            err_code);
        m_advertising_active = false;
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;32m[ADVERTISING]\x1b[0m Advertising iniciado exitosamente "
                         "(continuo, sin timeout)");
        m_advertising_active = true;
    }
    // Posible crash
    // APP_ERROR_CHECK(err_code);
}

/**
 * @brief Función para verificar y reiniciar el advertising si es necesario
 * @details Verifica si el advertising debería estar activo y lo reinicia si no lo está
 */
void check_and_restart_advertising(void)
{
    // Solo intentar advertising si:
    // 1. El dispositivo está activo
    // 2. No hay conexión activa con celular
    // 3. El advertising no está actualmente activo
    if (m_device_active && m_conn_handle == BLE_CONN_HANDLE_INVALID && !m_advertising_active)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;36m[ADV CHECK]\x1b[0m Detectado advertising inactivo, reiniciando...");
        advertising_start();
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;35m[ADV CHECK]\x1b[0m Estado: device_active=%d, conn_handle=%d, "
                         "adv_active=%d",
                         m_device_active, m_conn_handle, m_advertising_active);
    }
}

void advertising_stop(void)
{
    if (!m_advertising_active)
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;33m[ADVERTISING]\x1b[0m Advertising ya está detenido, omitiendo stop");
        return;
    }

    // Usar la API del SoftDevice directamente
    ret_code_t err_code = sd_ble_gap_adv_stop(m_advertising.adv_handle);

    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[ADVERTISING]\x1b[0m Advertising detenido correctamente");
        m_advertising_active = false;
    }
    else if (err_code == NRF_ERROR_INVALID_STATE)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[ADVERTISING]\x1b[0m Advertising ya estaba detenido");
        m_advertising_active = false; // Actualizar estado
    }
    else
    {
        NRF_LOG_RAW_INFO(
            "\n\x1b[1;31m[ADVERTISING ERROR]\x1b[0m Error al detener advertising: 0x%08X",
            err_code);
        // Forzar estado como detenido para evitar bucles
        m_advertising_active = false;
    }
}

void disconnect_all_devices(void)
{
    ret_code_t err_code;

    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[DISCONNECT]\x1b[0m Desconectando celular...");
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if (err_code == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;32m[DISCONNECT]\x1b[0m Celular desconectado correctamente");
        }
        else
        {
            NRF_LOG_RAW_INFO(
                "\n\x1b[1;31m[DISCONNECT ERROR]\x1b[0m Error al desconectar celular: 0x%08X",
                err_code);
            // No usar APP_ERROR_CHECK aquí para evitar reset
        }
        m_conn_handle = BLE_CONN_HANDLE_INVALID;
    }

    if (m_emisor_conn_handle != BLE_CONN_HANDLE_INVALID)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m[DISCONNECT]\x1b[0m Desconectando emisor...");
        err_code =
            sd_ble_gap_disconnect(m_emisor_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
        if (err_code == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;32m[DISCONNECT]\x1b[0m Emisor desconectado correctamente");
        }
        else
        {
            NRF_LOG_RAW_INFO(
                "\n\x1b[1;31m[DISCONNECT ERROR]\x1b[0m Error al desconectar emisor: 0x%08X",
                err_code);
            // No usar APP_ERROR_CHECK aquí para evitar reset
        }
        m_emisor_conn_handle = BLE_CONN_HANDLE_INVALID;
    }

    NRF_LOG_RAW_INFO("\n\x1b[1;32m[DISCONNECT]\x1b[0m Proceso de desconexión completado");
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
