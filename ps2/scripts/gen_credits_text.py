"""Genera Ps2CreditsText.hpp: los textos de la intro de créditos como máscaras alfa (8 bits)
listas para mezclar en la PS2. Uso: python gen_credits_text.py <ruta de salida .hpp>"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONTS = "C:/Windows/Fonts/"
TEXTS = [
    # (identificador, texto, fuente, tamaño)
    ("kTextPortFor", "CREADORES DEL PORT DE PLAYSTATION 2", "trebucbd.ttf", 22),
    ("kTextAroy", "Aroy24", "georgiab.ttf", 70),
    ("kTextAnd", "&", "georgiab.ttf", 40),
    ("kTextWilliam", "William_piraton", "georgiab.ttf", 62),
    ("kTextGame", "ODDWORLD: ABE'S EXODDUS", "trebucbd.ttf", 20),
]


def render(text, font_file, size):
    font = ImageFont.truetype(FONTS + font_file, size)
    left, top, right, bottom = font.getbbox(text)
    pad = 4
    w, h = right - left + pad * 2, bottom - top + pad * 2
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((pad - left, pad - top), text, font=font, fill=255)
    return img


def main(out_path):
    lines = [
        "#pragma once",
        "// Generado por scripts/gen_credits_text.py - no editar a mano.",
        "// Textos de la intro de créditos del port de PS2 como máscaras alfa de 8 bits.",
        '#include "Types.hpp"',
        "",
        "struct Ps2CreditsMask final",
        "{",
        "    s32 mWidth;",
        "    s32 mHeight;",
        "    const u8* mAlpha;",
        "};",
        "",
    ]
    for name, text, font_file, size in TEXTS:
        img = render(text, font_file, size)
        data = img.tobytes()
        lines.append(f"// \"{text}\" ({font_file}, {size}px)")
        lines.append(f"static const u8 {name}Data[{len(data)}] = {{")
        for i in range(0, len(data), 32):
            lines.append("    " + ",".join(str(b) for b in data[i:i + 32]) + ",")
        lines.append("};")
        lines.append(f"static const Ps2CreditsMask {name} = {{{img.width}, {img.height}, {name}Data}};")
        lines.append("")
        print(f"{name}: {img.width}x{img.height}")
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1])
