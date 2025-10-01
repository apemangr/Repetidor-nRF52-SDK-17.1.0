#ifndef VARIABLES_H
#define VARIABLES_H

#define RTC_PRESCALER          4095
#define MAGIC_PASSWORD         0xABCD /** MAGIC */
#define APP_BLE_CONN_CFG_TAG   1      /** NORDIC VARS */
#define APP_BLE_OBSERVER_PRIO  3
#define UART_TX_BUF_SIZE       256
#define UART_RX_BUF_SIZE       256
#define ECHOBACK_BLE_UART_DATA 1

// TIEMPO
#define DEFAULT_DEVICE_ON_TIME_MS             15000 // Tiempo de encendido por defecto
#define DEFAULT_DEVICE_EXTENDED_ON_TIME_MS    400000
#define DEFAULT_DEVICE_SLEEP_TIME_MS          (00000 + 5000) // Tiempo de dormido por defecto + delay
#define DEFAULT_DEVICE_EXTENDED_SLEEP_TIME_MS 10000

// MAC'S
#define MAC_FILE_ID              0x0001 // Dirección FILE_ID MAC's
#define MAC_EMISOR_RECORD_KEY    0x0002 // MAC del Emisor
#define MAC_REPETIDOR_RECORD_KEY 0x0003 // MAC del Repetidor

// TIEMPOS RTC
#define TIME_FILE_ID                   0x0004 // Dirección FILE_ID Tiempos
#define TIME_ON_RECORD_KEY             0x0005 // Tiempo de encendido del repetidor
#define TIME_SLEEP_RECORD_KEY          0x0006 // Tiempo de dormido del repetidor
#define TIME_EXTENDED_SLEEP_RECORD_KEY 0x0007 // Tiempo de dormido del repetidor
#define TIME_EXTENDED_ON_RECORD_KEY    0x0008 // Tiempo extentido del repetidor

// FECHA Y HORA
#define DATE_AND_TIME_FILE_ID    0x0009 // Dirección FILE_ID Fecha y Tiempo (RTC)
#define DATE_AND_TIME_RECORD_KEY 0x000a // Valores del calendario RTC

// CONFIGURACION
#define CONFIG_FILE_ID    0x000B
#define CONFIG_RECORD_KEY 0x000C

// HISTORY
#define HISTORY_FILE_ID               0x000C             // Dirección FILE_ID Historiales
#define HISTORY_COUNTER_RECORD_KEY    0x000D             // Contador de historiales
#define HISTORY_ADC_VALUES_RECORD_KEY 0x000E             // Valores de los ADC's y el contador ADV
#define HISTORY_RECORD_KEY            0x1000             // Dirección inicial de los historiales
#define HISTORY_BUFFER_SIZE           500                // Cantidad de historiales
#define HISTORY_RECORD_KEY_START      HISTORY_RECORD_KEY // Renombre para comodidad

// HELPERS
#define MSB_16(a) (((a) & 0xFF00) >> 8) // Parte de arriba de un uint32_t
#define LSB_16(a) ((a) & 0x00FF)        // Parte de abajo de un uint32_t

// LOGS
#define LOG_EXEC "\n[\033[1;36m EXEC \033[0m]"
#define LOG_OK   "\n[\033[1;32m  OK  \033[0m]"
#define LOG_FAIL "\n[\033[1;31m FAIL \033[0m]"
#define LOG_WARN "\n[\033[1;35m WARN \033[0m]"
#define LOG_INFO "\n[\033[1;33m INFO \033[0m]"

extern bool m_device_active;
extern bool m_connected_this_cycle; // Nueva variable para controlar el modo de reconexión
extern bool m_extended_mode_on;

#endif // VARIABLES_H
