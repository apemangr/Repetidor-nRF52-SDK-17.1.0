#include "filesystem.h"

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS STARTS HERE.
//-------------------------------------------------------------------------------------------------------------

ret_code_t save_history_record(store_history const *p_history_data)
{
    ret_code_t        ret;
    fds_record_desc_t desc_history   = {0};
    fds_record_desc_t desc_counter   = {0};
    fds_find_token_t  token          = {0};

    uint32_t          history_count  = 0;
    bool              counter_exists = (fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY, &desc_counter, &token) == NRF_SUCCESS);

    if (counter_exists)
    {
        fds_flash_record_t flash_record;
        ret = fds_record_open(&desc_counter, &flash_record);
        if (ret != NRF_SUCCESS)
            return ret;

        // Verifica que el tamaño sea el de un uint32_t
        if (flash_record.p_header->length_words == (sizeof(uint32_t) + sizeof(uint32_t) - 1) / sizeof(uint32_t))
        {
            memcpy(&history_count, flash_record.p_data, sizeof(uint32_t));
        }
        else
        {
            NRF_LOG_ERROR("El registro del contador tiene un tamaño inesperado: %u palabras", flash_record.p_header->length_words);
            history_count = 0; // O maneja el error como prefieras
        }
        fds_record_close(&desc_counter);
        NRF_LOG_RAW_INFO("\nContador de historial encontrado: %lu\n", history_count);
    }

    // 2. Preparar nuevo registro histórico
    fds_record_t new_record = {
        .file_id           = HISTORY_FILE_ID,
        .key               = (uint16_t)(HISTORY_RECORD_KEY_START + history_count),
        .data.p_data       = p_history_data,
        .data.length_words = (sizeof(store_history) + sizeof(uint32_t) - 1) / sizeof(uint32_t)};

    // 3. Escribir registro histórico
    ret = fds_record_write(&desc_history, &new_record);
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error al escribir registro: %d", ret);
        return ret;
    }

    // 4. Actualizar contador
    history_count++;
    fds_record_t counter_record = {
        .file_id           = HISTORY_FILE_ID,
        .key               = HISTORY_COUNTER_RECORD_KEY,
        .data.p_data       = &history_count,
        .data.length_words = (sizeof(uint32_t) + sizeof(uint32_t) - 1) / sizeof(uint32_t)};

    if (counter_exists)
    {
        ret = fds_record_update(&desc_counter, &counter_record);
        NRF_LOG_RAW_INFO("\nActualizando contador de historial: %lu\n", history_count);
    }
    else
    {
        ret = fds_record_write(&desc_counter, &counter_record);
    }

    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Error al actualizar contador: %d", ret);
        return ret;
    }

    return NRF_SUCCESS;
}

ret_code_t read_history_record_by_id(uint32_t record_id, store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // La clave del registro se calcula sumando el ID a la clave base.
    uint16_t record_key = (uint16_t)(HISTORY_RECORD_KEY_START + record_id);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) == NRF_SUCCESS)
    {
        fds_flash_record_t flash_record = {0};
        ret_code_t         ret          = fds_record_open(&desc, &flash_record);
        if (ret != NRF_SUCCESS)
        {
            return ret;
        }

        // Verificar que el tamaño del registro en flash coincida con el del struct.
        if (flash_record.p_header->length_words != (sizeof(store_history) + sizeof(uint32_t) - 1) / sizeof(uint32_t))
        {
            fds_record_close(&desc);
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
    if (fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY, &desc, &token) == NRF_SUCCESS)
    {
        fds_flash_record_t flash_record  = {0};
        uint32_t           history_count = 0;

        fds_record_open(&desc, &flash_record);
        memcpy(&history_count, flash_record.p_data, sizeof(uint32_t));
        fds_record_close(&desc);

        if (history_count == 0)
        {
            return NRF_ERROR_NOT_FOUND; // No hay registros guardados.
        }

        // 2. El ID del último registro es (contador - 1).
        //    Llamamos a la función que lee por ID para obtenerlo.
        uint32_t last_record_id = history_count - 1;
        return read_history_record_by_id(last_record_id, p_history_data);
    }

    // Si el contador no se encuentra, significa que no hay registros.
    return NRF_ERROR_NOT_FOUND;
}

