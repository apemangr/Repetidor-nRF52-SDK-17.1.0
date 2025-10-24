#include "filesystem.h"
#include "variables.h"
#include "ble_gap.h"
#include "nrf_sdh_ble.h"
#include "app_nus_server.h"
#include <stdint.h>

// Buffer estático para evitar problemas con variables locales en el stack
static store_history g_temp_history_buffer;
extern void          app_nus_client_on_data_received(
                    const uint8_t *data_ptr,
                    uint16_t       data_length);
extern config_repeater_t config_repeater;

//-------------------------------------------------------------------------------------------------------------
//                                      FDS INIT FUNCTIONS STARTS HERE
//-------------------------------------------------------------------------------------------------------------

// Función para realizar la recolección de basura
static void perform_garbage_collection(void)
{
    ret_code_t err_code = fds_gc();
    if (err_code == NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(LOG_OK " Recoleccion de basura completada.");
    }
    else {
        NRF_LOG_RAW_INFO(
                   LOG_FAIL " Error en la recoleccion de basura: %d",
                   err_code);
    }
}

static void fds_evt_handler(fds_evt_t const *p_evt)
{
    if (p_evt->id == FDS_EVT_INIT) {
        if (p_evt->result == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_EXEC " Iniciando el modulo de almacenamiento...");

            fds_stat_t stat = {0};
            fds_stat(&stat);
            NRF_LOG_RAW_INFO(
                       LOG_INFO " Se encontraron %d registros validos.",
                       stat.valid_records);
            NRF_LOG_RAW_INFO(
                       LOG_INFO " Se encontraron %d registros no validos.",
                       stat.dirty_records);

            if (stat.dirty_records > 0) {
                // Realiza la recolección de basura
                NRF_LOG_RAW_INFO(LOG_INFO " Limpiando registros no validos...");
                perform_garbage_collection();
            }
            NRF_LOG_RAW_INFO(LOG_OK " Modulo inicializado correctamente.");
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " nError al inicializar FDS: %d",
                       p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_WRITE) {
        if (p_evt->result == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Registro escrito correctamente!\x1b[0m");
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL "Error al escribir el registro: %d",
                       p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_UPDATE) {
        if (p_evt->result == NRF_SUCCESS) {
            // NRF_LOG_RAW_INFO(
            //     "\n\n\x1b[1;32m>>\x1b[0m Registro actualizado
            //     correctamente!");
        }
        else {
            NRF_LOG_ERROR("Error al actualizar el registro: %d", p_evt->result);
        }
    }
    else if (p_evt->id == FDS_EVT_DEL_RECORD) {
        if (p_evt->result == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO("\nRegistro eliminado correctamente.");
        }
        else {
            NRF_LOG_ERROR("Error al eliminar el registro: %d", p_evt->result);
        }
    }
}

void fds_initialize(void)
{
    ret_code_t err_code;

    // Registra el manejador de eventos
    err_code = fds_register(fds_evt_handler);
    APP_ERROR_CHECK(err_code);

    // Inicializa el módulo FDS
    err_code = fds_init();
    APP_ERROR_CHECK(err_code);
}

//-------------------------------------------------------------------------------------------------------------
//                                      FDS INIT FUNCTIONS ENDS HERE
//-------------------------------------------------------------------------------------------------------------

ret_code_t save_history_record_emisor(
           store_history const *p_history_data,
           uint16_t             offset)
{
    ret_code_t        ret;
    fds_record_desc_t desc_history = {0};
    fds_record_desc_t desc_counter = {0};
    fds_find_token_t  token        = {0};

    memcpy(&g_temp_history_buffer, p_history_data, sizeof(store_history));
    adc_values.contador = g_temp_history_buffer.contador;
    adc_values.V1       = g_temp_history_buffer.V1;
    adc_values.V2       = g_temp_history_buffer.V2;

    // Preparar nuevo registro histórico
    uint16_t     record_key = HISTORY_RECORD_KEY_START + offset;
    fds_record_t new_record = {
               .file_id     = HISTORY_FILE_ID,
               .key         = record_key,
               .data.p_data = &g_temp_history_buffer, // Usar buffer estático
               .data.length_words = BYTES_TO_WORDS(sizeof(store_history))};

    // Buscar el registro del historial, si no existe lo escribe
    ret = fds_record_find(HISTORY_FILE_ID, record_key, &desc_history, &token);
    if (ret == NRF_SUCCESS) {
        // Si el registro ya existe, lo actualiza
        ret = fds_record_update(&desc_history, &new_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al actualizar el registro: %d",
                       ret);
            return ret;
        }
        NRF_LOG_RAW_INFO(
                   LOG_INFO " Registro actualizado con KEY: 0x%04X",
                   record_key);

        // Esperar que FDS complete la operación + flush de GC si es necesario
        nrf_delay_ms(1000);
        (void)fds_gc(); // Forzar garbage collection si es necesario
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        // Si no existe, lo escribe
        ret = fds_record_write(&desc_history, &new_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al escribir el registro: %d",
                       ret);
            return ret;
        }
        NRF_LOG_RAW_INFO(
                   LOG_INFO " Registro escrito con KEY: 0x%04X",
                   record_key);

        // Esperar que FDS complete la operación + flush de GC si es necesario
        nrf_delay_ms(1000);
        (void)fds_gc(); // Forzar garbage collection si es necesario
        
        // Incrementar contador de historiales en memoria
        // La configuración se guardará cuando el dispositivo entre en modo sleep
        config_repeater.cantidad_historiales++;
        NRF_LOG_RAW_INFO(LOG_INFO " Contador de historiales: %u", 
                       config_repeater.cantidad_historiales);
    }
    else {
        NRF_LOG_RAW_INFO(LOG_FAIL " Error al buscar el registro: %d", ret);
        return ret;
    }

    return NRF_SUCCESS;
}

