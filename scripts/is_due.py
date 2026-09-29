"""Workflow gate: one daily edition, including retries and manual runs."""
import json
import os
import re
from datetime import datetime
from pathlib import Path
from zoneinfo import ZoneInfo

import requests


def due(manifest, day):
    return manifest.get("demo", False) or manifest.get("date", "") < day


def main():
    repo = os.environ["GITHUB_REPOSITORY"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Invalid repository")
    if os.environ.get("FORCE", "false") == "true":
        print("true")
        return
    run = os.environ.get("GITHUB_RUN_ID", "0")
    response = requests.get(f"https://raw.githubusercontent.com/{repo}/refs/heads/journal/manifest.json?run={run}", timeout=30)
    if response.status_code == 404:
        print("true")
        return
    response.raise_for_status()
    config = json.loads(Path("config.json").read_text(encoding="utf-8"))
    day = datetime.now(ZoneInfo(config["timezone"])).date().isoformat()
    print(str(due(response.json(), day)).lower())


if __name__ == "__main__":
    main()
