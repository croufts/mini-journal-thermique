import argparse
import hashlib
import json
import logging
from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from .ai import select, validate
from .feeds import collect
from .layout import render
from .protocol import encode


def main():
    parser = argparse.ArgumentParser(description="Générer le mini-journal de Mathias")
    parser.add_argument("--config", type=Path, default=Path("config.json"))
    parser.add_argument("--output", type=Path, default=Path("out"))
    parser.add_argument("--demo", action="store_true", help="Exemple fictif hors ligne, sans API")
    parser.add_argument("--check-feeds", action="store_true", help="Tester seulement les flux RSS")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    config = json.loads(args.config.read_text(encoding="utf-8"))
    now = datetime.now(timezone.utc)
    day = now.astimezone(ZoneInfo(config["timezone"])).date()
    if args.demo:
        articles = json.loads((Path(__file__).resolve().parents[1] / "examples" / "demo.json").read_text(encoding="utf-8"))
        provider = "Démonstration fictive"
    else:
        candidates = collect(config, now)
        logging.info("%d candidats disponibles", len(candidates))
        if args.check_feeds:
            return
        articles, provider = select(candidates, config)
        articles = validate(articles, candidates)
    image, fitted, layout = render(articles, day, config["printer"], provider == "RSS (secours)")
    if args.demo:
        # Demonstration is conspicuous in the image and must never reach daily publication.
        from PIL import ImageDraw
        ImageDraw.Draw(image).text((26, image.height - 48), "EXEMPLE FICTIF", fill=0)
    wire = encode(image, config["printer"]["feed_lines"], config["printer"]["prefix"],
                  config["printer"]["legacy_lf_workaround"])
    digest = hashlib.sha256(wire).hexdigest()
    filename = f"journal-{day.isoformat()}-{digest[:16]}.bin"
    args.output.mkdir(parents=True, exist_ok=True)
    # Manifest is written last. Hash-based binary names avoid cache/version mixtures.
    (args.output / filename).write_bytes(wire)
    image.save(args.output / "preview.png", dpi=(300, 300))
    (args.output / "edition.json").write_text(json.dumps({"provider": provider, "articles": fitted,
        "layout": layout}, ensure_ascii=False, indent=2), encoding="utf-8")
    manifest = {"schema": 1, "date": day.isoformat(), "file": filename, "sha256": digest,
                "size": len(wire), "width": image.width, "height": image.height,
                "generated_at": now.isoformat(), "demo": args.demo}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    logging.info("Journal %s : %dx%d, %d octets, %s", day, image.width, image.height, len(wire), provider)


if __name__ == "__main__":
    main()
