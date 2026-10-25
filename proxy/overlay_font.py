# proxy/overlay_font.h dosyasını yazar: oyun içi panelin (in-game panel) glifleri, kenarları yumuşatılmış (antialiased), panel ölçeği başına bir boyut (overlay.h scale_for).
#   python proxy/overlay_font.py <Inter-SemiBold.ttf>
# Yazı Tipi: Inter 4.1 SemiBold, şuradan indirilebilir extras/ttf/Inter-SemiBold.ttf of https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
# (SHA-256 78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3), SIL Open Font License 1.1 (THIRD-PARTY-NOTICES.md).
# Pillow'un FreeType'ı ile rasterleştirilmiştir, piksel başına 4 bit kapsama alanı (coverage). Bir geri sayımın her attığında
# kaymaması için rakamlar (digits) en geniş rakamın ilerleme değerini (advance) alır.
import hashlib, os, sys
from PIL import Image, ImageDraw, ImageFont

TEXTS = ['SCSKiller oncesi sonrasi 0123456789:', 'Bitti, oyunu kapatabilirsiniz', 'SHADER DERLEME TAKILMASI!']
SIZES = [12, 18, 27, 36]  # 1-4 ölçeklerinde (720p, 1080p, 1440p, 4K) em pikselleri
SHA256 = '78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3'


def main():
    path = sys.argv[1]
    if hashlib.sha256(open(path, 'rb').read()).hexdigest() != SHA256:
        sys.exit('dosya Inter 4.1 SemiBold degil')
    chars = sorted(set(''.join(TEXTS)) - {' '})
    data, sizes = [], []
    for px in SIZES:
        f = ImageFont.truetype(path, px, layout_engine=ImageFont.Layout.BASIC)
        boxes = {c: f.getbbox(c, anchor='ls') for c in chars}
        ascent = -min(b[1] for b in boxes.values())
        descent = max(b[3] for b in boxes.values())
        digit = max(round(f.getlength(d)) for d in '0123456789')
        glyphs = []
        for c in chars:
            l, t, r, b = boxes[c]
            w, h = r - l, b - t
            adv = round(f.getlength(c))
            x = l
            if c.isdigit():
                x, adv = l + (digit - adv) // 2, digit
            img = Image.new('L', (w, h))
            ImageDraw.Draw(img).text((-l, -t), c, font=f, fill=255, anchor='ls')
            px4 = [(v * 15 + 127) // 255 for v in img.tobytes()]
            at = len(data) * 2
            row = []
            for y in range(h):  # her satır bir bayt üzerinde başlar
                line = px4[y * w:(y + 1) * w] + ([0] if w % 2 else [])
                row += [line[i] | line[i + 1] << 4 for i in range(0, len(line), 2)]
            data += row
            glyphs.append((c, x, ascent + t, w, h, adv, at))
        space = round(f.getlength(' '))
        sizes.append((px, ascent + descent, ascent, space, glyphs))
    o = ['// Inter 4.1 SemiBold fontundan overlay_font.py tarafindan olusturulmustur (SIL Open Font License 1.1, THIRD-PARTY-NOTICES.md).',
         '#pragma once', '#include <cstdint>', '', 'namespace ovfont {', '',
         '// x: kalemden, y: satir ustunden; at: ilk piksel, 4 bit pikseller (once dusuk bit (low nibble)), herbiri (w + 1) / 2 bayt uzunlugunda satirlar.',
         'struct Glyph { char c; int8_t x, y; uint8_t w, h, adv; uint32_t at; };',
         'struct Size { int px, height, baseline, space; const Glyph* glyphs; int n; };', '',
         'inline constexpr uint8_t kPixels[%d] = {' % len(data)]
    for i in range(0, len(data), 32):
        o.append('    ' + ', '.join(str(v) for v in data[i:i + 32]) + ',')
    o.append('};')
    for px, _, _, _, glyphs in sizes:
        o.append('inline constexpr Glyph kGlyphs%d[] = {' % px)
        for c, x, y, w, h, adv, at in glyphs:
            o.append("    {'%s', %d, %d, %d, %d, %d, %d}," % (c, x, y, w, h, adv, at))
        o.append('};')
    o.append('inline constexpr Size kSizes[] = {')
    for px, height, base, space, glyphs in sizes:
        o.append('    {%d, %d, %d, %d, kGlyphs%d, %d},' % (px, height, base, space, px, len(glyphs)))
    o += ['};', '', '}  // namespace ovfont', '']
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'overlay_font.h')
    open(out, 'w', newline='\n').write('\n'.join(o))
    print('%s: %d glif (glyphs) x %d boyut (sizes), %d piksellik bayt' % (out, len(chars), len(SIZES), len(data)))


if __name__ == '__main__':
    main()
