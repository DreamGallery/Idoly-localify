"""Source-guarded spelling normalization for the IIIX agency name."""

import re


SOURCE = "プレタポルテ"
CANONICAL = "Prêt-à-Porter"
ALIASES = re.compile(
    r"高级定制时装|高级时装品牌|高级定制|高级成衣|成衣品牌|"
    r"普雷塔波尔特|普拉达波尔特|普雷塔波特|"
    r"Prêt-à-Porter|Prete-a-Porter|Pret-a-Porter|Prettaporte|PRET-A-PORTER|"
    r"PRÊT-À-PORTE|プレタポルテ",
    re.IGNORECASE,
)


def normalize(source: str, translation: str) -> str:
    if SOURCE not in source:
        return translation
    return ALIASES.sub(CANONICAL, translation)