ret_code_t save_adv_history_record(const store_adv_history *p_adv_history, 
                                    uint32_t contador)
{
    ret_code_t        ret;
    fds_record_desc_t desc        = {0};
    fds_find_token_t  token       = {0};
    
    // Calcular el offset basado en el contador (cada 142 contadores = 1 historial)
    uint16_t history_number = (uint16_t)(contador / 142);
    uint16_t record_key = ADV_HISTORY_RECORD_KEY + history_number;
    
    NRF_LOG_RAW_INFO(LOG_INFO " Guardando historial ADV #%u (contador=%u, record_key=0x%04X)",
                   history_number, contador, record_key);
    
    // Preparar registro para FDS
    fds_record_t new_record = {
        .file_id           = ADV_HISTORY_FILE_ID,
        .key               = record_key,
        .data.p_data       = p_adv_history,
        .data.length_words = BYTES_TO_WORDS(sizeof(store_adv_history))
    };
    
    // Buscar si ya existe el registro
    ret = fds_record_find(ADV_HISTORY_FILE_ID, record_key, &desc, &token);
    
    if (ret == NRF_SUCCESS) {
        // Si existe, actualizar
        ret = fds_record_update(&desc, &new_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_FAIL " Error al actualizar historial ADV: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO(LOG_OK " Historial ADV #%u actualizado", history_number);
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        // Si no existe, crear nuevo
        ret = fds_record_write(&desc, &new_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_FAIL " Error al escribir historial ADV: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO(LOG_OK " Historial ADV #%u creado", history_number);
    }
    else {
        NRF_LOG_RAW_INFO(LOG_FAIL " Error al buscar historial ADV: %d", ret);
        return ret;
    }
    
    // Dar tiempo para que FDS complete la operación
    nrf_delay_ms(50);
    
    return NRF_SUCCESS;
}

ret_code_t update_history_counter(uint32_t new_count)
{
    fds_record_desc_t desc_counter = {0};
    fds_find_token_t  token        = {0};
    ret_code_t        ret;

    // Buscar el registro del contador
    ret = fds_record_find(
               HISTORY_FILE_ID,
               HISTORY_COUNTER_RECORD_KEY,
               &desc_counter,
               &token);

    nrf_delay_ms(500);
    fds_record_t counter_record = {
               .file_id           = HISTORY_FILE_ID,
               .key               = HISTORY_COUNTER_RECORD_KEY,
               .data.p_data       = &new_count,
               .data.length_words = 1};

    if (ret == NRF_SUCCESS) {
        // Actualizar el registro existente
        ret = fds_record_update(&desc_counter, &counter_record);
        nrf_delay_ms(500); // Delay tras actualizar
        if (ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_INFO " Contador actualizado correctamente a: %lu",
                       new_count);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        // Si no existe, escribir el valor inicial (0) en memoria
        uint32_t initial_value     = 1;
        counter_record.data.p_data = &initial_value;
        ret                        = fds_record_write(NULL, &counter_record);
        nrf_delay_ms(500); // Delay tras escribir
        if (ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Contador creado en memoria con valor 1.");
        }
        else {
            NRF_LOG_RAW_INFO(LOG_FAIL " Error al crear el contador: %d", ret);
        }
    }
    else {
        NRF_LOG_RAW_INFO(
                   LOG_FAIL " Error al buscar el registro del contador: %d",
                   ret);
    }

    return ret;
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS STARTS HERE.
//-------------------------------------------------------------------------------------------------------------

ret_code_t read_history_record_by_id(
           uint16_t       record_id,
           store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // La clave del registro se calcula sumando el ID a la clave base.
    uint16_t record_key = (uint16_t)(HISTORY_RECORD_KEY_START + record_id);

    NRF_LOG_RAW_INFO(
               "\nLeyendo registro de historial con \x1B[33mID:\x1B[0m %u "
               "y \x1B[33mRECORD_KEY:\x1B[0m 0x%04X",
               record_id,
               record_key);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) ==
        NRF_SUCCESS) {
        nrf_delay_ms(100);
        fds_flash_record_t flash_record = {0};
        ret_code_t         ret          = fds_record_open(&desc, &flash_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al abrir el registro de historial");
            nrf_delay_ms(100);
            return ret;
        }

        // Verificar que el tamaño del registro en flash coincida con el del
        // struct.
        if (flash_record.p_header->length_words !=
            BYTES_TO_WORDS(sizeof(store_history))) {
            NRF_LOG_RAW_INFO(
                       LOG_WARN
                       " Tamano del registro en flash no coincide con el "
                       "esperado.");
            fds_record_close(&desc);
            nrf_delay_ms(10);
            return NRF_ERROR_INVALID_DATA;
        }

        // Copiar los datos al puntero de salida.
        memcpy(p_history_data, flash_record.p_data, sizeof(store_history));

        return fds_record_close(&desc);
    }

    return NRF_ERROR_NOT_FOUND;
}

ret_code_t read_last_history_record(store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // 1. Buscar el contador para saber cuál es el último registro.
    if (fds_record_find(
                   HISTORY_FILE_ID,
                   (uint16_t)HISTORY_COUNTER_RECORD_KEY,
                   &desc,
                   &token) == NRF_SUCCESS) {
        fds_flash_record_t flash_record  = {0};
        uint32_t           history_count = 0; // Correcto: uint32_t

        fds_record_open(&desc, &flash_record);
        memcpy(&history_count,
               flash_record.p_data,
               sizeof(uint32_t)); // Leer 4 bytes, no 2
        fds_record_close(&desc);

        if (history_count == 0) {
            return NRF_ERROR_NOT_FOUND; // No hay registros guardados.
        }

        // 2. El ID del último registro es (contador - 1).
        uint32_t last_record_id = history_count - 1;
        return read_history_record_by_id(
                   (uint16_t)last_record_id,
                   p_history_data);
    }

    // Si el contador no se encuentra, significa que no hay registros.
    return NRF_ERROR_NOT_FOUND;
}

void print_history_record(store_history const *p_record, const char *p_title)
{
    // Calcula el largo de la línea de cierre según el título
    NRF_LOG_RAW_INFO(
               "\n\n\x1B[1;36m=======\x1B[0m %s \x1B[1;36m=======\x1B[0m\n\n",
               p_title);
    NRF_LOG_RAW_INFO(
               "Fecha        : %02d/%02d/%04d\n",
               p_record->day,
               p_record->month,
               p_record->year);
    NRF_LOG_RAW_INFO(
               "Hora         : %02d:%02d:%02d\n",
               p_record->hour,
               p_record->minute,
               p_record->second);
    NRF_LOG_RAW_INFO("Contador     : %lu\n", p_record->contador);
    NRF_LOG_RAW_INFO("V1           : %u\n", p_record->V1);
    NRF_LOG_RAW_INFO("V2           : %u\n", p_record->V2);
    NRF_LOG_RAW_INFO("V3           : %u\n", p_record->V3);
    NRF_LOG_RAW_INFO("V4           : %u\n", p_record->V4);
    NRF_LOG_RAW_INFO("V5           : %u\n", p_record->V5);
    NRF_LOG_RAW_INFO("V6           : %u\n", p_record->V6);
    NRF_LOG_RAW_INFO("V7           : %u\n", p_record->V7);
    NRF_LOG_RAW_INFO("V8           : %u\n", p_record->V8);
    NRF_LOG_RAW_INFO("Temp         : %u\n", p_record->temp);
    NRF_LOG_RAW_INFO("Bateria      : %u%%\n", p_record->battery);
    NRF_LOG_RAW_INFO(
               "\n\x1B[1;36m================\x1B[0m FIN "
               "\x1B[1;36m================\x1B[0m\n");
    NRF_LOG_FLUSH();
}


void print_adv_history_record(const store_adv_history *p_record, const char *p_title)
{
    NRF_LOG_RAW_INFO(
               "\n\n\x1B[1;36m=======\x1B[0m %s \x1B[1;36m=======\x1B[0m\n\n",
               p_title);
    NRF_LOG_RAW_INFO(
               "Fecha        : %02d/%02d/%04d\n",
               p_record->day,
               p_record->month,
               p_record->year);
    NRF_LOG_RAW_INFO(
               "Hora         : %02d:%02d:%02d\n",
               p_record->hour,
               p_record->minute,
               p_record->second);
    NRF_LOG_RAW_INFO("Contador ADV : %lu\n", p_record->contador);
    NRF_LOG_RAW_INFO("V1 (ADC1)    : %u\n", p_record->V1);
    NRF_LOG_RAW_INFO("V2 (ADC2)    : %u\n", p_record->V2);
    NRF_LOG_RAW_INFO(
               "\n\x1B[1;36m================\x1B[0m FIN "
               "\x1B[1;36m================\x1B[0m\n");
    NRF_LOG_FLUSH();
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS ENDS HERE.
//-------------------------------------------------------------------------------------------------------------

uint32_t read_time_from_flash(valor_type_t valor_type, uint32_t default_valor)
{
    fds_flash_record_t flash_record;
    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok      = {0}; // Importante inicializar a cero
    uint32_t           resultado = default_valor;
    uint32_t          *data;
    ret_code_t         err_code;
    const char        *label;
    uint16_t           record_key;

    switch (valor_type) {
    case TIEMPO_ENCENDIDO:
        record_key = TIME_ON_RECORD_KEY;
        label      = "encendido";
        break;
    case TIEMPO_SLEEP:
        record_key = TIME_SLEEP_RECORD_KEY;
        label      = "sleep";
        break;
    case TIEMPO_EXTENDED_SLEEP:
        record_key = TIME_EXTENDED_SLEEP_RECORD_KEY;
        label      = "sleep extendido";
        break;
    case TIEMPO_EXTENDED_ENCENDIDO:
        record_key = TIME_EXTENDED_ON_RECORD_KEY;
        label      = "encendido extendido";
        break;
    default:
        record_key = TIME_ON_RECORD_KEY;
        label      = "desconocido";
        break;
    }

    // Busca el registro en la memoria flash
    err_code = fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

    if (err_code == NRF_SUCCESS) {
        // Si el registro existe, abre y lee el valor
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code == NRF_SUCCESS) {
            // Verifica que el tamaño del dato leído sea el esperado
            if (flash_record.p_header->length_words == 1) {
                // Copiar directamente el valor desde flash
                data      = (uint32_t *)flash_record.p_data;
                resultado = *data;
                // NRF_LOG_RAW_INFO(LOG_INFO " Tiempo de %s cargado: %u
                // segundos", label, resultado/1000);
            }
            else {
                NRF_LOG_RAW_INFO(
                           "\n\t>> Tamaño del dato en memoria flash no "
                           "coincide con "
                           "el "
                           "esperado.\n");
            }

            err_code = fds_record_close(&record_desc);
            if (err_code != NRF_SUCCESS) {
                NRF_LOG_RAW_INFO(
                           "\n\t>> Error al cerrar el registro: 0x%X",
                           err_code);
                return default_valor;
            }
        }
        else {
            NRF_LOG_RAW_INFO(
                       "\n\t>> Error al abrir el registro: 0x%X",
                       err_code);
        }
    }
    else {
        // NRF_LOG_RAW_INFO(
        //     "\n\t>> Registro no encontrado. Usando valor predeterminado: %u",
        //     default_valor);
    }

    return resultado;
}

datetime_t read_date_from_flash(void)
{
    const uint16_t     len = sizeof(datetime_t);
    fds_flash_record_t flash_record;
    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok = {0};
    ret_code_t         err_code;

    datetime_t         resultado = {
                       .year   = 3000,
                       .month  = 5,
                       .day    = 30,
                       .hour   = 0,
                       .minute = 0,
                       .second = 0};

    err_code = fds_record_find(
               DATE_AND_TIME_FILE_ID,
               DATE_AND_TIME_RECORD_KEY,
               &record_desc,
               &ftok);

    if (err_code != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(
                   "\n\t>> Registro no encontrado (0x%X). Usando "
                   "predeterminado.",
                   err_code);
        return resultado;
    }

    err_code = fds_record_open(&record_desc, &flash_record);
    if (err_code != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO("\n\t>> Error al abrir registro: 0x%X", err_code);
        return resultado;
    }

    // Verificar tamaño en BYTES (no palabras)
    const uint32_t data_size_bytes =
               flash_record.p_header->length_words * sizeof(uint32_t);

    if (data_size_bytes >= len) {
        memcpy(&resultado, flash_record.p_data, len);
    }
    else {
        NRF_LOG_RAW_INFO(
                   "\n\t>> Dato corrupto: tamaño %u < %u",
                   data_size_bytes,
                   len);
    }

    // Cerrar usando DESCRIPTOR (no flash_record)
    err_code = fds_record_close(&record_desc);
    if (err_code != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO("\n\t>> Error al cerrar registro: 0x%X", err_code);
    }

    return resultado;
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS ENDS HERE.
//-------------------------------------------------------------------------------------------------------------

void write_time_to_flash(valor_type_t valor_type, uint32_t valor)
{
    uint16_t    record_key;
    const char *label;

    switch (valor_type) {
    case TIEMPO_ENCENDIDO:
        record_key = TIME_ON_RECORD_KEY;
        label      = "encendido";
        break;
    case TIEMPO_SLEEP:
        record_key = TIME_SLEEP_RECORD_KEY;
        label      = "sleep";
        break;
    case TIEMPO_EXTENDED_SLEEP:
        record_key = TIME_EXTENDED_SLEEP_RECORD_KEY;
        label      = "sleep extendido";
        break;
    case TIEMPO_EXTENDED_ENCENDIDO:
        record_key = TIME_EXTENDED_ON_RECORD_KEY;
        label      = "encendido extendido";
        break;
    default:
        record_key = TIME_ON_RECORD_KEY;
        label      = "desconocido";
        break;
    }

    fds_record_t record = {
               .file_id           = TIME_FILE_ID,
               .key               = record_key,
               .data.p_data       = &valor,
               .data.length_words = 1};
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    ret_code_t        err_code =
               fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

    if (err_code == NRF_SUCCESS) {
        err_code = fds_record_update(&record_desc, &record);
        NRF_LOG_RAW_INFO(
                   LOG_INFO " Tiempo de %s %s: %d segundos.",
                   label,
                   (err_code == NRF_SUCCESS) ? "actualizado"
                                             : "falló al actualizar",
                   valor / 1000);
    }
    else if (err_code == FDS_ERR_NOT_FOUND) {
        err_code = fds_record_write(&record_desc, &record);
        NRF_LOG_RAW_INFO(
                   LOG_OK " Tiempo de %s %s: %d segundos.",
                   label,
                   (err_code == NRF_SUCCESS) ? "guardado" : "falló al guardar",
                   valor / 1000);
    }
    else {
        NRF_LOG_ERROR(
                   "Error al buscar el registro en memoria flash: %d",
                   err_code);
    }
}

ret_code_t write_date_to_flash(const datetime_t *p_date)
{
    ret_code_t        err_code;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok   = {0};

    fds_record_t      record = {
                    .file_id = DATE_AND_TIME_FILE_ID,
                    .key     = DATE_AND_TIME_RECORD_KEY,
                    .data    = {.p_data       = p_date,
                                .length_words = (sizeof(datetime_t) + 3) /
                                                sizeof(uint32_t)}};

    err_code = fds_record_find(
               DATE_AND_TIME_FILE_ID,
               DATE_AND_TIME_RECORD_KEY,
               &record_desc,
               &ftok);

    if (err_code == NRF_SUCCESS) {
        err_code = fds_record_update(&record_desc, &record);
        if (err_code == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Fecha y hora actualizada correctamente");
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error actualizando fecha y hora: 0x%X",
                       err_code);
        }
    }
    else if (err_code == FDS_ERR_NOT_FOUND) {
        err_code = fds_record_write(NULL, &record);
        if (err_code == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Fecha y hora guardada correctamente");
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error escribiendo fecha y hora: 0x%X",
                       err_code);
        }
    }
    else {
        NRF_LOG_RAW_INFO(LOG_FAIL " Error buscando: 0x%X", err_code);
    }

    return err_code;
}

