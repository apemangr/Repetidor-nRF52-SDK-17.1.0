#include "filesystem.h"

// static bool write_time_to_flash(valor_type_t valor_type, uint32_t valor)
// {
// 	uint16_t record_key = (valor_type == TIEMPO_ENCENDIDO)
// 	                          ? TIME_ON_RECORD_KEY
// 	                          : TIME_SLEEP_RECORD_KEY;
// 	fds_record_t record = {.file_id = TIME_FILE_ID,
// 	                       .key = record_key,
// 	                       .data.p_data = &valor,
// 	                       .data.length_words = 1};
// 	fds_record_desc_t record_desc;
// 	fds_find_token_t ftok = {0};
// 	ret_code_t err_code =
// 	    fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);

// 	if (err_code == NRF_SUCCESS)
// 	{
// 		err_code = fds_record_update(&record_desc, &record);
// 		NRF_LOG_RAW_INFO(
// 		    "\nTiempo de %s %s: %d segundos.\n",
// 		    (valor_type == TIEMPO_ENCENDIDO) ? "encendido" : "sleep",
// 		    (err_code == NRF_SUCCESS) ? "actualizado" : "falló al actualizar",
// 		    valor / 1000);
// 	}
// 	else if (err_code == FDS_ERR_NOT_FOUND)
// 	{
// 		err_code = fds_record_write(&record_desc, &record);
// 		NRF_LOG_RAW_INFO(
// 		    "\nTiempo de %s %s: %d segundos.\n",
// 		    (valor_type == TIEMPO_ENCENDIDO) ? "encendido" : "sleep",
// 		    (err_code == NRF_SUCCESS) ? "guardado" : "falló al guardar",
// 		    valor / 1000);
// 	}
// 	else
// 	{
// 		NRF_LOG_ERROR("Error al buscar el registro en memoria flash: %d",
// 		              err_code);
// 	}
// 	return (err_code == NRF_SUCCESS);
// }

// /**
//  * @brief Guarda un valor en la memoria flash.
//  *
//  * @param valor_type Tipo de valor (TIEMPO_ENCENDIDO o TIEMPO_SLEEP).
//  * @param valor El valor numérico que se desea guardar.
//  * @return true si el valor se guardó correctamente, false en caso de error.
//  */
// bool save_time_to_flash(valor_type_t valor_type, uint32_t valor)
// {
//     fds_record_t record;
//     fds_record_desc_t record_desc;
//     fds_find_token_t ftok = {0}; // Importante inicializar a cero

//     // Determinar el Record Key según el tipo de valor
//     uint32_t record_key = (valor_type == TIEMPO_ENCENDIDO)
//                               ? TIME_ON_RECORD_KEY
//                               : TIME_SLEEP_RECORD_KEY;

//     // Configura el registro con los datos a escribir
//     record.file_id = TIME_FILE_ID;
//     record.key = record_key;
//     // CORRECCIÓN: Pasar la dirección de la variable 'valor'
//     record.data.p_data = &valor;
//     // Correcto para un uint32_t
//     record.data.length_words = sizeof(valor) / sizeof(uint32_t); // Esto es 1

//     // Busca si ya existe un registro con el mismo file_id y record_key
//     if (fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok) == NRF_SUCCESS)
//     {
//         // Si el registro ya existe, actualízalo
//         if (fds_record_update(&record_desc, &record) == NRF_SUCCESS)
//         {
//             NRF_LOG_RAW_INFO(
//                 "\n\nValor %s actualizado en memoria flash: %u",
//                 (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial",
//                 valor);
//             return true;
//         }
//         else
//         {
//             NRF_LOG_ERROR(
//                 "Error al actualizar el valor %s en memoria flash.",
//                 (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial");
//             return false;
//         }
//     }
//     else
//     {
//         // Si el registro no existe, crea uno nuevo
//         if (fds_record_write(&record_desc, &record) == NRF_SUCCESS)
//         {
//             NRF_LOG_RAW_INFO(
//                 "\n\nValor %s guardado en memoria flash: %u",
//                 (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial",
//                 valor);
//             return true;
//         }
//         else
//         {
//             NRF_LOG_ERROR(
//                 "Error al guardar el valor %s en memoria flash.",
//                 (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial");
//             return false;
//         }
//     }
// }

// /**
//  * @brief Lee un valor desde la memoria flash.
//  *
//  * @param valor_type Tipo de valor (TIEMPO_ENCENDIDO o TIEMPO_SLEEP).
//  * @param default_valor Valor predeterminado si no se encuentra el registro.
//  * @return Valor leído desde la memoria flash.
//  */
// uint32_t read_time_from_flash(valor_type_t valor_type, uint32_t default_valor)
// {
//     fds_record_desc_t record_desc;
//     fds_find_token_t ftok = {0}; // Importante inicializar a cero
//     fds_flash_record_t flash_record;
//     uint32_t resultado = default_valor;

//     // Determinar el Record Key según el tipo de valor
//     uint16_t record_key = (valor_type == TIEMPO_ENCENDIDO) ? TIME_ON_RECORD_KEY : TIME_SLEEP_RECORD_KEY;

//     // Busca el registro en la memoria flash
//     ret_code_t err_code = fds_record_find(TIME_FILE_ID, record_key, &record_desc, &ftok);
    
//     if (err_code == NRF_SUCCESS)
//     {
//         err_code = fds_record_open(&record_desc, &flash_record);
//         if (err_code == NRF_SUCCESS)
//         {
//             // Copiar directamente el valor desde flash
//             memcpy(&resultado, flash_record.p_data, sizeof(uint32_t));
            
//             // Cerrar el registro
//             fds_record_close(&record_desc);
            
//             NRF_LOG_RAW_INFO("\n\nValor %s leido desde memoria flash: %u",
//                         (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial", resultado);
//         }
//         else
//         {
//             NRF_LOG_ERROR("Error al abrir el registro del valor %s en memoria flash.",
//                          (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial");
//         }
//     }
//     else
//     {
//         NRF_LOG_WARNING("Registro del valor %s no encontrado en memoria flash. Usando valor predeterminado: %u.",
//                        (valor_type == TIEMPO_ENCENDIDO) ? "normal" : "especial", default_valor);
//     }

//     return resultado;
// }

// // /**
// //  * @brief Ejemplo de uso guardando el valor 666000
// //  */
// // void guardar_valor_ejemplo(void)
// // {
// //     // Guardar el valor 666000 como valor normal
// //     if (save_value_to_flash(TIEMPO_ENCENDIDO, 666000))
// //     {
// //         NRF_LOG_INFO("Se guardó correctamente el valor 666000");
// //     }

// //     // Leer el valor guardado
// //     uint32_t valor_leido = read_value_from_flash(TIEMPO_ENCENDIDO, 0);
// //     NRF_LOG_INFO("El valor leído es: %u", valor_leido);
// // }
