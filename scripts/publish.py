"""Publish a verified snapshot on journal using the workflow's short-lived token.

No permanent PAT, Pages service or release-download redirect is needed by the ESP32.
The branch is replaced with a single snapshot commit to avoid retaining daily raster history.
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

from mini_journal.protocol import decode


def checked_artifacts(output):
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("demo", True) or manifest.get("schema") != 1:
        raise ValueError("Une démonstration ne doit jamais être publiée")
    edition = json.loads((output / "edition.json").read_text(encoding="utf-8"))
    diagnostics = json.loads((output / "ai-diagnostics.json").read_text(encoding="utf-8"))
    if manifest.get("provider") != "OpenRouter" or edition.get("provider") != "OpenRouter" or diagnostics.get("status") != "validated":
        raise ValueError("Seule une édition OpenRouter validée peut être publiée")
    date, digest = manifest["date"], manifest["sha256"]
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", date) or not re.fullmatch(r"[a-f0-9]{64}", digest):
        raise ValueError("Date ou empreinte invalide")
    expected = f"journal-{date}-{digest[:16]}.bin"
    if manifest["file"] != expected:
        raise ValueError("Nom de binaire invalide")
    wire = (output / expected).read_bytes()
    if len(wire) != manifest["size"] or hashlib.sha256(wire).hexdigest() != digest:
        raise ValueError("Binaire invalide")
    image = decode(wire)
    if image.size != (manifest["width"], manifest["height"]):
        raise ValueError("Dimensions invalides")
    return ["manifest.json", expected, "preview.png", "edition.json"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("out"))
    args = parser.parse_args()
    artifacts = checked_artifacts(args.output)
    repo, token = os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_TOKEN"]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Dépôt invalide")
    env = os.environ.copy()
    authorization = base64.b64encode(f"x-access-token:{token}".encode()).decode()
    # Credentials are passed only through the environment, never written into the checkout.
    env.update({"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "http.https://github.com/.extraheader",
                "GIT_CONFIG_VALUE_0": f"AUTHORIZATION: basic {authorization}", "GIT_TERMINAL_PROMPT": "0"})
    with tempfile.TemporaryDirectory(prefix="mini-journal-publish-") as directory:
        def git(*command, capture=False):
            return subprocess.run(["git", *command], cwd=directory, env=env, check=True,
                                  text=True, capture_output=capture).stdout
        git("init", "-b", "journal")
        git("remote", "add", "origin", f"https://github.com/{repo}.git")
        remote = git("ls-remote", "--heads", "origin", "journal", capture=True).strip()
        previous = remote.split()[0] if remote else ""
        for name in artifacts:
            shutil.copy2(args.output / name, Path(directory) / name)
        git("config", "user.name", "github-actions[bot]")
        git("config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
        git("add", "--", *artifacts)
        git("commit", "-m", "Édition quotidienne du mini-journal")
        git("push", f"--force-with-lease=refs/heads/journal:{previous}", "origin", "HEAD:refs/heads/journal")
    print("Édition publiée et vérifiée sur la branche journal")


if __name__ == "__main__":
    main()