void load_mac_from_flash(mac_type_t mac_type, uint8_t *mac_out)
{
    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok = {0};
    fds_flash_record_t flash_record;
    uint16_t           record_key;
    const char        *mac_label;

    // Determinar la clave de registro y etiqueta según el tipo de MAC
    switch (mac_type) {
    case MAC_EMISOR:
        record_key = MAC_EMISOR_RECORD_KEY;
        mac_label  = "MAC del emisor";
        break;
    case MAC_REPETIDOR:
        record_key = MAC_REPETIDOR_RECORD_KEY;
        mac_label  = "MAC del repetidor";
        break;
    default:
        record_key = MAC_EMISOR_RECORD_KEY;
        mac_label  = "MAC desconocida";
        break;
    }

    // Busca el registro en la memoria flash
    ret_code_t err_code;
    err_code = fds_record_find(MAC_FILE_ID, record_key, &record_desc, &ftok);
    if (err_code == NRF_SUCCESS) {
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code == NRF_SUCCESS &&
            flash_record.p_header->length_words * 4 >= 6) {
            // Verifica que el tamaño del dato sea correcto
            if (flash_record.p_header->length_words != 2) {
                NRF_LOG_RAW_INFO(
                           "\n\t>> Tamaño de MAC en memoria flash no coincide "
                           "con "
                           "el esperado.");
                fds_record_close(&record_desc);
                return;
            }

            // Copia la MAC al buffer de salida
            memcpy(mac_out, flash_record.p_data, sizeof(uint8_t) * 6);
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO(LOG_OK " %s cargada desde memoria: ", mac_label);
            NRF_LOG_RAW_INFO(
                       "%02X:%02X:%02X:%02X:%02X:%02X",

                       mac_out[0],
                       mac_out[1],
                       mac_out[2],
                       mac_out[3],
                       mac_out[4],
                       mac_out[5]);
        }
        else {
            memcpy(mac_out, flash_record.p_data, sizeof(mac_out));
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO(LOG_OK " %s cargada desde memoria: ", mac_label);
            NRF_LOG_RAW_INFO(
                       "%02X:%02X:%02X:%02X:%02X:%02X",

                       mac_out[0],
                       mac_out[1],
                       mac_out[2],
                       mac_out[3],
                       mac_out[4],
                       mac_out[5]);
        }
    }
    else {
        NRF_LOG_RAW_INFO(
                   LOG_WARN " No se encontro %s. Usando valor "
                            "predeterminado.",
                   mac_label);

        // Valores predeterminados según el tipo de MAC
        if (mac_type == MAC_EMISOR) {
            // MAC predeterminada del emisor
            // Cargar MAC desde la config del repetidor
            mac_out[0] = config_repeater.mac_emisor[0];
            mac_out[1] = config_repeater.mac_emisor[1];
            mac_out[2] = config_repeater.mac_emisor[2];
            mac_out[3] = config_repeater.mac_emisor[3];
            mac_out[4] = config_repeater.mac_emisor[4];
            mac_out[5] = config_repeater.mac_emisor[5];
        }
        else // MAC_REPETIDOR
        {
            // MAC predeterminada del repetidor
            mac_out[0] = config_repeater.mac_repetidor[0];
            mac_out[1] = config_repeater.mac_repetidor[1];
            mac_out[2] = config_repeater.mac_repetidor[2];
            mac_out[3] = config_repeater.mac_repetidor[3];
            mac_out[4] = config_repeater.mac_repetidor[4];
            mac_out[5] = config_repeater.mac_repetidor[5];
        }

        NRF_LOG_RAW_INFO(LOG_INFO " %s cargada (predeterminada): ", mac_label);
        NRF_LOG_RAW_INFO(
                   "%02X:%02X:%02X:%02X:%02X:%02X",
                   mac_out[0],
                   mac_out[1],
                   mac_out[2],
                   mac_out[3],
                   mac_out[4],
                   mac_out[5]);
    }
}

