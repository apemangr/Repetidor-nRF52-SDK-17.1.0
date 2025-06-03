#include "filesystem.h"

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
        NRF_LOG_RAW_INFO(
            "\n\t>> Registro no encontrado. Usando valor predeterminado: %u\n",
            default_valor);
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
        NRF_LOG_RAW_INFO("\t>> No se encontro MAC. Usando valor "
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