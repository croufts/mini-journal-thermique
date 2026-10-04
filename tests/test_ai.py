"""Exercise the actual editorial pipeline without consuming API quota."""
import json
from copy import deepcopy
from unittest.mock import Mock

import pytest
import requests

from mini_journal import ai

CONFIG = {"openrouter_model": "openrouter/free"}
CANDIDATES = [{"id": s, "section": s, "title": "Une information importante",
               "description": "Les autorités annoncent une décision.", "published": "2026-10-04"}
              for s in ai.SECTIONS]
SELECTION = {s: [s] for s in ai.SECTIONS}
VALID = {s: [{"id": s, "title": "Une décision annoncée", "summary": "Les autorités annoncent une décision."}]
         for s in ai.SECTIONS}


def response(value, model="tested/model:free", finish="stop", usage=None):
    r = Mock()
    r.json.return_value = {"model": model, "choices": [{"finish_reason": finish,
        "message": {"content": json.dumps(value)}}], "usage": usage or {}}
    return r


@pytest.fixture
def setup(monkeypatch):
    monkeypatch.setenv("OPENROUTER_API_KEY", "test-secret")
    monkeypatch.setattr(ai.time, "sleep", lambda _: None)
    def run(responses, config=None):
        post = Mock(side_effect=responses)
        monkeypatch.setattr(ai.requests, "post", post)
        diagnostics = {}
        articles, provider = ai.select(CANDIDATES, config or CONFIG, diagnostics)
        return articles, provider, post, diagnostics
    return run


def test_selection_drafting_review_preserve_free_model_and_groq_is_unused(setup, monkeypatch):
    monkeypatch.setenv("GROQ_API_KEY", "unused")
    revised = deepcopy(VALID)
    revised["world"][0]["title"] = "La décision entre en vigueur"
    articles, provider, post, d = setup([response(SELECTION), response(VALID), response(revised)])
    assert provider == "OpenRouter" and articles == revised
    assert [c["stage"] for c in d["calls"]] == ["selection", "redaction", "relecture"]
    assert all(c.args[0] == ai.URL for c in post.call_args_list)
    assert [c.kwargs["json"]["model"] for c in post.call_args_list] == ["openrouter/free", "tested/model:free", "tested/model:free"]
    assert d["status"] == "validated"


def test_bad_article_is_repaired_without_rewriting_other_articles(setup):
    draft = deepcopy(VALID)
    draft["tech"][0]["summary"] = "Un résumé beaucoup trop long. " * 15
    patch = {"articles": [VALID["tech"][0]]}
    articles, _, post, d = setup([response(SELECTION), response(draft), response(draft), response(patch)])
    assert articles == VALID
    data = json.loads(post.call_args_list[3].kwargs["json"]["messages"][1]["content"].split("DONNÉES :\n")[1])
    assert [c["id"] for c in data["candidats"]] == ["tech"]
    assert "caractères" in data["erreurs"][0]["motifs"][0]
    assert d["repairs"][0][0]["id"] == "tech"


def test_bad_review_does_not_destroy_valid_draft(setup):
    bad = deepcopy(VALID)
    bad["tech"][0]["summary"] = "Texte sans fin"
    articles, _, post, d = setup([response(SELECTION), response(VALID), response(bad)])
    assert articles == VALID and post.call_count == 3 and d["review"] == "completed"


def test_review_failure_keeps_valid_ai_draft(setup):
    articles, _, post, d = setup([response(SELECTION), response(VALID), requests.Timeout(), requests.Timeout()])
    assert articles == VALID and post.call_count == 4 and d["review"] == "unavailable"


def test_review_changing_selection_is_rejected(setup):
    bad = deepcopy(VALID)
    bad["tech"][0]["id"] = "france"
    articles, _, _, d = setup([response(SELECTION), response(VALID), response(bad), response(bad)])
    assert articles == VALID and d["review"] == "unavailable"


def test_truncation_retries_with_more_tokens_and_records_reasoning(setup):
    usage = {"prompt_tokens": 200, "completion_tokens": 16384,
             "completion_tokens_details": {"reasoning_tokens": 16380}}
    _, _, post, d = setup([response({}, finish="length", usage=usage), response(SELECTION), response(VALID), response(VALID)])
    assert post.call_args_list[0].kwargs["json"]["max_tokens"] == 16384
    assert post.call_args_list[1].kwargs["json"]["max_tokens"] == 32768
    assert post.call_args_list[1].kwargs["json"]["response_format"]["type"] == "json_object"
    assert d["calls"][0]["usage"]["reasoning_tokens"] == 16380
    assert d["calls"][0]["finish_reason"] == "length"
    assert all(c.kwargs["json"].get("reasoning", {}).get("enabled") is not False for c in post.call_args_list)


@pytest.mark.parametrize("status", [401, 402, 403, 429])
def test_auth_or_quota_failure_stops_without_repeating_or_rss(setup, status):
    r = Mock(status_code=status)
    error = requests.HTTPError(response=r)
    with pytest.raises(RuntimeError, match=f"HTTP {status}"):
        setup([error])