void save_mac_to_flash(mac_type_t mac_type, uint8_t *mac_addr)
{
    fds_record_t      record;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    uint32_t          aligned_data_buffer[2]; // 2 * 4 = 8 bytes
    ret_code_t        ret;
    uint16_t          record_key;
    const char       *mac_label;

    // Determinar la clave de registro y etiqueta según el tipo de MAC
    switch (mac_type) {
    case MAC_EMISOR:
        record_key = MAC_EMISOR_RECORD_KEY;
        mac_label  = "MAC del emisor";
        break;
    case MAC_REPETIDOR:
        record_key = MAC_REPETIDOR_RECORD_KEY;
        mac_label  = "MAC del repetidor";
        break;
    default:
        record_key = MAC_EMISOR_RECORD_KEY;
        mac_label  = "MAC desconocida";
        break;
    }

    memcpy(aligned_data_buffer, mac_addr, 6);

    // Configura el registro con la MAC
    record.file_id     = MAC_FILE_ID;
    record.key         = record_key;
    record.data.p_data = aligned_data_buffer; // Apunta al buffer alineado

    record.data.length_words =
               (6 + sizeof(uint32_t) - 1) / sizeof(uint32_t); // (6 + 3) / 4 = 2

    // Realiza la recolección de basura si es necesario
    // perform_garbage_collection();

    ret = fds_record_find(MAC_FILE_ID, record_key, &record_desc, &ftok);
    // Si ya existe un registro, actualízalo
    if (ret == NRF_SUCCESS) {
        if (fds_record_update(&record_desc, &record) == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_INFO " Actualizando %s en memoria flash.",
                       mac_label);
            // NVIC_SystemReset();  // Reinicia el dispositivo
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al actualizar %s en memoria flash.",
                       mac_label);
        }
    }
    else {
        // Si no existe, crea un nuevo registro
        ret = fds_record_write(&record_desc, &record);

        if (ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_OK " %s guardada correctamente en memoria.",
                       mac_label);
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al crear el registro de %s: %d",
                       mac_label,
                       ret);
        }
    }
}

// Variables globales para el envío asíncrono de historial (similar a cmd15)
static bool     history_send_active    = false;
static uint32_t history_current_record = 0;
static uint32_t history_total_records  = 0;
static uint32_t history_sent_count     = 0;
static uint32_t history_failed_count   = 0;

// Buffer para almacenar los record keys válidos encontrados por
// fds_record_iterate
#define MAX_HISTORY_RECORDS 248
static uint16_t history_valid_keys[MAX_HISTORY_RECORDS];
static uint16_t history_valid_count = 0;


static ret_code_t read_history_record_by_key(
           uint16_t       record_key,
           store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    NRF_LOG_DEBUG("Leyendo registro con RECORD_KEY: 0x%04X", record_key);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) ==
        NRF_SUCCESS) {
        fds_flash_record_t flash_record = {0};
        ret_code_t         ret          = fds_record_open(&desc, &flash_record);
        if (ret != NRF_SUCCESS) {
            NRF_LOG_ERROR("Error al abrir el registro");
            return ret;
        }

        // Verificar que el tamaño del registro sea correcto
        if (flash_record.p_header->length_words !=
            BYTES_TO_WORDS(sizeof(store_history))) {
            NRF_LOG_ERROR("Tamaño del registro no coincide");
            fds_record_close(&desc);
            return NRF_ERROR_INVALID_DATA;
        }

        // Copiar los datos al puntero de salida
        memcpy(p_history_data, flash_record.p_data, sizeof(store_history));

        return fds_record_close(&desc);
    }

    return NRF_ERROR_NOT_FOUND;
}

