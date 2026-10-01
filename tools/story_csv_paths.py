"""Pure ADV CSV classification, matching HoshimiAdvSync without its runtime dependency."""
from pathlib import Path
import re

GROUP_NAMES = {
    "liz": "LizNoir",
    "mna": "長瀬麻奈",
    "moon": "月のテンペスト",
    "sun": "サニーピース",
    "thrx": "ⅢX",
    "tri": "TRINITYAiLE",
}
SAFE_NAME = re.compile(r"^adv_[a-z0-9][a-z0-9_-]*\.txt$")
SAFE_CODE = re.compile(r"^[a-z][a-z0-9]*$")


def classify(name: str) -> Path:
    """Classify an ADV filename without changing the original basename."""
    if not SAFE_NAME.fullmatch(name) or Path(name).name != name:
        raise ValueError(f"Unsafe adventure filename: {name!r}")
    parts = name[:-4].split("_")
    category = parts[1]
    filename = name.replace(".txt", ".csv")
    if category == "card" and len(parts) >= 5 and SAFE_CODE.fullmatch(parts[2]):
        numbered = parts[:-1] if parts[-1] == "short" else parts
        if len(numbered) >= 5 and numbered[-2].isdigit():
            return Path("card") / parts[2] / numbered[-2] / filename
    if category == "bond" and len(parts) >= 3 and SAFE_CODE.fullmatch(parts[2]):
        return Path("bond") / parts[2] / filename
    if category in {"hbd", "userhbd"}:
        if (len(parts) >= 4 and parts[2].isdigit() and
                SAFE_CODE.fullmatch(parts[3])):
            return Path("hbd") / parts[3] / parts[2] / filename
        return Path("hbd") / filename
    if category == "event" and len(parts) >= 4:
        if parts[2] in {"cmn", "excursion"} and SAFE_CODE.fullmatch(parts[3]):
            group = Path("event") / parts[2] / parts[3]
            if len(parts) >= 6 and parts[-2].isdigit():
                group /= parts[-2]
            return group / filename
        if (re.fullmatch(r"\d{4}", parts[2]) and
                re.fullmatch(r"\d{2}", parts[3])):
            group = Path("event") / parts[2] / parts[3]
            if len(parts) == 5 and SAFE_CODE.fullmatch(parts[4]):
                return group / parts[4] / filename
            if len(parts) >= 6 and parts[-2].isdigit():
                group /= parts[-2]
            return group / filename
    if category == "group" and len(parts) >= 5:
        if SAFE_CODE.fullmatch(parts[2]) and parts[-2].isdigit():
            return Path("group") / GROUP_NAMES.get(parts[2], parts[2]) / parts[-2] / filename
    if (category == "love" and len(parts) >= 5 and
            re.fullmatch(r"\d{4}", parts[2]) and parts[-2].isdigit()):
        return Path("love") / parts[2] / parts[-2] / filename
    if category == "main" and len(parts) >= 5:
        if parts[2].isdigit() and parts[3].isdigit():
            return Path("main") / parts[2] / parts[3] / filename
    return Path("hbd" if category == "userhbd" else category) / filename

