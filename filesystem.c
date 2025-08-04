#include "filesystem.h"

// Buffer estático para evitar problemas con variables locales en el stack
static store_history g_temp_history_buffer;
extern void app_nus_client_on_data_received(const uint8_t *data_ptr, uint16_t data_length);

ret_code_t save_history_record_emisor(store_history const *p_history_data,
                                      uint16_t             offset)
{
    ret_code_t        ret;
    fds_record_desc_t desc_history = {0};
    fds_record_desc_t desc_counter = {0};
    fds_find_token_t  token        = {0};

    memcpy(&g_temp_history_buffer, p_history_data, sizeof(store_history));
    
    // Preparar nuevo registro histórico
    uint16_t     record_key = HISTORY_RECORD_KEY_START + offset;
    fds_record_t new_record = {
        .file_id     = HISTORY_FILE_ID,
        .key         = record_key,
        .data.p_data = &g_temp_history_buffer,  // Usar buffer estático
        .data.length_words = BYTES_TO_WORDS(sizeof(store_history))
    };

    // Buscar el registro del historial, si no existe lo escribe
    ret = fds_record_find(HISTORY_FILE_ID, record_key, &desc_history, &token);
    if (ret == NRF_SUCCESS)
    {
        // Si el registro ya existe, lo actualiza
        ret = fds_record_update(&desc_history, &new_record);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al actualizar el registro: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO("\nRegistro actualizado con KEY: 0x%04X", record_key);
        
        // Esperar que FDS complete la operación + flush de GC si es necesario
        nrf_delay_ms(1000);
        (void)fds_gc(); // Forzar garbage collection si es necesario
    }
    else if (ret == FDS_ERR_NOT_FOUND)
    {
        // Si no existe, lo escribe
        ret = fds_record_write(&desc_history, &new_record);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al escribir el registro: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO("\nRegistro escrito con KEY: 0x%04X", record_key);
        
        // Esperar que FDS complete la operación + flush de GC si es necesario
        nrf_delay_ms(1000);
        (void)fds_gc(); // Forzar garbage collection si es necesario
    }
    else
    {
        NRF_LOG_RAW_INFO("\nError al buscar el registro: %d", ret);
        return ret;
    }

    return NRF_SUCCESS;
}

