import json
from datetime import date, datetime, timezone
from unittest.mock import Mock

import pytest
import requests

from mini_journal import feeds, history, ai


def story(identity="old", day="2026-10-17", section="tech"):
    return {"date": day, "section": section, "id": identity,
            "title": "Cyberattaque des frigos de supermarché",
            "summary": "Des magasins touchés par une attaque informatique."}


def test_memory_expires_and_does_not_include_future():
    items = [story(day=d) for d in ("2026-10-10", "2026-10-12", "2026-10-17", "2026-10-19")]
    value = history.recent({"schema": 1, "articles": items}, date(2026, 10, 18))
    assert [a["date"] for a in value] == ["2026-10-17", "2026-10-12"]


def test_memory_records_only_fitted_articles_and_replaces_same_day():
    old = [story(), story("replaced", "2026-10-18")]
    articles = {"tech": [{"id": "new", "title": "Nouvelle découverte", "summary": "Une découverte annoncée."}]}
    value = history.updated(old, articles, date(2026, 10, 18))
    assert {a["id"] for a in value["articles"]} == {"old", "new"}


def test_upgrade_seeds_memory_from_previous_real_edition(monkeypatch):
    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/journal")
    ref = Mock(); ref.json.return_value = {"object": {"sha": "a" * 40}}
    ref.__enter__ = Mock(return_value=ref); ref.__exit__ = Mock(return_value=False)
    monkeypatch.setattr(history.requests, "get", Mock(return_value=ref))
    reads = []
    def snapshot(base, filename):
        reads.append((base, filename))
        return {"history.json": None, "manifest.json": {"date": "2026-10-17", "demo": False},
                "edition.json": {"articles": {"tech": [{"id": "old", "title": "Titre", "summary": "Texte."}]}}}[filename]
    monkeypatch.setattr(history, "snapshot", snapshot)
    value = history.load(date(2026, 10, 18))
    assert value[0]["id"] == "old"
    assert all("/" + "a" * 40 + "/" in base for base, _ in reads)


def test_corrupt_or_unavailable_memory_is_not_silently_erased(monkeypatch):
    monkeypatch.setenv("GITHUB_REPOSITORY", "owner/journal")
    monkeypatch.setattr(history.requests, "get", Mock(side_effect=requests.Timeout()))
    with pytest.raises(requests.Timeout): history.load(date(2026, 10, 18))
    with pytest.raises(ValueError): history.recent({"schema": 1, "articles": [{}]}, date(2026, 10, 18))


def test_observed_column_and_promotions_filtered_but_launches_kept():
    assert feeds.commentary_or_promotion({"title": "La cyberattaque montre à quel point Internet est imbriqué",
                                         "url": "https://www.lemonde.fr/idees/article/2026/10/18/topic.html"})
    assert feeds.commentary_or_promotion({"title": "Les bons plans", "url": "https://example.com/bons-plans/phone"})
    assert feeds.commentary_or_promotion({"title": "Le récap pop culture de la semaine"})
    assert not feeds.commentary_or_promotion({"title": "Tesla dévoile le nouveau Roadster"})
    assert not feeds.commentary_or_promotion({"title": "New telescope reveals a planet"})


def test_filters_before_candidate_cap_and_tech_uses_longer_window(monkeypatch):
    items = {s: [{"id": s, "section": s, "title": "Nouveau fait", "description": "Annonce.",
                  "published": "2026-10-18T04:00:00+00:00"}] for s in feeds.SECTIONS}
    items["tech"] = [{**items["tech"][0], "id": "old"},
                     {**items["tech"][0], "id": "ad", "url": "https://example.com/bons-plans/phone"},
                     {**items["tech"][0], "id": "launch", "title": "Tesla dévoile le Roadster"}]
    ages = []
    def fetch(feed, now, age):
        ages.append((feed["section"], age)); return items[feed["section"]]
    monkeypatch.setattr(feeds, "fetch_one", fetch)
    diagnostics = {}
    candidates = feeds.collect({"feeds": [{"section": s, "url": s} for s in feeds.SECTIONS],
        "max_age_hours": 36, "tech_max_age_hours": 72, "max_candidates_per_section": 1},
        datetime(2026, 10, 18, tzinfo=timezone.utc), [story()], diagnostics)
    assert [c["id"] for c in candidates if c["section"] == "tech"] == ["launch"]
    assert ("tech", 72) in ages and ("france", 36) in ages
    assert {a["reason"] for a in diagnostics["rejected"]} == {"already_published", "commentary_or_promotion"}


def test_semantic_history_reaches_selection_without_extra_ai_calls(monkeypatch):
    candidates = [{"id": s, "section": s} for s in feeds.SECTIONS]
    calls = []
    def request(self, stage, instruction, data, schema, validator):
        calls.append((stage, instruction, data))
        if stage == "selection": return candidates
        return {s: [{"id": s, "title": "Une nouvelle décision", "summary": "Une décision annoncée."}]
                for s in feeds.SECTIONS}
    monkeypatch.setenv("OPENROUTER_API_KEY", "test-secret")
    monkeypatch.setattr(ai.OpenRouter, "request", request)
    ai.select(candidates, {}, history=[story()])
    assert calls[0][2]["sujets_deja_publies"] == [story()]
    assert "développement majeur nouveau" in calls[0][1]
    assert [c[0] for c in calls] == ["selection", "redaction", "relecture"]


def test_launch_below_weekend_features_is_considered_first(monkeypatch):
    stories = [{"id": str(i), "section": "tech", "title": f"Dossier technologique numéro {i}",
                "published": "2026-10-18T04:00:00+00:00"} for i in range(15)]
    stories.append({"id": "launch", "section": "tech", "title": "Tesla présente son nouveau Roadster",
                    "published": "2026-10-16T06:00:00+00:00"})
    assert sorted(stories, key=feeds.event_priority, reverse=True)[0]["id"] == "launch"


def test_publication_rejects_memory_not_matching_printed_text(tmp_path):
    from scripts.publish import checked_artifacts
    (tmp_path / "manifest.json").write_text(json.dumps({"schema": 1, "demo": False, "provider": "OpenRouter",
        "date": "2026-10-18", "sha256": "a" * 64}))
    (tmp_path / "edition.json").write_text(json.dumps({"provider": "OpenRouter", "articles": {
        "tech": [{"id": "new", "title": "Découverte", "summary": "Annonce."}]}}))
    (tmp_path / "ai-diagnostics.json").write_text(json.dumps({"status": "validated"}))
    (tmp_path / "history.json").write_text(json.dumps({"schema": 1, "articles": [story()]}))
    with pytest.raises(ValueError, match="Historique incohérent"):
        checked_artifacts(tmp_path)
