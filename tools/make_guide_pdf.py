"""Render docs/guide/S3WIDE-GUIDE.md to docs/guide/S3WIDE-GUIDE.pdf.

Needs fpdf2 and Pillow (pip install fpdf2 pillow) and the Arial and
Consolas TrueType fonts (Windows' C:\\Windows\\Fonts, or FONT_DIR).

The Markdown subset the guide uses: # / ## / ### headings, paragraphs,
- bullets (two-space indent nests), 1. numbered items, ``` code blocks,
| tables |, ![caption](image), **bold** and `code` inline, and
<!-- pagebreak -->.
"""
import os
import re
import sys

from fpdf import FPDF
from fpdf.fonts import FontFace

HERE = os.path.dirname(os.path.abspath(__file__))
GUIDE = os.path.join(HERE, '..', 'docs', 'guide')
FONT_DIR = os.environ.get('FONT_DIR', r'C:\Windows\Fonts')

INK = (33, 37, 41)
MUTED = (95, 99, 104)
ACCENT = (0, 84, 147)
RULE = (206, 212, 218)
CODE_BG = (243, 245, 247)
HEAD_BG = (226, 234, 243)


class Guide(FPDF):
    title_text = ''

    def header(self):
        if self.page_no() <= 1:
            return
        self.set_font('body', '', 8)
        self.set_text_color(*MUTED)
        self.set_y(8)
        self.cell(0, 5, self.title_text, align='L')
        self.set_draw_color(*RULE)
        self.line(self.l_margin, 14, self.w - self.r_margin, 14)
        self.set_y(20)

    def footer(self):
        if self.page_no() <= 1:
            return
        self.set_y(-12)
        self.set_font('body', '', 8)
        self.set_text_color(*MUTED)
        self.cell(0, 5, str(self.page_no()), align='C')


def inline_runs(text):
    """Split text into (style, text) runs: '' plain, 'B' bold, 'C' code."""
    runs = []
    for part in re.split(r'(`[^`]+`|\*\*[^*]+\*\*)', text):
        if not part:
            continue
        if part.startswith('`'):
            runs.append(('C', part[1:-1]))
        elif part.startswith('**'):
            runs.append(('B', part[2:-2]))
        else:
            runs.append(('', part))
    return runs


def write_inline(pdf, text, size=10, height=5.2, color=INK):
    pdf.set_text_color(*color)
    for style, run in inline_runs(text):
        if style == 'C':
            pdf.set_font('mono', '', size - 0.8)
            pdf.set_text_color(150, 30, 60)
            pdf.write(height, run)
            pdf.set_text_color(*color)
        else:
            pdf.set_font('body', style, size)
            pdf.write(height, run)
    pdf.ln(height)


def plain(text):
    return re.sub(r'\*\*([^*]+)\*\*', r'\1', text).replace('`', '')


def render_toc(pdf, outline):
    pdf.set_x(pdf.l_margin)
    pdf.set_font('body', 'B', 16)
    pdf.set_text_color(*ACCENT)
    pdf.cell(0, 10, 'Contents', new_x='LMARGIN', new_y='NEXT')
    pdf.ln(2)
    for section in outline:
        if section.level > 1:
            continue
        indent = 0 if section.level == 0 else 8
        pdf.set_font('body', 'B' if section.level == 0 else '', 10.5 if section.level == 0 else 9.5)
        pdf.set_text_color(*INK)
        pdf.set_x(pdf.l_margin + indent)
        link = pdf.add_link(page=section.page_number)
        width = pdf.w - pdf.l_margin - pdf.r_margin - indent
        pdf.cell(width - 12, 6.2 if section.level == 0 else 5.4, section.name, link=link)
        pdf.cell(12, 6.2 if section.level == 0 else 5.4, str(section.page_number), align='R',
                 new_x='LMARGIN', new_y='NEXT', link=link)


def parse_table(lines):
    rows = []
    for line in lines:
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if all(re.fullmatch(r':?-{3,}:?', c) for c in cells):
            continue
        rows.append(cells)
    return rows


