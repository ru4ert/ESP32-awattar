// Parst lv_font_conv-Output (--format lvgl, --bpp 8, --no-compress) und packt
// ihn in das projekteigene DEFont-Format (include/font_de.h + src/font_de.cpp).
//
// Komplette Neugenerierung (siehe auch tools/README.md):
//   npx --yes lv_font_conv --font Montserrat-Medium.ttf \
//     -r 0x20-0x7E,0xB0,0xC4,0xD6,0xD8,0xDC,0xDF,0xE4,0xF6,0xFC,0x20AC \
//     --size 14 --bpp 8 --no-compress --no-prefilter --format lvgl \
//     -o de14.c --force-fast-kern-format          (analog --size 10 → de10.c)
//   node tools/pack_font.js de14.c de10.c include/font_de.h src/font_de.cpp
const fs = require('fs');

function parseFont(file) {
  const src = fs.readFileSync(file, 'utf8');

  const bmSec = src.match(/glyph_bitmap\[\] = \{([\s\S]*?)\n\};/)[1];
  const parts = bmSec.split(/\/\* U\+([0-9A-F]+) .*? \*\//).slice(1);
  const glyphsBm = [];
  for (let i = 0; i < parts.length; i += 2) {
    const cp = parseInt(parts[i], 16);
    const bytes = (parts[i + 1].match(/0x[0-9a-fA-F]{1,2}/g) || []).map(h => parseInt(h, 16));
    glyphsBm.push({ cp, bytes });
  }

  const dsc = [...src.matchAll(/\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g)]
    .map(m => ({ bi: +m[1], adv: +m[2], w: +m[3], h: +m[4], ox: +m[5], oy: +m[6] }));
  dsc.shift(); // id 0 reserviert

  if (dsc.length !== glyphsBm.length)
    throw new Error(`${file}: dsc=${dsc.length} != bitmaps=${glyphsBm.length}`);

  const lineHeight = +src.match(/\.line_height = (\d+)/)[1];
  const baseLine = +src.match(/\.base_line = (\d+)/)[1];

  const glyphs = [];
  for (let i = 0; i < dsc.length; i++) {
    const d = dsc[i], b = glyphsBm[i];
    if (b.bytes.length !== d.w * d.h)
      throw new Error(`${file}: U+${b.cp.toString(16)} bytes=${b.bytes.length} != ${d.w}x${d.h}`);
    // Achtung: ...d zuerst, sonst überschreibt d.adv (1/16-px-Rohwert) das
    // umgerechnete adv wieder! (Das war der "verstreute Buchstaben"-Bug.)
    glyphs.push({ ...d, cp: b.cp, adv: Math.round(d.adv / 16), bytes: b.bytes });
  }
  glyphs.sort((a, b) => a.cp - b.cp);
  return { glyphs, lineHeight, baseLine };
}

function emit(font, name) {
  let off = 0, bmHex = '', dscLines = '';
  for (const g of font.glyphs) {
    dscLines += `    {0x${g.cp.toString(16).padStart(4, '0')}, ${off}, ${g.w}, ${g.h}, ${g.ox}, ${g.oy}, ${g.adv}},\n`;
    for (const byte of g.bytes) {
      bmHex += '0x' + byte.toString(16).padStart(2, '0') + ',';
      if ((off + 1) % 20 === 0) bmHex += '\n    ';
      else bmHex += ' ';
      off++;
    }
  }
  return `
static const uint8_t ${name}_BM[] = {
    ${bmHex.trimEnd().replace(/,$/, '')}
};
static const DEGlyph ${name}_GL[] = {
${dscLines}};
const DEFont ${name} = { ${name}_GL, ${name}_BM, ${font.glyphs.length}, ${font.lineHeight}, ${font.baseLine} };
`;
}

const f14 = parseFont(process.argv[2]);
const f10 = parseFont(process.argv[3]);

fs.writeFileSync(process.argv[4], `#pragma once
#include <stdint.h>

// Eigenes, minimales Font-Format (aus Montserrat-Medium via lv_font_conv,
// bpp 8 = ein Alpha-Byte pro Pixel). Gerendert von drawDE() in main.cpp.
// Neu generieren: siehe tools/README.md

struct DEGlyph {
    uint16_t cp;    // Unicode-Codepoint
    uint16_t off;   // Offset ins Bitmap-Array
    uint8_t  w, h;  // Box-Größe in px
    int8_t   ox;    // X-Versatz ab Stiftposition
    int8_t   oy;    // Y-Versatz Box-UNTERKANTE relativ zur Baseline (+ = darüber)
    uint8_t  adv;   // Vorschub in px
};

struct DEFont {
    const DEGlyph *glyphs;
    const uint8_t *bitmap;
    uint16_t count;
    uint8_t  lineHeight;
    uint8_t  baseLine;
};

extern const DEFont FONT_DE14;
extern const DEFont FONT_DE10;
`);

fs.writeFileSync(process.argv[5],
  `// Generiert mit tools/pack_font.js aus Montserrat-Medium (OFL-Lizenz)
#include "font_de.h"
${emit(f14, 'FONT_DE14')}${emit(f10, 'FONT_DE10')}`);

const kb = n => (fs.statSync(n).size / 1024).toFixed(1) + 'KB';
console.log(`ok: DE14 ${f14.glyphs.length} Glyphen, DE10 ${f10.glyphs.length} Glyphen → ${kb(process.argv[5])}`);
