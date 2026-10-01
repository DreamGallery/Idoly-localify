"""Generate the small card-only picker index from reviewed story metadata."""

import json
from pathlib import Path


def build(root: Path) -> bytes:
    catalog = json.loads((root / "translations/story-metadata/catalog.json").read_text(encoding="utf-8"))
    stories = json.loads((root / "translations/master/zh-Hans/Story.json").read_text(encoding="utf-8"))
    cards = json.loads((root / "translations/master/zh-Hans/Card.json").read_text(encoding="utf-8"))
    original_cards_path = root / "translations/master/orig/Card.json"
    original_cards = (json.loads(original_cards_path.read_text(encoding="utf-8"))
                      if original_cards_path.is_file() else cards)
    names = json.loads((root / "translations/glossary/names.json").read_text(encoding="utf-8"))
    result = []
    seen = set()
    for story_id, value in catalog["stories"].items():
        if value["categories"] != ["card"]:
            continue
        if len(value["assets"]) != 1 or len(value["references"]) != 1:
            raise ValueError(f"Card story has ambiguous resource: {story_id}")
        reference = value["references"][0]
        asset = value["assets"][0]
        card_id = reference["card_id"]
        if (reference["category"] != "card" or not asset.startswith("card_") or
                not asset.replace("_", "").isalnum() or card_id not in original_cards):
            raise ValueError(f"Card story has invalid resource or card: {story_id}")
        # New official cards can precede their translation. Preserve the valid
        # entry using its official text until reviewed text becomes available.
        card = {**original_cards[card_id], **cards.get(card_id, {})}
        file = f"adv_{asset}.txt"
        if file in seen:
            raise ValueError(f"Card resource is shared by multiple stories: {file}")
        seen.add(file)
        result.append({"story_id": story_id, "file": file, "card_id": card_id,
                       "episode": reference["episode"],
                       "character_id": reference.get("character_id", ""),
                       "character_code": reference.get("character_code", ""),
                       "character_name": names.get(reference.get("character_name", ""),
                                                   reference.get("character_name", "")),
                       "title": stories.get(story_id, {}).get("name", value["name"]),
                       "card_name": card.get("name", ""),
                       "description": card.get("description", "")})
    return (json.dumps({"schema_version": 1, "stories": result},
                       ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    target = root / "app/src/main/assets/idoly-localify/card-stories.json"
    target.write_bytes(build(root))
    print(f"{target}: generated")