def draw_table(pdf, rows):
    width = pdf.w - pdf.l_margin - pdf.r_margin
    cols = len(rows[0])
    # Column widths from content length, within limits.
    lengths = []
    for i in range(cols):
        texts = [plain(r[i]) for r in rows if i < len(r)]
        longest_word = max(len(w) for t in texts for w in (t.split() or ['']))
        lengths.append(max(longest_word + 3, min(max(len(t) for t in texts), 50)))
    total = float(sum(lengths))
    widths = [width * n / total for n in lengths]
    pdf.set_draw_color(*RULE)
    pdf.set_line_width(0.2)
    with pdf.table(col_widths=widths, text_align='LEFT', line_height=4.6,
                   padding=1.4, borders_layout='HORIZONTAL_LINES',
                   headings_style=None, first_row_as_headings=False) as table:
        for i, cells in enumerate(rows):
            row = table.row()
            for c in range(cols):
                text = plain(cells[c]) if c < len(cells) else ''
                if i == 0:
                    row.cell(text, style=FontFace(emphasis='BOLD', fill_color=HEAD_BG))
                else:
                    row.cell(text)
    pdf.ln(3)


def main():
    src = os.path.join(GUIDE, 'S3WIDE-GUIDE.md')
    out = os.path.join(GUIDE, 'S3WIDE-GUIDE.pdf')
    lines = open(src, encoding='utf-8').read().split('\n')

    pdf = Guide(format='A4')
    pdf.set_margins(20, 20, 20)
    pdf.set_auto_page_break(True, 18)
    pdf.add_font('body', '', os.path.join(FONT_DIR, 'arial.ttf'))
    pdf.add_font('body', 'B', os.path.join(FONT_DIR, 'arialbd.ttf'))
    pdf.add_font('body', 'I', os.path.join(FONT_DIR, 'ariali.ttf'))
    pdf.add_font('mono', '', os.path.join(FONT_DIR, 'consola.ttf'))
    pdf.set_title('S3Wide User Guide')
    pdf.set_author('S3Wide')
    pdf.add_page()

    i = 0
    first_h1 = True
    title_page = True
    para = []

    def flush_para():
        if para:
            # A lead-in ("...:") stays with what follows it.
            if plain(para[-1]).endswith(':') and pdf.get_y() > pdf.h - 80:
                pdf.add_page()
            write_inline(pdf, ' '.join(para))
            pdf.ln(2.2)
            para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if stripped == '<!-- pagebreak -->':
            flush_para()
            if title_page:
                # Contents on its own pages after the title page.
                pdf.add_page()
                pdf.insert_toc_placeholder(render_toc, pages=1)
                title_page = False
            else:
                pdf.add_page()
            i += 1
            continue
        if stripped.startswith('```'):
            flush_para()
            code = []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith('```'):
                code.append(lines[i])
                i += 1
            i += 1
            pdf.set_font('mono', '', 8.4)
            pdf.set_fill_color(*CODE_BG)
            pdf.set_text_color(*INK)
            height = 4.1 * len(code) + 3
            if pdf.get_y() + height > pdf.h - pdf.b_margin and height < 120:
                pdf.add_page()
            pdf.ln(0.5)
            pdf.multi_cell(0, 4.1, '\n'.join(code), fill=True, padding=(1.5, 3, 1.5, 3),
                           new_x='LMARGIN', new_y='NEXT')
            pdf.ln(2.5)
            continue
        if stripped.startswith('|'):
            flush_para()
            block = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                block.append(lines[i])
                i += 1
            pdf.set_font('body', '', 8.6)
            pdf.set_text_color(*INK)
            draw_table(pdf, parse_table(block))
            continue
        m = re.match(r'!\[(.*)\]\((.*)\)', stripped)
        if m:
            flush_para()
            caption, path = m.groups()
            from PIL import Image
            img = Image.open(os.path.join(GUIDE, path))
            # Never enlarge small screenshots past about 120 dpi.
            width = min(pdf.w - pdf.l_margin - pdf.r_margin, img.width * 25.4 / 120)
            h = width * img.height / img.width
            if h > 110:
                h = 110
                width = h * img.width / img.height
            if pdf.get_y() + h + 12 > pdf.h - pdf.b_margin:
                pdf.add_page()
            x = (pdf.w - width) / 2
            pdf.image(os.path.join(GUIDE, path), x=x, w=width, h=h)
            pdf.set_draw_color(*RULE)
            pdf.rect(x, pdf.get_y() - h, width, h)
            pdf.ln(1.5)
            pdf.set_font('body', 'I', 8.4)
            pdf.set_text_color(*MUTED)
            pdf.multi_cell(0, 4.2, caption, align='C', new_x='LMARGIN', new_y='NEXT')
            pdf.ln(3)
            i += 1
            continue
        m = re.match(r'(#{1,3}) (.*)', line)
        if m:
            flush_para()
            level = len(m.group(1))
            text = m.group(2)
            if level == 1 and first_h1:
                first_h1 = False
                pdf.title_text = text
                pdf.set_y(85)
                pdf.set_font('body', 'B', 30)
                pdf.set_text_color(*ACCENT)
                pdf.cell(0, 14, text, new_x='LMARGIN', new_y='NEXT')
                pdf.set_draw_color(*ACCENT)
                pdf.set_line_width(0.8)
                pdf.line(pdf.l_margin, pdf.get_y() + 2, pdf.l_margin + 50, pdf.get_y() + 2)
                pdf.ln(8)
                # The next paragraphs are the subtitle and the version line.
                i += 1
                while i < len(lines) and lines[i].strip() != '<!-- pagebreak -->':
                    if lines[i].strip():
                        pdf.set_font('body', '', 13)
                        pdf.set_text_color(*INK)
                        pdf.multi_cell(0, 6.5, plain(lines[i].strip()), new_x='LMARGIN',
                                       new_y='NEXT')
                        pdf.ln(4)
                    i += 1
                continue
            if level == 2:
                if pdf.get_y() > pdf.h - 60:
                    pdf.add_page()
                pdf.start_section(text, level=0)
                pdf.ln(2)
                pdf.set_font('body', 'B', 17)
                pdf.set_text_color(*ACCENT)
                pdf.cell(0, 9, text, new_x='LMARGIN', new_y='NEXT')
                pdf.set_draw_color(*RULE)
                pdf.set_line_width(0.3)
                pdf.line(pdf.l_margin, pdf.get_y(), pdf.w - pdf.r_margin, pdf.get_y())
                pdf.ln(4)
            else:
                if pdf.get_y() > pdf.h - 60:
                    pdf.add_page()
                pdf.start_section(text, level=1)
                pdf.ln(1.5)
                pdf.set_font('body', 'B', 12.5)
                pdf.set_text_color(*INK)
                pdf.cell(0, 7, text, new_x='LMARGIN', new_y='NEXT')
                pdf.ln(1.2)
            i += 1
            continue
        m = re.match(r'(\s*)(- |\d+\. )(.*)', line)
        if m:
            flush_para()
            indent = len(m.group(1)) // 2
            marker = m.group(2).strip()
            text = m.group(3)
            i += 1
            # Continuation lines are indented deeper than the marker.
            while (i < len(lines) and lines[i].strip() and
                   re.match(r'\s{%d,}\S' % (len(m.group(1)) + 2), lines[i]) and
                   not re.match(r'\s*(- |\d+\. )', lines[i])):
                text += ' ' + lines[i].strip()
                i += 1
            left = pdf.l_margin + 4 + indent * 6
            pdf.set_x(left)
            pdf.set_font('body', 'B' if marker != '-' else '', 10)
            pdf.set_text_color(*ACCENT if marker != '-' else MUTED)
            pdf.cell(5, 5.2, marker if marker != '-' else '\u2022')
            old = pdf.l_margin
            pdf.set_left_margin(left + 5)
            write_inline(pdf, text)
            pdf.set_left_margin(old)
            pdf.ln(1.2)
            if i < len(lines) and not lines[i].strip():
                pdf.ln(0.8)
            continue
        if not stripped:
            flush_para()
            i += 1
            continue
        para.append(stripped)
        i += 1
    flush_para()
    pdf.output(out)
    print(out, pdf.page_no(), 'pages')


if __name__ == '__main__':
    sys.exit(main())
