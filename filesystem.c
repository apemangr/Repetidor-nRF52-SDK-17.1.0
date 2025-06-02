#include "filesystem.h"

ret_code_t write_date_to_flash(const char* fecha_str)
{
	ret_code_t err_code;
	fds_find_token_t ftok = {0};
	fds_record_desc_t record_desc;
	fds_record_t record = {.file_id = DATE_AND_TIME_FILE_ID,
						   .key = DATE_AND_TIME_RECORD_KEY,
						   .data.p_data = fecha_str,
						   .data.length_words = 1};

	err_code =
		fds_record_find(DATE_AND_TIME_FILE_ID, DATE_AND_TIME_RECORD_KEY, &record_desc, &ftok);

	if(err_code == NRF_SUCCESS)
	{
		err_code = fds_record_update(&record_desc, &record);
		NRF_LOG_RAW_INFO("\n> Fecha actualizada a: %s", fecha_str);
	}
	else if(err_code == FDS_ERR_NOT_FOUND)
	{
		err_code = fds_record_write(&record_desc, &record);
		NRF_LOG_RAW_INFO("\n> Fecha guardada con exito!");
	}
	else
	{
		NRF_LOG_RAW_INFO("Error al guardar la fecha en la memoria flash :(  ERROR:%d.", err_code);
	}
}

datetime_t read_date_from_flash()
{
	uint16_t len = sizeof(datetime_t);
	fds_flash_record_t flash_record;
	fds_record_desc_t record_desc;
	fds_find_token_t ftok = {0};
	const char* fecha_str;
	ret_code_t err_code;
	datetime_t resultado = {
		.year = 2025, .month = 5, .day = 30, .hour = 0, .minute = 0, .second = 0};

	err_code =
		fds_record_find(DATE_AND_TIME_FILE_ID, DATE_AND_TIME_RECORD_KEY, &record_desc, &ftok);

	// Encontro el registro
	if(err_code == NRF_SUCCESS)
	{
		err_code = fds_record_open(&record_desc, &flash_record);
		if(err_code == NRF_SUCCESS)
		{
			// Si tiene una longitud correcta copia el valor de la memoria
			// a la variable que sera devuelta
			if(flash_record.p_header->length_words == 1)
			{
				memcpy(&resultado, flash_record.p_data, len);
			}
			// No contiene nada el registro
			else
			{
				NRF_LOG_RAW_INFO("\n\t>> El tamano del dato es muy pequeno.\n");
			}
			err_code = fds_record_close(&flash_record);
			// No se pudo cerrar el registro
			if(err_code != NRF_SUCCESS)
			{
				NRF_LOG_RAW_INFO("\n\t>> Error al cerrar el registro: 0x%X", err_code);
				return resultado;
			}
		}
		else
		{
			NRF_LOG_RAW_INFO("\n\t>> Error al abrir el registro: 0x%X", err_code);
		}
	}
	else
	{
		NRF_LOG_RAW_INFO("\n\t>> Registro no encontrado. Usando valor predeterminado.\n");
	}
	return resultado;
}