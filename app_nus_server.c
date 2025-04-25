#include "app_nus_server.h"

#include "app_timer.h"
#include "app_uart.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_nus.h"
#include "bsp_btn_ble.h"
#include "fds.h"  // Para manejar la memoria flash
#include "nrf.h"  // Para NVIC_SystemReset()
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "variables.h"

#define APP_BLE_CONN_CFG_TAG 1

#define DEVICE_NAME                                                            \
	"Repetidor" /**< Name of device. Will be included in the advertising data. \
	             */
#define NUS_SERVICE_UUID_TYPE                                             \
	BLE_UUID_TYPE_VENDOR_BEGIN /**< UUID type for the Nordic UART Service \
	                              (vendor specific). */

#define APP_BLE_OBSERVER_PRIO                                                \
	3 /**< Application's BLE observer priority. You shouldn't need to modify \
	     this value. */

#define APP_ADV_INTERVAL                                               \
	64 /**< The advertising interval (in units of 0.625 ms. This value \
	      corresponds to 40 ms). */

#define APP_ADV_DURATION                                             \
	18000 /**< The advertising duration (180 seconds) in units of 10 \
	         milliseconds. */
#define MIN_CONN_INTERVAL                                                      \
	MSEC_TO_UNITS(                                                             \
	    20, UNIT_1_25_MS) /**< Minimum acceptable connection interval (20 ms), \
	                         Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL                                                      \
	MSEC_TO_UNITS(                                                             \
	    75, UNIT_1_25_MS) /**< Maximum acceptable connection interval (75 ms), \
	                         Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY 0   /**< Slave latency. */
#define CONN_SUP_TIMEOUT                                                       \
	MSEC_TO_UNITS(4000,                                                        \
	              UNIT_10_MS) /**< Connection supervisory timeout (4 seconds), \
	                             Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY                                       \
	APP_TIMER_TICKS(                                                         \
	    5000) /**< Time from initiating event (connect or start of           \
	             notification) to first time sd_ble_gap_conn_param_update is \
	             called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY                                      \
	APP_TIMER_TICKS(                                                       \
	    30000) /**< Time between each call to sd_ble_gap_conn_param_update \
	              after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT                                    \
	3 /**< Number of attempts before giving up the connection parameter \
	     negotiation. */

#define DEAD_BEEF                                                          \
	0xDEADBEEF /**< Value used as error code on stack dump, can be used to \
	              identify stack location on stack unwind. */

BLE_NUS_DEF(m_nus,
            NRF_SDH_BLE_TOTAL_LINK_COUNT); /**< BLE NUS service instance. */
NRF_BLE_QWR_DEF(m_qwr);             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising); /**< Advertising module instance. */

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;       // Handle del celular
static uint16_t m_emisor_conn_handle = BLE_CONN_HANDLE_INVALID; // Handle del emisor
static uint16_t m_ble_nus_max_data_len =
    BLE_GATT_ATT_MTU_DEFAULT -
    3; /**< Maximum length of data (in bytes) that can be transmitted to the
          peer by the Nordic UART service module. */
static ble_uuid_t m_adv_uuids[] = /**< Universally unique service identifier. */
    {{BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE}};

static app_nus_server_on_data_received_t m_on_data_received = 0;

// Variable global para almacenar la MAC en formato binario
static uint8_t custom_mac_addr_[6] = {0};  // Arreglo de 6 bytes para la MAC
static uint8_t mac_address_from_flash[6] = {
    0};  // Arreglo de 6 bytes para la MAC

static ble_gap_addr_t
    m_target_periph_addr;  // No const, se inicializa en tiempo de ejecución

static void load_mac_from_flash(void)
{
	fds_record_desc_t record_desc;
	fds_find_token_t ftok = {0};
	fds_flash_record_t flash_record;

	// Busca el registro en la memoria flash
	ret_code_t err_code =
	    fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok);
	if (err_code == NRF_SUCCESS)
	{
		err_code = fds_record_open(&record_desc, &flash_record);
		if (err_code == NRF_SUCCESS)
		{
			memcpy(mac_address_from_flash, flash_record.p_data,
			       sizeof(mac_address_from_flash));
			fds_record_close(&record_desc);
			NRF_LOG_INFO(
			    "MAC cargada desde memoria flash: "
			    "%02X:%02X:%02X:%02X:%02X:%02X",
			    mac_address_from_flash[0], mac_address_from_flash[1],
			    mac_address_from_flash[2], mac_address_from_flash[3],
			    mac_address_from_flash[4], mac_address_from_flash[5]);
		}
	}
	else
	{
		NRF_LOG_WARNING(
		    "No se encontro una MAC en la memoria flash. Usando valor "
		    "predeterminado.");
		// Si no se encuentra una MAC, usa una dirección predeterminada
		mac_address_from_flash[0] = 0x63;
		mac_address_from_flash[1] = 0x98;
		mac_address_from_flash[2] = 0x41;
		mac_address_from_flash[3] = 0xD3;
		mac_address_from_flash[4] = 0x03;
		mac_address_from_flash[5] = 0xFB;
	}
}

