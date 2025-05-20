#ifndef VARIABLES_H
#define VARIABLES_H

#define RESTART_TIMEOUT_REPETIDOR_MS \
	10000 /**< Tiempo de encendido for the repeater. */
#define APP_BLE_CONN_CFG_TAG                                            \
	1 /**< Tag that refers to the BLE stack configuration set with @ref \
	     sd_ble_cfg_set. The default tag is @ref BLE_CONN_CFG_TAG_DEFAULT. */
#define APP_BLE_OBSERVER_PRIO                                            \
	3 /**< BLE observer priority of the application. There is no need to \
	     modify this value. */
#define UART_TX_BUF_SIZE 256 /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE 256 /**< UART RX buffer size. */
#define ECHOBACK_BLE_UART_DATA                                              \
	1 /**< Echo the UART data that is received over the Nordic UART Service \
	     (NUS) back to the sender. */

/** TIMERS   */
// --- Configuración de Tiempos (Interna a este módulo) ---
#define DEFAULT_DEVICE_ON_TIME_MS \
	10000  // Tiempo que el dispositivo estará activo (ej: 5 segundos)
#define DEFAULT_DEVICE_SLEEP_TIME_MS \
	20000  // Tiempo que el dispositivo estará dormido (ej: 10 segundos)

/** STORAGE	 */
// --- Configuración de Almacenamiento para guardar la MAC ---
#define MAC_FILE_ID 0x1111     // Identificador único para el archivo
#define MAC_RECORD_KEY 0x2222  // Identificador único para el registro

#define TIME_FILE_ID 0x3333  // ID del archivo para el tiempo de encendido
#define TIME_ON_RECORD_KEY 0x4444
#define TIME_SLEEP_RECORD_KEY 0x5555

#endif // VARIABLES_H