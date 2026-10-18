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


def commentary_or_promotion(item):
    """Filter identifiable columns, recaps and shopping before the prompt cap."""
    title = item["title"].casefold()
    url = item.get("url", "").casefold()
    return (bool(re.search(r"/(?:idees|opinions?|tribunes?|bons-plans?|guide-dachat|pop-culture)/", url)) or
            bool(re.search(r"\b(?:tribune|chronique|éditorial|editorial|opinion|bon plan|code promo|"
                           r"récap|recap|on vous raconte la semaine|buying guide|deals|review|sponso)\b", title)) or
            (item.get("section") == "tech" and bool(re.search(
                r"(?:j[’']ai testé|on a testé|^test .+ :|sur quelle chaîne|tout ce qu[’']il faut savoir)", title))) or
            bool(re.search(r"\b(?:meilleurs?|best)\s+(?:films?|séries?|series|smartphones?|laptops?|phones?)\b", title)))


def event_priority(item):
    """Keep concrete announcements visible even below many newer features."""
    event = bool(re.search(r"\b(?:dévoil\w*|présent\w*|annonc\w*|lanc\w*|découvr\w*|"
                          r"unveils?|reveals?|launch\w*|announc\w*|discover\w*)\b", item["title"], re.I))
    return (event if item["section"] == "tech" else False, item["published"])


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
        title = clean(entry.get("title", ""), 2000)
        if not title:
            continue
        link = entry.get("link", "")
        identity = link or title.casefold()
        result.append({
            "id": hashlib.sha256(identity.encode()).hexdigest()[:16],
            "section": section, "title": title,
            "url": link,
            "description": clean(entry.get("summary", entry.get("description", "")), 260),
            "published": published.isoformat(),
        })
    return result


def fetch_one(feed, now, max_age_hours):
    try:
        with requests.get(feed["url"], timeout=(10, 25), stream=True,
                          headers={"User-Agent": "MiniJournal/1.0 (RSS reader)"}) as response:
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


def collect(config, now, history=None, diagnostics=None):
    history = history or []
    diagnostics = diagnostics if diagnostics is not None else {}
    printed = {a["id"] for a in history}
    with ThreadPoolExecutor(max_workers=5) as pool:
        groups = list(pool.map(lambda feed: fetch_one(feed, now,
            config.get("tech_max_age_hours", config["max_age_hours"]) if feed["section"] == "tech"
            else config["max_age_hours"]), config["feeds"]))
    diagnostics["feeds"] = [{"url": feed["url"], "section": feed["section"], "recent": len(items)}
                            for feed, items in zip(config["feeds"], groups)]
    rejected = []
    filtered = []
    for group in groups:
        kept = []
        for item in group:
            reason = "already_published" if item["id"] in printed else "commentary_or_promotion" if commentary_or_promotion(item) else ""
            if reason:
                rejected.append({"id": item["id"], "title": item["title"], "reason": reason})
            else:
                kept.append(item)
        filtered.append(sorted(kept, key=event_priority, reverse=True))
    groups = filtered
    # Round-robin across publishers: a prolific feed must not fill the whole prompt.
    selected, seen_ids = [], set()
    for index in range(max((len(g) for g in groups), default=0)):
        for group in groups:
            if index >= len(group):
                continue
            item = group[index]
            peers = [s for s in selected if s["section"] == item["section"]]
            limit = config.get("max_tech_candidates", config["max_candidates_per_section"]) if item["section"] == "tech" else config["max_candidates_per_section"]
            if len(peers) >= limit or item["id"] in seen_ids:
                continue
            normalized = item["title"].casefold()
            if any(SequenceMatcher(None, normalized, p["title"].casefold()).ratio() > .88 for p in peers):
                continue
            selected.append(item)
            seen_ids.add(item["id"])
    for section in SECTIONS:
        if not any(i["section"] == section for i in selected):
            raise ValueError(f"Aucune actualité récente pour {section}. Publication annulée.")
    diagnostics.update(rejected=rejected, candidates=selected)
    return selected
