#!/usr/bin/env python3
"""Add Simplified Chinese glyph data to a local copy of IDOLY PRIDE's Unity Font.

The input game APK and the font stay on the user's machine. This script never
packages or publishes a game APK or the original Fontworks font in this repo.
"""

import argparse
import copy
from functools import lru_cache
from io import BytesIO
from pathlib import Path
import zipfile

from fontTools.ttLib import TTCollection, TTFont
import UnityPy


FONT_NAME = "FOT-UDKakugo_LargePro-DB"
INDEPENDENT_FONT_NAME = "Localify-ResourceHanRoundedSC-ExtraBold"
FONT_MODES = ("replace-fot", "add-font")
SPLIT_PREFIX = "assets/bin/Data/sharedassets0.assets.split"
CHUNK_SIZE = 1024 * 1024
REQUIRED = "菜单账号简体中文"


@lru_cache(maxsize=65536)
def is_cjk_character(codepoint: int) -> bool:
    """CJK routing predicate; this does not subset the embedded font."""
    return (0x4E00 <= codepoint <= 0x9FFF or
            codepoint in (0x3005, 0x3007, 0x303B) or
            0x3400 <= codepoint <= 0x4DBF or 0xF900 <= codepoint <= 0xFAFF or
            0x20000 <= codepoint <= 0x2FA1F or 0x30000 <= codepoint <= 0x3347F or
            0x3041 <= codepoint <= 0x3096 or 0x3099 <= codepoint <= 0x309F or
            0x30A1 <= codepoint <= 0x30FA or 0x30FC <= codepoint <= 0x30FF or
            0x31F0 <= codepoint <= 0x31FF or 0xFF66 <= codepoint <= 0xFF9F or
            0x1AFF0 <= codepoint <= 0x1AFFF or 0x1B000 <= codepoint <= 0x1B16F)


def embedded_font_bytes(path: Path, face: int | None, mode: str) -> bytes:
    """Both patch modes embed the complete font for whole-label replacement."""
    if mode not in FONT_MODES:
        raise ValueError(f"Unknown font patch mode: {mode}")
    return font_bytes(path, face)


def replacement_family(data: bytes) -> str:
    family = TTFont(BytesIO(data))["name"].getDebugName(1)
    if not family:
        raise ValueError("Replacement font is missing its family name")
    return family


def verify_independent_dependencies(environment, replacement: bytes) -> None:
    objects = {obj.path_id: obj for obj in environment.objects}
    fonts = [obj for obj in environment.objects
             if obj.type.name == "Font" and obj.read().m_Name == INDEPENDENT_FONT_NAME]
    if len(fonts) != 1:
        raise ValueError("Expected one independent Chinese Font")
    font = fonts[0].read_typetree()
    if font["m_FontNames"] != [replacement_family(replacement)]:
        raise ValueError("Independent Font family differs from embedded font")
    def resolve(pointer, expected_type):
        obj = objects.get(pointer["m_PathID"]) if pointer["m_FileID"] == 0 else None
        if obj is None or obj.type.name != expected_type:
            raise ValueError(f"Independent Font has invalid {expected_type} reference")
        return obj
    material = resolve(font["m_DefaultMaterial"], "Material")
    texture = resolve(font["m_Texture"], "Texture2D")
    if any(obj.path_id != fonts[0].path_id and obj.type.name == "Font" and
           (obj.read_typetree()["m_DefaultMaterial"] == font["m_DefaultMaterial"] or
            obj.read_typetree()["m_Texture"] == font["m_Texture"])
           for obj in environment.objects):
        raise ValueError("Independent Font shares legacy material or texture")
    slots = dict(material.read_typetree()["m_SavedProperties"]["m_TexEnvs"])
    if slots.get("_MainTex", {}).get("m_Texture") != font["m_Texture"]:
        raise ValueError("Independent material does not reference its Font texture")