// Función auxiliar para enviar el siguiente paquete de historial (similar a
// cmd15_send_next_packet)
void history_send_next_packet(void)
{
    if (!history_send_active ||
        history_current_record >= history_total_records) {
        return;
    }

    uint32_t       packets_sent_this_round = 0;
    const uint32_t MAX_PACKETS_PER_ROUND = 5; // Enviar hasta 5 paquetes por vez

    while (history_send_active &&
           history_current_record < history_total_records &&
           packets_sent_this_round < MAX_PACKETS_PER_ROUND) {
        // Verificar que tengamos un índice válido
        if (history_current_record >= history_valid_count) {
            NRF_LOG_RAW_INFO(
                       LOG_WARN " Indice fuera de rango: %d >= %d",
                       history_current_record,
                       history_valid_count);
            history_send_active = false;
            break;
        }

        // Obtener el record key del registro actual
        uint16_t current_key = history_valid_keys[history_current_record];

        // Leer el registro actual directamente desde flash usando el record key
        store_history current_record;
        ret_code_t    read_result =
                   read_history_record_by_key(current_key, &current_record);

        if (read_result != NRF_SUCCESS) {
            // Si no se puede leer el registro, pasar al siguiente
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " No se pudo leer registro key 0x%04X: 0x%X",
                       current_key,
                       read_result);
            history_current_record++;
            history_failed_count++;
            continue;
        }

        // Estructurar los datos en array BLE
        static uint8_t data_array[244];
        uint16_t       position = 0;

        // Byte 0: Magic
        data_array[position++] = 0x08;

        // Bytes 1-7: Fecha y hora
        data_array[position++] = current_record.day;
        data_array[position++] = current_record.month;
        data_array[position++] = (current_record.year >> 8) & 0xFF;
        data_array[position++] = (current_record.year & 0xFF);
        data_array[position++] = current_record.hour;
        data_array[position++] = current_record.minute;
        data_array[position++] = current_record.second;

        // Bytes 8-11: Contador (4 bytes) - convertir a big-endian
        data_array[position++] = (current_record.contador >> 24) & 0xFF;
        data_array[position++] = (current_record.contador >> 16) & 0xFF;
        data_array[position++] = (current_record.contador >> 8) & 0xFF;
        data_array[position++] = (current_record.contador & 0xFF);

        // Bytes 12-15: V1, V2 (2 bytes cada uno) - convertir a big-endian
        data_array[position++] = (current_record.V1 >> 8) & 0xFF;
        data_array[position++] = (current_record.V1 & 0xFF);
        data_array[position++] = (current_record.V2 >> 8) & 0xFF;
        data_array[position++] = (current_record.V2 & 0xFF);

        // Byte 16: Battery
        data_array[position++] = current_record.battery;

        // Bytes 17-28: MACs (rellenar con ceros)
        for (int j = 0; j < 12; j++) {
            data_array[position++] = 0x00;
        }

        // Bytes 29-40: V3-V8 (2 bytes cada uno) - convertir a big-endian
        data_array[position++] = (current_record.V3 >> 8) & 0xFF;
        data_array[position++] = (current_record.V3 & 0xFF);
        data_array[position++] = (current_record.V4 >> 8) & 0xFF;
        data_array[position++] = (current_record.V4 & 0xFF);
        data_array[position++] = (current_record.V5 >> 8) & 0xFF;
        data_array[position++] = (current_record.V5 & 0xFF);
        data_array[position++] = (current_record.V6 >> 8) & 0xFF;
        data_array[position++] = (current_record.V6 & 0xFF);
        data_array[position++] = (current_record.V7 >> 8) & 0xFF;
        data_array[position++] = (current_record.V7 & 0xFF);
        data_array[position++] = (current_record.V8 >> 8) & 0xFF;
        data_array[position++] = (current_record.V8 & 0xFF);

        // Byte 41: Temperatura
        data_array[position++] = current_record.temp;

        // Byte 42-43: last_position
        data_array[position++] = 0x11;
        data_array[position++] = 0x22;

        // Intentar enviar el paquete
        ret_code_t ret = app_nus_server_send_data(data_array, position);

        if (ret == NRF_SUCCESS) {
            history_current_record++;
            history_sent_count++;
            packets_sent_this_round++;

            // Mostrar progreso cada 10 registros
            if (history_sent_count % 10 == 0) {
                NRF_LOG_RAW_INFO(
                           "\nHistorial: %d/%d enviados",
                           history_sent_count,
                           history_total_records);
            }
        }
        else if (ret == NRF_ERROR_RESOURCES || ret == NRF_ERROR_BUSY) {
            // Buffer lleno - esperar al próximo TX_RDY
            break;
        }
        else {
            // Error real - detener el envío
            history_failed_count++;
            NRF_LOG_RAW_INFO(
                       "\nError enviando registro %d: 0x%X - Deteniendo envío",
                       history_current_record + 1,
                       ret);
            history_send_active = false;
            break;
        }
    }

    // Verificar finalización
    if (history_send_active &&
        history_current_record >= history_total_records) {
        history_send_active = false;
        NRF_LOG_RAW_INFO("\n=== ENVIO DE HISTORIAL COMPLETADO ===");
        NRF_LOG_RAW_INFO(
                   "\nRegistros enviados: %d/%d, Fallos: %d",
                   history_sent_count,
                   history_total_records,
                   history_failed_count);
        if (history_total_records > 0) {
            NRF_LOG_RAW_INFO(
                       "\nTasa exito: %d%%\n",
                       (history_sent_count * 100) / history_total_records);
        }
    }
}

ret_code_t send_all_history(void)
{
    ret_code_t    err_code;
    store_history history_record = {0};
    uint16_t      valid_records  = 0;

    NRF_LOG_RAW_INFO(
               "\n\n" LOG_EXEC
               "--- INICIANDO ENVIO DE HISTORIAL ASINCRONO ---");

    // Verificar si ya hay un envío activo
    if (history_send_active) {
        NRF_LOG_RAW_INFO(
                   LOG_INFO
                   " Envio de historial ya esta activo - ignorando nueva "
                   "solicitud");
        return NRF_ERROR_BUSY;
    }

    // Verificar estado inicial enviando un pequeño paquete de prueba
    uint8_t test_data[] = {0x08, 0x00}; // Magic + test byte
    err_code            = app_nus_server_send_data(test_data, 2);
    if (err_code != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(
                   LOG_WARN " No se puede enviar por BLE (0x%X). Verifica "
                            "conexion.",
                   err_code);
        if (err_code == NRF_ERROR_INVALID_STATE) {
            NRF_LOG_RAW_INFO(
                       LOG_WARN
                       " Asegurate de que hay un dispositivo conectado");
            NRF_LOG_RAW_INFO(
                       LOG_WARN
                       " Verifica que las notificaciones esten habilitadas");
        }
        return err_code;
    }
    nrf_delay_ms(100);

    NRF_LOG_RAW_INFO(
               LOG_EXEC " --- FASE 1: Contando registros validos en flash ---");

    // FASE 1: Contar y almacenar los record keys de registros válidos usando
    // fds_record_iterate
    fds_find_token_t   token          = {0};
    fds_record_desc_t  record_desc    = {0};
    fds_flash_record_t flash_record   = {0};
    uint16_t           expected_words = BYTES_TO_WORDS(sizeof(store_history));

    // Resetear contador y array de keys válidos
    history_valid_count = 0;

    // Iterar a través de todos los registros del HISTORY_FILE_ID
    while (fds_record_iterate(&record_desc, &token) == NRF_SUCCESS &&
           history_valid_count < MAX_HISTORY_RECORDS) {
        // Abrir el registro para acceder a su header
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code != NRF_SUCCESS) {
            continue;
        }

        // Verificar que sea un registro de historial
        if (flash_record.p_header->file_id != HISTORY_FILE_ID) {
            fds_record_close(&record_desc);
            continue;
        }

        // Verificar que no sea el registro del contador
        if (flash_record.p_header->record_key == HISTORY_COUNTER_RECORD_KEY) {
            fds_record_close(&record_desc);
            continue;
        }

        // Verificar que el tamaño sea correcto
        if (flash_record.p_header->length_words ==
            expected_words) { // Inicializado a 0
            // Almacenar el record key válido
            history_valid_keys[history_valid_count] =
                       flash_record.p_header->record_key;
            history_valid_count++;
            valid_records++;

            if (valid_records % 10 == 0) {
                NRF_LOG_RAW_INFO(
                           LOG_INFO " Contados %d registros...",
                           valid_records);
            }
        }

        // Cerrar el registro
        fds_record_close(&record_desc);
    }

    NRF_LOG_RAW_INFO(
               LOG_OK
               " --- FASE 1 COMPLETADA: %d registros validos encontrados "
               "---",
               history_valid_count);

    if (history_valid_count == 0) {
        NRF_LOG_RAW_INFO(LOG_INFO " No hay registros para enviar");
        return NRF_SUCCESS;
    }

    // FASE 2: Inicializar variables para envío asíncrono (usando array de
    // record keys)
    NRF_LOG_RAW_INFO(
               LOG_EXEC
               " --- FASE 2: Iniciando envio asincrono (usando record keys) "
               "---");

    history_send_active    = true;
    history_current_record = 0;
    history_total_records  = history_valid_count;
    history_sent_count     = 0;
    history_failed_count   = 0;

    NRF_LOG_RAW_INFO(
               LOG_EXEC " Enviando %d registros de forma asincrona...",
               history_total_records);

    // Enviar el primer lote de paquetes - los siguientes se enviarán en
    // BLE_NUS_EVT_TX_RDY
    history_send_next_packet();

    NRF_LOG_FLUSH();
    // Deleted delay function
    // nrf_delay_ms(1000);
    return NRF_SUCCESS;
}

