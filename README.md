# Firmaware para dispositivo BLE Repetidor

## Como usar
Extraer en `${NORDIC_SDK}/examples/ble_central_and_peripheral`.

## Soporta
- NRF52832
- NRF52840 (sin revisión)

## Comandos
Todos los comandos que vayan dirigidos al repetidor deben tener el prefijo `111`.

| Comando | Función                            | Descripción                                                                           | Ejemplo                                                  |
| :------ | :--------------------------------- | :------------------------------------------------------------------------------------ | :------------------------------------------------------- |
| 01      | Guardar MAC                        | Escribe en la memoria la MAC a conectarse                                             | 11101A566CE57FF66                                        |
| 02      | Leer MAC                           | Lee la MAC guardada en la memoria flash                                               | 11102                                                    |
| 03      | Reiniciar                          | Reinicia el repetidor                                                                 | 11103                                                    |
| 04      | Guardar tiempo encendido           | Escribe en la memoria flash el tiempo de encendido en segundos (máximo 666 segundos)  | 11104666                                                 |
| 05      | Leer tiempo encendido              | Lee el tiempo en que debe mantenerse despierto el dispositivo                         | 11105                                                    |
| 06      | Grabar tiempo dormido              | Escribe en la memoria flash el tiempo de dormido en segundos (máximo 6666 segundos)   | 111066666                                                |
| 07      | Leer tiempo dormido                | Lee el tiempo en que debe mantenerse dormido el dispositivo                           | 11107                                                    |
| 08      | Guardar fecha y hora               | Escribe en la memoria flash la fecha (AAAAMMDD) y hora (HHMMSS), formato ISO8601      | 1110820250530130200 <br> (30 de mayo del 2025, 13:02:00) |
| 09      | Leer fecha y hora                  | Lee en la memoria flash la fecha y hora                                               | 11109                                                    |
| 10      | Guardar tiempo encendido extendido | Guarda el tiempo que pasa extra cuando se conecta el celular                          | 1111065                                                  |
| 11      | Leer el tiempo encendido extendido | Lee el tiempo en que se extiendo el modo de encendido cuando se conecta el celular    | 11111                                                    |
| 12      | Lee un historial por ID            | Lee un registro del repetidor segun el ID indicado                                    | 111125 <br> (Lee el historial 5)                         |
| 13      | Borra un historial por ID          | Borra un registro del repetidor segun el ID indicado                                  | 111136 <br> (Borra el historial 6)                       |
| 14      | Solicita todos los historiales     | Manda por NUS todos los historiales que se tengan registrados en el repetidor         | 11114                                                    |
| 15      | Comienza prueba de señal           | Espera a recibir el primer advertising del objetivo para comenzar a registrar         | 11115                                                    |
| 16      | Detiene prueba de señal            | Verifica si se esta ejecutando la prueba para detenerla                               | 11116                                                    |
| 17      | Estado prueba de señal             | Comprueba si se esta ejecutando la prueba de señal y entrega la cantdidad de paquetes | 11117                                                    |
| 18      | Guarda MAC de escaneo              | Guarda en la memoria FLASH la MAC que debe ser escaneada                              | 11118A566CE57FF66                                        |
| 19      | Lee MAC de escaneo                 | Lee desde la memoria FLASH la MAC que debe ser escaneada                              | 11119                                                    |
| 20      | Guarda MAC del repetidor           | Guarda en la memoria FLASH la MAC custom del repetidor                                | 11120A566CE57FF66                                        |
| 21      | Lee MAC del repetidor              | Lee desde la memoria FLASH la MAC custom del repetidor                                | 11121                                                    |
| 22      | Enviar configuración repetidor     | Envía por NUS la configuración del repetidor como MAC's y tiempos de algunos timers   | 11122                                                    |
| 99      | Borra todos los historiales        | Limpia de la memoria flash todos los registros almacenados                            | 11199                                                    |