ret_code_t update_history_counter(uint32_t new_count)
{
    fds_record_desc_t desc_counter = {0};
    fds_find_token_t  token        = {0};
    ret_code_t        ret;

    // Buscar el registro del contador
    ret = fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY,
                          &desc_counter, &token);

    nrf_delay_ms(500);
    fds_record_t counter_record = {.file_id           = HISTORY_FILE_ID,
                                   .key               = HISTORY_COUNTER_RECORD_KEY,
                                   .data.p_data       = &new_count,
                                   .data.length_words = 1};

    if (ret == NRF_SUCCESS)
    {
        // Actualizar el registro existente
        ret = fds_record_update(&desc_counter, &counter_record);
        nrf_delay_ms(500); // Delay tras actualizar
        if (ret == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nContador actualizado correctamente a: %lu",
                             new_count);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND)
    {
        // Si no existe, escribir el valor inicial (0) en memoria
        uint32_t initial_value     = 1;
        counter_record.data.p_data = &initial_value;
        ret                        = fds_record_write(NULL, &counter_record);
        nrf_delay_ms(500); // Delay tras escribir
        if (ret == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nContador creado en memoria con valor 1.");
        }
        else
        {
            NRF_LOG_RAW_INFO("\nError al crear el contador: %d", ret);
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\nError al buscar el registro del contador: %d", ret);
    }

    return ret;
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS STARTS HERE.
//-------------------------------------------------------------------------------------------------------------
ret_code_t save_history_record(store_history const *p_history_data)
{
    ret_code_t        ret;
    fds_record_desc_t desc_history  = {0};
    fds_record_desc_t desc_counter  = {0};
    fds_find_token_t  token         = {0};
    uint32_t          history_count = 0; // Inicializado a 0

    bool              counter_exists =
        (fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY,
                         &desc_counter, &token) == NRF_SUCCESS);

    nrf_delay_ms(500);
    if (counter_exists)
    {
        NRF_LOG_RAW_INFO("\nContador de historial encontrado.");
        fds_flash_record_t flash_record;
        ret = fds_record_open(&desc_counter, &flash_record);

        nrf_delay_ms(500);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al abrir el registro del contador");
            return ret;
        }

        // Verificar longitud del contador
        if (flash_record.p_header->length_words != 1)
        {
            NRF_LOG_RAW_INFO("\nError: longitud inválida del contador");
            fds_record_close(&desc_counter);
            return FDS_ERR_INVALID_ARG;
        }
        memcpy(&history_count, flash_record.p_data, sizeof(uint32_t));
        NRF_LOG_RAW_INFO("\nValor actual del contador: %u", history_count);

        // Cerrar registro después de leer
        ret = fds_record_close(&desc_counter);

        nrf_delay_ms(500);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al cerrar el contador");
            return ret;
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\nContador no encontrado, usando 0.");
    }

    // CRÍTICO: Copiar los datos a un buffer estático para evitar 
    // problemas con variables locales que se destruyen
    memcpy(&g_temp_history_buffer, p_history_data, sizeof(store_history));

    // Preparar nuevo registro histórico
    uint16_t     record_key = HISTORY_RECORD_KEY_START + history_count;
    fds_record_t new_record = {
        .file_id     = HISTORY_FILE_ID,
        .key         = record_key,
        .data.p_data = &g_temp_history_buffer,  // Usar buffer estático
        .data.length_words = BYTES_TO_WORDS(sizeof(store_history))
    };

    // Escribir registro histórico
    ret = fds_record_write(&desc_history, &new_record);

    nrf_delay_ms(500);
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\nError al escribir registro: %d", ret);
        return ret;
    }
    NRF_LOG_RAW_INFO("\nRegistro escrito con KEY: 0x%04X", record_key);

    // Actualizar contador
    history_count++;
    ret = update_history_counter(history_count);

    nrf_delay_ms(500);
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\nError al actualizar el contador: %d", ret);
        return ret;
    }

    return NRF_SUCCESS;
}

ret_code_t read_history_record_by_id(uint16_t       record_id,
                                     store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // La clave del registro se calcula sumando el ID a la clave base.
    uint16_t record_key = (uint16_t)(HISTORY_RECORD_KEY_START + record_id);

    NRF_LOG_RAW_INFO("\nLeyendo registro de historial con \x1B[33mID:\x1B[0m %u "
                     "y \x1B[33mRECORD_KEY:\x1B[0m 0x%04X",
                     record_id, record_key);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) ==
        NRF_SUCCESS)
    {

        nrf_delay_ms(100);
        fds_flash_record_t flash_record = {0};
        ret_code_t         ret          = fds_record_open(&desc, &flash_record);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al abrir el registro de historial");
            nrf_delay_ms(100);
            return ret;
        }

        // Verificar que el tamaño del registro en flash coincida con el del struct.
        if (flash_record.p_header->length_words != BYTES_TO_WORDS(sizeof(store_history)))
        {
            NRF_LOG_RAW_INFO(
                "\nTamano del registro en flash no coincide con el esperado.");
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
    if (fds_record_find(HISTORY_FILE_ID, (uint16_t)HISTORY_COUNTER_RECORD_KEY,
                        &desc, &token) == NRF_SUCCESS)
    {
        fds_flash_record_t flash_record  = {0};
        uint32_t           history_count = 0; // Correcto: uint32_t

        fds_record_open(&desc, &flash_record);
        memcpy(&history_count, flash_record.p_data,
               sizeof(uint32_t)); // Leer 4 bytes, no 2
        fds_record_close(&desc);

        if (history_count == 0)
        {
            return NRF_ERROR_NOT_FOUND; // No hay registros guardados.
        }

        // 2. El ID del último registro es (contador - 1).
        uint32_t last_record_id = history_count - 1;
        return read_history_record_by_id((uint16_t)last_record_id, p_history_data);
    }

    // Si el contador no se encuentra, significa que no hay registros.
    return NRF_ERROR_NOT_FOUND;
}