// Funciones de estado para el envío de historial
bool history_send_is_active(void)
{
    return history_send_active;
}

uint32_t history_get_progress(void)
{
    if (history_total_records == 0)
        return 0;
    return (history_sent_count * 100) / history_total_records;
}

void delete_all_history(void)
{
    ret_code_t ret;

    ret = fds_file_delete(HISTORY_FILE_ID);

    if (ret != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(
                   LOG_WARN " No se pudieron eliminar todos "
                            "los "
                            "historiales: %d",
                   ret);
    }
    else {
        // Si se eliminaron exitosamente, resetear el contador
        config_repeater.cantidad_historiales = 0;
        
        // Guardar la configuración actualizada
        ret_code_t save_ret = save_config_to_flash(&config_repeater);
        if (save_ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Contador de historiales reseteado a 0");
        }
        else {
            NRF_LOG_RAW_INFO(LOG_WARN " Error al guardar contador: 0x%X", save_ret);
        }
    }
    
    NRF_LOG_RAW_INFO(LOG_EXEC " Corriendo el recolector de basura...");

    ret = fds_gc();
    if (ret != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(
                   LOG_FAIL " Error al correr el recolector "
                            "de "
                            "basura: %d",
                   ret);
    }
}

ret_code_t delete_history_record_by_id(uint16_t record_id)
{
    ret_code_t        ret;
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // Calcular la clave del registro usando el offset del ID
    uint16_t record_key = HISTORY_RECORD_KEY_START + record_id;

    NRF_LOG_RAW_INFO(
               LOG_EXEC " --- Eliminando registro de historial ID: %u "
                        "---",
               record_id);
    NRF_LOG_RAW_INFO(LOG_EXEC " Buscando registro con KEY: 0x%04X", record_key);

    // Buscar el registro en la memoria flash
    ret = fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token);

    if (ret == NRF_SUCCESS) {
        // El registro existe, proceder a eliminarlo
        ret = fds_record_delete(&desc);

        if (ret == NRF_SUCCESS) {
            // Ejecutar recolección de basura para liberar espacio
            nrf_delay_ms(100);
            ret_code_t gc_ret = fds_gc();
            if (gc_ret == NRF_SUCCESS) {
                NRF_LOG_RAW_INFO(LOG_OK " Recoleccion de basura completada");
            }
            else {
                NRF_LOG_RAW_INFO(
                           LOG_FAIL " Error en recoleccion de basura: "
                                    "0x%X",
                           gc_ret);
            }
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al eliminar el registro ID %u: "
                                "0x%X",
                       record_id,
                       ret);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        NRF_LOG_RAW_INFO(
                   LOG_FAIL " Registro ID %u no encontrado, no se realizo "
                            "ninguna accion",
                   record_id);
        // No es un error, simplemente el registro no existe
        ret = NRF_SUCCESS;
    }
    else {
        NRF_LOG_RAW_INFO(
                   LOG_FAIL " Error al buscar el registro ID %u: "
                            "0x%X",
                   record_id,
                   ret);
    }

    return ret;
}

ret_code_t load_adc_values(adc_values_t *adc_values_cargados)
{
    ret_code_t        err_code;
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    err_code                = fds_record_find(
               HISTORY_FILE_ID,
               HISTORY_ADC_VALUES_RECORD_KEY,
               &desc,
               &token);
    if (err_code == NRF_SUCCESS) {
        fds_flash_record_t flash_record = {0};
        err_code                        = fds_record_open(&desc, &flash_record);
        if (err_code != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " No se pudo cargar desde FLASH los ADC "
                                "VALUES");
            return err_code;
        }

        memcpy(adc_values_cargados, flash_record.p_data, sizeof(adc_values_t));

        return fds_record_close(&desc);
    }

    return NRF_ERROR_NOT_FOUND;
}

ret_code_t save_adc_values(adc_values_t const *valores_a_guardar)
{
    ret_code_t        err_code;
    fds_record_desc_t desc_value = {0};
    fds_find_token_t  token      = {0};
    fds_record_t      record     = {
                        .file_id = HISTORY_FILE_ID,
                        .key     = HISTORY_ADC_VALUES_RECORD_KEY,
                        .data    = {.p_data       = valores_a_guardar,
                                    .length_words = (sizeof(adc_values_t) + 3) /
                                                    sizeof(uint32_t)}};

    if (valores_a_guardar->V1 >= 0 && valores_a_guardar->V1 <= 1023 && //
        valores_a_guardar->V2 >= 0 && valores_a_guardar->V2 <= 1023 && //
        valores_a_guardar->contador >= 1 &&
        valores_a_guardar->contador <= 65535) {
        adc_values.V1       = valores_a_guardar->V1;
        adc_values.V2       = valores_a_guardar->V2;
        adc_values.contador = valores_a_guardar->contador;
        NRF_LOG_RAW_INFO("\n\nLos valores de \"adc_values\" cargados son:\n");
        NRF_LOG_RAW_INFO(
                   "V1: %d\tV2: %d\tContador: %d\n",
                   adc_values.V1,
                   adc_values.V2,
                   adc_values.contador);
        NRF_LOG_FLUSH();
    }
    else {
        NRF_LOG_RAW_INFO("\n[ERROR] Valores de ADC_VALUES fuera de rango");
        return FDS_ERR_NOT_FOUND;
    }

    err_code = fds_record_find(
               HISTORY_FILE_ID,
               HISTORY_ADC_VALUES_RECORD_KEY,
               &desc_value,
               &token);
    if (err_code == NRF_SUCCESS) {
        err_code = fds_record_update(&desc_value, &record);
        if (err_code != NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       "\n[ERROR] No se pudo actualizar el registro de "
                       "ADC_VALUES");
        }
    }
    else if (err_code == FDS_ERR_NOT_FOUND) {
        err_code = fds_record_write(NULL, &record);
        if (err_code == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       "\n[ERROR] No se pudo escribir el registro ADC_VALUES");
        }
    }
    else {
        NRF_LOG_RAW_INFO(
                   "\n[ERROR] No se encontro y no se pudo guardar el registro "
                   "ADV_VALUES");
    }
    return err_code;
}

//
//

