"""Build same-format event pickers from reviewed MasterDB relationships.

Only a single Story.advAssetIds script with play type 1 and a prefix accepted
by the runtime picker is eligible. MasterDB categories can reuse scripts from
other categories. LoveStoryEpisode.assetId names a presentation asset, not the TXT.
"""
import json
import re
from pathlib import Path


def build(root: Path, kind: str) -> bytes:
    if kind not in ("event", "love"):
        raise ValueError("Unsupported event story category")
    catalog = json.loads((root / "translations/story-metadata/catalog.json").read_text())
    stories = json.loads((root / "translations/master/zh-Hans/Story.json").read_text())
    event_path = root / "translations/master/zh-Hans/EventStory.json"
    events = json.loads(event_path.read_text()) if event_path.is_file() else {}
    groups_path = root / "translations/story-metadata/love-group-names.json"
    groups = json.loads(groups_path.read_text()).get("groups", {}) if groups_path.is_file() else {}
    emblems_path = root / "translations/master/zh-Hans/Emblem.json"
    emblems = json.loads(emblems_path.read_text()) if emblems_path.is_file() else {}
    result, seen = [], {}
    for story_id, value in catalog["stories"].items():
        if value["categories"] != [kind]:
            continue
        # Do not silently substitute one segment of a multi-asset/live story.
        if len(value["assets"]) != 1 or value["adv_play_types"] != [1]:
            continue
        if len(value["references"]) != 1:
            raise ValueError(f"Ambiguous story reference: {story_id}")
        reference = value["references"][0]
        asset = value["assets"][0]
        if (reference["category"] != kind or not isinstance(asset, str) or
                not re.fullmatch(r"[a-z][a-z0-9]*_[a-z0-9_]+", asset)):
            raise ValueError(f"Invalid story script: {story_id}")
        # EventStory can reference a main_* script. Existing plugins accept only
        # their picker's prefix; omit unsupported/short entries here without
        # excluding their scripts or MasterDB records from text translation.
        if not asset.startswith(kind + "_") or asset.endswith("_short"):
            continue
        file = f"adv_{asset}.txt"
        group = reference["event_id" if kind == "event" else "love_id"]
        group_name = (events.get(group, {}).get("description") or group) if kind == "event" else "特殊活动 · " + group.removeprefix("love-")
        if kind == "love" and group in groups:
            relation = groups[group]
            if relation["asset_id"] != group:
                raise ValueError(f"Invalid Love/Emblem relation: {group}")
            group_name = emblems.get(relation["emblem_id"], {}).get("name") or relation["original_name"]
            if group_name.startswith("「") and group_name.endswith("」"):
                group_name = group_name[1:-1]
        title = stories.get(story_id, {}).get("name", value["name"])
        branch = story_id.split("-", 5)[5] if kind == "love" and len(story_id.split("-", 5)) == 6 else ""
        words = {"happy": "幸福结局", "normal": "普通结局", "bad": "失败", "good": "成功",
                 "end": "结局", "magical": "魔法少女", "succubus": "魅魔",
                 "ngs": "渚", "smr": "堇", "chs": "千纱", "rui": "瑠依"}
        branch_label = " · ".join(words.get(word, word) for word in branch.split("-") if word)
        display_title = title.replace("\\n", " ").replace("\n", " ").strip()
        episode_prefix = f"第{reference['episode']}话"
        # MasterDB often uses only 第N話 as the title; show the episode once.
        if re.match(rf"^第\s*{reference['episode']}\s*[话話](?:$|[\s·:：])", display_title):
            label = re.sub(r"^第\s*\d+\s*[话話]", episode_prefix, display_title, count=1)
        else:
            label = episode_prefix + (f" · {display_title}" if display_title else "")
        label += f" · {branch_label}" if branch_label else ""
        if file in seen:
            existing = seen[file]
            if existing["group_id"] != group:
                raise ValueError(f"Resource shared across activities: {file}")
            existing["display_label"] += " / " + label + "（共用剧情）"
            existing["story_ids"].append(story_id)
            continue
        row = {"story_id": story_id, "story_ids": [story_id], "file": file, "group_id": group,
               "group_name": group_name, "episode": reference["episode"], "title": title,
               "display_label": label}
        result.append(row)
        seen[file] = row
    return (json.dumps({"schema_version": 1, "stories": result},
                       ensure_ascii=False, separators=(",", ":")) + "\n").encode()


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    for kind in ("event", "love"):
        target = root / f"app/src/main/assets/idoly-localify/{kind}-stories.json"
        target.write_bytes(build(root, kind))
        print(f"{target}: generated")