void print_history_record(store_history const *p_record, const char *p_title)
{
    // Calcula el largo de la línea de cierre según el título
    NRF_LOG_RAW_INFO("\n\n\x1B[36m=======\x1B[0m %s \x1B[36m=======\x1B[0m\n\n",
                     p_title);
    NRF_LOG_RAW_INFO("Fecha        : %02d/%02d/%04d\n", p_record->day,
                     p_record->month, p_record->year);
    NRF_LOG_RAW_INFO("Hora         : %02d:%02d:%02d\n", p_record->hour,
                     p_record->minute, p_record->second);
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
    NRF_LOG_RAW_INFO("\n\x1B[36m================\x1B[0m FIN "
                     "\x1B[36m================\x1B[0m\n");
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
    uint16_t           record_key;
    ret_code_t         err_code;
    // Determinar el Record Key según el tipo de valor
    if (valor_type == TIEMPO_ENCENDIDO) {
        record_key = TIME_ON_RECORD_KEY;
    } else if (valor_type == TIEMPO_ENCENDIDO_EXTENDED) {
        record_key = TIME_ON_EXTENDED_RECORD_KEY;
    } else {
        record_key = TIME_SLEEP_RECORD_KEY;
    }

    // Busca el registro en la memoria flash
    err_code = fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

    if (err_code == NRF_SUCCESS)
    {
        // Si el registro existe, abre y lee el valor
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code == NRF_SUCCESS)
        {
            // Verifica que el tamaño del dato leído sea el esperado
            if (flash_record.p_header->length_words == 1)
            {
                // Copiar directamente el valor desde flash
                data      = (uint32_t *)flash_record.p_data;
                resultado = *data;
                const char* tipo_str = (valor_type == TIEMPO_ENCENDIDO) ? "encendido" :
                                      (valor_type == TIEMPO_ENCENDIDO_EXTENDED) ? "encendido extendido" :
                                      "sleep";
                NRF_LOG_RAW_INFO("\n\t>> Tiempo de %s cargado: %u ms", tipo_str, resultado);
            }
            else
            {
                NRF_LOG_RAW_INFO(
                    "\n\t>> Tamaño del dato en memoria flash no coincide con "
                    "el "
                    "esperado.\n");
            }

            err_code = fds_record_close(&record_desc);
            if (err_code != NRF_SUCCESS)
            {
                NRF_LOG_RAW_INFO("\n\t>> Error al cerrar el registro: 0x%X", err_code);
                return default_valor;
            }
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\t>> Error al abrir el registro: 0x%X", err_code);
        }
    }
    else
    {
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

    err_code = fds_record_find(DATE_AND_TIME_FILE_ID, DATE_AND_TIME_RECORD_KEY,
                               &record_desc, &ftok);

    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO(
            "\n\t>> Registro no encontrado (0x%X). Usando predeterminado.",
            err_code);
        return resultado;
    }

    err_code = fds_record_open(&record_desc, &flash_record);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\t>> Error al abrir registro: 0x%X", err_code);
        return resultado;
    }

    // Verificar tamaño en BYTES (no palabras)
    const uint32_t data_size_bytes =
        flash_record.p_header->length_words * sizeof(uint32_t);

    if (data_size_bytes >= len)
    {
        memcpy(&resultado, flash_record.p_data, len);
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> Dato corrupto: tamaño %u < %u", data_size_bytes,
                         len);
    }

    // Cerrar usando DESCRIPTOR (no flash_record)
    err_code = fds_record_close(&record_desc);
    if (err_code != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n\t>> Error al cerrar registro: 0x%X", err_code);
    }

    return resultado;
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS ENDS HERE.
//-------------------------------------------------------------------------------------------------------------

