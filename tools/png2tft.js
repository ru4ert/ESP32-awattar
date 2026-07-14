// PNG (RGBA) → TFT_eSPI uint16_t RGB565-Array (für include/img_emoji.h).
// Alpha wird gegen eine feste Ziel-Hintergrundfarbe geblendet (Badge-BG),
// danach wird die Panel-Kompensation angewendet: sent = ~swapRB(target).
//
// Verwendung (siehe tools/README.md):
//   npm install pngjs
//   node tools/png2tft.js battery24.png IMG_BATTERY_24 101010 out.inc
const fs = require('fs');
const { PNG } = require('pngjs');

const [, , inFile, cName, bgHex, outFile] = process.argv;
const png = PNG.sync.read(fs.readFileSync(inFile));
const { width: w, height: h, data } = png;

const bg = parseInt(bgHex, 16);
const bgR = (bg >> 16) & 0xff, bgG = (bg >> 8) & 0xff, bgB = bg & 0xff;

const swapRB = c => ((c & 0xF800) >> 11) | (c & 0x07E0) | ((c & 0x001F) << 11);
const panel  = c => (~swapRB(c)) & 0xFFFF;

let out = `// ${cName}: ${w}x${h} RGB565, Alpha vorgeblendet auf #${bgHex},\n` +
          `// Panel-kompensiert (siehe include/panel_color.h). Quelle: Noto Emoji (Apache-2.0).\n` +
          `const uint16_t ${cName}[${w * h}] = {\n    `;
for (let i = 0; i < w * h; i++) {
  const a = data[i * 4 + 3] / 255;
  const r = Math.round(data[i * 4]     * a + bgR * (1 - a));
  const g = Math.round(data[i * 4 + 1] * a + bgG * (1 - a));
  const b = Math.round(data[i * 4 + 2] * a + bgB * (1 - a));
  const c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  out += '0x' + panel(c565).toString(16).padStart(4, '0') + ',';
  out += (i + 1) % 12 === 0 ? '\n    ' : ' ';
}
out = out.trimEnd().replace(/,$/, '') + '\n};\n';
fs.writeFileSync(outFile, out);
console.log(`${cName}: ${w}x${h} ok`);
