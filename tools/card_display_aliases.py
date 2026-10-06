"""Compile exact card/name labels, including partially localized gacha history."""

from collections import defaultdict
import re


CARD_ID = re.compile(r"card-([a-z]+)-[0-9]{2}-[a-z0-9]+-[0-9]{2}\Z")
SEPARATOR = "\u3000"


def derive(cards: dict, translated: dict, catalog: dict, names: dict) -> dict[str, str]:
    # The story index preserves the official Card -> Character relationship.
    # Its character codes also cover low-rarity cards without card stories.
    by_card, by_code = defaultdict(set), defaultdict(set)
    for story in catalog.get("stories", {}).values():
        for ref in story.get("references", []):
            if ref.get("category") != "card" or not ref.get("character_name"):
                continue
            name = ref["character_name"]
            by_card[ref["card_id"]].add(name)
            code = ref.get("character_code")
            if code and ref.get("character_id") == f"char-{code}":
                by_code[code].add(name)
    result = {}
    for card_id, fields in cards.items():
        original_title = fields.get("name")
        if not original_title:
            continue
        candidates = by_card.get(card_id, set())
        match = CARD_ID.fullmatch(card_id)
        if match:
            candidates = candidates | by_code.get(match[1], set())
        if len(candidates) > 1:
            raise ValueError(f"Conflicting card character relationship: {card_id}")
        if not candidates:
            continue  # Never guess a character for an unknown card-ID format.
        original_name = next(iter(candidates))
        target_name = names.get(original_name, original_name)
        target_title = translated.get(card_id, {}).get("name") or original_title
        if any(not isinstance(value, str) or not value or
               any(char in value for char in "\r\n\0")
               for value in (original_title, target_title, original_name, target_name)):
            raise ValueError(f"Invalid single-line card label: {card_id}")
        # Only exact known title/name pairs are generated. No suffix matching,
        # free-form player names, dates, or arbitrary numbers are interpreted.
        for suffix in ("", *(f"{SEPARATOR}★{star}" for star in range(1, 6))):
            target = f"{target_title}{SEPARATOR}{target_name}{suffix}"
            for title in {original_title, target_title}:
                for name in {original_name, target_name}:
                    source = f"{title}{SEPARATOR}{name}{suffix}"
                    if source == target:
                        continue
                    if source in result and result[source] != target:
                        raise ValueError(f"Conflicting card display alias: {source}")
                    result[source] = target
    return dict(sorted(result.items()))
