#!/usr/bin/env python3
"""bdf2mono.py -- convert a fixed-cell BDF font (the misc-fixed sizes) to a
MonoFont header (src/helpers/ui/MonoFont.h): every glyph as a whole cell, one
bit per codepoint for whether the font has it, and a rank table to find a
glyph's cell from that bit. Codepoints the BDF doesn't define are drawn blank.

Usage: bdf2mono.py <font.bdf> <first_hex> <last_hex> <VarPrefix> > out.h
"""
import sys
import unicodedata


def parse_bdf(path):
    """-> (glyphs {cp: (bbx, rows)}, ascent, descent, cell width)."""
    glyphs = {}
    asc = desc = cell_w = None
    with open(path, 'r', errors='replace') as f:
        cur = None
        bits = None
        for line in f:
            p = line.split()
            if not p:
                continue
            k = p[0]
            if bits is not None and k != 'ENDCHAR':
                bits.append(int(k, 16))
            elif k == 'FONT_ASCENT':
                asc = int(p[1])
            elif k == 'FONT_DESCENT':
                desc = int(p[1])
            elif k == 'FONTBOUNDINGBOX':
                cell_w = int(p[1])
            elif k == 'STARTCHAR':
                cur = {'cp': None, 'bbx': None}
            elif k == 'ENCODING':
                cur['cp'] = int(p[1])
            elif k == 'BBX':
                cur['bbx'] = tuple(int(x) for x in p[1:5])
            elif k == 'BITMAP':
                bits = []
            elif k == 'ENDCHAR':
                if cur['cp'] is not None and cur['cp'] >= 0 and cur['bbx']:
                    glyphs[cur['cp']] = (cur['bbx'], bits or [])
                cur = bits = None
    return glyphs, asc, desc, cell_w


def cell_of(bbx, rows, w, h, asc):
    """The glyph placed in its w x h cell: a list of h rows, bit (w-1-col) = column col."""
    gw, gh, xo, yo = bbx
    top = asc - (yo + gh)
    if xo < 0 or top < 0 or xo + gw > w or top + gh > h:
        raise ValueError(f'glyph box {bbx} is outside the {w}x{h} cell')
    rowbytes = (gw + 7) // 8
    cell = [0] * h
    for r in range(gh):
        v = rows[r] if r < len(rows) else 0
        for c in range(gw):
            if (v >> (rowbytes * 8 - 1 - c)) & 1:
                cell[top + r] |= 1 << (w - 1 - (xo + c))
    return cell


def emit(prefix, source, first, last, w, h, asc, cells):
    """cells: {cp: [h row ints]} for the codepoints the font draws."""
    n = last - first + 1
    words = [0] * ((n + 31) // 32)
    for cp in cells:
        if first <= cp <= last:
            i = cp - first
            words[i >> 5] |= 1 << (i & 31)
    rank, total = [], 0
    for wd in words:
        rank.append(total)
        total += bin(wd).count('1')
    o = [f'// {prefix}: misc-fixed {w}x{h}, converted from {source} by bdf2mono.py',
         f'// Range: U+{first:04X}-U+{last:04X} ({total} glyphs, the rest drawn blank), ascent {asc}',
         '#pragma once',
         '#include "MonoFont.h"',
         '',
         f'static const uint8_t {prefix}Bitmaps[] PROGMEM = {{']
    for cp in range(first, last + 1):
        if cp not in cells:
            continue
        acc = nb = 0
        out = []
        for row in cells[cp]:
            for c in range(w):
                acc = (acc << 1) | ((row >> (w - 1 - c)) & 1)
                nb += 1
                if nb == 8:
                    out.append(acc)
                    acc = nb = 0
        if nb:
            out.append(acc << (8 - nb))
        # The glyph itself as a reminder, but never a backslash: it would carry the comment onto the next line.
        ch = chr(cp) if unicodedata.category(chr(cp))[0] in 'LNPS' and cp != 0x5C else ''
        o.append('  ' + ', '.join(f'0x{b:02X}' for b in out) + f',  // U+{cp:04X} {ch}'.rstrip())
    o += ['};', '',
          f'static const uint32_t {prefix}Defined[] PROGMEM = {{']
    for i in range(0, len(words), 6):
        o.append('  ' + ', '.join(f'0x{x:08X}' for x in words[i:i + 6]) + ',')
    o += ['};', '',
          f'static const uint16_t {prefix}Rank[] PROGMEM = {{']
    for i in range(0, len(rank), 12):
        o.append('  ' + ', '.join(str(x) for x in rank[i:i + 12]) + ',')
    o += ['};', '',
          f'static const MonoFont {prefix} = {{',
          f'  {prefix}Bitmaps, {prefix}Defined, {prefix}Rank,',
          f'  0x{first:04X}, 0x{last:04X}, {w}, {h}, {asc}',
          '};']
    return '\n'.join(o) + '\n', total


def main():
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    bdf, first, last, prefix = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), sys.argv[4]
    glyphs, asc, desc, w = parse_bdf(bdf)
    h = asc + desc
    cells = {cp: cell_of(bbx, rows, w, h, asc)
             for cp, (bbx, rows) in glyphs.items() if first <= cp <= last and bbx[0] and bbx[1]}
    text, total = emit(prefix, bdf.split('/')[-1], first, last, w, h, asc, cells)
    sys.stdout.write(text)
    sys.stderr.write(f'{total} of {last - first + 1} codepoints, cell {w}x{h}, ascent {asc}\n')


if __name__ == '__main__':
    main()
