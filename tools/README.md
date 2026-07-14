# Tools: Fonts & Emojis regenerieren

Beide Pipelines brauchen Node (npx) und laufen komplett offline nach dem
ersten Paket-Download.

## Fonts (include/font_de.h + src/font_de.cpp)

Quelle: Montserrat-Medium.ttf, z. B. aus dem LVGL-Repo:
`https://raw.githubusercontent.com/lvgl/lvgl/release/v8.3/scripts/built_in_font/Montserrat-Medium.ttf`

```bash
RANGE="0x20-0x7E,0xB0,0xC4,0xD6,0xD8,0xDC,0xDF,0xE4,0xF6,0xFC,0x20AC"
for S in 14 10; do
  npx --yes lv_font_conv --font Montserrat-Medium.ttf -r "$RANGE" \
    --size $S --bpp 8 --no-compress --no-prefilter --format lvgl \
    -o de$S.c --force-fast-kern-format
done
node tools/pack_font.js de14.c de10.c include/font_de.h src/font_de.cpp
```

Neue Zeichen → einfach die RANGE erweitern und neu generieren. Gerendert wird
von `drawDE()` in `src/main.cpp` (y = **Baseline**, Blending gegen die
übergebene Hintergrundfarbe, Panel-Kompensation pro Pixel).

## Emojis (include/img_emoji.h)

Quelle: Noto Emoji PNGs (Apache-2.0), 72 px, per Codepoint:
`https://raw.githubusercontent.com/googlefonts/noto-emoji/main/png/72/emoji_u1f50b.png` (🔋)
`https://raw.githubusercontent.com/googlefonts/noto-emoji/main/png/72/emoji_u1f6e2.png` (🛢)

```bash
sips -z 24 24 emoji_u1f50b.png --out battery24.png
npm install pngjs
node tools/png2tft.js battery24.png IMG_BATTERY_24 101010 battery.inc
# battery.inc + oil.inc → include/img_emoji.h zusammenfügen
# (#pragma once + #include <stdint.h> davor)
```

`101010` ist die Badge-Hintergrundfarbe (#101010 = Zielfarbe von C_HDR):
Alpha wird beim Konvertieren fest dagegen geblendet. Gezeichnet wird mit
`tft.pushImage(...)` — dafür ist in `setup()` `tft.setSwapBytes(true)` gesetzt.

**Wichtig:** Beide Generatoren geben PANEL-kompensierte bzw. Ziel-Werte richtig
aus — Details zum Panel-Quirk in `CLAUDE.md` und `include/panel_color.h`.
