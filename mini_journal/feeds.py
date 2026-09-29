import hashlib
import html
import logging
import re
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
from difflib import SequenceMatcher

import feedparser
import requests

LOG = logging.getLogger(__name__)
SECTIONS = ("france", "world", "tech")


def clean(value, limit=400):
    value = html.unescape(re.sub(r"<[^>]*>", " ", str(value)))
    return " ".join(value.split())[:limit]


def parse_feed(data, section, now, max_age_hours):
    result = []
    for entry in feedparser.parse(data).entries:
        stamp = entry.get("published_parsed") or entry.get("updated_parsed")
        if not stamp:
            continue  # An undated item cannot be presented as today's news.
        published = datetime(*stamp[:6], tzinfo=timezone.utc)
        if not now - timedelta(hours=max_age_hours) <= published <= now + timedelta(minutes=10):
            continue
        title = clean(entry.get("title", ""), 130)
        if not title:
            continue
        link = entry.get("link", "")
        identity = link or title.casefold()
        result.append({
            "id": hashlib.sha256(identity.encode()).hexdigest()[:16],
            "section": section, "title": title,
            "description": clean(entry.get("summary", entry.get("description", "")), 260),
            "published": published.isoformat(),
        })
    return result


def fetch_one(feed, now, max_age_hours):
    try:
        with requests.get(feed["url"], timeout=(10, 25), stream=True,
                          headers={"User-Agent": "MiniJournalMathias/1.0 (RSS reader)"}) as response:
            response.raise_for_status()
            data = bytearray()
            for chunk in response.iter_content(16384):
                data.extend(chunk)
                if len(data) > 2_000_000:
                    raise ValueError("Flux trop volumineux")
        items = parse_feed(bytes(data), feed["section"], now, max_age_hours)
        LOG.info("RSS %s : %d articles récents", feed["url"], len(items))
        return items
    except (requests.RequestException, ValueError) as exc:
        LOG.warning("RSS %s indisponible (%s)", feed["url"], type(exc).__name__)
        return []


def collect(config, now):
    with ThreadPoolExecutor(max_workers=5) as pool:
        groups = list(pool.map(lambda feed: fetch_one(feed, now, config["max_age_hours"]), config["feeds"]))
    # Round-robin across publishers: a prolific feed must not fill the whole prompt.
    selected, seen_ids = [], set()
    for index in range(max((len(g) for g in groups), default=0)):
        for group in groups:
            if index >= len(group):
                continue
            item = group[index]
            peers = [s for s in selected if s["section"] == item["section"]]
            if len(peers) >= config["max_candidates_per_section"] or item["id"] in seen_ids:
                continue
            normalized = item["title"].casefold()
            if any(SequenceMatcher(None, normalized, p["title"].casefold()).ratio() > .88 for p in peers):
                continue
            selected.append(item)
            seen_ids.add(item["id"])
    for section in SECTIONS:
        if not any(i["section"] == section for i in selected):
            raise ValueError(f"Aucune actualité récente pour {section}. Publication annulée.")
    return selected
