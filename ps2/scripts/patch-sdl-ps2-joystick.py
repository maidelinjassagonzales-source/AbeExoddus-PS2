# Corrige el driver de mandos PS2 de SDL3 (src/joystick/ps2/SDL_sysjoystick.c):
#  1) Los instance_id empiezan en 1 (SDL_PrivateJoystickAdded(enabled_pads) tras el ++), pero
#     Open/Update/Rumble/Close indexaban joyInfo[instance_id]: el mando 1 leía el puerto 2.
#  2) Sin multitap, padPortOpen también "abre" los slots 1-3 y salían 8 mandos fantasma.
import sys

path = sys.argv[1]
src = open(path, encoding="utf-8").read()
orig = src

src = src.replace("int index = joystick->instance_id;", "int index = joystick->instance_id - 1;")

old = "            struct JoyInfo *info = &joyInfo[enabled_pads];\n"
new = ("            if (slot > 0 && mtapGetConnection(port) != 1) {\n"
       "                continue; // no multitap on this port: only slot 0 exists\n"
       "            }\n"
       "            struct JoyInfo *info = &joyInfo[enabled_pads];\n")
if "mtapGetConnection(port) != 1" not in src:
    assert old in src, "joystick init loop not found"
    src = src.replace(old, new, 1)

# 3) Vibración: el juego pide vibrar desde el primer fotograma, cuando el mando aún no está listo.
#    padSetActAlign fallaba y rumble_ready se quedaba en -1 para siempre: nunca vibraba. Ahora solo se
#    prepara con el mando estable y, si falla, se reintenta más tarde (cada 30 llamadas).
old_ready = """    if (info->rumble_ready == 0) {
        actAlign[0] = 0;"""
new_ready = """    if (info->rumble_ready <= 0) {
        static uint8_t retry[MAX_CONTROLLERS];
        int state = padGetState(info->port, info->slot);
        if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
            return 0; // aún no: se vuelve a intentar en la próxima llamada
        }
        if (info->rumble_ready < 0 && ++retry[index] < 30) {
            return 0;
        }
        retry[index] = 0;
        if (padInfoAct(info->port, info->slot, -1, 0) <= 0) {
            info->rumble_ready = -1; // mando sin motores (o aún no lo dice): se reintenta
            return 0;
        }
        actAlign[0] = 0;"""
if "PAD_STATE_FINDCTP1) {\n            return 0; // aún no" not in src:
    assert old_ready in src, "rumble_status not found"
    src = src.replace(old_ready, new_ready, 1)
# El motor pequeño del DualShock solo es encendido/apagado (1/0); el grande va de 0 a 255.
old_small = "    actAlign[0] = low_frequency_rumble >> 8;  // Enable small engine"
new_small = "    actAlign[0] = low_frequency_rumble >= 0x4000 ? 1 : 0; // motor pequeño: encendido o apagado"
if old_small in src:
    src = src.replace(old_small, new_small, 1)

assert "instance_id - 1" in src
if src != orig:
    open(path, "w", encoding="utf-8").write(src)
    print("SDL ps2 joystick: patched")
else:
    print("SDL ps2 joystick: already patched")
