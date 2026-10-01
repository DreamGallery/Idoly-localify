"""Build a deterministic story category index from SolisClient MasterDB JSON.

The translation export intentionally keeps only text. This index preserves the
non-text relations needed to distinguish main, event, and Love stories by the
story ID and Octo resource asset ID.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from collections import defaultdict
from pathlib import Path
import tempfile


TABLES = ("Story", "StoryPart", "EventStory", "ExtraStory", "ExtraStoryPart",
          "LoveStoryEpisode", "Card", "Character")


def read_table(master_dir: Path, name: str) -> list[dict]:
    path = master_dir / f"{name}.json"
    rows = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows):
        raise ValueError(f"{path}: expected a list of records")
    return rows


def build_catalog(master_dir: Path) -> dict:
    if (master_dir / ".master-fetch-in-progress.json").is_file():
        raise ValueError(f"{master_dir}: MasterDB fetch is incomplete")
    state_path = master_dir / "master-fetch-state.json"
    if state_path.is_file():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        if (not isinstance(state, dict) or not state.get("version") or
                state.get("complete_version") != state["version"] or
                not isinstance(state.get("tables"), dict)):
            raise ValueError(f"{state_path}: a complete MasterDB fetch is required")
        for name in TABLES:
            path = master_dir / f"{name}.json"
            record = state["tables"].get(name)
            if (not isinstance(record, dict) or not path.is_file() or
                    record.get("sha256") != hashlib.sha256(path.read_bytes()).hexdigest()):
                raise ValueError(f"{path}: differs from the verified MasterDB fetch")
    data = {name: read_table(master_dir, name) for name in TABLES}
    stories = {}
    for row in data["Story"]:
        story_id = row["id"]
        if story_id in stories:
            raise ValueError(f"duplicate Story ID: {story_id}")
        stories[story_id] = {
            "name": row.get("name", ""),
            "adv_play_types": row.get("advPlayTypes", []),
            "assets": sorted(set(row.get("advAssetIds", []))),
            "categories": [],
            "references": [],
        }

    def reference(story_id: str, category: str, details: dict) -> None:
        if story_id not in stories:
            raise ValueError(f"{category} references unknown Story ID: {story_id}")
        entry = stories[story_id]
        if category not in entry["categories"]:
            entry["categories"].append(category)
        entry["references"].append({"category": category, **details})

    for part in data["StoryPart"]:
        part_id = part["id"]
        category = "main" if part_id.startswith("st-part-main-") else (
            "original" if part_id.startswith("st-part-original-") else "group"
        )
        for chapter in part.get("chapters", []):
            for episode in chapter.get("episodes", []):
                reference(episode["storyId"], category, {
                    "table": "StoryPart", "part_id": part_id,
                    "chapter": chapter["chapter"], "episode": episode["episode"],
                    "asset_id": episode.get("assetId", ""),
                })

    for event in data["EventStory"]:
        for episode in event.get("episodes", []):
            reference(episode["storyId"], "event", {
                "table": "EventStory", "event_id": event["id"],
                "episode": episode["episode"], "asset_id": episode.get("assetId", ""),
            })

    extra_parts = {row["id"]: row for row in data["ExtraStoryPart"]}
    for extra in data["ExtraStory"]:
        part_id = extra["extraStoryPartId"]
        if part_id not in extra_parts:
            raise ValueError(f"ExtraStory references unknown part: {part_id}")
        for episode in extra.get("episodes", []):
            reference(episode["storyId"], "extra", {
                "table": "ExtraStory", "extra_id": extra["id"],
                "part_id": part_id, "part_type": extra_parts[part_id].get("type"),
                "episode": episode["episode"], "asset_id": episode.get("assetId", ""),
            })

    for episode in data["LoveStoryEpisode"]:
        reference(episode["storyId"], "love", {
            "table": "LoveStoryEpisode", "love_id": episode["loveId"],
            "episode": episode["episode"], "asset_id": episode.get("assetId", ""),
        })

    characters = {row["id"]: row for row in data["Character"]}
    for card in data["Card"]:
        character = characters.get(card.get("characterId"), {})
        for number, episode in enumerate(card.get("stories", []), start=1):
            reference(episode["storyId"], "card", {
                "table": "Card", "card_id": card["id"], "episode": number,
                "character_id": character.get("id", ""),
                "character_code": character.get("assetId", ""),
                "character_name": character.get("name", ""),
            })

    for character in data["Character"]:
        for episode in character.get("companyEnjoyStories", []):
            reference(episode["storyId"], "company", {
                "table": "Character", "character_id": character["id"],
                "episode": episode["number"], "asset_id": episode.get("assetId", ""),
            })

    assets: dict[str, list[str]] = defaultdict(list)
    for story_id, entry in stories.items():
        entry["categories"].sort()
        entry["references"].sort(key=lambda value: json.dumps(value, sort_keys=True, ensure_ascii=False))
        for asset in entry["assets"]:
            assets[asset].append(story_id)
    return {
        "schema_version": 1,
        "stories": dict(sorted(stories.items())),
        "assets": {asset: sorted(ids) for asset, ids in sorted(assets.items())},
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("master_dir", type=Path, help="directory with full SolisClient MasterDB tables")
    parser.add_argument("output", type=Path, help="JSON catalog path")
    args = parser.parse_args()
    result = build_catalog(args.master_dir)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=args.output.parent,
                                     prefix=".story-catalog-", delete=False) as stream:
        temporary = Path(stream.name)
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    try:
        os.replace(temporary, args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"{len(result['stories'])} stories, {len(result['assets'])} resource assets -> {args.output}")


if __name__ == "__main__":
    main()