void write_time_to_flash(valor_type_t valor_type, uint32_t valor)
{
    uint16_t record_key;
    if (valor_type == TIEMPO_ENCENDIDO) {
        record_key = TIME_ON_RECORD_KEY;
    } else if (valor_type == TIEMPO_ENCENDIDO_EXTENDED) {
        record_key = TIME_ON_EXTENDED_RECORD_KEY;
    } else {
        record_key = TIME_SLEEP_RECORD_KEY;
    }
    
    fds_record_t      record     = {.file_id           = TIME_FILE_ID,
                                    .key               = record_key,
                                    .data.p_data       = &valor,
                                    .data.length_words = 1};
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    ret_code_t        err_code =
        fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

    const char* tipo_str = (valor_type == TIEMPO_ENCENDIDO) ? "encendido" :
                          (valor_type == TIEMPO_ENCENDIDO_EXTENDED) ? "encendido extendido" :
                          "sleep";

    if (err_code == NRF_SUCCESS)
    {
        err_code = fds_record_update(&record_desc, &record);
        NRF_LOG_RAW_INFO("\n> Tiempo de %s %s: %d segundos.",
                         tipo_str,
                         (err_code == NRF_SUCCESS) ? "actualizado"
                                                   : "falló al actualizar",
                         valor / 1000);
    }
    else if (err_code == FDS_ERR_NOT_FOUND)
    {
        err_code = fds_record_write(&record_desc, &record);
        NRF_LOG_RAW_INFO("\nTiempo de %s %s: %d segundos.\n",
                         tipo_str,
                         (err_code == NRF_SUCCESS) ? "guardado"
                                                   : "falló al guardar",
                         valor / 1000);
    }
    else
    {
        NRF_LOG_ERROR("Error al buscar el registro en memoria flash: %d", err_code);
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
                         .length_words = (sizeof(datetime_t) + 3) / sizeof(uint32_t)}};

    err_code = fds_record_find(DATE_AND_TIME_FILE_ID, DATE_AND_TIME_RECORD_KEY,
                               &record_desc, &ftok);

    if (err_code == NRF_SUCCESS)
    {
        err_code = fds_record_update(&record_desc, &record);
        if (err_code != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Error actualizando: 0x%X", err_code);
        }
    }
    else if (err_code == FDS_ERR_NOT_FOUND)
    {
        err_code = fds_record_write(NULL, &record);
        if (err_code == NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Error escribiendo: 0x%X", err_code);
        }
    }
    else
    {
        NRF_LOG_ERROR("Error buscando: 0x%X", err_code);
    }

    return err_code;
}

void load_mac_from_flash(uint8_t *mac_out)
{
    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok = {0};
    fds_flash_record_t flash_record;

    // Busca el registro en la memoria flash
    ret_code_t err_code;
    err_code = fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok);
    if (err_code == NRF_SUCCESS)
    {
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code == NRF_SUCCESS &&
            flash_record.p_header->length_words * 4 >= 6)
        {
            // Verifica que el tamaño del dato sea correcto
            if (flash_record.p_header->length_words != 2)
            {
                NRF_LOG_RAW_INFO(
                    "\n\t>> Tamaño de MAC en memoria flash no coincide con "
                    "el esperado.");
                fds_record_close(&record_desc);
                return;
            }

            // Copia la MAC al buffer de salida
            memcpy(mac_out, flash_record.p_data, sizeof(uint8_t) * 6);
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac_out[5], mac_out[4], mac_out[3], mac_out[2],
                             mac_out[1], mac_out[0]);
        }
        else
        {
            memcpy(mac_out, flash_record.p_data, sizeof(mac_out));
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac_out[5], mac_out[4], mac_out[3], mac_out[2],
                             mac_out[1], mac_out[0]);
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> No se encontro MAC. Usando valor "
                         "predeterminado.");
        // Si no se encuentra una MAC, usa una dirección predeterminada
        // mac_out[5] = 0x6A;
        // mac_out[4] = 0x0C;
        // mac_out[3] = 0x04;
        // mac_out[2] = 0xB3;
        // mac_out[1] = 0x72;
        // mac_out[0] = 0xE4;

        mac_out[0] = 0x10;
        mac_out[1] = 0x4A;
        mac_out[2] = 0x7C;
        mac_out[3] = 0xD9;
        mac_out[4] = 0x3E;
        mac_out[5] = 0xC7;

        // mac_out[0] = 0x08;
        // mac_out[1] = 0x63;
        // mac_out[2] = 0x50;
        // mac_out[3] = 0xD4;
        // mac_out[4] = 0x4C;
        // mac_out[5] = 0xD2;



        NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac_out[5], mac_out[4], mac_out[3], mac_out[2], mac_out[1],
                         mac_out[0]);
    }
}

