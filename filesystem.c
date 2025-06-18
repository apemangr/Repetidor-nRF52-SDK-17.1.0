#include "filesystem.h"

ret_code_t save_history_record_emisor(store_history const *p_history_data, uint16_t offset)
{
    ret_code_t        ret;
    fds_record_desc_t desc_history   = {0};
    fds_record_desc_t desc_counter   = {0};
    fds_find_token_t  token          = {0};

    // Preparar nuevo registro histórico
    uint16_t     record_key = HISTORY_RECORD_KEY_START + offset;
    fds_record_t new_record = {
        .file_id           = HISTORY_FILE_ID,
        .key               = record_key,
        .data.p_data       = p_history_data,
        .data.length_words = (sizeof(store_history) + 3) / sizeof(uint32_t) // Cálculo correcto
    };

    // Buscar el registro del historial, si no existe lo escribe 
    ret = fds_record_find(HISTORY_FILE_ID, record_key, &desc_history, &token);
    if( ret == NRF_SUCCESS)
    {
        // Si el registro ya existe, lo actualiza
        ret = fds_record_update(&desc_history, &new_record);
        nrf_delay_ms(100); // Delay tras actualizar
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al actualizar el registro: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO("\nRegistro actualizado con KEY: 0x%04X", record_key);
    }
    else if (ret == FDS_ERR_NOT_FOUND)
    {
        // Si no existe, lo escribe
        ret = fds_record_write(&desc_history, &new_record);
        nrf_delay_ms(100); // Delay tras escribir
        if (ret != NRF_SUCCESS)
        {
            NRF_LOG_RAW_INFO("\nError al escribir el registro: %d", ret);
            return ret;
        }
        NRF_LOG_RAW_INFO("\nRegistro escrito con KEY: 0x%04X", record_key);
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
    ret = fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY, &desc_counter, &token);

    nrf_delay_ms(500);
    fds_record_t counter_record = {
        .file_id           = HISTORY_FILE_ID,
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
            NRF_LOG_RAW_INFO("\nContador actualizado correctamente a: %lu", new_count);
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
    fds_record_desc_t desc_history   = {0};
    fds_record_desc_t desc_counter   = {0};
    fds_find_token_t  token          = {0};
    uint32_t          history_count  = 0; // Inicializado a 0

    bool              counter_exists = (fds_record_find(HISTORY_FILE_ID, HISTORY_COUNTER_RECORD_KEY, &desc_counter, &token) == NRF_SUCCESS);

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

    // Preparar nuevo registro histórico
    uint16_t     record_key = HISTORY_RECORD_KEY_START + history_count;
    fds_record_t new_record = {
        .file_id           = HISTORY_FILE_ID,
        .key               = record_key,
        .data.p_data       = p_history_data,
        .data.length_words = (sizeof(store_history) + 3) / sizeof(uint32_t) // Cálculo correcto
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

ret_code_t read_history_record_by_id(uint16_t record_id, store_history *p_history_data)
{
    fds_record_desc_t desc  = {0};
    fds_find_token_t  token = {0};

    // La clave del registro se calcula sumando el ID a la clave base.
    uint16_t record_key = (uint16_t)(HISTORY_RECORD_KEY_START + record_id);

    NRF_LOG_RAW_INFO("\nTratando de leer registro de historial con ID: %u (RECORD_KEY: 0x%04X)",
                     record_id, record_key);

    if (fds_record_find(HISTORY_FILE_ID, record_key, &desc, &token) == NRF_SUCCESS)
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
        if (flash_record.p_header->length_words != (sizeof(store_history) + sizeof(uint32_t) - 1) / sizeof(uint32_t))
        {
            NRF_LOG_RAW_INFO("\nTamano del registro en flash no coincide con el esperado.");
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
    if (fds_record_find(HISTORY_FILE_ID, (uint16_t)HISTORY_COUNTER_RECORD_KEY, &desc, &token) == NRF_SUCCESS)
    {
        fds_flash_record_t flash_record  = {0};
        uint16_t           history_count = 0; // <-- Ahora uint16_t

        fds_record_open(&desc, &flash_record);
        memcpy(&history_count, flash_record.p_data, sizeof(uint16_t)); // <-- Lee solo 2 bytes
        fds_record_close(&desc);

        if (history_count == 0)
        {
            return NRF_ERROR_NOT_FOUND; // No hay registros guardados.
        }

        // 2. El ID del último registro es (contador - 1).
        uint16_t last_record_id = history_count - 1;
        return read_history_record_by_id(last_record_id, p_history_data);
    }

    // Si el contador no se encuentra, significa que no hay registros.
    return NRF_ERROR_NOT_FOUND;
}

void print_history_record(store_history const *p_record, const char *p_title)
{
    NRF_LOG_RAW_INFO("\n\n--- %s ---", p_title);
    NRF_LOG_RAW_INFO("\nFecha: %d/%d/%d", p_record->day, p_record->month, p_record->year);
    NRF_LOG_RAW_INFO("\nHora:  %02d:%02d:%02d", p_record->hour, p_record->minute, p_record->second);
    NRF_LOG_RAW_INFO("\nContador: %lu", p_record->contador);
    NRF_LOG_RAW_INFO("\nVoltajes: V1=%u, V2=%u, V3=%u, V4=%u", p_record->V1, p_record->V2, p_record->V3, p_record->V4);
    NRF_LOG_RAW_INFO("\nVoltajes: V5=%u, V6=%u, V7=%u, V8=%u", p_record->V5, p_record->V6, p_record->V7, p_record->V8);
    NRF_LOG_RAW_INFO("\nTemp: %u C, Bateria: %u %%", p_record->temp, p_record->battery);
    NRF_LOG_RAW_INFO("\n--------------------------");
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
    ret_code_t err_code =
        fds_record_find(MAC_FILE_ID, MAC_RECORD_KEY, &record_desc, &ftok);
    if (err_code == NRF_SUCCESS)
    {
        err_code = fds_record_open(&record_desc, &flash_record);
        if (err_code == NRF_SUCCESS && flash_record.p_header->length_words * 4 >= 6)
        {
            // Verifica que el tamaño del dato sea correcto
            if (flash_record.p_header->length_words != 2)
            {
                NRF_LOG_RAW_INFO("\n\t>> Tamaño de MAC en memoria flash no coincide con "
                                 "el esperado.");
                fds_record_close(&record_desc);
                return;
            }

            // Copia la MAC al buffer de salida
            memcpy(mac_out, flash_record.p_data, sizeof(uint8_t) * 6);
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac_out[0], mac_out[1],
                             mac_out[2], mac_out[3],
                             mac_out[4], mac_out[5]);
        }
        else
        {
            memcpy(mac_out, flash_record.p_data,
                   sizeof(mac_out));
            fds_record_close(&record_desc);
            NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                             "%02X:%02X:%02X:%02X:%02X:%02X",
                             mac_out[0], mac_out[1],
                             mac_out[2], mac_out[3],
                             mac_out[4], mac_out[5]);
        }
    }
    else
    {
        NRF_LOG_RAW_INFO("\n\t>> No se encontro MAC. Usando valor "
                         "predeterminado.");
        // Si no se encuentra una MAC, usa una dirección predeterminada
        mac_out[0] = 0x63;
        mac_out[1] = 0x98;
        mac_out[2] = 0x41;
        mac_out[3] = 0xD3;
        mac_out[4] = 0x03;
        mac_out[5] = 0xFB;

        NRF_LOG_RAW_INFO("\n\t>> MAC cargada desde memoria: "
                         "%02X:%02X:%02X:%02X:%02X:%02X",
                         mac_out[0], mac_out[1],
                         mac_out[2], mac_out[3],
                         mac_out[4], mac_out[5]);
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
