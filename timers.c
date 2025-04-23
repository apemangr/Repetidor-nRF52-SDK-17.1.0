// timers.c
#include "timers.h"  // Incluye su propia cabecera

#include "app_error.h"  // Para APP_ERROR_CHECK
#include "app_timer.h"  // Necesario para todas las funciones de app_timer
#include "nrf_log.h"  // Para logging dentro de los handlers y la inicialización
#include "nrf_log_ctrl.h"  // Necesario para NRF_LOG_INFO, etc.
#include "variables.h"

// --- Definiciones de Timers (Internas a este módulo) ---
APP_TIMER_DEF(
    m_on_time_timer_id);  // Declara el timer para el tiempo de actividad
APP_TIMER_DEF(
    m_sleep_time_timer_id);  // Declara el timer para el tiempo de reposo


// --- Handlers de los Timers (Estáticos) ---
/**
 * @brief Handler llamado cuando el timer de actividad (ON) expira.
 */
static void on_timer_handler(void* p_context)
{
	NRF_LOG_INFO("Tiempo de encendido terminado. Se apagara por %d ms.",
	             DEVICE_SLEEP_TIME_MS);

	// Podrías añadir lógica específica de desactivación aquí

	// Iniciar el timer de reposo
	ret_code_t err_code =
	    app_timer_start(m_sleep_time_timer_id, SLEEP_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);
	// El sistema entrará en reposo vía nrf_pwr_mgmt_run() en main loop
}

/**
 * @brief Handler llamado cuando el timer de reposo (SLEEP) expira.
 */
static void sleep_timer_handler(void* p_context)
{
	NRF_LOG_INFO("Encendiendo dispositivo del reposo por %d ms.",
	             DEVICE_ON_TIME_MS);

	// Podrías añadir lógica específica de reactivación aquí

	// Iniciar el timer de actividad para reiniciar el ciclo
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, ON_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);
}

// --- Implementación de Funciones Públicas ---

/**
 * @brief Inicializa y crea los timers específicos de la aplicación (ON/SLEEP).
 */
ret_code_t timers_app_init(void)
{
	ret_code_t err_code;

	// Crear el timer para el tiempo de actividad (modo single shot)
	err_code = app_timer_create(&m_on_time_timer_id, APP_TIMER_MODE_SINGLE_SHOT,
	                            on_timer_handler);
	VERIFY_SUCCESS(
	    err_code);  // Usa VERIFY_SUCCESS para retornar el error si falla
	NRF_LOG_DEBUG("Timer de encendido creado.");

	// Crear el timer para el tiempo de reposo (modo single shot)
	err_code =
	    app_timer_create(&m_sleep_time_timer_id, APP_TIMER_MODE_SINGLE_SHOT,
	                     sleep_timer_handler);
	VERIFY_SUCCESS(err_code);
	NRF_LOG_DEBUG("Timer de reposo creado.");

	NRF_LOG_INFO("Timers creados correctamente.");
	return NRF_SUCCESS;
}

/**
 * @brief Inicia el ciclo de timers, comenzando con el timer de actividad (ON
 * timer).
 */
ret_code_t timers_start_cycle(void)
{
	NRF_LOG_INFO("Starting timer cycle (ON -> SLEEP).");
	ret_code_t err_code =
	    app_timer_start(m_on_time_timer_id, ON_DURATION_TICKS, NULL);
	APP_ERROR_CHECK(err_code);  // O podrías retornar err_code si prefieres
	                            // manejarlo en main
	return err_code;            // Retorna el resultado de app_timer_start
}