void save_mac_to_flash(uint8_t *mac_addr)
{
    fds_record_t      record;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    uint32_t          aligned_data_buffer[2]; // 2 * 4 = 8 bytes
    ret_code_t        ret;
    memcpy(aligned_data_buffer, mac_addr, 6);

    // Configura el registro con la MAC
    record.file_id     = MAC_FILE_ID;
    record.key         = MAC_RECORD_KEY;
    record.data.p_data = aligned_data_buffer; // Apunta al buffer alineado

    record.data.length_words =
        (6 + sizeof(uint32_t) - 1) / sizeof(uint32_t); // (6 + 3) / 4 = 2

    // Realiza la recolección de basura si es necesario
    // perform_garbage_collection();

    ret = fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok);
    // Si ya existe un registro, actualízalo
    if (ret == NRF_SUCCESS)
    {
        if (fds_record_update(&record_desc, &record) == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n> Actualizando la MAC en memoria flash.");
            // NVIC_SystemReset();  // Reinicia el dispositivo
        }
        else
        {
            NRF_LOG_ERROR("Error al actualizar la MAC en memoria flash. Con registro "
                          "existente.");
        }
    }
    else
    {
        // Si no existe, crea un nuevo registro
        ret = fds_record_write(&record_desc, &record);

        if (ret == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nRegistro creado correctamente.");
        }
        else
        {
            NRF_LOG_ERROR("Error al crear el registro: %d", ret);
        }
    }
}


// Variables globales para el envío asíncrono de historial (similar a cmd15)
static bool history_send_active = false;
static uint32_t history_current_record = 0;
static uint32_t history_total_records = 0;
static uint32_t history_sent_count = 0;
static uint32_t history_failed_count = 0;

// Buffer para almacenar los record keys válidos encontrados por fds_record_iterate
#define MAX_HISTORY_RECORDS 248
static uint16_t history_valid_keys[MAX_HISTORY_RECORDS];
static uint16_t history_valid_count = 0;

/**
 * @brief Lee un registro de historial por record key
 * @param record_key Key del registro a leer
 * @param p_history_data Puntero donde almacenar los datos leídos
 * @return ret_code_t Código de retorno
 */