void load_default_config(config_repeater_t *p_config)
{
    if (p_config == NULL)
        return;

    // MACs predeterminadas
    // MAC del emisor
    p_config->mac_emisor[5] = 0xFF;
    p_config->mac_emisor[4] = 0x00;
    p_config->mac_emisor[3] = 0x00;
    p_config->mac_emisor[2] = 0x00;
    p_config->mac_emisor[1] = 0xAB;
    p_config->mac_emisor[0] = 0xC1;

    // MAC del repetidor
    p_config->mac_repetidor[5] = 0x01;
    p_config->mac_repetidor[4] = 0x00;
    p_config->mac_repetidor[3] = 0x00;
    p_config->mac_repetidor[2] = 0x00;
    p_config->mac_repetidor[1] = 0xAB;
    p_config->mac_repetidor[0] = 0xC2;

    // Por defecto, usar MAC custom (comportamiento previo)
    p_config->enable_custom_mac_repetidor = true;

    // Tiempos predeterminados
    p_config->tiempo_encendido = DEFAULT_DEVICE_ON_TIME_MS;
    p_config->tiempo_dormido   = DEFAULT_DEVICE_SLEEP_TIME_MS;
    p_config->tiempo_extendido = DEFAULT_DEVICE_EXTENDED_ON_TIME_MS;

    // Version de firmware
    p_config->version[0] = 1;
    p_config->version[1] = 0;
    p_config->version[2] = 0;

    // Fecha y hora predeterminada de configuracion
    p_config->fecha.year   = 2024;
    p_config->fecha.month  = 1;
    p_config->fecha.day    = 1;
    p_config->fecha.hour   = 0;
    p_config->fecha.minute = 0;
    p_config->fecha.second = 0;

    // Cantidad de historiales guardados (inicialmente 0)
    p_config->cantidad_historiales = 0;

    NRF_LOG_RAW_INFO(LOG_INFO " Configuracion predeterminada cargada");
}


ret_code_t save_config_to_flash(config_repeater_t *p_config)
{
    if (p_config == NULL) {
        return NRF_ERROR_NULL;
    }

    // Actualizar la fecha de configuracion con el tiempo actual del RTC
    datetime_t current_time;
    if (calendar_get_time(&current_time)) {
        p_config->fecha = current_time;
    }
    else {
        NRF_LOG_RAW_INFO(LOG_WARN " No se pudo obtener tiempo actual del RTC");
    }

    fds_record_t      record;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    ret_code_t        ret;

    // Configura el registro
    record.file_id     = CONFIG_FILE_ID;
    record.key         = CONFIG_RECORD_KEY;
    record.data.p_data = p_config;
    record.data.length_words =
               (sizeof(config_repeater_t) + sizeof(uint32_t) - 1) /
               sizeof(uint32_t);

    // Buscar si ya existe el registro
    ret = fds_record_find(
               CONFIG_FILE_ID,
               CONFIG_RECORD_KEY,
               &record_desc,
               &ftok);

    if (ret == NRF_SUCCESS) {
        // Si existe, actualizar
        ret = fds_record_update(&record_desc, &record);
        if (ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(
                       LOG_OK " Configuracion actualizada en memoria flash");
        }
        else {
            NRF_LOG_ERROR("Error al actualizar configuracion: 0x%X", ret);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        // Si no existe, crear nuevo registro
        ret = fds_record_write(&record_desc, &record);
        if (ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Configuracion guardada en memoria flash");
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_OK " Error al guardar configuracion: 0x%X",
                       ret);
        }
    }
    else {
        NRF_LOG_RAW_INFO(LOG_FAIL " Error al buscar configuracion: 0x%X", ret);
    }

    return ret;
}

ret_code_t load_config_from_flash(config_repeater_t *p_config)
{
    if (p_config == NULL) {
        return NRF_ERROR_NULL;
    }

    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok = {0};
    fds_flash_record_t flash_record;
    ret_code_t         ret;

    // Buscar el registro de configuración
    ret = fds_record_find(
               CONFIG_FILE_ID,
               CONFIG_RECORD_KEY,
               &record_desc,
               &ftok);

    if (ret == NRF_SUCCESS) {
        // Abrir el registro
        ret = fds_record_open(&record_desc, &flash_record);
        if (ret == NRF_SUCCESS) {
            // Verificar tamaño
            uint32_t expected_words =
                       (sizeof(config_repeater_t) + sizeof(uint32_t) - 1) /
                       sizeof(uint32_t);
            if (flash_record.p_header->length_words >= expected_words) {
                // Copiar los datos
                memcpy(p_config,
                       flash_record.p_data,
                       sizeof(config_repeater_t));

                // Cerrar el registro
                fds_record_close(&record_desc);

                // Validar si la fecha es valida (diferente de cero)
                if (p_config->fecha.year == 0 || p_config->fecha.month == 0 ||
                    p_config->fecha.day == 0) {
                    NRF_LOG_RAW_INFO(
                               LOG_FAIL " Fecha de configuracion no valida - "
                                        "configuracion antigua");
                }

                return NRF_SUCCESS;
            }
            else {
                NRF_LOG_RAW_INFO(
                           LOG_FAIL
                           " Tamano de configuracion en flash no coincide");
                fds_record_close(&record_desc);
                return NRF_ERROR_INVALID_DATA;
            }
        }
        else {
            NRF_LOG_RAW_INFO(
                       LOG_FAIL " Error al abrir configuracion: 0x%X",
                       ret);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND) {
        NRF_LOG_RAW_INFO(
                   LOG_WARN
                   " Configuracion no encontrada en flash - usando valores "
                   "predeterminados");
        load_default_config(p_config);
        return NRF_SUCCESS;
    }
    else {
        NRF_LOG_RAW_INFO(LOG_FAIL " Error al buscar configuracion: 0x%X", ret);
    }

    return ret;
}


void set_custom_mac_repeater(void)
{
    ret_code_t err_code;
    ble_gap_addr_t gap_addr;

    // Si enable_custom_mac_repetidor es true, configurar MAC custom
    if (config_repeater.enable_custom_mac_repetidor)
    {
        // Obtener la dirección actual
        err_code = sd_ble_gap_addr_get(&gap_addr);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO(LOG_FAIL " Error al obtener dirección actual: 0x%X", err_code);
            return;
        }

        // Configurar la nueva dirección con la MAC del repetidor
        // Los bytes deben estar en orden inverso para BLE (little-endian)
        gap_addr.addr[0] = config_repeater.mac_repetidor[5];
        gap_addr.addr[1] = config_repeater.mac_repetidor[4];
        gap_addr.addr[2] = config_repeater.mac_repetidor[3];
        gap_addr.addr[3] = config_repeater.mac_repetidor[2];
        gap_addr.addr[4] = config_repeater.mac_repetidor[1];
        gap_addr.addr[5] = config_repeater.mac_repetidor[0];
        
        gap_addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;

        // Establecer la nueva dirección
        err_code = sd_ble_gap_addr_set(&gap_addr);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO(LOG_FAIL " Error al establecer MAC custom: 0x%X", err_code);
            return;
        }

        NRF_LOG_RAW_INFO("\n" LOG_OK " MAC custom del repetidor configurada: %02X:%02X:%02X:%02X:%02X:%02X",
                         config_repeater.mac_repetidor[0],
                         config_repeater.mac_repetidor[1],
                         config_repeater.mac_repetidor[2],
                         config_repeater.mac_repetidor[3],
                         config_repeater.mac_repetidor[4],
                         config_repeater.mac_repetidor[5]);
    }
    else
    {
        NRF_LOG_RAW_INFO(LOG_INFO " Usando MAC por defecto del dispositivo");
    }
}

