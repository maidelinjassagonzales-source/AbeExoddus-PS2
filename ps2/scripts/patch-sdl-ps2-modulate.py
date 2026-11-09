"""Parche para SDL3 (render PS2): colores saturados/quemados.

El GS de la PS2 mezcla textura y color de vértice en modo MODULATE con 0x80 como "100 %":
salida = textura * color / 128. SDL pasaba 255 (1.0 * 255) en los vértices con textura, así que
toda la imagen salía casi al doble de brillo y con los colores saturados. Este parche escala esos
vértices a 128. Es idempotente. Uso: python patch-sdl-ps2-modulate.py <SDL_render_ps2.c>"""
import sys

path = sys.argv[1]
src = open(path, encoding="utf-8").read()

if "float_color_to_RGBAQ_tex" in src:
    print("ya parcheado")
    sys.exit(0)

helper = '''
// PS2 port patch: the GS MODULATE texture function treats 0x80 as 1.0, so textured vertices must
// use 128 for full intensity (255 nearly doubled the brightness and saturated every colour).
static gs_rgbaq float_color_to_RGBAQ_tex(const SDL_FColor *color, float color_scale)
{
    uint8_t colorR = (uint8_t)SDL_roundf(SDL_clamp(color->r * color_scale, 0.0f, 1.0f) * 128.0f);
    uint8_t colorG = (uint8_t)SDL_roundf(SDL_clamp(color->g * color_scale, 0.0f, 1.0f) * 128.0f);
    uint8_t colorB = (uint8_t)SDL_roundf(SDL_clamp(color->b * color_scale, 0.0f, 1.0f) * 128.0f);
    uint8_t colorA = (uint8_t)SDL_roundf(SDL_clamp(color->a, 0.0f, 1.0f) * 128.0f);

    return color_to_RGBAQ(colorR, colorG, colorB, colorA, 0x00);
}
'''

anchor = "static uint64_t float_GS_SETREG_RGBAQ("
assert anchor in src, "no encuentro float_GS_SETREG_RGBAQ"
src = src.replace(anchor, helper + "\n" + anchor, 1)

textured = """            vertices->rgbaq = float_color_to_RGBAQ(col_, color_scale);
            vertices->uv = vertex_to_UV("""
assert textured in src, "no encuentro el bucle de vértices con textura"
src = src.replace(textured, textured.replace("float_color_to_RGBAQ(", "float_color_to_RGBAQ_tex("), 1)

open(path, "w", encoding="utf-8", newline="\n").write(src)
print("parche aplicado")
