from copy import deepcopy
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from .ai import complete_sentences
from .feeds import SECTIONS

FONTS = Path(__file__).resolve().parents[1] / "assets" / "fonts"
LABELS = {"france": "FRANCE", "world": "MONDE", "tech": "TECH"}
MONTHS = ("janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre")


def font(size, bold=False):
    return ImageFont.truetype(str(FONTS / ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")), size)


def wrap(text, face, width):
    lines, line = [], ""
    for word in text.split():
        # Split an unbroken token so even malformed model output cannot cross the margin.
        pieces, piece = [], ""
        for char in word:
            if piece and face.getlength(piece + char) > width:
                pieces.append(piece)
                piece = ""
            piece += char
        if piece:
            pieces.append(piece)
        for part in pieces:
            candidate = (line + " " + part).strip()
            if line and face.getlength(candidate) > width:
                lines.append(line)
                line = part
            else:
                line = candidate
    if line:
        lines.append(line)
    return lines


def compose(articles, day, width, body_size, emergency):
    margin, y = 26, 24
    commands = []
    def text(value, size, bold=False, gap=8):
        nonlocal y
        face = font(size, bold)
        for line in wrap(value, face, width - margin * 2):
            commands.append(("text", (margin, y), line, face))
            y += size + 6
        y += gap
    def rule(gap=18):
        nonlocal y
        commands.append(("rule", (margin, y, width - margin, y), None, None))
        y += gap
    text("Bonjour Mathias.", 43, True, 5)
    text(f"{day.day} {MONTHS[day.month - 1]} {day.year}", 23, gap=14)
    if emergency:
        text("Édition de secours RSS", 22, gap=10)
    rule()
    for section in SECTIONS:
        text(LABELS[section], 29, True, 12)
        for index, article in enumerate(articles[section]):
            text(article["title"], body_size + (8 if index == 0 else 4), True, 4)
            text(article["summary"], body_size, gap=17)
        rule()
    return commands, y + 24


def render(articles, day, printer, emergency=False):
    width, height = printer["width"], printer["height"]
    if width != 626 or not 800 <= height <= 2362:
        raise ValueError("Format attendu : largeur 626, hauteur 800 à 2362")
    fitted = deepcopy(articles)
    if len(fitted["tech"]) != 1 or any(not fitted[s] for s in SECTIONS):
        raise ValueError("Les trois sections et exactement une info Tech sont nécessaires")
    # Fit the actual pixel geometry, never crop text. Keep a readable 27px minimum.
    while True:
        for body_size in (31, 29, 27):
            commands, used = compose(fitted, day, width, body_size, emergency)
            if used <= height:
                image = Image.new("L", (width, used), 255)
                draw = ImageDraw.Draw(image)
                for kind, position, value, face in commands:
                    if kind == "text":
                        draw.text(position, value, font=face, fill=0, anchor="lt")
                    else:
                        draw.line(position, fill=0, width=2)
                image = image.point(lambda pixel: 255 if pixel >= printer["threshold"] else 0, mode="1")
                return image, fitted, {"body_size": body_size, "used_height": used}
        # Remove the last (least important) general news while preserving every section.
        removable = [s for s in ("france", "world") if len(fitted[s]) > 1]
        if removable:
            section = max(removable, key=lambda s: len(fitted[s]))
            fitted[section].pop()
            continue
        changed = False
        for section in SECTIONS:
            article = fitted[section][0]
            if len(article["summary"]) > 70:
                shorter = complete_sentences(article["summary"], max(70, len(article["summary"]) - 35))
                if shorter and shorter != article["summary"]:
                    article["summary"] = shorter
                    changed = True
        if not changed:
            raise ValueError("Le contenu ne tient pas dans le format demandé")