def font_bytes(path: Path, face: int | None) -> bytes:
    data = path.read_bytes()
    if data[:4] == b"ttcf":
        collection = TTCollection(BytesIO(data))
        if face is None:
            matches = []
            for index, font in enumerate(collection.fonts):
                families = {record.toUnicode() for record in font["name"].names if record.nameID == 1}
                if any("Noto Sans CJK SC" in family for family in families):
                    matches.append(index)
            if len(matches) != 1:
                raise ValueError("TTC 中无法唯一识别简中字体，请指定 --face")
            face = matches[0]
        font = TTFont(BytesIO(data), fontNumber=face)
        stream = BytesIO()
        font.save(stream)
        data = stream.getvalue()
    font = TTFont(BytesIO(data))
    missing = "".join(character for character in REQUIRED if ord(character) not in font.getBestCmap())
    if missing:
        raise ValueError(f"目标字体缺少测试用简体字：{missing}")
    return data


def split_names(apk: zipfile.ZipFile) -> list[str]:
    result = [name for name in apk.namelist() if name.startswith(SPLIT_PREFIX)]
    if not result or any(not name[len(SPLIT_PREFIX):].isdigit() for name in result):
        raise ValueError("APK 中找不到 sharedassets0.assets 分片")
    result.sort(key=lambda name: int(name[len(SPLIT_PREFIX):]))
    if [int(name[len(SPLIT_PREFIX):]) for name in result] != list(range(len(result))):
        raise ValueError("sharedassets0.assets 分片不连续")
    return result


def replace_font(serialized: bytes, replacement: bytes, mode: str = "replace-fot") -> bytes:
    if mode not in FONT_MODES:
        raise ValueError(f"Unknown font patch mode: {mode}")
    environment = UnityPy.load(serialized)
    candidates = [obj for obj in environment.objects
                  if obj.type.name == "Font" and obj.read().m_Name == FONT_NAME]
    if len(candidates) != 1:
        raise ValueError(f"预期找到一个 {FONT_NAME}，实际找到 {len(candidates)} 个；游戏版本可能已变化")
    original = candidates[0].read()
    original_font = TTFont(BytesIO(bytes(original.m_FontData)))
    if ord("单") in original_font.getBestCmap():
        raise ValueError("原字体已经含有简中字形，输入可能是已修改的 APK")
    if mode == "add-font":
        if any(obj.type.name == "Font" and obj.read().m_Name == INDEPENDENT_FONT_NAME
               for obj in environment.objects):
            raise ValueError("独立中文字体已经存在，输入可能是已修改的 APK")
        asset_file = candidates[0].assets_file
        original_payloads = {obj.path_id: obj.get_raw_data() for obj in environment.objects}
        source_font = candidates[0].read_typetree()
        def dependency(field, expected_type):
            pointer = source_font[field]
            obj = asset_file.objects.get(pointer["m_PathID"]) if pointer["m_FileID"] == 0 else None
            if obj is None or obj.type.name != expected_type:
                raise ValueError(f"Cannot clone original Font {field}: expected local {expected_type}")
            return obj
        source_material = dependency("m_DefaultMaterial", "Material")
        source_texture = dependency("m_Texture", "Texture2D")
        source_slots = dict(source_material.read_typetree()["m_SavedProperties"]["m_TexEnvs"])
        if source_slots.get("_MainTex", {}).get("m_Texture") != source_font["m_Texture"]:
            raise ValueError("Original Font material/texture dependency differs")
        texture_data = source_texture.read_typetree()
        if texture_data["m_Width"] or texture_data["m_Height"] or texture_data["image data"]:
            raise ValueError("Expected empty original dynamic Font texture")
        def clone_object(source):
            clone = copy.copy(source)
            clone.path_id = max(asset_file.objects) + 1
            clone.set_raw_data(source.get_raw_data())
            asset_file.objects[clone.path_id] = clone
            return clone
        clone = clone_object(candidates[0])
        material_clone = clone_object(source_material)
        texture_clone = clone_object(source_texture)
        independent = copy.deepcopy(source_font)
        independent.update(m_Name=INDEPENDENT_FONT_NAME, m_FontData=list(replacement),
                           m_FontNames=[replacement_family(replacement)],
                           m_DefaultMaterial={"m_FileID": 0, "m_PathID": material_clone.path_id},
                           m_Texture={"m_FileID": 0, "m_PathID": texture_clone.path_id})
        clone.save_typetree(independent)
        material_data = source_material.read_typetree()
        material_data["m_Name"] = INDEPENDENT_FONT_NAME + " Material"
        for name, slot in material_data["m_SavedProperties"]["m_TexEnvs"]:
            if name == "_MainTex":
                slot["m_Texture"] = independent["m_Texture"].copy()
        material_clone.save_typetree(material_data)
        texture_data["m_Name"] = INDEPENDENT_FONT_NAME + " Texture"
        texture_clone.save_typetree(texture_data)
        preloads = [obj for obj in environment.objects if obj.type.name == "PreloadData"]
        if len(preloads) != 1:
            raise ValueError("预期一个 PreloadData，不能安全添加独立字体")
        preload = preloads[0].read_typetree()
        preload["m_Assets"].append({"m_FileID": 0, "m_PathID": clone.path_id})
        preloads[0].save_typetree(preload)
        output = environment.file.save()
        checked = UnityPy.load(output)
        verify_independent_dependencies(checked, replacement)
        for obj in checked.objects:
            if obj.path_id in original_payloads and obj.path_id != preloads[0].path_id:
                if obj.get_raw_data() != original_payloads[obj.path_id]:
                    raise ValueError(f"添加中文字体修改了原资源 {obj.path_id}")
        return output
    original.m_FontData = list(replacement)
    original.save()
    return environment.file.save()


