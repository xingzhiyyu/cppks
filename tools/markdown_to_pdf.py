#!/usr/bin/env python3
from __future__ import annotations

import html
import re
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.platypus import (
    BaseDocTemplate,
    Frame,
    ListFlowable,
    ListItem,
    PageBreak,
    PageTemplate,
    Paragraph,
    Preformatted,
    Spacer,
    Table,
    TableStyle,
)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont


PAGE_BG = colors.HexColor("#f5f4ed")
IVORY = colors.HexColor("#faf9f5")
BRAND = colors.HexColor("#1B365D")
INK = colors.HexColor("#141413")
MUTED = colors.HexColor("#504e49")
BORDER = colors.HexColor("#e8e6dc")
CODE_BG = colors.HexColor("#f0eee6")


class NumberedCanvasMixin:
    pass


def register_fonts() -> None:
    font_dir = Path.home() / ".agents/skills/kami/assets/fonts"
    pdfmetrics.registerFont(TTFont("DocSerif", str(font_dir / "TsangerJinKai02-W04.ttf")))
    pdfmetrics.registerFont(TTFont("DocSerifBold", str(font_dir / "TsangerJinKai02-W05.ttf")))


class ProjectDocTemplate(BaseDocTemplate):
    def __init__(self, filename: str, title: str):
        super().__init__(
            filename,
            pagesize=A4,
            leftMargin=20 * mm,
            rightMargin=20 * mm,
            topMargin=18 * mm,
            bottomMargin=18 * mm,
            title=title,
            author="Codex",
            subject="Qt Widgets 课程设计答辩说明",
            keywords=["Qt", "C++", "售票系统", "课程设计", "答辩"],
        )
        frame = Frame(
            self.leftMargin,
            self.bottomMargin,
            self.width,
            self.height,
            id="normal",
        )
        template = PageTemplate(id="main", frames=[frame], onPage=self.draw_page)
        self.addPageTemplates([template])
        self.doc_title = title

    def draw_page(self, canvas, doc) -> None:
        canvas.saveState()
        canvas.setFillColor(PAGE_BG)
        canvas.rect(0, 0, A4[0], A4[1], fill=1, stroke=0)
        canvas.setFont("DocSerif", 8)
        canvas.setFillColor(colors.HexColor("#6b6a64"))
        if doc.page > 1:
            canvas.drawRightString(A4[0] - 20 * mm, A4[1] - 10 * mm, self.doc_title)
            canvas.drawCentredString(A4[0] / 2, 10 * mm, f"{doc.page}")
        canvas.restoreState()


def make_styles():
    base = getSampleStyleSheet()
    base.add(
        ParagraphStyle(
            name="CoverTitle",
            fontName="DocSerif",
            fontSize=28,
            leading=36,
            textColor=INK,
            alignment=TA_LEFT,
            spaceAfter=12,
        )
    )
    base.add(
        ParagraphStyle(
            name="CoverSub",
            fontName="DocSerif",
            fontSize=12,
            leading=20,
            textColor=MUTED,
            spaceAfter=8,
        )
    )
    base.add(
        ParagraphStyle(
            name="H1CN",
            fontName="DocSerif",
            fontSize=18,
            leading=24,
            textColor=INK,
            borderColor=BRAND,
            borderWidth=0,
            borderPadding=0,
            leftIndent=0,
            spaceBefore=12,
            spaceAfter=8,
            keepWithNext=True,
        )
    )
    base.add(
        ParagraphStyle(
            name="H2CN",
            fontName="DocSerif",
            fontSize=13.5,
            leading=19,
            textColor=INK,
            spaceBefore=10,
            spaceAfter=6,
            keepWithNext=True,
        )
    )
    base.add(
        ParagraphStyle(
            name="H3CN",
            fontName="DocSerif",
            fontSize=11.5,
            leading=17,
            textColor=colors.HexColor("#3d3d3a"),
            spaceBefore=8,
            spaceAfter=4,
            keepWithNext=True,
        )
    )
    base.add(
        ParagraphStyle(
            name="BodyCN",
            fontName="DocSerif",
            fontSize=9.6,
            leading=15.2,
            textColor=INK,
            spaceAfter=5,
        )
    )
    base.add(
        ParagraphStyle(
            name="BulletCN",
            fontName="DocSerif",
            fontSize=9.3,
            leading=14.3,
            textColor=INK,
            leftIndent=8,
            firstLineIndent=0,
            spaceAfter=2,
        )
    )
    base.add(
        ParagraphStyle(
            name="CodeCN",
            fontName="DocSerif",
            fontSize=7.6,
            leading=10.5,
            textColor=colors.HexColor("#222222"),
            backColor=CODE_BG,
            borderColor=BORDER,
            borderWidth=0.4,
            borderPadding=5,
            leftIndent=0,
            rightIndent=0,
            spaceBefore=3,
            spaceAfter=6,
        )
    )
    base.add(
        ParagraphStyle(
            name="TableCN",
            fontName="DocSerif",
            fontSize=8.2,
            leading=11.2,
            textColor=INK,
        )
    )
    base.add(
        ParagraphStyle(
            name="SmallCN",
            fontName="DocSerif",
            fontSize=8.2,
            leading=12,
            textColor=MUTED,
            alignment=TA_CENTER,
        )
    )
    return base


def inline_markup(text: str) -> str:
    text = html.escape(text)
    text = re.sub(r"`([^`]+)`", r"<font name='DocSerifBold' color='#1B365D'>\1</font>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<b>\1</b>", text)
    return text


