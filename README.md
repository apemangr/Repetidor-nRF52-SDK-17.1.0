# Firmaware para dispositivo BLE Repetidor

## Como usar
Extraer en `examples/ble_central_and_peripheral`.

## Soporta
- NRF52832
- NRF52840 (sin revisión)



## Comandos repetidor

Todos los comandos que vayan dirigidos al repetidor deben tener el prefijo `111`.


#### 01 - Guardar MAC

El formato del comando debe ser:
`111` + `MAC a escribir`


>Ej: 11101A566CE57FF66



#### 02 - Leer MAC guardada

El formato del comando debe ser:
`111` + `02`


#### 03 - Reiniciar repetidor

El formato del comando debe ser:
`111` + `03`



#### 04 - Grabar tiempo de encendido

El formato del comando debe ser:
`111` + `04` + `Tiempo en segundos`

> [!NOTE]
> El valor máximo es de 666 segundos



#### 05 - Leer tiempo de encendido

El formato del comando debe ser:
`111` + `05`



#### 06 - Grabar tiempo de dormido

El formato del comando debe ser:
`111` + `06` + `Tiempo en segundos`
> [!NOTE]
> El valor máximo es de 6666 segundos



#### 07 - Leer tiempo de dormido

El formato del comando debe ser:
`111` + `07`


#### 08 - Escribir la fecha y hora del dispositivo [WIP]

El formato del comando debe ser:
`111` + `08` + `DDMMAAAA` + `HHMMSS`

**Fecha**<br>
D -> Día<br>
M -> Mes<br>
A -> Año<br>

**Hora**<br>
H -> Hora<br>
M -> Minutos<br>
S -> Segundos<br>

>Ej: 1110830052025130200

El ejemplo representa la fecha 30 de mayo del 2025 y la hora 13:02.