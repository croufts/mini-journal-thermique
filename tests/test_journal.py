import json
from copy import deepcopy
from datetime import date, datetime, timezone
from pathlib import Path
from unittest.mock import Mock

import pytest
from PIL import Image

from mini_journal import ai
from mini_journal.feeds import parse_feed
from mini_journal.layout import render, wrap, font
from mini_journal.protocol import encode, decode
from scripts.is_due import due
from scripts.publish import checked_artifacts

ROOT = Path(__file__).resolve().parents[1]
CONFIG = json.loads((ROOT / "config.json").read_text(encoding="utf-8"))
DEMO = json.loads((ROOT / "examples/demo.json").read_text(encoding="utf-8"))
CANDIDATES = [{"id": f"id-{s}", "section": s, "title": "Une nouvelle importante",
               "description": "Des précisions factuelles.", "published": "2026-09-29T04:00:00+00:00"}
              for s in ("france", "world", "tech")]
VALID = {c["section"]: [{"id": c["id"], "title": c["title"], "summary": c["description"]}] for c in CANDIDATES}


@pytest.mark.parametrize("height", [1, 254, 255, 256, 510, 2362])
def test_wire_round_trip_polarity_padding_and_block_boundaries(height):
    image = Image.new("1", (626, height), 1)
    for y in range(height):
        for x in (0, 4, 6, 625):
            image.putpixel((x, y), 0)
    wire = encode(image)
    assert wire[5:13] == b"\x1d\x76\x30\x00\x4f\x00" + min(height, 255).to_bytes(2, "little")
    assert decode(wire).tobytes() == image.tobytes()
    assert wire[13] == 0x8a  # black=1, MSB first
    assert wire[13 + 78] == 0x40  # Last actual dot black; six padding dots white.


def test_white_black_and_lf_byte_preserved():
    white = Image.new("1", (626, 1), 1)
    assert encode(white)[13:92] == bytes(79)
    white.putpixel((4, 0), 0)
    white.putpixel((6, 0), 0)
    assert encode(white)[13] == 0x0a
    assert decode(encode(white)).tobytes() == white.tobytes()
    assert decode(encode(Image.new("1", (626, 1), 0))).getpixel((625, 0)) == 0


def test_decoder_rejects_truncation():
    with pytest.raises(ValueError):
        decode(encode(Image.new("1", (626, 2362), 1))[:-1])


def test_layout_fits_long_content_and_keeps_tech():
    long = {s: [{"id": f"{s}-{i}", "title": "Titre très long avec de nombreuses informations à présenter",
                  "summary": ("Une information détaillée et importante. " * 6)[:240]}
                 for i in range(1 if s == "tech" else 3)] for s in ("france", "world", "tech")}
    image, fitted, stats = render(long, date(2026, 9, 29), CONFIG["printer"])
    assert image.size == (626, stats["used_height"]) and image.mode == "1"
    assert stats["used_height"] <= 2362
    assert len(fitted["tech"]) == 1 and fitted["france"] and fitted["world"]
    assert len(fitted["france"]) + len(fitted["world"]) < 6
    face = font(31)
    assert all(face.getlength(line) <= 574 for line in wrap("A" * 1000, face, 574))


def test_short_edition_has_no_unused_page_tail():
    image, fitted, stats = render(VALID, date(2026, 9, 30), CONFIG["printer"])
    assert image.height == stats["used_height"] < 2362
    # The final separator stays intact, followed by only a small cutting margin.
    ink = image.convert("L").point(lambda p: 255 - p).getbbox()
    assert 20 <= image.height - ink[3] <= 45
    assert decode(encode(image)).size == image.size


@pytest.mark.parametrize("change", ["second-tech", "unknown-id", "wrong-section", "url", "missing", "long-summary"])
def test_rejects_invalid_ai(change):
    invalid = deepcopy(VALID)
    if change == "second-tech": invalid["tech"] *= 2
    elif change == "unknown-id": invalid["tech"][0]["id"] = "invented"
    elif change == "wrong-section": invalid["tech"][0]["id"] = "id-france"
    elif change == "url": invalid["tech"][0]["summary"] = "Voir https://example.com"
    elif change == "missing": del invalid["world"]
    elif change == "long-summary": invalid["tech"][0]["summary"] = "x" * 241
    with pytest.raises(ValueError): ai.validate(invalid, CANDIDATES)


@pytest.mark.parametrize("title", [
    "Les Etats-Unis annoncent la fin officielle de la mission de la",
    "Budget social : les conséquences redoutées d’une",
    "Les géants de l’IA ne s’intéressent aux résultats mathématiques",
])
def test_rejects_observed_incomplete_titles(title):
    invalid = deepcopy(VALID)
    invalid["tech"][0]["title"] = title
    with pytest.raises(ValueError, match="grammaticalement incomplet"):
        ai.validate(invalid, CANDIDATES)


