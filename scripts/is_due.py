"""Skip generation once today's edition is published (stdlib only)."""
import json
import os
from datetime import datetime
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import urlopen
from zoneinfo import ZoneInfo


def due(manifest, day):
    return manifest.get("demo", False) or manifest.get("date", "") < day


def main():
    if os.environ.get("FORCE", "false") == "true":
        print("true")
        return
    repo = os.environ["GITHUB_REPOSITORY"]
    run = os.environ.get("GITHUB_RUN_ID", "0")
    url = f"https://raw.githubusercontent.com/{repo}/refs/heads/journal/manifest.json?run={run}"
    try:
        with urlopen(url, timeout=30) as response:
            manifest = json.load(response)
    except HTTPError as error:
        if error.code != 404:
            raise
        print("true")
        return
    config = json.loads(Path("config.json").read_text(encoding="utf-8"))
    day = datetime.now(ZoneInfo(config["timezone"])).date().isoformat()
    print(str(due(manifest, day)).lower())


if __name__ == "__main__":
    main()