def patch_apk(source: Path, font: Path, output: Path, face: int | None = None,
              mode: str = "replace-fot") -> None:
    if source.resolve() == output.resolve():
        raise ValueError("输出路径不能覆盖原 APK")
    replacement = embedded_font_bytes(font, face, mode)
    with zipfile.ZipFile(source) as input_apk:
        old_names = split_names(input_apk)
        serialized = b"".join(input_apk.read(name) for name in old_names)
        patched = replace_font(serialized, replacement, mode)
        chunks = [patched[index:index + CHUNK_SIZE]
                  for index in range(0, len(patched), CHUNK_SIZE)]
        first = input_apk.getinfo(old_names[0])
        output.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(output, "w", allowZip64=True) as output_apk:
            for entry in input_apk.infolist():
                if entry.filename in old_names:
                    index = int(entry.filename[len(SPLIT_PREFIX):])
                    if index < len(chunks):
                        output_apk.writestr(entry, chunks[index])
                else:
                    output_apk.writestr(entry, input_apk.read(entry))
            for index in range(len(old_names), len(chunks)):
                entry = zipfile.ZipInfo(SPLIT_PREFIX + str(index), first.date_time)
                entry.compress_type = first.compress_type
                entry.external_attr = first.external_attr
                output_apk.writestr(entry, chunks[index])
    with zipfile.ZipFile(output) as check:
        rebuilt = b"".join(check.read(name) for name in split_names(check))
        if rebuilt != patched:
            raise ValueError("输出 APK 字体资源校验失败")
    print(f"已生成 {output}；字体数据 {len(replacement):,} 字节，Unity 资源 {len(chunks)} 个分片")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base_apk", type=Path, help="从自己的游戏安装中导出的原版 base.apk")
    parser.add_argument("cjk_font", type=Path, help="Noto Sans CJK SC OTF，或 Android 的 NotoSansCJK-Regular.ttc")
    parser.add_argument("output_apk", type=Path, help="输出的本地测试 APK")
    parser.add_argument("--face", type=int, help="TTC 字体面索引；省略时自动寻找 Noto Sans CJK SC")
    parser.add_argument("--font-patch-mode", choices=FONT_MODES, default="replace-fot",
                        help="add-font 保留原字体并添加独立中文 Font")
    args = parser.parse_args()
    patch_apk(args.base_apk, args.cjk_font, args.output_apk, args.face, args.font_patch_mode)


if __name__ == "__main__":
    main()
