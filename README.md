# Firmaware para dispositivo BLE Repetidor

## Como usar
Extraer en `${NORDIC_SDK}/examples/ble_central_and_peripheral`.

## Soporta
- NRF52832
- NRF52840 (sin revisión)

## Comandos

Todos los comandos que vayan dirigidos al repetidor deben tener el prefijo `111`.

| Comando | Función                             | Descripción                                                                          | Ejemplo                                                  |
| :------ | :---------------------------------- | :----------------------------------------------------------------------------------- | :------------------------------------------------------- |
| 01      | Guardar MAC                         | Escribe en la memoria la MAC a conectarse                                            | 11101A566CE57FF66                                        |
| 02      | Leer MAC                            | Lee la MAC guardada en la memoria flash                                              | 11102                                                    |
| 03      | Reiniciar                           | Reinicia el repetidor                                                                | 11103                                                    |
| 04      | Guardar tiempo encendido            | Escribe en la memoria flash el tiempo de encendido en segundos (máximo 666 segundos) | 11104666                                                 |
| 05      | Leer tiempo encendido               | Lee el tiempo en que debe mantenerse despierto el dispositivo                        | 11105                                                    |
| 06      | Grabar tiempo dormido               | Escribe en la memoria flash el tiempo de dormido en segundos (máximo 6666 segundos)  | 111066666                                                |
| 07      | Leer tiempo dormido                 | Lee el tiempo en que debe mantenerse dormido el dispositivo                          | 11107                                                    |
| 08      | Guardar fecha y hora                | Escribe en la memoria flash la fecha (AAAAMMDD) y hora (HHMMSS), formato ISO8601     | 1110820250530130200 <br> (30 de mayo del 2025, 13:02:00) |
| 09      | Leer fecha y hora                   | Lee en la memoria flash la fecha y hora                                              | 11109                                                    |
| 10      | Solicitar el ultimo historial       | Lee del repetidor el último valor guardado en el historial                           | 11110                                                    |
| 11      | Solicitar un historial por ID (WIP) | Lee un registro del repetidor segun el ID indicado                                   | 111115 <br> (Solicita el 5 historial)                   |
| 12      | Borra un historial por ID (WIP)     | Elimina un registro del repetidor segun el ID indicado                               | 111125 <br> (Borra el 5 historial)                      |
| 666     | Borra todos los historiales (WIP)   | Limpia de la memoria flash todos los registros almacenados                           | 111666                                                   |


# Roadmap

- [ ] Sincronizar hora y fecha con el emisor al conectarse
