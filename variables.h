#ifndef VARIABLES_H
#define VARIABLES_H

#include "stdbool.h"

#define RESTART_TIMEOUT_REPETIDOR_MS 10000
#define APP_BLE_CONN_CFG_TAG         1
#define APP_BLE_OBSERVER_PRIO        3
#define UART_TX_BUF_SIZE             256
#define UART_RX_BUF_SIZE             256
#define ECHOBACK_BLE_UART_DATA       1
#define DEFAULT_DEVICE_ON_TIME_MS    11000
#define DEFAULT_DEVICE_SLEEP_TIME_MS 600000 // Tiempo que el dispositivo estará dormido (ej: 10 segundos)

/** STORAGE	 */
#define MAC_FILE_ID              0x1111
#define MAC_RECORD_KEY           0x1112

#define TIME_FILE_ID             0x2222
#define TIME_ON_RECORD_KEY       0x4444
#define TIME_SLEEP_RECORD_KEY    0x5555

#define DATE_AND_TIME_FILE_ID    0x6666
#define DATE_AND_TIME_RECORD_KEY 0x6667

#define HISTORY_FILE_ID          0x7777
#define HISTORY_RECORD_KEY       0x7888
#define HISTORY_BUFFER_SIZE      500
#define HISTORY_RECORD_KEY_START HISTORY_RECORD_KEY

#endif // VARIABLES_H