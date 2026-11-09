"""Genera Ps2CoopText.hpp: avisos del modo cooperativo como máscaras alfa de 8 bits.

El juego dibuja a 640x240 y la imagen se estira x2 en vertical al mostrarse, así que el texto se
genera aplastado a media altura para que en pantalla se vea con proporciones normales.
Uso: python gen_coop_text.py <ruta de salida .hpp>"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "C:/Windows/Fonts/trebucbd.ttf"
SIZE = 22
TEXTS = [
    ("kCoopTextJoin", "JUGADOR 2: PULSA START PARA UNIRTE"),
    ("kCoopTextJoined", "¡JUGADOR 2 SE HA UNIDO!"),
    ("kCoopTextHowToLeave", "MANTÉN START PARA SALIR"),
    ("kCoopTextLeft", "JUGADOR 2 HA SALIDO"),
]


def render(text):
    font = ImageFont.truetype(FONT, SIZE)
    left, top, right, bottom = font.getbbox(text)
    pad = 3
    w, h = right - left + pad * 2, bottom - top + pad * 2
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad - left, pad - top), text, font=font, fill=255)
    return img.resize((w, max(1, h // 2)), Image.LANCZOS)


def main(out_path):
    lines = [
        "#pragma once",
        "// Generado por scripts/gen_coop_text.py - no editar a mano.",
        "// Avisos del cooperativo de PS2 como máscaras alfa de 8 bits (aplastadas a media altura).",
        '#include "Types.hpp"',
        "",
        "struct Ps2CoopTextMask final",
        "{",
        "    s32 mWidth;",
        "    s32 mHeight;",
        "    const u8* mAlpha;",
        "};",
        "",
    ]
    for name, text in TEXTS:
        img = render(text)
        data = img.tobytes()
        lines.append(f"// \"{text}\"")
        lines.append(f"static const u8 {name}Data[{len(data)}] = {{")
        for i in range(0, len(data), 32):
            lines.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
        lines.append("};")
        lines.append(f"static const Ps2CoopTextMask {name} = {{{img.width}, {img.height}, {name}Data}};")
        lines.append("")
        print(f"{name}: {img.width}x{img.height}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1])