void print_history_record(store_history const *p_record, const char *p_title)
{
    NRF_LOG_INFO("\n--- %s ---", p_title);
    NRF_LOG_INFO("Fecha: %d/%d/%d", p_record->day, p_record->month, p_record->year);
    NRF_LOG_INFO("Hora:  %02d:%02d:%02d", p_record->hour, p_record->minute, p_record->second);
    NRF_LOG_INFO("Contador: %lu", p_record->contador);

    // Split the Log in two, showing 4 and 4
    NRF_LOG_INFO("Voltajes: V1=%u, V2=%u, V3=%u, V4=%u", p_record->V1, p_record->V2, p_record->V3, p_record->V4);
    NRF_LOG_INFO("Voltajes: V5=%u, V6=%u, V7=%u, V8=%u", p_record->V5, p_record->V6, p_record->V7, p_record->V8);
    NRF_LOG_INFO("Temp: %u C, Bateria: %u %%", p_record->temp, p_record->battery);
    NRF_LOG_INFO("--------------------------");
}

//-------------------------------------------------------------------------------------------------------------
//                                      HISTORY FUNCTIONS ENDS HERE.
//-------------------------------------------------------------------------------------------------------------

void write_time_to_flash(valor_type_t valor_type, uint32_t valor)
{
    uint16_t          record_key = (valor_type == TIEMPO_ENCENDIDO)
                                       ? TIME_ON_RECORD_KEY
                                       : TIME_SLEEP_RECORD_KEY;
    fds_record_t      record     = {.file_id           = TIME_FILE_ID,
                                    .key               = record_key,
                                    .data.p_data       = &valor,
                                    .data.length_words = 1};
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    ret_code_t        err_code =
        fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

    if (err_code == NRF_SUCCESS)
    {
        err_code = fds_record_update(&record_desc, &record);
        NRF_LOG_RAW_INFO("\n> Tiempo de %s %s: %d segundos.",
                         (valor_type == TIEMPO_ENCENDIDO) ? "encendido" : "sleep",
                         (err_code == NRF_SUCCESS) ? "actualizado"
                                                   : "falló al actualizar",
                         valor / 1000);
    }
    else if (err_code == FDS_ERR_NOT_FOUND)
    {
        err_code = fds_record_write(&record_desc, &record);
        NRF_LOG_RAW_INFO("\nTiempo de %s %s: %d segundos.\n",
                         (valor_type == TIEMPO_ENCENDIDO) ? "encendido" : "sleep",
                         (err_code == NRF_SUCCESS) ? "guardado"
                                                   : "falló al guardar",
                         valor / 1000);
    }
    else
    {
        NRF_LOG_ERROR("Error al buscar el registro en memoria flash: %d", err_code);
    }
}

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
    record_key = (valor_type == TIEMPO_ENCENDIDO) ? TIME_ON_RECORD_KEY
                                                  : TIME_SLEEP_RECORD_KEY;

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
                NRF_LOG_RAW_INFO("\n\t>> Tiempo de %s cargado: %u ms",
                                 (valor_type == TIEMPO_ENCENDIDO) ? "encendido"
                                                                  : "sleep",
                                 resultado);
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

void load_mac_from_flash(void)
{
    fds_record_desc_t  record_desc;
    fds_find_token_t   ftok = {0};
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
            NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac_address_from_flash[0], mac_address_from_flash[1],
                             mac_address_from_flash[2], mac_address_from_flash[3],
                             mac_address_from_flash[4], mac_address_from_flash[5]);
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> No se encontro MAC. Usando valor "
                         "predeterminado.");
        // Si no se encuentra una MAC, usa una dirección predeterminada
        mac_address_from_flash[0] = 0x63;
        mac_address_from_flash[1] = 0x98;
        mac_address_from_flash[2] = 0x41;
        mac_address_from_flash[3] = 0xD3;
        mac_address_from_flash[4] = 0x03;
        mac_address_from_flash[5] = 0xFB;

        NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac_address_from_flash[0], mac_address_from_flash[1],
                         mac_address_from_flash[2], mac_address_from_flash[3],
                         mac_address_from_flash[4], mac_address_from_flash[5]);
        nrf_delay_ms(10);
    }
}

void save_mac_to_flash(uint8_t *mac_addr)
{
    fds_record_t      record;
    fds_record_desc_t record_desc;
    fds_find_token_t  ftok = {0};
    uint32_t          aligned_data_buffer[2]; // 2 * 4 = 8 bytes

    memcpy(aligned_data_buffer, mac_addr, 6);

    // Configura el registro con la MAC
    record.file_id     = MAC_FILE_ID;
    record.key         = MAC_RECORD_KEY;
    record.data.p_data = aligned_data_buffer; // Apunta al buffer alineado

    record.data.length_words =
        (6 + sizeof(uint32_t) - 1) / sizeof(uint32_t); // (6 + 3) / 4 = 2

    // Realiza la recolección de basura si es necesario
    // perform_garbage_collection();

    // Si ya existe un registro, actualízalo
    if (fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok) ==
        NRF_SUCCESS)
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
        ret_code_t ret = fds_record_write(&record_desc, &record);

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