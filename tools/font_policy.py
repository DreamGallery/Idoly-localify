"""Shared runtime-font / APK-payload contract for build and deployment."""

DEFAULT_FONT_MODE = "replace-ui"
INDEPENDENT_FONT_MODES = ("preserve-non-cjk", DEFAULT_FONT_MODE)
DEFAULT_PATCH_MODE = "module-font"
# Stable Unity asset ID retained for saved settings and older LSPatch packages.
# The actual font family is selected by config/font-source.json.
INDEPENDENT_FONT_NAME = "Localify-ResourceHanRoundedSC-ExtraBold"
SUPPORTED_REPLACEMENT_FAMILIES = (
    "Noto Sans CJK SC", "Resource Han Rounded SC", "Resource Han Rounded SC VF",
)
LEGACY_FONT_NAME = "FOT-UDKakugo_LargePro-DB"
FONT_PATCH_MODES = ("replace-fot", "add-font", "module-font")
FONT_MODES = ("fallback", "replace-translated", "replace-all") + INDEPENDENT_FONT_MODES


def required_patch_mode(font_mode: str) -> str:
    if font_mode not in FONT_MODES:
        raise ValueError(f"Unsupported runtime font mode: {font_mode!r}")
    return DEFAULT_PATCH_MODE if font_mode in INDEPENDENT_FONT_MODES else "replace-fot"


def validate_font_pair(font_mode: str, patch_mode: str, font_enabled: bool = True) -> None:
    expected = required_patch_mode(font_mode)
    if patch_mode not in FONT_PATCH_MODES:
        raise ValueError(f"Unsupported APK font patch mode: {patch_mode!r}")
    if type(font_enabled) is not bool:
        raise ValueError("Runtime font flag must be a boolean")
    compatible = patch_mode == expected or (patch_mode == "add-font" and font_mode in INDEPENDENT_FONT_MODES)
    if font_enabled and not compatible:
        raise ValueError(f"Runtime font mode {font_mode!r} requires APK font payload {expected!r}; "
                         f"selected {patch_mode!r}. Build/install the matching APK before deploying.")


def validate_dictionary_font_policy(dictionary: dict, patch_mode: str) -> None:
    if not isinstance(dictionary, dict):
        raise ValueError("Runtime dictionary must be an object")
    validate_font_pair(dictionary.get("font_mode"), patch_mode, dictionary.get("font"))
