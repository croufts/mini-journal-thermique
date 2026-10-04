import argparse
import hashlib
import json
import logging
import os
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from .ai import select
from .feeds import collect
from .layout import render, font
from .protocol import encode


def main():
    parser = argparse.ArgumentParser(description="Générer un mini-journal thermique")
    parser.add_argument("--config", type=Path, default=Path("config.json"))
    parser.add_argument("--output", type=Path, default=Path("out"))
    parser.add_argument("--demo", action="store_true", help="Exemple fictif hors ligne, sans API")
    parser.add_argument("--check-feeds", action="store_true", help="Tester seulement les flux RSS")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    config = json.loads(args.config.read_text(encoding="utf-8"))
    now = datetime.now(timezone.utc)
    day = now.astimezone(ZoneInfo(config["timezone"])).date()
    if not args.demo and not args.check_feeds:
        args.output.mkdir(parents=True, exist_ok=True)
        # Never leave an older publication manifest after a failed new generation.
        (args.output / "manifest.json").unlink(missing_ok=True)
    if args.demo:
        articles = json.loads((Path(__file__).resolve().parents[1] / "examples" / "demo.json").read_text(encoding="utf-8"))
        provider = "Démonstration fictive"
    else:
        candidates = collect(config, now)
        logging.info("%d candidats disponibles", len(candidates))
        if args.check_feeds:
            return
        diagnostics = {}
        args.output.mkdir(parents=True, exist_ok=True)
        try:
            articles, provider = select(candidates, config, diagnostics)
        finally:
            # Available as an artifact even when generation fails. No raw API responses or keys.
            (args.output / "ai-diagnostics.json").write_text(
                json.dumps(diagnostics, ensure_ascii=False, indent=2), encoding="utf-8")
    image, fitted, layout = render(articles, day, config["printer"],
        greeting=os.environ.get("JOURNAL_GREETING") or config.get("greeting", "Bonjour."))
    if args.demo:
        # Demonstration is conspicuous in the image and must never reach daily publication.
        from PIL import ImageDraw
        ImageDraw.Draw(image).text((26, image.height - 28), "EXEMPLE FICTIF", font=font(17), fill=0, anchor="lt")
    wire = encode(image, config["printer"]["feed_lines"])
    digest = hashlib.sha256(wire).hexdigest()
    filename = f"journal-{day.isoformat()}-{digest[:16]}.bin"
    args.output.mkdir(parents=True, exist_ok=True)
    # Manifest is written last. Hash-based binary names avoid cache/version mixtures.
    (args.output / filename).write_bytes(wire)
    image.save(args.output / "preview.png", dpi=(300, 300))
    (args.output / "edition.json").write_text(json.dumps({"provider": provider, "articles": fitted,
        "layout": layout}, ensure_ascii=False, indent=2), encoding="utf-8")
    manifest = {"schema": 1, "date": day.isoformat(), "provider": provider, "file": filename, "sha256": digest,
                "size": len(wire), "width": image.width, "height": image.height,
                "generated_at": datetime.now(timezone.utc).isoformat(), "demo": args.demo}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    logging.info("Journal %s : %dx%d, %d octets, %s", day, image.width, image.height, len(wire), provider)


if __name__ == "__main__":
    main()
