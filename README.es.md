# Falcon v4.0.0

[English](README.md) | [Français](README.fr.md) | **Español** | [Русский](README.ru.md)

Firmware para UV-K1 / K5(v3), derivado de [Sonic](https://github.com/Robby69400/Sonic) y compatible con [F4HWN](https://github.com/armel/uv-k1-k5v3-firmware-custom) (64 slots de apps).

- Pantallas claras, ayuda Keys Infos en F (#) y dos velocidades de escaneo: Normal para una detección fiable (~300 canales/s) y FastScan para barrer muchos canales rápidamente (~500 canales/s).
- Full Watch sigue los dos VFO y hasta cuatro canales prioritarios; Multilist Watch escanea tus listas de memorias en segundo plano.
- Escaneo directo desde el VFO: Long * escanea la lista del canal, F + * escanea todos los canales en FastScan, con A y B en modo frecuencia Long * escanea el rango entre ambos, y el atajo BAND SCAN escanea una banda guardada, manteniendo la pantalla del VFO.
- El Espectro y Sentinel escanean listas, bandas o rangos, con un Historial de hasta 200 detecciones y listas negras; el Espectro añade una cascada (tecla 8) y un zoom alrededor de la última recepción (tecla 4).
- SKIP MEM (tecla 7) oculta las frecuencias que ya están en memoria en el Espectro y Sentinel; SPECTRE VFO abre el Espectro alrededor de la frecuencia del VFO; el procesador duerme entre tareas para una mayor autonomía.
- Los canales, listas y bandas se pueden crear y editar directamente desde el teclado.
- Apps de Falcon (API de apps nivel 4, compatible con F4HWN v7):
  - **FKeys**: reasigna las teclas largas y F+ de la pantalla VFO. Elige un preset (FALCON, F4HWN o SONIC) o ajusta cada tecla tú mismo; los drivers CHIRP 4.0.0 editan la misma tabla de teclas.
  - **FMessage**: mensajes de texto de 64 caracteres en FSK entre radios.
    - Los mensajes se guardan en la radio y se abren desde el VFO (atajo MESSAGES) o desde la app.
    - Llamada con una melodía que indica quién llama, y muestra las radios que tienen la app abierta (melodía e indicativo ajustados en FMessageOption).
    - Cada radio que ha confirmado la recepción del último mensaje se marca con un +.
  - **POCSAG Scan**: mensajes de buscapersonas a 1200 / 512 baudios, hasta 512 caracteres, registro en el Historial.
  - **FCatch**: captura Close Call de señales VHF y UHF.
  - **FirstAid**, **FSurvival** y **FMorse**: para situaciones de emergencia.
  - **FGuide**: guía de teclas y atajos del firmware.
  - **FSystemInfo**: información del sistema.
  - **FSnake**: paredes abiertas, teclas 3/7 para ambos sentidos.
- Instalación en un solo archivo, multiboot con Sonic y F4HWN (apps compatibles) y dos ediciones: Falcon 4K (4096 canales) y Falcon 1K (1024 canales con sus propios ajustes).
- El firmware Falcon está programado con Claude Code.

**Guía de usuario (ES): [docs/Falcon_v4.0.0_User_Guide_ES.pdf](docs/Falcon_v4.0.0_User_Guide_ES.pdf)**. La guía de cada versión también se adjunta a las [Releases](../../releases).

**Compatibilidad (4.0.0):** la V4 cambia la organización de la memoria externa. En una misma radio, todos los Falcon instalados en multiboot deben pasar a la V4 a la vez; un Falcon 3.x en otro slot perdería sus módulos. Las apps POCSAG Scan, FCatch, FKeys y FMessage deben actualizarse con la V4. Los canales y ajustes se conservan; usa los drivers CHIRP 4.0.0.

## Capturas de pantalla

Pantallas de Falcon 4.0.0, en orden:

<table>
<tr><td align="center" width="33%"><img src="docs/screenshots/01-single-vfo-rx.png" alt="VFO simple, recepción"><br>1. VFO simple, recepción</td><td align="center" width="33%"><img src="docs/screenshots/02-dual-vfo-rx.png" alt="VFO doble, recepción"><br>2. VFO doble, recepción</td><td align="center" width="33%"><img src="docs/screenshots/03-dual-vfo-full-watch.png" alt="VFO doble, Full Watch"><br>3. VFO doble, Full Watch</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/04-menu-g.png" alt="Menú G"><br>4. Menú G</td><td align="center" width="33%"><img src="docs/screenshots/05-menu-all.png" alt="Menú All"><br>5. Menú All</td><td align="center" width="33%"><img src="docs/screenshots/06-spectre-list.png" alt="Espectro, lista"><br>6. Espectro, lista</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/07-sentinel-range.png" alt="Sentinel, rango"><br>7. Sentinel, rango</td><td align="center" width="33%"><img src="docs/screenshots/08-edit-channel-popup.png" alt="Edición de canal, ventana"><br>8. Edición de canal, ventana</td><td align="center" width="33%"><img src="docs/screenshots/09-edit-channel.png" alt="Edición de canal"><br>9. Edición de canal</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/10-scanlist.png" alt="Listas de escaneo"><br>10. Listas de escaneo</td><td align="center" width="33%"><img src="docs/screenshots/11-scanlist-popup.png" alt="Lista de escaneo, ventana"><br>11. Lista de escaneo, ventana</td><td align="center" width="33%"><img src="docs/screenshots/12-scanlist-edit.png" alt="Lista de escaneo, edición"><br>12. Lista de escaneo, edición</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/13-radio-fm.png" alt="Radio FM"><br>13. Radio FM</td><td align="center" width="33%"><img src="docs/screenshots/14-apps.png" alt="Apps"><br>14. Apps</td><td align="center" width="33%"><img src="docs/screenshots/15-fmessage.png" alt="FMessage"><br>15. FMessage</td></tr>
<tr><td align="center" width="33%"><img src="docs/screenshots/16-fmessage-inbox.png" alt="FMessage, bandeja de entrada"><br>16. FMessage, bandeja de entrada</td><td align="center" width="33%"><img src="docs/screenshots/17-vfo-keys.png" alt="FKeys, teclas VFO"><br>17. FKeys, teclas VFO</td></tr>
</table>

## Contenido

- `firmware/f4hwn.falcon-4k.v4.0.0.bin`: firmware e instalador automático todo en uno (4.0.0: SKIP MEM, SPECTRE VFO, reposo del procesador, API de apps nivel 4, página POCSAG, 30 listas; lista completa de cambios en la primera página de la guía). El nombre del archivo da «Falcon 4k» en la lista multiboot de UV Studio.
- `firmware/f4hwn.falcon-1k.v4.0.0.bin`: Falcon 1K, la misma versión con 1024 canales guardados en los ajustes propios de su slot (formato F4HWN). En multiboot con Sonic 4K, cada uno conserva sus ajustes y sus canales. Falcon 4K comparte sus 4096 canales con Sonic 4K.
- `Falcon apps/FGuide.app`: referencia de las teclas de Falcon en la radio (10 secciones, en inglés), versión 4.0.0 para Falcon 4.0.0.
- `Falcon apps/FSystemInfo.app`: información del sistema de Falcon (firmware, memoria, pila, batería, identidad).
- `Falcon apps/FCatch.app`: captura Close Call de emisores cercanos, con tono, nombre del canal de memoria, elección VHF / UHF / VHF+UHF, bloqueo de frecuencias (SK2) e historial guardado de 17 entradas (Falcon 3.1.1 o posterior).
- `Falcon apps/POCSAGScan.app`: POCSAG Scan 2.1F, mensajes de buscapersonas (1200 / 512 baudios) en la lista de memorias POCSAG, registro en el Historial (solo Falcon 4.0).
- `Falcon apps/FKeys.app`: FKeys 4.0.0, reasigna las teclas largas y F+ de la pantalla VFO (preajustes FALCON, F4HWN, SONIC o personalizado).
- `Falcon apps/FirstAid.app`: primeros auxilios (ERC 2025) y metrónomo de RCP.
- `Falcon apps/FSurvival.app`: consejos de supervivencia y baliza SOS con la linterna.
- `Falcon apps/FMorse.app`: mensajes en Morse, con la linterna o por radio (antes Morse).
- `Falcon apps/FSnake.app`: juego Snake (antes Snake F).
- `Falcon apps/Translations`: FirstAid y FSurvival en francés (FR), español (ES) y polaco (PL).
- `Falcon apps/FMessage.app` y `Falcon apps/FMessageOption.app`: mensajería entre radios (FMessage 3.0) y sus ajustes («FMessOption» en la lista de apps). Instalar ambas.
- `F4WHN apps`: las 46 apps oficiales de F4HWN disponibles (versión 6.3.0), opcionales.
- `chirp/falcon4k.chirp.v4.0.0.py`: driver CHIRP de Falcon 4K, con los cuatro canales prioritarios, SetRxG y los atajos REVERSE FQ, MESSAGES, FMESSAGE y BAND SCAN, las 30 listas, los menús SCAN y DISPLAY y la tabla de teclas de FKeys.
- `chirp/falcon1k.chirp.v4.0.0.py`: driver CHIRP de Falcon 1K (memorias 995 a 1024: las bandas). Solo acepta Falcon 1K 4.0; cada driver rechaza la otra edición.
- `sources/runtime`: código fuente completo del firmware, drivers y dependencias integradas.
- `sources/installer`: código fuente del instalador temporal (reutiliza los drivers de `sources/runtime`).
- `sources/fguide`, `sources/fsysteminfo`: código fuente de FGuide y FSystemInfo.
- `sources/fcatch`: código fuente de FCatch (captura y tonos derivados de FSearch / GOGUFW Search, Apache-2.0).
- `sources/firstaid`, `sources/fsurvival`, `sources/fmorse`, `sources/fsnake`: código fuente de FirstAid, FSurvival (con sus traducciones), FMorse y FSnake.
- `sources/fmessage`: código fuente y pruebas de FMessage y FMessOption.
- `sources/pocsagrx`: fuentes de POCSAG Scan (derivada de POCSAG RX de Armel F4HWN, Apache-2.0).
- `sources/fkeys`: fuentes de FKeys.
- `tools`, `validation/tests`, `Dockerfile`, `build-with-docker.sh`: compilación y pruebas.
- `LICENSE`, `NOTICE`: licencias y atribuciones. El código de terceros conserva sus avisos.

## Instalación

Usa una versión de UVStudio que admita los 64 slots de apps.

Para conservar el firmware principal: arranca ese firmware, abre Firmware Slots en UVStudio, escribe el BIN en un slot libre del 1 al 4 y reinicia manteniendo MENU para elegir ese slot.

Para sustituir el firmware principal: usa Firmware Flash / DFU con el mismo BIN.

En el primer arranque, deja que la pantalla de instalación «FALCON v4.0.0» y «Powering Up» termine su trabajo hasta el reinicio automático. No cortes la alimentación durante la instalación; si se interrumpe, se reanuda en el siguiente arranque, en el mismo slot. No hace falta pulsar «Instalar los assets».

Si la pantalla de instalación muestra `S 04 E 10`, la instalación se ha rechazado sin borrar nada: la zona de Falcon contiene apps escritas por otro firmware (slots de apps 69 a 98 de un futuro F4HWN). Pulsa PTT para restaurar el firmware principal.

Si Falcon muestra «MODULES MISSING / REINSTALL FALCON», sus módulos en la memoria externa se han borrado o sustituido: vuelve a escribir el mismo BIN.

Las apps (las de `Falcon apps` y `F4WHN apps`) se instalan por separado desde la pestaña Apps de UVStudio, a elección. Se abren desde el menú de apps (F+5) en Falcon.

## Organización de la memoria

- Flash interna: el núcleo del firmware.
- Memoria externa, por slot de firmware: los módulos de código (cargados bajo demanda en 4 KB de RAM, lectura DMA y comprobación por el CRC de hardware; un módulo puede escribir la flash o llamar a otro módulo, y se recarga antes de volver a él) y los recursos (fuentes, textos de los menús). Ocupan la zona de assets v7 del slot (0x1CA000 + slot × 16 KB) y un banco de 48 KB en la reserva de apps v7 (0x18C000 + slot × 48 KB, slots de apps 69 a 98), tras el registro POCSAG (0x182000, 32 KB) y 8 KB reservados para MESSAGES (0x18A000). Los 64 slots de apps, la configuración, la calibración y los canales nunca se tocan. Ver `sources/runtime/App/store_layout.h`.
- La pantalla solo recibe las líneas que cambian, con una imagen completa al menos una vez por segundo.

## Funcionamiento de esta versión

Full Watch (menú RX Mode «FULL WATCH») vigila los dos VFO y hasta cuatro canales prioritarios, con una franja de 100 ms por posición. Una posición se abandona antes (desde 50 ms) cuando dos medidas seguidas del nivel recibido están claramente por debajo del umbral de apertura del squelch (margen de 6 dB); cerca o por encima del umbral se mantienen los 100 ms completos. El Dual normal conserva sus 100 ms, con una constante distinta de la de Full Watch. F+1 a F+4 asignan o quitan la prioridad correspondiente. F+EXIT añade o quita el canal de la lista Monitor fuera de una entrada activa. El centro de la pantalla muestra «<< F WATCH >>» («<<F WATCH>>» cuando cuatro canales de 4 cifras lo exigen) y la barra superior muestra FW.

Multilist Watch conserva su funcionamiento anterior, con sus dos prioridades (sin burbujas de números de canal). Todos los atajos F+ funcionan en él como en el VFO (F+0, F+5, F+6 a F+9, F+SK1/SK2, F+EXIT…), y la vigilancia se reanuda cuando la pantalla principal está en reposo. Se conservan los registros de la radio, los umbrales de squelch y el motor de escaneo.

Después de introducir directamente un número de canal sin lista, las flechas recorren los canales en orden numérico desde esa posición, saltando las posiciones vacías.

Historial: la acción «HISTORY» (F1/F2 corto o largo, M largo) abre el historial desde el VFO, sin iniciar un escaneo; el título pasa a la barra de estado y la lista gana una línea. En el Espectro, Sentinel y desde el VFO, una entrada escuchada (flechas) usa su propia modulación y ancho de banda; pulsación larga en 1: la frecuencia de la entrada pasa al VFO principal (modo frecuencia) y la pantalla vuelve al VFO.

Mensajes: `*` cambia entre la página HISTORY y la página MESSAGES (el escaneo y la recepción se detienen en MESSAGES). Cada mensaje recibido por FMessage se guarda allí automáticamente (memoria externa 0x012000, dos sectores de 4 KB, zona libre en F4HWN); los 32 últimos se listan como «01-AA: INICIO», del más reciente al más antiguo, con un número de llegada de 01 a 32 (no cambia tras un borrado y vuelve a 01 tras CLEAR ALL) y una línea de puntos entre dos sesiones de FMessage. `M` abre el mensaje, `6` lo borra (DELETE?), `6` largo lo borra todo (CLEAR ALL?). La acción «MESSAGES» (F1/F2, M largo) abre el historial directamente en esta página. Pantalla, lectura y borrado están en el módulo externo «messages». El registro RX/TX ya no existe.

POCSAG: un segundo `*` abre la página POCSAG, que lee el registro escrito por POCSAG Scan (memoria externa 0x182000, 8 sectores, hasta 512 caracteres por mensaje, conservado tras apagar); mismas teclas que MESSAGES.

FMessage: la acción «FMESSAGE» (F1/F2, M largo) abre la app FMessage (atajo de app 15); doble pitido si no está instalada.

REVERSE: línea «REVERSE:» ON/OFF en el editor de canales, acción «REVERSE FQ», indicador «R» en lugar de +/-; como en F4HWN, el ajuste se guarda y también vale en modo frecuencia.

Audio en recepción alineado con F4HWN: valores originales del chip en FM, AM y USB, menú SetRxG (puerta de ruido RX, ON por defecto, forzada a ON con el compander RX; byte 0x00A0C0 como en F4HWN), compander corregido para el BK4829. Potencias de transmisión y nivel del tono de 1750 Hz como en F4HWN; el tono de 1750 Hz se oye en el altavoz.

Guardado del VFO: la frecuencia y el número de canal se guardan 3 s después del último cambio (menos desgaste de la memoria), y de inmediato antes de una transmisión, del menú, de un escaneo o de una app.

Atajos: una tecla mantenida (F1, F2, M largo) no provoca nada más después de su acción; el historial, el Espectro, Sentinel, los escaneos y FMessage abiertos por un atajo ignoran la tecla todavía pulsada hasta que se suelta.

VOX: tomado de F4HWN (menú VOX OFF/1 a 10, acción «VOX», icono en la barra de estado; mismos bytes de ajuste y umbrales de calibración que F4HWN).

Escáner: el Espectro, los escaneos del VFO (F+*, pulsación larga en *, ML Watch) y el historial abierto desde el Espectro o el VFO se activan con una subida fija de 5 (2,5 dB); su sensibilidad se ajusta con el squelch en dBm. Sentinel y su historial conservan su umbral dinámico DS (teclas * y F).

La app FSystemInfo muestra la pila libre mínima medida desde el arranque.

64 slots de apps. DTMF y Live Viewer no activados.

## Compilación

Con Docker:

```sh
bash build-with-docker.sh
```

En local: ARM GNU Toolchain 13.2.Rel1 (`arm-none-eabi-gcc` en el PATH), CMake >= 3.22, Ninja y Python 3 con `zopfli==0.4.3`.

```sh
python3 -m pip install zopfli==0.4.3
bash tools/build-local.sh
```

Los BIN todo en uno (Falcon 1K y Falcon 4K) se generan en `firmware/`. La versión se lee en `sources/runtime/CMakePresets.json` (`VERSION_STRING_2`). `sources/runtime/build/S4K/onefile-measurements.json` (y `build/S1K` para la 1K) indica la ocupación de la flash interna y de la memoria externa.

El estilo del código se describe en `sources/.clang-format`.

Para recompilar las apps de Falcon:

```sh
bash sources/fguide/guide/build.sh
bash sources/fsysteminfo/sysinfo/build.sh
bash sources/fmessage/fmessage/build.sh
bash sources/fmessage/fmsgoption/build.sh
bash sources/fkeys/keys/build.sh
bash sources/pocsagrx/pocsagrx/make.sh
```

## Pruebas

Después de compilar, instala las dependencias y ejecuta:

```sh
python3 -m pip install -r tools/requirements-tests.txt
bash tools/test-local.sh
```

Las pruebas ARM ejecutan el firmware compilado con periféricos simulados; no sustituyen a una prueba en la radio. Las pruebas de FMessage y FMessOption se describen en `sources/fmessage/README.md`.

## Integridad

`SHA256SUMS` contiene las huellas de los archivos del archivo, salvo este mismo archivo.