static ret_code_t read_history_record_by_key(uint16_t record_key, store_history *p_history_data)
{
    fds_record_desc_t desc = {0};
    fds_find_token_t token = {0};

    NRF_LOG_DEBUG("Leyendo registro con RECORD_KEY: 0x%04X", record_key);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) == NRF_SUCCESS)
    {
        fds_flash_record_t flash_record = {0};
        ret_code_t ret = fds_record_open(&desc, &flash_record);
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_ERROR("Error al abrir el registro");
            return ret;
        }

        // Verificar que el tamaño del registro sea correcto
        if (flash_record.p_header->length_words != BYTES_TO_WORDS(sizeof(store_history)))
        {
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



// Función auxiliar para enviar el siguiente paquete de historial (similar a cmd15_send_next_packet)
void history_send_next_packet(void)
{
    if (!history_send_active || history_current_record >= history_total_records) {
        return;
    }
    
    uint32_t packets_sent_this_round = 0;
    const uint32_t MAX_PACKETS_PER_ROUND = 5; // Enviar hasta 5 paquetes por vez
    
    while (history_send_active && 
           history_current_record < history_total_records && 
           packets_sent_this_round < MAX_PACKETS_PER_ROUND)
    {
        // Verificar que tengamos un índice válido
        if (history_current_record >= history_valid_count) {
            NRF_LOG_ERROR("Índice fuera de rango: %d >= %d", history_current_record, history_valid_count);
            history_send_active = false;
            break;
        }
        
        // Obtener el record key del registro actual
        uint16_t current_key = history_valid_keys[history_current_record];
        
        // Leer el registro actual directamente desde flash usando el record key
        store_history current_record;
        ret_code_t read_result = read_history_record_by_key(current_key, &current_record);
        
        if (read_result != NRF_SUCCESS) {
            // Si no se puede leer el registro, pasar al siguiente
            NRF_LOG_WARNING("No se pudo leer registro key 0x%04X: 0x%X", current_key, read_result);
            history_current_record++;
            history_failed_count++;
            continue;
        }
        
        // Estructurar los datos en array BLE
        static uint8_t data_array[244];
        uint16_t position = 0;

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
                NRF_LOG_RAW_INFO("\nHistorial: %d/%d enviados", history_sent_count, history_total_records);
            }
        }
        else if (ret == NRF_ERROR_RESOURCES || ret == NRF_ERROR_BUSY) {
            // Buffer lleno - esperar al próximo TX_RDY
            break;
        }
        else {
            // Error real - detener el envío
            history_failed_count++;
            NRF_LOG_RAW_INFO("\nError enviando registro %d: 0x%X - Deteniendo envío", history_current_record + 1, ret);
            history_send_active = false;
            break;
        }
    }
    
    // Verificar finalización
    if (history_send_active && history_current_record >= history_total_records) {
        history_send_active = false;
        NRF_LOG_RAW_INFO("\n=== ENVIO DE HISTORIAL COMPLETADO ===");
        NRF_LOG_RAW_INFO("\nRegistros enviados: %d/%d, Fallos: %d", 
                         history_sent_count, history_total_records, history_failed_count);
        if (history_total_records > 0) {
            NRF_LOG_RAW_INFO("\nTasa exito: %d%%\n", (history_sent_count * 100) / history_total_records);
        }
    }
}

ret_code_t send_all_history(void)
{
    ret_code_t    err_code;
    store_history history_record = {0};
    uint16_t      valid_records = 0;

    NRF_LOG_RAW_INFO("\n\n--- INICIANDO ENVIO DE HISTORIAL ASINCRONO ---");
    
    // Verificar si ya hay un envío activo
    if (history_send_active) {
        NRF_LOG_RAW_INFO("\nEnvio de historial ya esta activo - ignorando nueva solicitud");
        return NRF_ERROR_BUSY;
    }
    
    // Verificar estado inicial enviando un pequeño paquete de prueba
    uint8_t test_data[] = {0x08, 0x00}; // Magic + test byte
    err_code = app_nus_server_send_data(test_data, 2);
    if (err_code != NRF_SUCCESS) {
        NRF_LOG_RAW_INFO("\nError: No se puede enviar por BLE (0x%X). Verifica conexion.", err_code);
        if (err_code == NRF_ERROR_INVALID_STATE) {
            NRF_LOG_RAW_INFO("\n- Asegurate de que hay un dispositivo conectado");
            NRF_LOG_RAW_INFO("\n- Verifica que las notificaciones esten habilitadas");
        }
        return err_code;
    }
    nrf_delay_ms(100);

    NRF_LOG_RAW_INFO("\n--- FASE 1: Contando registros validos en flash ---");

    // FASE 1: Contar y almacenar los record keys de registros válidos usando fds_record_iterate
    fds_find_token_t token = {0};
    fds_record_desc_t record_desc = {0};
    fds_flash_record_t flash_record = {0};
    uint16_t expected_words = BYTES_TO_WORDS(sizeof(store_history));
    
    // Resetear contador y array de keys válidos
    history_valid_count = 0;

    // Iterar a través de todos los registros del HISTORY_FILE_ID
    while (fds_record_iterate(&record_desc, &token) == NRF_SUCCESS && 
           history_valid_count < MAX_HISTORY_RECORDS)
    {
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
        if (flash_record.p_header->length_words == expected_words) {
            // Almacenar el record key válido
            history_valid_keys[history_valid_count] = flash_record.p_header->record_key;
            history_valid_count++;
            valid_records++;
            
            if (valid_records % 10 == 0) {
                NRF_LOG_RAW_INFO("\nContados %d registros...", valid_records);
            }
        }

        // Cerrar el registro
        fds_record_close(&record_desc);
    }

    NRF_LOG_RAW_INFO("\n\n--- FASE 1 COMPLETADA: %d registros validos encontrados ---", history_valid_count);
    
    if (history_valid_count == 0) {
        NRF_LOG_RAW_INFO("\nNo hay registros para enviar");
        return NRF_SUCCESS;
    }

    // FASE 2: Inicializar variables para envío asíncrono (usando array de record keys)
    NRF_LOG_RAW_INFO("\n--- FASE 2: Iniciando envio asincrono (usando record keys) ---");
    
    history_send_active = true;
    history_current_record = 0;
    history_total_records = history_valid_count;
    history_sent_count = 0;
    history_failed_count = 0;
    
    NRF_LOG_RAW_INFO("\nEnviando %d registros de forma asincrona...", history_total_records);
    
    // Enviar el primer lote de paquetes - los siguientes se enviarán en BLE_NUS_EVT_TX_RDY
    history_send_next_packet();
    
    NRF_LOG_FLUSH();
    // Deleted delay function
    //nrf_delay_ms(1000);
    return NRF_SUCCESS;
}

