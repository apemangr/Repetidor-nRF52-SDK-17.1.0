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
