# Oddworld: Abe's Exoddus – Port para PlayStation 2

Port no oficial de **Oddworld: Abe's Exoddus** a PlayStation 2, basado en el proyecto de
descompilación [R.E.L.I.V.E.](https://github.com/AliveTeam/alive_reversing) (AliveTeam).

**Port creado por Aroy24 y William_piraton.**

> Este repositorio contiene **solo código**. No incluye ningún archivo del juego.
> Para jugar necesitas tu propia copia de Abe's Exoddus para PC (por ejemplo, la de Steam).

## Updates

### v1.1 (13-11-2026)

- **Varios idiomas**: el disco puede llevar los datos del juego en inglés, español, francés,
  alemán e italiano (`DATA/EN`, `DATA/ES`, `DATA/FR`, `DATA/DE`, `DATA/IT`). El juego elige solo el
  idioma configurado en la consola. Si el disco no lo lleva, usa el inglés, luego el español y, por
  último, lo que haya directamente en `DATA/`, así que las ISO antiguas siguen funcionando.
  Los archivos que son iguales en todos los idiomas (músicas, sonidos...) se guardan una sola vez
  en la ISO: con los 5 idiomas ocupa 2,75 GB (cabe en un DVD de una capa).
- **ISO de tamaño DVD arrancable**: en un DVD, la BIOS de la PS2 busca la tabla de rutas del
  ISO 9660 siempre en el sector 257 (como la deja el generador de discos de Sony) y no encontraba el
  ejecutable. `make-iso.sh` ahora la coloca ahí.
- **Cinemáticas sin cortes de audio**: el sonido de los vídeos se entrecortaba y se repetía cuando
  el disco tardaba en responder. Ahora un hilo aparte va leyendo el vídeo por delante, con un búfer
  de hasta 1,5 MB, y la reproducción ya no se queda sin datos.
- **Vibración del mando (DualShock 2)**: los temblores de pantalla del juego (explosiones, bombas,
  golpes...) ahora hacen vibrar el mando. Antes el driver de mandos de SDL intentaba activar los
  motores antes de que el mando estuviera listo y desactivaba la vibración para siempre.

## Novedades de la versión PS2

- **60 FPS**: interpolación de la imagen entre los ticks de lógica del juego.
- **Cooperativo para 2 jugadores con dos Abes**: el jugador 2 pulsa START en el mando 2 para unirse
  (en una esquina aparece "PULSA START PARA UNIRTE"). También puede poseer criaturas, y para salir
  mantiene START pulsado.
- **Intro de créditos del port** antes del logo de Oddworld Inhabitants.
- Colores corregidos para el GS de la PS2, mezclador de audio propio y partidas guardadas en la
  tarjeta de memoria (`mc0:/EXODDUS`).
- Funciona como un juego más en **OPL**: ISO instalada en el disco duro interno, en USB o por SMB.

## Estado

| Plataforma | Estado |
|---|---|
| PCSX2 | Todos los niveles y los 139 vídeos probados |
| PS2 real (OPL, HDD) | En pruebas: arranca y reproduce los vídeos; mandos y sonido corregidos, pendientes de confirmar |

Si en una PS2 real el juego falla o se queda colgado, aparece una pantalla de **informe de error**
con los últimos pasos del juego. Hazle una foto y ábrela como *issue*.

## Compilar

Hace falta el toolchain de [ps2dev](https://github.com/ps2dev/ps2dev) (probado en WSL Ubuntu 24.04)
y SDL3 compilado para PS2, con los parches de este repositorio aplicados. Todos los scripts están en
`ps2/scripts`:

1. `build-sdl3.sh`: compila SDL3 3.2.24 para PS2 con los parches de audio, color y mandos.
2. `build-ps2-usb.sh`: compila el ELF para consola real (`RELIVE_PS2_HOSTFS=OFF`).
   `build-ps2.sh` compila la versión de pruebas para PCSX2, que lee los archivos con `host:`.
3. `make-iso.sh`: genera la ISO `SLUS_999.98` con el ELF y los datos del juego (`*.LVL`, `*.DDV` y
   `SOUNDS.DAT`) en `DATA/`. Para una ISO con varios idiomas, pon cada versión del juego en una
   carpeta `EN`, `ES`, `FR`, `DE` o `IT` dentro de `$LANGS` (en Steam: Propiedades → Idioma, y
   copia los archivos tras cada descarga).
4. `instalar-en-hdd.ps1`: instala la ISO en el disco duro de la PS2 con hdl_dump.
   `crear-portada-opl.ps1` y `copiar-portada-hdd.sh` generan la portada y la ficha de OPL y las copian.

Las rutas de los scripts apuntan a la máquina donde se desarrolló el port. Cámbialas por las tuyas.

## Licencia

El código de R.E.L.I.V.E. está sujeto a la licencia de su proyecto original. Oddworld y Abe's Exoddus
son marcas de Oddworld Inhabitants. Este proyecto no está afiliado a Oddworld Inhabitants.