def is_table_divider(line: str) -> bool:
    cells = [c.strip() for c in line.strip().strip("|").split("|")]
    return bool(cells) and all(re.fullmatch(r":?-{3,}:?", cell or "") for cell in cells)


def parse_table(lines: list[str], start: int) -> tuple[list[list[str]], int]:
    rows: list[list[str]] = []
    i = start
    while i < len(lines) and lines[i].strip().startswith("|"):
        if not is_table_divider(lines[i]):
            row = [cell.strip() for cell in lines[i].strip().strip("|").split("|")]
            rows.append(row)
        i += 1
    return rows, i


def build_table(rows: list[list[str]], styles, available_width: float) -> Table:
    max_cols = max(len(row) for row in rows)
    fixed_rows = [row + [""] * (max_cols - len(row)) for row in rows]
    data = [
        [Paragraph(inline_markup(cell), styles["TableCN"]) for cell in row]
        for row in fixed_rows
    ]
    col_widths = [available_width / max_cols] * max_cols
    table = Table(data, colWidths=col_widths, hAlign="LEFT", repeatRows=1)
    table.setStyle(
        TableStyle(
            [
                ("BACKGROUND", (0, 0), (-1, 0), IVORY),
                ("TEXTCOLOR", (0, 0), (-1, 0), BRAND),
                ("FONTNAME", (0, 0), (-1, -1), "DocSerif"),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("GRID", (0, 0), (-1, -1), 0.35, BORDER),
                ("LINEBELOW", (0, 0), (-1, 0), 0.8, BRAND),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 5),
                ("TOPPADDING", (0, 0), (-1, -1), 4),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
            ]
        )
    )
    return table


def flush_paragraph(story, para_lines: list[str], styles) -> None:
    if not para_lines:
        return
    text = " ".join(line.strip() for line in para_lines).strip()
    if text:
        story.append(Paragraph(inline_markup(text), styles["BodyCN"]))
    para_lines.clear()


def flush_bullets(story, bullets: list[str], styles) -> None:
    if not bullets:
        return
    items = [
        ListItem(Paragraph(inline_markup(item), styles["BulletCN"]), leftIndent=10)
        for item in bullets
    ]
    story.append(
        ListFlowable(
            items,
            bulletType="bullet",
            start="bulletchar",
            bulletFontName="DocSerif",
            bulletFontSize=8,
            bulletColor=BRAND,
            leftIndent=16,
            bulletDedent=8,
            spaceAfter=5,
        )
    )
    bullets.clear()


def markdown_to_story(markdown: str, styles, available_width: float):
    lines = markdown.splitlines()
    story = []
    para_lines: list[str] = []
    bullets: list[str] = []
    i = 0
    in_code = False
    code_lines: list[str] = []
    after_cover = False

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        if stripped.startswith("```"):
            if in_code:
                text = "\n".join(code_lines)
                story.append(Preformatted(text, styles["CodeCN"], maxLineLength=100))
                code_lines.clear()
                in_code = False
            else:
                flush_paragraph(story, para_lines, styles)
                flush_bullets(story, bullets, styles)
                in_code = True
            i += 1
            continue

        if in_code:
            code_lines.append(line)
            i += 1
            continue

        if not stripped:
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            story.append(Spacer(1, 3))
            i += 1
            continue

        if stripped.startswith("|") and i + 1 < len(lines) and is_table_divider(lines[i + 1]):
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            rows, i = parse_table(lines, i)
            story.append(build_table(rows, styles, available_width))
            story.append(Spacer(1, 6))
            continue

        if stripped.startswith("# "):
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            title = stripped[2:].strip()
            story.append(Spacer(1, 86))
            story.append(Paragraph(inline_markup(title), styles["CoverTitle"]))
            story.append(Spacer(1, 10))
            after_cover = True
            i += 1
            continue

        if after_cover and stripped.startswith("课程设计题目"):
            meta = []
            while i < len(lines) and lines[i].strip():
                meta.append(lines[i].strip())
                i += 1
            story.append(Paragraph("<br/>".join(inline_markup(line) for line in meta), styles["CoverSub"]))
            story.append(Spacer(1, 100))
            story.append(Paragraph("答辩版说明文档", styles["SmallCN"]))
            story.append(PageBreak())
            after_cover = False
            continue

        if stripped.startswith("## "):
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            story.append(Paragraph(inline_markup(stripped[3:].strip()), styles["H1CN"]))
            i += 1
            continue

        if stripped.startswith("### "):
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            story.append(Paragraph(inline_markup(stripped[4:].strip()), styles["H2CN"]))
            i += 1
            continue

        if stripped.startswith("#### "):
            flush_paragraph(story, para_lines, styles)
            flush_bullets(story, bullets, styles)
            story.append(Paragraph(inline_markup(stripped[5:].strip()), styles["H3CN"]))
            i += 1
            continue

        if stripped.startswith("- "):
            flush_paragraph(story, para_lines, styles)
            bullets.append(stripped[2:].strip())
            i += 1
            continue

        para_lines.append(stripped)
        i += 1

    flush_paragraph(story, para_lines, styles)
    flush_bullets(story, bullets, styles)
    return story


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: markdown_to_pdf.py input.md output.pdf", file=sys.stderr)
        return 2

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    register_fonts()
    styles = make_styles()
    markdown = input_path.read_text(encoding="utf-8")
    title = "星轨快线智能购票系统项目说明"
    doc = ProjectDocTemplate(str(output_path), title=title)
    story = markdown_to_story(markdown, styles, doc.width)
    doc.build(story)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