// Funciones de estado para el envío de historial
bool history_send_is_active(void)
{
    return history_send_active;
}

uint32_t history_get_progress(void)
{
    if (history_total_records == 0) return 0;
    return (history_sent_count * 100) / history_total_records;
}


void delete_all_history(void)
{
    ret_code_t ret;

    ret = fds_file_delete(HISTORY_FILE_ID);

    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n[\033[1;31mERROR\033[0m] No se pudieron eliminar todos los historiales: %d", ret);
    }
    NRF_LOG_RAW_INFO("\n[INFO] Corriendo el recolector de basura...");

    ret = fds_gc();
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_RAW_INFO("\n[\033[1;31mERROR\033[0m] Error al correr el recolector de basura: %d", ret);
    }
}

ret_code_t delete_history_record_by_id(uint16_t record_id)
{
    ret_code_t        ret;
    fds_record_desc_t desc = {0};
    fds_find_token_t  token = {0};
    
    // Calcular la clave del registro usando el offset del ID
    uint16_t record_key = HISTORY_RECORD_KEY_START + record_id;
    
    NRF_LOG_RAW_INFO("\n\n\x1b[1;33m--- Eliminando registro de historial ID: %u ---\x1b[0m", record_id);
    NRF_LOG_RAW_INFO("\n> Buscando registro con KEY: 0x%04X", record_key);
    
    // Buscar el registro en la memoria flash
    ret = fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token);
    
    if (ret == NRF_SUCCESS)
    {
        // El registro existe, proceder a eliminarlo
        ret = fds_record_delete(&desc);
        
        if (ret == NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;32m>> Registro ID %u eliminado correctamente\x1b[0m", record_id);
            
            // Ejecutar recolección de basura para liberar espacio
            nrf_delay_ms(100);
            ret_code_t gc_ret = fds_gc();
            if (gc_ret == NRF_SUCCESS)
            {
                NRF_LOG_RAW_INFO("\n>> Recolección de basura completada");
            }
            else
            {
                NRF_LOG_RAW_INFO("\n>> Advertencia: Error en recolección de basura: 0x%X", gc_ret);
            }
        }
        else
        {
            NRF_LOG_RAW_INFO("\n\x1b[1;31m>> Error al eliminar el registro ID %u: 0x%X\x1b[0m", record_id, ret);
        }
    }
    else if (ret == FDS_ERR_NOT_FOUND)
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;33m>> Registro ID %u no encontrado, no se realizó ninguna acción\x1b[0m", record_id);
        // No es un error, simplemente el registro no existe
        ret = NRF_SUCCESS;
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\x1b[1;31m>> Error al buscar el registro ID %u: 0x%X\x1b[0m", record_id, ret);
    }
    
    return ret;
}