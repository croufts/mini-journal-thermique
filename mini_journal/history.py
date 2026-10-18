"""Bounded editorial memory, published atomically with the edition it describes."""
import json
import logging
import os
import re
from datetime import date, timedelta

import requests

LOG = logging.getLogger(__name__)
DAYS = 7
MAX_ITEMS = 56


def recent(value, day):
    if not isinstance(value, dict) or value.get("schema") != 1 or not isinstance(value.get("articles"), list):
        raise ValueError("Historique éditorial invalide")
    result = []
    for item in value["articles"]:
        if not isinstance(item, dict) or not all(isinstance(item.get(k), str) for k in
                ("date", "section", "id", "title", "summary")):
            raise ValueError("Article d'historique invalide")
        stamp = date.fromisoformat(item["date"])
        if item["section"] not in ("france", "world", "tech"):
            raise ValueError("Rubrique d'historique invalide")
        if day - timedelta(days=DAYS - 1) <= stamp <= day:
            result.append({k: item[k][:500] for k in ("date", "section", "id", "title", "summary")})
    return sorted(result, key=lambda a: a["date"], reverse=True)[:MAX_ITEMS]


def snapshot(base, filename):
    # A missing optional file is different from an unreadable/corrupt memory.
    with requests.get(base + filename, timeout=(10, 20), stream=True) as response:
        if response.status_code == 404:
            return None
        response.raise_for_status()
        data = bytearray()
        for chunk in response.iter_content(16384):
            data.extend(chunk)
            if len(data) > 100_000:
                raise ValueError("Historique trop volumineux")
    return json.loads(data)


def load(day):
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not repo:
        LOG.warning("Sans GITHUB_REPOSITORY : historique distant indisponible")
        return []
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Dépôt invalide")
    # Pin all reads to a single commit; publication replaces this branch daily.
    with requests.get(f"https://api.github.com/repos/{repo}/git/ref/heads/journal", timeout=(10, 20)) as response:
        if response.status_code == 404:
            return []
        response.raise_for_status()
        sha = response.json()["object"]["sha"]
    if not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("Révision d'historique invalide")
    base = f"https://raw.githubusercontent.com/{repo}/{sha}/"
    value = snapshot(base, "history.json")
    if value is not None:
        return recent(value, day)
    # Upgrade existing installations using the last real edition as the seed.
    manifest, edition = snapshot(base, "manifest.json"), snapshot(base, "edition.json")
    if manifest is None or edition is None or manifest.get("demo", True):
        return []
    items = [{"date": manifest["date"], "section": section, **article}
             for section, articles in edition["articles"].items() for article in articles]
    return recent({"schema": 1, "articles": items}, day)


def updated(history, articles, day):
    # Replace a same-day regenerated edition, keeping previous days only.
    items = [a for a in history if a["date"] != day.isoformat()]
    items += [{"date": day.isoformat(), "section": section, **article}
              for section, values in articles.items() for article in values]
    return {"schema": 1, "articles": recent({"schema": 1, "articles": items}, day)}