// Definición de la función
static void delete_old_records(void)
{
	fds_record_desc_t record_desc;
	fds_find_token_t ftok = {0};

	// Busca y elimina todos los registros con el mismo File ID
	while (fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok) ==
	       NRF_SUCCESS)
	{
		ret_code_t err_code = fds_record_delete(&record_desc);
		APP_ERROR_CHECK(err_code);
		NRF_LOG_INFO("Registro antiguo eliminado.");
	}

	// Llama a fds_gc() para realizar la recolección de basura
	ret_code_t err_code = fds_gc();
	APP_ERROR_CHECK(err_code);
}

// Función para realizar la recolección de basura
static void perform_garbage_collection(void)
{
	ret_code_t err_code = fds_gc();
	if (err_code == NRF_SUCCESS)
	{
		NRF_LOG_INFO("Recoleccion de basura completada.");
	}
	else
	{
		NRF_LOG_ERROR("Error en la recoleccion de basura: %d", err_code);
	}
}

// Función para guardar la MAC en la memoria flash
static void save_mac_to_flash_and_reset(uint8_t* mac_addr)
{
	fds_record_t record;
	fds_record_desc_t record_desc;
	fds_find_token_t ftok = {0};
	//	uint32_t sample_data = 1234567855;
	uint32_t aligned_data_buffer[2];  // 2 * 4 = 8 bytes
	memcpy(aligned_data_buffer, mac_addr, 6);

	// Configura el registro con la MAC
	record.file_id = MAC_FILE_ID;
	record.key = MAC_RECORD_KEY;
	record.data.p_data = aligned_data_buffer;  // Apunta al buffer alineado

	// record.data.p_data = mac_addr;
	// record.data.length_words = sizeof(sample_data) / sizeof(uint32_t);
	record.data.length_words =
	    (6 + sizeof(uint32_t) - 1) / sizeof(uint32_t);  // (6 + 3) / 4 = 2

	NRF_LOG_INFO("Length words: %d", record.data.length_words);
	// Realiza la recolección de basura si es necesario
	// perform_garbage_collection();

	// Si ya existe un registro, actualízalo
	if (fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok) ==
	    NRF_SUCCESS)
	{
		if (fds_record_update(&record_desc, &record) == NRF_SUCCESS)
		{
			NRF_LOG_INFO(
			    "MAC actualizada en memoria flash. Reiniciando el "
			    "dispositivo...");
			// NVIC_SystemReset();  // Reinicia el dispositivo
		}
		else
		{
			NRF_LOG_ERROR(
			    "Error al actualizar la MAC en memoria flash. Con registro "
			    "existente.");
		}
	}
	else
	{
		// Si no existe, crea un nuevo registro
		ret_code_t ret = fds_record_write(&record_desc, &record);

		if (ret == NRF_SUCCESS)
		{
			NRF_LOG_INFO("Registro creado correctamente.");
		}
		else
		{
			NRF_LOG_ERROR("Error al crear el registro: %d", ret);
		}
	}
}

