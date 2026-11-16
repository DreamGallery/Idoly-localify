"""Build the bond story picker from Character.companyEnjoyStories relations."""
import json
import re
from pathlib import Path


def build(root: Path) -> bytes:
    data = root / "translations"
    catalog = json.loads((data / "story-metadata/catalog.json").read_text())["stories"]
    titles = json.loads((data / "master/zh-Hans/Story.json").read_text())
    names = json.loads((data / "glossary/names.json").read_text())
    # Older relationship catalogs stored character names only on card records.
    # Join by the explicit character ID, never by a filename abbreviation.
    characters = {}
    for value in catalog.values():
        for ref in value["references"]:
            if ref.get("character_id") and ref.get("character_name"):
                characters[ref["character_id"]] = ref
    rows, seen = [], set()
    for story_id, value in sorted(catalog.items()):
        if value["categories"] != ["company"]:
            continue
        if len(value["assets"]) != 1 or value["adv_play_types"] != [1]:
            continue
        asset = value["assets"][0]
        if not isinstance(asset, str) or not re.fullmatch(r"bond_[a-z0-9_]+", asset):
            continue
        if asset.endswith("_short"):
            continue
        if len(value["references"]) != 1:
            raise ValueError(f"Ambiguous bond story reference: {story_id}")
        ref = value["references"][0]
        owner = ref.get("character_id")
        episode = ref.get("episode")
        if (ref.get("category") != "company" or ref.get("table") != "Character" or
                not isinstance(owner, str) or not owner or
                type(episode) is not int or not 1 <= episode <= 20):
            raise ValueError(f"Invalid bond story relationship: {story_id}")
        file = f"adv_{asset}.txt"
        if file in seen:
            raise ValueError(f"Bond resource is shared by multiple stories: {file}")
        seen.add(file)
        character = characters.get(owner, {})
        original_name = ref.get("character_name") or character.get("character_name", "")
        name = names.get(original_name, original_name) or owner
        rows.append({"story_id": story_id, "file": file, "episode": episode,
                     "title": titles.get(story_id, {}).get("name", value["name"]),
                     "group_id": owner, "group_name": name,
                     "character_id": owner, "character_name": name,
                     "character_code": ref.get("character_code") or character.get("character_code", "")})
    rows.sort(key=lambda row: (row["character_id"], row["episode"], row["file"]))
    return (json.dumps({"schema_version": 1, "stories": rows}, ensure_ascii=False,
                       separators=(",", ":")) + "\n").encode()


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    target = root / "app/src/main/assets/idoly-localify/bond-stories.json"
    target.write_bytes(build(root))
    print(f"{target}: generated")
