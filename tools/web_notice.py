#!/usr/bin/env python3
"""Capture public game notice DOM text and compile exact WebView translations."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlsplit
from urllib.request import urlopen

from localize import ROOT, read_json, validate, write_json


GAME = "game.qualiarts.idolypride"
NOTICE_HOST = "stat.game-idolypride.jp"
NOTICE_PATH = re.compile(r"^/notice/[0-9a-f]{64}/index\.html$")
SOURCE = ROOT / "translations/notice/source.json"
TRANSLATION = ROOT / "translations/notice/zh-Hans.json"
UI_TRANSLATION = ROOT / "translations/ui/zh-Hans.json"
OUTPUT = ROOT / "app/src/main/assets/idoly-localify/web-notice.json"
SNAPSHOT = ROOT / ".analysis/web-notice-capture.json"

CAPTURE_EXPRESSION = r"""(async () => {
    // The localizer mutates the live DOM. Fetch the same official page again
    // so a capture cannot accidentally record translated text as the source.
    const response = await fetch(location.href, {credentials: 'same-origin'});
    if (!response.ok || !response.headers.get('content-type')?.includes('text/html'))
        throw new Error('Official notice HTML is unavailable');
    const source = new DOMParser().parseFromString(await response.text(), 'text/html');
    const walker = source.createTreeWalker(source.body, NodeFilter.SHOW_TEXT);
    const nodes = [];
    for (let node; node = walker.nextNode();) {
        const parent = node.parentElement;
        if (!parent || parent.closest('script,style,noscript,textarea,input')) continue;
        const text = node.nodeValue.trim();
        if (text) nodes.push({text, tag: parent.tagName});
    }
    return JSON.stringify({path: location.pathname, title: source.title, nodes});
})()"""


def notice_path(path: str) -> str:
    if not isinstance(path, str) or not NOTICE_PATH.fullmatch(path):
        raise ValueError(f"Invalid IDOLY PRIDE notice path: {path!r}")
    return path


def capture(serial: str, adb_bin: str, snapshot: Path) -> dict:
    """Read only the current official notice page through local WebView debugging."""
    try:
        import websocket
    except ImportError as error:
        raise RuntimeError("Install tools/requirements-web.txt for WebView capture") from error

    pid = subprocess.run([adb_bin, "-s", serial, "shell", "pidof", GAME],
                         check=True, capture_output=True, text=True).stdout.strip().split()
    if len(pid) != 1 or not pid[0].isdigit():
        raise RuntimeError(f"Expected one running {GAME} process on {serial}")
    remote = f"localabstract:webview_devtools_remote_{pid[0]}"
    forward = subprocess.run([adb_bin, "-s", serial, "forward", "tcp:0", remote],
                             check=True, capture_output=True, text=True)
    port = forward.stdout.strip()
    if not port.isdigit():
        raise RuntimeError("ADB did not return a local WebView debugging port")
    try:
        with urlopen(f"http://127.0.0.1:{port}/json", timeout=10) as response:
            pages = json.load(response)
        matches = []
        for page in pages:
            address = urlsplit(page.get("url", ""))
            if address.scheme == "https" and address.hostname == NOTICE_HOST and \
                    NOTICE_PATH.fullmatch(address.path):
                matches.append(page)
        if len(matches) != 1:
            raise RuntimeError(f"Expected one open official notice WebView, found {len(matches)}")
        endpoint = urlsplit(matches[0]["webSocketDebuggerUrl"])
        if endpoint.scheme != "ws" or endpoint.hostname not in ("localhost", "127.0.0.1") \
                or endpoint.port != int(port):
            raise RuntimeError("WebView debugger endpoint is not the local ADB forward")
        connection = websocket.create_connection(matches[0]["webSocketDebuggerUrl"],
                                                 timeout=10, suppress_origin=True)
        try:
            connection.send(json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {
                "expression": CAPTURE_EXPRESSION, "returnByValue": True,
                "awaitPromise": True}}))
            while True:
                result = json.loads(connection.recv())
                if result.get("id") == 1:
                    if result.get("error") or result.get("result", {}).get("exceptionDetails"):
                        raise RuntimeError("WebView DOM evaluation failed")
                    snapshot_data = json.loads(result["result"]["result"]["value"])
                    break
        finally:
            connection.close()
    finally:
        subprocess.run([adb_bin, "-s", serial, "forward", "--remove", f"tcp:{port}"],
                       check=False, capture_output=True)
    snapshot_data["path"] = notice_path(snapshot_data["path"])
    if not isinstance(snapshot_data.get("title"), str) or \
            not isinstance(snapshot_data.get("nodes"), list) or any(
                not isinstance(node, dict) or not isinstance(node.get("text"), str)
                or not node["text"] or not isinstance(node.get("tag"), str)
                for node in snapshot_data["nodes"]):
        raise ValueError("Invalid captured notice DOM")
    write_json(snapshot, snapshot_data)
    return snapshot_data


def import_snapshot(snapshot: Path, source_path: Path) -> tuple[str, int]:
    data = read_json(snapshot)
    path = notice_path(data.get("path"))
    if not isinstance(data.get("title"), str) or not isinstance(data.get("nodes"), list):
        raise ValueError(f"{snapshot}: invalid notice snapshot")
    texts = []
    headline = None
    for node in data["nodes"]:
        if not isinstance(node, dict) or not isinstance(node.get("text"), str) or \
                not node["text"].strip() or not isinstance(node.get("tag"), str):
            raise ValueError(f"{snapshot}: invalid notice text node")
        if node["text"] not in texts:
            texts.append(node["text"])
        if headline is None and node["tag"].upper() == "H1":
            headline = node["text"]
    source = read_json(source_path)
    pages = source.setdefault("pages", {})
    if not isinstance(pages, dict):
        raise ValueError(f"{source_path}: pages must be an object")
    prior = pages.get(path)
    if prior is not None and (not isinstance(prior, dict) or
                             not isinstance(prior.get("texts"), list) or
                             prior.get("title") != data["title"] or
                             (prior.get("headline") is not None and
                              prior["headline"] != headline)):
        raise ValueError(f"{source_path}: existing page metadata conflicts with capture")
    previous = prior["texts"] if prior is not None else []
    if any(not isinstance(value, str) for value in previous):
        raise ValueError(f"{source_path}: invalid existing page texts")
    merged = list(dict.fromkeys([*previous, *texts]))
    if prior is None or merged != previous or (headline and "headline" not in prior):
        pages[path] = {"title": data["title"], "headline": headline, "texts": merged}
        write_json(source_path, source)
    return path, len(merged) - len(previous)


def build(source_path: Path, translation_path: Path, ui_path: Path,
          output_path: Path) -> dict:
    source = read_json(source_path).get("pages", {})
    translation = read_json(translation_path).get("pages", {})
    ui = read_json(ui_path).get("text", {})
    if not isinstance(source, dict) or not isinstance(translation, dict):
        raise ValueError("Web notice sources and translations must contain pages objects")
    if not isinstance(ui, dict):
        raise ValueError(f"{ui_path}: text must be an object")
    unknown_pages = set(translation) - set(source)
    if unknown_pages:
        raise ValueError(f"Web notice translations have unknown pages: {sorted(unknown_pages)[:3]}")
    result = {"host": NOTICE_HOST, "pages": {}}
    for path, data in sorted(source.items()):
        notice_path(path)
        if not isinstance(data, dict) or not isinstance(data.get("texts"), list) or \
                any(not isinstance(value, str) or not value for value in data["texts"]):
            raise ValueError(f"{source_path}: invalid page {path}")
        texts = set(data["texts"])
        if data.get("headline") is not None and data["headline"] not in texts:
            raise ValueError(f"{source_path}: headline is not in page texts: {path}")
        values = translation.get(path, {})
        if not isinstance(values, dict) or any(not isinstance(key, str) or
                                                not isinstance(value, str)
                                                for key, value in values.items()):
            raise ValueError(f"{translation_path}: invalid page translations for {path}")
        if set(values) - texts:
            raise ValueError(f"{translation_path}: translations not in source page {path}")
        compiled = {}
        for original in data["texts"]:
            translated = values[original] if original in values else ui.get(original)
            if translated:
                validate(original, translated, f"web_notice:{path}:{original[:30]}")
                if translated != original:
                    compiled[original] = translated
        if compiled:
            result["pages"][path] = compiled
    write_json(output_path, result)
    return result


def reuse_reviewed_new_titles(source_path: Path, translation_path: Path,
                             ui_path: Path, resolutions_path: Path,
                             previous_pages: dict) -> int:
    """Seed new, untranslated headings before machine memory or model requests."""
    if not resolutions_path.is_file():
        return 0
    review = read_json(resolutions_path)
    if review.get("schema_version") != 1 or not isinstance(review.get("resolutions"), dict):
        raise ValueError("Invalid notice title resolutions")
    source = read_json(source_path)["pages"]
    table = read_json(translation_path)
    translations = table.setdefault("pages", {})
    ui = read_json(ui_path)
    count = 0
    for original, decision in review["resolutions"].items():
        if not isinstance(decision, dict):
            continue  # The final title validator reports invalid decisions.
        candidates = decision.get("candidates")
        selected = decision.get("translation")
        if (not isinstance(candidates, dict) or not candidates
                or selected not in candidates.values()
                or decision.get("review_method") not in ("human", "assistant")
                or not isinstance(decision.get("reason"), str) or not decision["reason"].strip()
                or any(translations.get(page, {}).get(original, ui.get(original)) != value
                       for page, value in candidates.items())):
            continue
        validate(original, selected, f"notice_heading:{original}")
        for page, data in source.items():
            if (page in candidates or original in previous_pages.get(page, {}).get("texts", [])
                    or original not in data.get("texts", [])
                    or original not in [data.get("headline"), *data.get("entry_titles", [])]
                    or original in translations.get(page, {})):
                continue
            translations.setdefault(page, {})[original] = selected
            count += 1
    if count:
        write_json(translation_path, table)
    return count


def headline_aliases(source_path: Path, compiled: dict,
                     resolutions_path: Path | None = None,
                     conflict_report: Path | None = None) -> list[tuple[str, str]]:
    """Export unique or explicitly reviewed list titles, never pick by page order."""
    source = read_json(source_path).get("pages", {})
    candidates = {}
    for path, data in sorted(source.items()):
        for headline in dict.fromkeys([data.get("headline"), *data.get("entry_titles", [])]):
            if not headline:
                continue
            translated = compiled.get("pages", {}).get(path, {}).get(headline)
            if translated:
                candidates.setdefault(headline, {})[path] = translated
    resolutions = {}
    if resolutions_path is not None and resolutions_path.exists():
        reviewed = read_json(resolutions_path)
        if reviewed.get("schema_version") != 1 or not isinstance(reviewed.get("resolutions"), dict):
            raise ValueError("Invalid notice title resolutions")
        resolutions = reviewed["resolutions"]
        if set(resolutions) - set(candidates):
            raise ValueError("Stale notice title resolution: source is no longer a title")
    aliases, conflicts = [], []
    for original, pages in sorted(candidates.items()):
        variants = set(pages.values())
        selected = next(iter(variants)) if len(variants) == 1 else None
        if original in resolutions:
            decision = resolutions[original]
            reviewed_pages = decision.get("candidates") if isinstance(decision, dict) else None
            # A recurring announcement can add a page using the already reviewed
            # title. Preserve that decision only when every old candidate is
            # unchanged and every new page agrees with the selected translation.
            compatible = (isinstance(reviewed_pages, dict) and bool(reviewed_pages)
                          and all(pages.get(path) == value for path, value in reviewed_pages.items())
                          and all(value == decision.get("translation")
                                  for path, value in pages.items() if path not in reviewed_pages))
            if (not isinstance(decision, dict) or not compatible
                    or decision.get("translation") not in variants
                    or decision.get("review_method") not in ("assistant", "human")
                    or not isinstance(decision.get("reason"), str) or not decision["reason"].strip()):
                raise ValueError(f"Stale or invalid notice title resolution: {original}")
            selected = decision["translation"]
        if len(variants) > 1:
            conflicts.append({"source": original, "candidates": pages,
                              "selected": selected})
        if selected:
            validate(original, selected, f"notice_heading:{original}")
            aliases.append((original, selected))
    if conflict_report is not None:
        write_json(conflict_report, {"conflicts": conflicts,
                   "exported_titles": len(aliases),
                   "unresolved_titles": sum(item["selected"] is None for item in conflicts)})
    return aliases


def import_pages(pages_path: Path, source_path: Path, index_path: Path | None = None) -> int:
    """Import static Toolkit output, preserving historical pages and texts.

    Index title/listTitle may differ from the page H1. Keep these translations
    associated with their official page instead of assuming they are equal.
    """
    incoming = read_json(pages_path).get("pages")
    if not isinstance(incoming, dict) or not incoming:
        raise ValueError("Expected nonempty official notice pages")
    current = read_json(source_path)
    pages = current.setdefault("pages", {})
    if not isinstance(pages, dict):
        raise ValueError("Existing notice pages must be an object")
    for path, data in incoming.items():
        notice_path(path)
        if (not isinstance(data, dict) or not isinstance(data.get("title"), str)
                or not isinstance(data.get("texts"), list)
                or any(not isinstance(text, str) or not text for text in data["texts"])
                or (data.get("headline") and data["headline"] not in data["texts"])):
            raise ValueError(f"Invalid official notice page: {path}")
        previous = pages.get(path, {})
        merged = {"title": data["title"], "texts": list(dict.fromkeys(
            [*previous.get("texts", []), *data["texts"]]))}
        if data.get("headline") or previous.get("headline"):
            merged["headline"] = data.get("headline") or previous["headline"]
        if previous.get("entry_titles"):
            merged["entry_titles"] = previous["entry_titles"]
        pages[path] = merged
    if index_path is not None:
        index = read_json(index_path)
        for category in ("notices", "malfunctionNotices", "prNotices", "activityNotices"):
            rows = index.get(category, [])
            if not isinstance(rows, list):
                raise ValueError("Notice index category must be a list")
            for row in rows:
                address = urlsplit(row.get("linkDetail", ""))
                if address.scheme != "https" or address.netloc != NOTICE_HOST:
                    raise ValueError("Notice index contains a nonofficial page")
                path = notice_path(address.path)
                if path not in pages:
                    raise ValueError(f"Fetch the indexed notice before import: {path}")
                for field in ("title", "listTitle"):
                    title = row.get(field, "")
                    if not isinstance(title, str):
                        raise ValueError("Notice index title must be a string")
                    if title:
                        for key in ("texts", "entry_titles"):
                            values = pages[path].setdefault(key, [])
                            if title not in values:
                                values.append(title)
    write_json(source_path, current)
    return len(incoming)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    get = commands.add_parser("capture", help="Read the currently open official notice WebView")
    get.add_argument("--serial", required=True)
    get.add_argument("--adb", default=os.getenv("ADB", "adb"))
    get.add_argument("--snapshot", type=Path, default=SNAPSHOT)
    promote = commands.add_parser("import", help="Merge a reviewed capture into source JSON")
    promote.add_argument("--snapshot", type=Path, default=SNAPSHOT)
    promote.add_argument("--source", type=Path, default=SOURCE)
    batch = commands.add_parser("import-pages", help="Import Toolkit pages and optional index titles")
    batch.add_argument("--pages", type=Path, required=True)
    batch.add_argument("--index", type=Path)
    batch.add_argument("--source", type=Path, default=SOURCE)
    compile_cmd = commands.add_parser("build", help="Compile exact WebView text mappings")
    compile_cmd.add_argument("--source", type=Path, default=SOURCE)
    compile_cmd.add_argument("--translations", type=Path, default=TRANSLATION)
    compile_cmd.add_argument("--ui", type=Path, default=UI_TRANSLATION)
    compile_cmd.add_argument("--output", type=Path, default=OUTPUT)
    args = parser.parse_args()
    try:
        if args.command == "capture":
            data = capture(args.serial, args.adb, args.snapshot)
            print(f"Captured {len(data['nodes'])} notice text nodes from {data['path']} "
                  f"to {args.snapshot}")
        elif args.command == "import-pages":
            count = import_pages(args.pages, args.source, args.index)
            print(f"Imported {count} official notice pages into {args.source}")
        elif args.command == "import":
            path, count = import_snapshot(args.snapshot, args.source)
            print(f"Added {count} text nodes from {path} to {args.source}")
        else:
            result = build(args.source, args.translations, args.ui, args.output)
            print(f"Compiled {sum(map(len, result['pages'].values()))} WebView mappings "
                  f"across {len(result['pages'])} notice pages")
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Web notice workflow failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