void init_sistema_configuracion(config_repeater_t *p_config)
{
    ret_code_t ret;

    // Validar parametro de entrada
    if (p_config == NULL) {
        NRF_LOG_RAW_INFO(LOG_FAIL " Puntero de configuracion nulo");
        return;
    }

    // Flush inicial para limpiar buffer
    NRF_LOG_FLUSH();
    nrf_delay_ms(10);

    NRF_LOG_RAW_INFO("\n\n\033[1;36m==================\033[0m ");
    NRF_LOG_RAW_INFO("\033[1;33mCONFIGURACION CARGADA\033[0m");
    NRF_LOG_RAW_INFO(" \033[1;36m===================\033[0m\n");
    NRF_LOG_FLUSH();
    nrf_delay_ms(20);

    // Cargar configuracion desde flash
    ret = load_config_from_flash(p_config);

    if (ret == NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(LOG_OK " Configuracion cargada desde flash\n\n");
    }
    else {
        NRF_LOG_RAW_INFO(LOG_INFO " Usando configuracion predeterminada\n\n");

        // Guardar configuracion predeterminada para futuras cargas
        ret_code_t save_ret = save_config_to_flash(p_config);
        if (save_ret == NRF_SUCCESS) {
            NRF_LOG_RAW_INFO(LOG_OK " Configuracion predeterminada guardada\n");
        }
    }
    NRF_LOG_FLUSH();
    nrf_delay_ms(20);

    // Mostrar MACs (una linea por MAC para respetar limite de argumentos)
    NRF_LOG_RAW_INFO(
               " - MAC Emisor    : %02X:%02X:%02X:",
               p_config->mac_emisor[0],
               p_config->mac_emisor[1],
               p_config->mac_emisor[2]);
    NRF_LOG_RAW_INFO(
               "%02X:%02X:%02X\n",
               p_config->mac_emisor[3],
               p_config->mac_emisor[4],
               p_config->mac_emisor[5]);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    NRF_LOG_RAW_INFO(
               " - MAC Repetidor : %02X:%02X:%02X:",
               p_config->mac_repetidor[0],
               p_config->mac_repetidor[1],
               p_config->mac_repetidor[2]);
    NRF_LOG_RAW_INFO(
               "%02X:%02X:%02X\n",
               p_config->mac_repetidor[3],
               p_config->mac_repetidor[4],
               p_config->mac_repetidor[5]);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    // Mostrar tiempos
    NRF_LOG_RAW_INFO(" - Tiempo Activo : %u ms\n", p_config->tiempo_encendido);
    NRF_LOG_RAW_INFO(" - Tiempo Sleep  : %u ms\n", p_config->tiempo_dormido);
    NRF_LOG_RAW_INFO(" - Tiempo Extend.: %u ms\n", p_config->tiempo_extendido);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    // Mostrar version
    NRF_LOG_RAW_INFO(
               " - Version FW    : v%u.%u.%u\n",
               p_config->version[0],
               p_config->version[1],
               p_config->version[2]);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    // Mostrar fecha de configuracion
    NRF_LOG_RAW_INFO(
               " - Fecha y hora  : %02u/%02u/%u ",
               p_config->fecha.day,
               p_config->fecha.month,
               p_config->fecha.year);
    NRF_LOG_RAW_INFO(
               "%02u:%02u:%02u\n",
               p_config->fecha.hour,
               p_config->fecha.minute,
               p_config->fecha.second);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    // Mostrar cantidad de historiales
    NRF_LOG_RAW_INFO(" - Historiales   : %u registros\n", p_config->cantidad_historiales);
    NRF_LOG_FLUSH();
    nrf_delay_ms(15);

    NRF_LOG_RAW_INFO(
               "\n\033[1;36m==================================================="
               "======="
               "==\033[0m");
    NRF_LOG_FLUSH();
    nrf_delay_ms(10);

    // Configurar la MAC del dispositivo según la configuración
    set_custom_mac_repeater();
}

ret_code_t send_config_via_ble(void)
{
    ret_code_t ret;

    // Crear buffer con identificadores + configuración
    uint16_t config_size = sizeof(config_repeater_t);
    uint16_t total_size =
               2 + config_size; // 2 bytes identificadores + configuración
    uint8_t *buffer = malloc(total_size);

    if (buffer == NULL) {
        NRF_LOG_RAW_INFO(
                   LOG_WARN " No se pudo reservar memoria para el buffer");
        return NRF_ERROR_NO_MEM;
    }

    // Agregar identificadores al inicio
    buffer[0] = 0xCC;
    buffer[1] = 0xAA;

    // Copiar la estructura de configuración después de los identificadores
    memcpy(&buffer[2], &config_repeater, config_size);

    // Enviar via BLE NUS
    ret = app_nus_server_send_data(buffer, total_size);

    if (ret == NRF_SUCCESS) {
        NRF_LOG_RAW_INFO(
                   LOG_OK " Configuracion enviada via BLE con identificadores "
                   "(%u "
                   "bytes total)",
                   total_size);

        // Mostrar los primeros bytes en log para debug
        NRF_LOG_RAW_INFO(LOG_INFO " Primeros bytes: ");
        for (int i = 0; i < (total_size > 16 ? 16 : total_size); i++) {
            NRF_LOG_RAW_INFO("%02X ", buffer[i]);
        }
        if (total_size > 16) {
            NRF_LOG_RAW_INFO("... (%u bytes total)", total_size);
        }
        NRF_LOG_RAW_INFO("\n");
    }
    else {
        NRF_LOG_RAW_INFO(
                   LOG_WARN " No se pudo enviar configuracion: 0x%X",
                   ret);
    }

    // Liberar memoria
    free(buffer);

    // También mostrar en log local para debug
    NRF_LOG_RAW_INFO("\n--- CONFIGURACION ACTUAL ---");
    NRF_LOG_RAW_INFO(
               "\nMAC Emisor: %02X:%02X:%02X:%02X:%02X:%02X",
               config_repeater.mac_emisor[5],
               config_repeater.mac_emisor[4],
               config_repeater.mac_emisor[3],
               config_repeater.mac_emisor[2],
               config_repeater.mac_emisor[1],
               config_repeater.mac_emisor[0]);
    NRF_LOG_RAW_INFO(
               "\nMAC Repetidor: %02X:%02X:%02X:%02X:%02X:%02X",
               config_repeater.mac_repetidor[5],
               config_repeater.mac_repetidor[4],
               config_repeater.mac_repetidor[3],
               config_repeater.mac_repetidor[2],
               config_repeater.mac_repetidor[1],
               config_repeater.mac_repetidor[0]);
    NRF_LOG_RAW_INFO(
               "\nMAC Custom Repetidor: %s",
               config_repeater.enable_custom_mac_repetidor ? "Habilitado" : "Deshabilitado");
    NRF_LOG_RAW_INFO(
               "\nTiempos: Activo=%ums, Sleep=%ums, Extendido=%ums",
               config_repeater.tiempo_encendido,
               config_repeater.tiempo_dormido,
               config_repeater.tiempo_extendido);
    NRF_LOG_RAW_INFO(
               "\nVersion: v%u.%u.%u",
               config_repeater.version[0],
               config_repeater.version[1],
               config_repeater.version[2]);
    NRF_LOG_RAW_INFO(
               "\nFecha Config: %02u/%02u/%u %02u:%02u:%02u",
               config_repeater.fecha.day,
               config_repeater.fecha.month,
               config_repeater.fecha.year,
               config_repeater.fecha.hour,
               config_repeater.fecha.minute,
               config_repeater.fecha.second);
    NRF_LOG_RAW_INFO(
               "\nCantidad Historiales: %u",
               config_repeater.cantidad_historiales);
    NRF_LOG_RAW_INFO("\n--- FIN CONFIGURACION ---\n");
    NRF_LOG_FLUSH();

    return ret;
}