def test_missing_key_never_uses_rss_even_with_legacy_configuration(monkeypatch):
    monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
    monkeypatch.setenv("GROQ_API_KEY", "unused")
    with pytest.raises(RuntimeError, match="absente"):
        ai.select(CANDIDATES, {**CONFIG, "allow_rss_fallback": True})


def test_paid_model_is_rejected_before_network(setup):
    with pytest.raises(ValueError, match="Seuls"):
        setup([], {"openrouter_model": "paid/model"})


def test_selection_rejects_duplicates_wrong_sections_and_unhashable_ids():
    for ids in (["tech", "tech"], ["france"], [{}]):
        with pytest.raises(ValueError):
            ai.validate_selection({**SELECTION, "tech": ids}, CANDIDATES)


def test_draft_order_is_restored_but_missing_articles_are_rejected():
    selected = CANDIDATES + [{**CANDIDATES[0], "id": "france2"}]
    draft = deepcopy(VALID)
    draft["france"].insert(0, {**VALID["france"][0], "id": "france2"})
    restored = ai.preserve_selection(draft, selected)
    assert [a["id"] for a in restored["france"]] == ["france", "france2"]
    with pytest.raises(ValueError, match="sélection"):
        ai.preserve_selection(VALID, selected)


def test_repair_order_is_restored_without_changing_selected_articles(setup):
    bad = deepcopy(VALID)
    for section in ("france", "tech"):
        bad[section][0]["summary"] = "Sans ponctuation"
    patch = {"articles": [VALID["tech"][0], VALID["france"][0]]}
    articles, _, _, _ = setup([response(SELECTION), response(bad), response(bad), response(patch)])
    assert articles == VALID


def test_observed_telegraphic_title_is_repaired(setup):
    bad = deepcopy(VALID)
    bad["france"][0]["title"] = "Wauquiez refuse taxer retraités"
    with pytest.raises(ValueError, match="préposition"):
        ai.validate(bad, CANDIDATES)
    articles, _, _, d = setup([response(SELECTION), response(bad), response(bad),
                               response({"articles": [VALID["france"][0]]})])
    assert articles == VALID and d["repairs"][0][0]["id"] == "france"


def test_repair_cannot_replace_valid_articles(setup):
    bad = deepcopy(VALID)
    bad["tech"][0]["summary"] = "Sans ponctuation"
    with pytest.raises(RuntimeError, match="correction"):
        setup([response(SELECTION), response(bad), response(bad),
               response({"articles": [VALID["france"][0]]}), response({"articles": [VALID["france"][0]]})])


def test_unrepairable_article_fails_instead_of_rss(setup):
    bad = deepcopy(VALID)
    bad["tech"][0]["summary"] = "Sans ponctuation"
    patch = {"articles": [bad["tech"][0]]}
    with pytest.raises(ValueError, match="fin de phrase"):
        setup([response(SELECTION), response(bad), response(bad), response(patch), response(patch)])


def test_request_budget_is_bounded(setup):
    bad = deepcopy(VALID)
    bad["tech"][0]["summary"] = "Sans ponctuation"
    with pytest.raises(RuntimeError, match="Budget"):
        setup([response(SELECTION), response(bad), response(bad)], {**CONFIG, "ai": {"max_calls": 3}})


def test_word_budget_is_repaired_without_truncating_sentences(setup):
    long = deepcopy(VALID)
    for s in ai.SECTIONS:
        long[s][0]["summary"] = "Le fait est là. " * 20  # Draft beyond both budgets.
    patch = {"articles": [VALID[s][0] for s in ai.SECTIONS]}
    articles, _, _, d = setup([response(SELECTION), response(long), response(long), response(patch)])
    assert articles == VALID and d["words"] <= 220


def test_diagnostics_never_store_raw_responses_or_credentials(setup, caplog):
    r = Mock()
    r.json.return_value = {"error": {"message": "test-secret Bearer xyz sk-or-v1-abc"}}
    _, _, _, d = setup([r, response(SELECTION), response(VALID), response(VALID)])
    combined = json.dumps(d) + caplog.text
    assert "test-secret" not in combined and "Bearer xyz" not in combined and "sk-or-v1-abc" not in combined
    assert "message" not in d["calls"][0]


def test_failed_cli_preserves_diagnostics_without_manifest(monkeypatch, tmp_path):
    from mini_journal import __main__ as cli
    config = tmp_path / "config.json"
    config.write_text(json.dumps({"timezone": "Europe/Paris", **CONFIG}), encoding="utf-8")
    output = tmp_path / "out"
    output.mkdir()
    (output / "manifest.json").write_text('{"old": true}')
    monkeypatch.setattr("sys.argv", ["mini_journal", "--config", str(config), "--output", str(output)])
    monkeypatch.setattr(cli, "collect", lambda *args: CANDIDATES)
    monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
    with pytest.raises(RuntimeError):
        cli.main()
    assert json.loads((output / "ai-diagnostics.json").read_text(encoding="utf-8"))["status"] == "failed"
    assert not (output / "manifest.json").exists()
