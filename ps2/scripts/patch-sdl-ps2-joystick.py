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

assert "instance_id - 1" in src
if src != orig:
    open(path, "w", encoding="utf-8").write(src)
    print("SDL ps2 joystick: patched")
else:
    print("SDL ps2 joystick: already patched")
