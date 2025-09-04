#include <stdbool.h>

#ifndef VARIABLES_H
#define VARIABLES_H

#define RTC_PRESCALER          4095
#define APP_BLE_CONN_CFG_TAG   1
#define APP_BLE_OBSERVER_PRIO  3
#define UART_TX_BUF_SIZE       256
#define UART_RX_BUF_SIZE       256
#define ECHOBACK_BLE_UART_DATA 1

// TIEMPO
#define DEFAULT_DEVICE_ON_TIME_MS          10000
#define DEFAULT_DEVICE_ON_TIME_EXTENDED_MS 80000
#define EXTENDED_SEARCH_TIME_MS            120000 // 2 minutos para búsqueda extendida del emisor (antes 999 segundos)
#define DEFAULT_DEVICE_SLEEP_TIME_MS       (10000 + 1000) // Agregar tiempo de medicion

// MACS
#define MAC_FILE_ID             0x0001
#define MAC_RECORD_KEY          0x0002 // MAC Emisor
#define MAC_SCAN_RECORD_KEY     0x0003 // MAC Escaneo
#define MAC_REPEATER_RECORD_KEY 0x0004 // MAC Custom Repetidor

// Tiempo
#define TIME_FILE_ID                0x0005
#define TIME_ON_RECORD_KEY          0x0006
#define TIME_SLEEP_RECORD_KEY       0x0007
#define TIME_ON_EXTENDED_RECORD_KEY 0x0008
#define DATE_AND_TIME_FILE_ID       0x0009
#define DATE_AND_TIME_RECORD_KEY    0x000A

// Historial
#define HISTORY_FILE_ID            0x000B
#define HISTORY_COUNTER_RECORD_KEY 0x000C
#define HISTORY_RECORD_KEY         0x1000
#define HISTORY_BUFFER_SIZE        500
#define HISTORY_RECORD_KEY_START   HISTORY_RECORD_KEY

#define MSB_16(a)                  (((a) & 0xFF00) >> 8)
#define LSB_16(a)                  ((a) & 0x00FF)

// Parámetros del modo de escaneo de paquetes
#define INACTIVITY_TIMEOUT_MS 10000  // 10 segundos sin paquetes → terminar
#define MAX_DETECTION_TIME_MS 300000 // Máximo 5 minutos total (seguridad)

extern bool m_device_active;

#endif // VARIABLES_H