static void fds_evt_handler(fds_evt_t const* p_evt)
{
	if (p_evt->id == FDS_EVT_INIT)
	{
		if (p_evt->result == NRF_SUCCESS)
		{
			NRF_LOG_INFO("FDS inicializado correctamente.");
		}
		else
		{
			NRF_LOG_ERROR("Error al inicializar FDS: %d", p_evt->result);
		}
	}
	else if (p_evt->id == FDS_EVT_WRITE)
	{
		if (p_evt->result == NRF_SUCCESS)
		{
			NRF_LOG_INFO("Registro escrito correctamente.");
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
			NRF_LOG_INFO("Registro actualizado correctamente.");
		}
		else
		{
			NRF_LOG_ERROR("Error al actualizar el registro: %d", p_evt->result);
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
static void nus_data_handler(ble_nus_evt_t* p_evt)
{
	if (p_evt->type == BLE_NUS_EVT_RX_DATA)
	{
		uint32_t err_code;

		NRF_LOG_DEBUG("Received data from BLE NUS. Writing data on UART.");
		NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data,
		                      p_evt->params.rx_data.length);

		// Asegúrate de que el mensaje sea tratado como una cadena de texto
		char message[BLE_NUS_MAX_DATA_LEN + 1];  // +1 para el carácter nulo
		if (p_evt->params.rx_data.length < sizeof(message))
		{
			memcpy(message, p_evt->params.rx_data.p_data,
			       p_evt->params.rx_data.length);
			message[p_evt->params.rx_data.length] =
			    '\0';  // Agregar terminador nulo

			// Verifica si el mensaje comienza con "111"
			if (p_evt->params.rx_data.length >= 5 && message[0] == '1' &&
			    message[1] == '1' && message[2] == '1')
			{
				// Extrae el comando (los dos caracteres después de "111")
				char command[3] = {message[3], message[4],
				                   '\0'};  // Comando de 2 caracteres

				// Manejo de comandos con un switch-case
				switch (atoi(command))  // Convierte el comando a entero
				{
					case 1:  // Comando 01: Guardar MAC
					{
						size_t mac_length = p_evt->params.rx_data.length - 5;
						if (mac_length ==
						    12)  // Verifica que la longitud sea válida
						{
							for (size_t i = 0; i < 6; i++)
							{
								char byte_str[3] = {message[5 + i * 2],
								                    message[6 + i * 2], '\0'};
								custom_mac_addr_[i] =
								    (uint8_t)strtol(byte_str, NULL, 16);
							}
							NRF_LOG_INFO(
							    "MAC recibida: %02X:%02X:%02X:%02X:%02X:%02X",
							    custom_mac_addr_[0], custom_mac_addr_[1],
							    custom_mac_addr_[2], custom_mac_addr_[3],
							    custom_mac_addr_[4], custom_mac_addr_[5]);

							// Guarda la MAC en la memoria flash y reinicia el
							// dispositivo
							save_mac_to_flash_and_reset(custom_mac_addr_);
						}
						else
						{
							NRF_LOG_WARNING("Longitud de MAC inválida: %d",
							                mac_length);
						}
						break;
					}
					case 2:  // Comando 02: Muestra la MAC custom guardada en la
					         // memoria flash
					{
						// Carga la MAC desde la memoria flash
						load_mac_from_flash();
						// muestra la MAC
					}
						NRF_LOG_RAW_INFO(
						    "\n\nComando 02 recibido. Mostrando MAC guardada en "
						    "memoria flash.");
						break;

					case 3:  // Comando 03: Lógica futura
						NRF_LOG_RAW_INFO(
						    "\n\nComando 03 recibido. Reiniciando dispositivo...");
						NVIC_SystemReset();

						break;

					case 4:  // Comando 04: Lógica futura
						NRF_LOG_INFO(
						    "Comando 04 recibido. Logica no implementada.");
						break;

					default:  // Comando desconocido
						NRF_LOG_WARNING("Comando desconocido: %s", command);
						break;
				}
			}
			else
			{
				// Reenvía el mensaje al emisor o lo maneja normalmente
				if (m_on_data_received)
				{
					m_on_data_received((uint8_t*)message,
					                   p_evt->params.rx_data.length);
				}
			}
		}
		else
		{
			NRF_LOG_WARNING("Mensaje demasiado largo para procesar.");
		}
	}
}
/**@snippet [Handling the data received over BLE] */

/**@brief Function for the GAP initialization.
 *
 * @details This function will set up all the necessary GAP (Generic Access
 * Profile) parameters of the device. It also sets the permissions and
 * appearance.
 */
static void gap_params_init(void)
{
	uint32_t err_code;
	ble_gap_conn_params_t gap_conn_params;
	ble_gap_conn_sec_mode_t sec_mode;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	err_code = sd_ble_gap_device_name_set(
	    &sec_mode, (const uint8_t*)DEVICE_NAME, strlen(DEVICE_NAME));
	APP_ERROR_CHECK(err_code);

	memset(&gap_conn_params, 0, sizeof(gap_conn_params));

	gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
	gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
	gap_conn_params.slave_latency = SLAVE_LATENCY;
	gap_conn_params.conn_sup_timeout = CONN_SUP_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for initializing services that will be used by the
 * application.
 */
static void services_init(void)
{
	uint32_t err_code;
	ble_nus_init_t nus_init;
	nrf_ble_qwr_init_t qwr_init = {0};

	// Initialize Queued Write Module.
	qwr_init.error_handler = nrf_qwr_error_handler;

	err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
	APP_ERROR_CHECK(err_code);

	// Initialize NUS.
	memset(&nus_init, 0, sizeof(nus_init));

	nus_init.data_handler = nus_data_handler;

	err_code = ble_nus_init(&m_nus, &nus_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling an event from the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection
 * Parameters Module which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by
 * simply setting the disconnect_on_fail config parameter, but instead we use
 * the event handler mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t* p_evt)
{
	uint32_t err_code;

	if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
	{
		err_code = sd_ble_gap_disconnect(m_conn_handle,
		                                 BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
		APP_ERROR_CHECK(err_code);
	}
}

/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went
 * wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error)
{
	APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void)
{
	uint32_t err_code;
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));

	cp_init.p_conn_params = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
	cp_init.disconnect_on_fail = false;
	cp_init.evt_handler = on_conn_params_evt;
	cp_init.error_handler = conn_params_error_handler;

	err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed
 * to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
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

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 */
void app_nus_server_ble_evt_handler(ble_evt_t const* p_ble_evt)
{
	uint32_t err_code;
	ble_gap_evt_t const* p_gap_evt = &p_ble_evt->evt.gap_evt;

	switch (p_ble_evt->header.evt_id)
	{
		case BLE_GAP_EVT_CONNECTED:
			if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_PERIPH)
			{
				NRF_LOG_RAW_INFO("\nCelular conectado");
				m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle; // Guardar handle del celular
			}
			else if (p_gap_evt->params.connected.role == BLE_GAP_ROLE_CENTRAL)
			{
				NRF_LOG_RAW_INFO("\nEmisor conectado");
				m_emisor_conn_handle = p_ble_evt->evt.gap_evt.conn_handle; // Guardar handle del emisor
			}
			break;

		case BLE_GAP_EVT_DISCONNECTED:
			if (p_gap_evt->conn_handle == m_conn_handle)
			{
				NRF_LOG_RAW_INFO("\nCelular desconectado");
				m_conn_handle = BLE_CONN_HANDLE_INVALID; // Invalida el handle del celular
			}
			else if (p_gap_evt->conn_handle == m_emisor_conn_handle)
			{
				NRF_LOG_RAW_INFO("\nEmisor desconectado");
				NRF_LOG_RAW_INFO("\n\nBuscando emisor...");

				m_emisor_conn_handle = BLE_CONN_HANDLE_INVALID; // Invalida el handle del emisor
			}
			break;

		case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
		{
			NRF_LOG_DEBUG("PHY update request.");
			ble_gap_phys_t const phys = {
			    .rx_phys = BLE_GAP_PHY_AUTO,
			    .tx_phys = BLE_GAP_PHY_AUTO,
			};
			err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle,
			                                 &phys);
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
			err_code = sd_ble_gap_disconnect(
			    p_ble_evt->evt.gattc_evt.conn_handle,
			    BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
			APP_ERROR_CHECK(err_code);
			break;

		case BLE_GATTS_EVT_TIMEOUT:
			// Disconnect on GATT Server timeout event.
			err_code = sd_ble_gap_disconnect(
			    p_ble_evt->evt.gatts_evt.conn_handle,
			    BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
			APP_ERROR_CHECK(err_code);
			break;

		default:
			// No implementation needed.
			break;
	}
}

uint32_t app_nus_server_send_data(const uint8_t* data_array, uint16_t length)
{
	return ble_nus_data_send(&m_nus, (uint8_t*)data_array, &length,
	                         m_conn_handle);
}

/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void)
{
	uint32_t err_code;
	ble_advertising_init_t init;

	memset(&init, 0, sizeof(init));

	init.advdata.name_type = BLE_ADVDATA_FULL_NAME;
	init.advdata.include_appearance = false;
	init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE;

	init.srdata.uuids_complete.uuid_cnt =
	    sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
	init.srdata.uuids_complete.p_uuids = m_adv_uuids;

	init.config.ble_adv_fast_enabled = true;
	init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
	init.config.ble_adv_fast_timeout = APP_ADV_DURATION;
	init.evt_handler = on_adv_evt;

	err_code = ble_advertising_init(&m_advertising, &init);
	APP_ERROR_CHECK(err_code);

	ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Function for starting advertising.
 */
static void advertising_start(void)
{
	uint32_t err_code =
	    ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
	APP_ERROR_CHECK(err_code);
}

void app_nus_server_init(app_nus_server_on_data_received_t on_data_received)
{
	m_on_data_received = on_data_received;
	gap_params_init();
	services_init();
	advertising_init();
	conn_params_init();
	fds_initialize();
	advertising_start();
}