def test_complete_sentence_fitting_preserves_negation():
    text = "Une phrase complète. Les géants ne privilégient pas la recherche."
    assert ai.complete_sentences(text, 48) == "Une phrase complète."
    assert ai.complete_sentences("Les géants ne privilégient pas la recherche.", 20) == ""


def test_feed_filters_old_undated_future_and_html():
    xml = b'''<rss version="2.0"><channel><title>Feed</title>
    <item><title>Recent &amp; useful</title><description>&lt;b&gt;Text&lt;/b&gt;</description><pubDate>Tue, 29 Sep 2026 04:00:00 GMT</pubDate></item>
    <item><title>Old</title><pubDate>Tue, 01 Sep 2026 04:00:00 GMT</pubDate></item>
    <item><title>Future</title><pubDate>Wed, 30 Sep 2026 04:00:00 GMT</pubDate></item>
    <item><title>Undated</title></item></channel></rss>'''
    parsed = parse_feed(xml, "france", datetime(2026, 9, 29, 5, tzinfo=timezone.utc), 36)
    assert len(parsed) == 1
    assert parsed[0]["title"] == "Recent & useful" and parsed[0]["description"] == "Text"


def test_daily_gate_avoids_same_day_and_future():
    assert due({"date": "2026-09-28"}, "2026-09-29")
    assert not due({"date": "2026-09-29"}, "2026-09-29")
    assert not due({"date": "2026-09-30"}, "2026-09-29")
    assert due({"date": "2026-09-29", "demo": True}, "2026-09-29")


def test_publication_rejects_demo_and_corruption(tmp_path):
    import hashlib
    wire = encode(Image.new("1", (626, 30), 1))
    digest = hashlib.sha256(wire).hexdigest()
    name = f"journal-2026-09-29-{digest[:16]}.bin"
    manifest = {"schema": 1, "demo": False, "provider": "OpenRouter", "date": "2026-09-29", "sha256": digest,
                "file": name, "size": len(wire), "width": 626, "height": 30}
    (tmp_path / name).write_bytes(wire)
    (tmp_path / "manifest.json").write_text(json.dumps(manifest))
    (tmp_path / "edition.json").write_text(json.dumps({"provider": "OpenRouter"}))
    (tmp_path / "ai-diagnostics.json").write_text(json.dumps({"status": "validated"}))
    assert name in checked_artifacts(tmp_path)
    (tmp_path / name).write_bytes(wire[:-1])
    with pytest.raises(ValueError): checked_artifacts(tmp_path)
    manifest["demo"] = True
    (tmp_path / "manifest.json").write_text(json.dumps(manifest))
    with pytest.raises(ValueError): checked_artifacts(tmp_path)


def test_publication_rejects_unvalidated_or_rss_edition(tmp_path):
    (tmp_path / "manifest.json").write_text(json.dumps({"schema": 1, "demo": False, "provider": "OpenRouter"}))
    (tmp_path / "edition.json").write_text(json.dumps({"provider": "OpenRouter"}))
    (tmp_path / "ai-diagnostics.json").write_text(json.dumps({"status": "failed"}))
    with pytest.raises(ValueError, match="OpenRouter validée"):
        checked_artifacts(tmp_path)


def test_decoder_rejects_short_header_and_excess_feed():
    with pytest.raises(ValueError):
        decode(b"\x1b\x40\x1b\x61\x01\x1d\x76\x30\x00")
    with pytest.raises(ValueError):
        decode(encode(Image.new("1", (626, 1), 1))[:-1] + b"\xff")


def test_greeting_is_configurable():
    from mini_journal.layout import compose
    commands, _ = compose(VALID, date(2026, 9, 30), 626, 31, False, "Bonjour Camille.")
    assert commands[0][2] == "Bonjour Camille."


@pytest.mark.parametrize("body,reason", [
    ({"error": {"message": "Provider unavailable"}}, "Erreur signalée"),
    ({"choices": []}, "sans choix"),
    ({"choices": [{"message": {"content": None, "reasoning": "unfinished"}}]}, "sans texte final"),
    ({"choices": [{"finish_reason": "length", "message": {"content": "{"}}]}, "budget de tokens"),
])
def test_api_envelope_errors_are_explicit(body, reason):
    r = Mock()
    r.json.return_value = body
    with pytest.raises(ValueError, match=reason):
        ai.response_content(r, "test-secret")


def test_api_error_redacts_credentials(caplog):
    r = Mock()
    r.json.return_value = {"error": {"message": "Error test-secret Bearer abc123 sk-or-v1-other"}}
    with pytest.raises(ValueError):
        ai.response_content(r, "test-secret")
    assert "test-secret" not in caplog.text and "abc123" not in caplog.text and "sk-or-v1-other" not in caplog.text
