#!/usr/bin/env python3
"""Build locally signed IDOLY PRIDE test APKs from the user's own game files."""

import argparse
import hashlib
from importlib.metadata import version as dependency_version
from io import BytesIO
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from zipfile import ZipFile, BadZipFile

from font_policy import DEFAULT_PATCH_MODE, FONT_PATCH_MODES, validate_dictionary_font_policy

ROOT = Path(__file__).resolve().parents[1]
GAME_PACKAGE = "game.qualiarts.idolypride"
MODULE_PACKAGE = "io.github.dreamgallery.idoly.localify"
TESTED_GAME_VERSION = "6.0.2"


def run(*command: str, env: dict[str, str]) -> None:
    subprocess.run(command, check=True, env=env)


def output(*command: str, env: dict[str, str]) -> str:
    return subprocess.run(command, check=True, capture_output=True,
                          text=True, env=env).stdout


def package_info(aapt: str, apk: Path, env: dict[str, str]) -> dict[str, str]:
    badging = output(aapt, "dump", "badging", str(apk), env=env)
    first = badging.splitlines()[0] if badging else ""
    if not first.startswith("package: "):
        raise ValueError(f"Cannot read package metadata from {apk}")
    fields = dict(re.findall(r"([A-Za-z]+)='([^']*)'", first))
    if not fields.get("name") or not fields.get("versionCode"):
        raise ValueError(f"Missing package name or versionCode in {apk}")
    return fields


def signer_certificates(signer: str, apk: Path, env: dict[str, str]) -> set[str]:
    details = output(signer, "verify", "--print-certs", str(apk), env=env)
    certificates = {match.group(1).lower() for line in details.splitlines()
                    if not line.startswith("Source Stamp")
                    if (match := re.search(
                        r"Signer: certificate SHA-256 digest: ([0-9a-fA-F]{64})$", line))}
    if not certificates:
        raise ValueError(f"Cannot read APK signing certificate from {apk}")
    return certificates


def preflight(base: Path, split: Path, module: Path | None, aapt: str,
              signer: str, env: dict[str, str], *, allow_untested_version: bool) -> str:
    base_info = package_info(aapt, base, env)
    split_info = package_info(aapt, split, env)
    if base_info["name"] != GAME_PACKAGE or base_info.get("split"):
        raise ValueError(f"Base APK must be the unsplit {GAME_PACKAGE} package")
    if split_info["name"] != GAME_PACKAGE or split_info.get("split") != "config.arm64_v8a":
        raise ValueError(f"Split APK must be {GAME_PACKAGE} config.arm64_v8a")
    if base_info["versionCode"] != split_info["versionCode"]:
        raise ValueError("Base and arm64 split APK versionCode values differ")
    if (split_info.get("versionName") and
            split_info["versionName"] != base_info.get("versionName")):
        raise ValueError("Base and arm64 split APK versionName values differ")
    version = base_info.get("versionName", "")
    if version != TESTED_GAME_VERSION and not allow_untested_version:
        raise ValueError(f"Game version {version or '(missing)'} has not been tested; "
                         "pass --allow-untested-version only after checking hooks and fonts")
    if signer_certificates(signer, base, env) != signer_certificates(signer, split, env):
        raise ValueError("Base and arm64 split APK signing certificates differ")
    if module is not None and package_info(aapt, module, env)["name"] != MODULE_PACKAGE:
        raise ValueError(f"Module APK must be {MODULE_PACKAGE}")
    return f"{version} (versionCode {base_info['versionCode']})"


def verify_font(apk: Path | BytesIO, font: Path, mode: str = "replace-fot",
                original_apk: Path | None = None) -> None:
    import UnityPy
    from patch_font import FONT_NAME, INDEPENDENT_FONT_NAME, embedded_font_bytes, split_names

    with ZipFile(apk) as wrapped:
        serialized = b"".join(wrapped.read(name) for name in split_names(wrapped))
    environment = UnityPy.load(serialized)
    expected_name = INDEPENDENT_FONT_NAME if mode == "add-font" else FONT_NAME
    candidates = [obj.read() for obj in environment.objects
                  if obj.type.name == "Font" and obj.read().m_Name == expected_name]
    if len(candidates) != 1 or bytes(candidates[0].m_FontData) != embedded_font_bytes(font, None, mode):
        raise ValueError("LSPatch output does not contain the requested Chinese font")
    if mode == "add-font":
        from patch_font import verify_independent_dependencies
        verify_independent_dependencies(environment, bytes(candidates[0].m_FontData))
    if mode == "add-font" and original_apk is not None:
        with ZipFile(original_apk) as original:
            original_data = b"".join(original.read(name) for name in split_names(original))
        source = UnityPy.load(original_data)
        output_objects = {obj.path_id: obj for obj in environment.objects}
        for obj in source.objects:
            if obj.type.name != "PreloadData":
                preserved = output_objects.get(obj.path_id)
                if preserved is None or preserved.get_raw_data() != obj.get_raw_data():
                    raise ValueError(f"Independent font changed original resource {obj.path_id}")
        independent_ids = {obj.path_id for obj in environment.objects
                           if obj.type.name == "Font" and obj.read().m_Name == expected_name}
        preloads = [obj.read_typetree() for obj in environment.objects
                    if obj.type.name == "PreloadData"]
        if len(preloads) != 1 or not any(
                ref["m_FileID"] == 0 and ref["m_PathID"] in independent_ids
                for ref in preloads[0]["m_Assets"]):
            raise ValueError("Independent font is missing its preload reference")


def verify_module_font_policy(module: Path, mode: str) -> None:
    try:
        with ZipFile(module) as package:
            dictionary = json.loads(package.read("assets/idoly-localify/dict.json").decode("utf-8"))
    except (OSError, BadZipFile, KeyError, ValueError, UnicodeError) as error:
        raise ValueError("Module APK must contain a valid assets/idoly-localify/dict.json") from error
    validate_dictionary_font_policy(dictionary, mode)


def verify_embedded(base: Path, module: Path, font: Path, mode: str = "replace-fot",
                    original_apk: Path | None = None) -> None:
    """Check the signed output, not only LSPatch's intermediate files."""
    verify_module_font_policy(module, mode)
    with ZipFile(base) as patched:
        module_name = f"assets/lspatch/modules/{MODULE_PACKAGE}.apk"
        if patched.read(module_name) != module.read_bytes():
            raise ValueError("LSPatch embedded module differs from the built module APK")
        origin = patched.read("assets/lspatch/origin.apk")
    if mode in ("module-font", "add-font"):
        verify_module_font_bundle(module, font)
    if mode == "module-font":
        if original_apk is None:
            raise ValueError("module-font verification requires original APK")
        from patch_font import split_names
        with ZipFile(BytesIO(origin)) as output_apk, ZipFile(original_apk) as original:
            if b"".join(output_apk.read(n) for n in split_names(output_apk)) != b"".join(original.read(n) for n in split_names(original)):
                raise ValueError("module-font build changed the original font resource")
    else:
        verify_font(BytesIO(origin), font, mode, original_apk)


def verify_module_font_bundle(module: Path, font: Path) -> None:
    from build_font_bundle import verify
    from patch_font import font_bytes
    try:
        with ZipFile(module) as package:
            data = package.read("assets/idoly-localify/resource-han-rounded.bundle")
        verify(data, font_bytes(font, None))
    except (OSError, KeyError, BadZipFile) as error:
        raise ValueError("Module APK must contain a valid standalone font bundle") from error


def audit_independent_font(font: Path) -> dict:
    """Check complete replacement-font coverage for labels selected by the runtime CJK rule."""
    from fontTools.ttLib import TTFont
    from font_coverage import audit_codepoints, translated_fields, source_fields
    from patch_font import embedded_font_bytes
    cmap = TTFont(BytesIO(embedded_font_bytes(font, None, "add-font"))).getBestCmap()
    def fields():
        yield from translated_fields(ROOT)
        yield from source_fields(ROOT)
    report = audit_codepoints(set(cmap), fields())
    report.update(source_fields_included=True, coverage_scope="complete-replacement-font",
                  routing_policy="module-dictionary-font-mode-and-ui_font_policy",
                  original_fonts="preserved; runtime-validation-pending")
    return report


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def font_cache_inputs(base: Path, font: Path, mode: str = "replace-fot") -> dict:
    from patch_font import font_bytes
    if mode == "replace-fot":
        embedded = font_bytes(font, None)
    else:
        from patch_font import embedded_font_bytes
        embedded = embedded_font_bytes(font, None, mode)
    return {"schema_version": 2, "font_patch_mode": mode, "base_sha256": file_sha256(base),
            "font_sha256": file_sha256(font),
            "embedded_font_sha256": hashlib.sha256(embedded).hexdigest(),
            "patcher_sha256": file_sha256(Path(__file__).with_name("patch_font.py")),
            "builder_sha256": file_sha256(Path(__file__)),
            "python_version": list(sys.version_info[:3]),
            "UnityPy_version": dependency_version("UnityPy"),
            "fonttools_version": dependency_version("fonttools")}


def prepare_font_apk(base: Path, font: Path, destination: Path, cache: Path | None,
                     aapt: str, env: dict[str, str], mode: str = "replace-fot") -> bool:
    """Return whether a verified, source-bound local cache supplied the font APK."""
    from patch_font import patch_apk
    def patch_candidate() -> None:
        if mode == "replace-fot":
            patch_apk(base, font, destination)
        else:
            patch_apk(base, font, destination, mode=mode)

    if cache is None:
        patch_candidate()
        return False
    inputs = font_cache_inputs(base, font, mode)
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    entry = cache / key
    expected_info = package_info(aapt, base, env)

    def validate_candidate(path: Path) -> None:
        if package_info(aapt, path, env) != expected_info:
            raise ValueError("Cached font APK package metadata differs from the original base")
        verify_font(path, font, mode, base)

    if entry.exists():
        try:
            manifest = json.loads((entry / "manifest.json").read_text(encoding="utf-8"))
            if manifest.get("inputs") != inputs:
                raise ValueError("Font cache source binding differs")
            # Check the local copy that will actually be signed, not a mutable cache path.
            shutil.copyfile(entry / "base-font.apk", destination)
            if file_sha256(destination) != manifest.get("apk_sha256"):
                raise ValueError("Font cache APK checksum differs")
            validate_candidate(destination)
        except (OSError, ValueError, KeyError) as error:
            raise ValueError(f"Invalid font cache {entry}; remove this cache entry and retry: {error}") from error
        if font_cache_inputs(base, font, mode) != inputs:
            raise ValueError("Base, font or patcher changed while reading font cache")
        print(f"已复用经过校验的字体缓存：{entry}")
        return True

    patch_candidate()
    validate_candidate(destination)
    if font_cache_inputs(base, font, mode) != inputs:
        raise ValueError("Base, font or patcher changed while creating font cache")
    cache.mkdir(parents=True, exist_ok=True)
    # A complete directory appears atomically; interrupted writes are never cache hits.
    with tempfile.TemporaryDirectory(prefix=".font-cache-", dir=cache) as temporary:
        staged = Path(temporary) / "entry"
        staged.mkdir()
        shutil.copyfile(destination, staged / "base-font.apk")
        manifest = {"inputs": inputs, "apk_sha256": file_sha256(staged / "base-font.apk")}
        (staged / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        try:
            staged.rename(entry)
        except OSError:
            if not entry.is_dir():
                raise
            # A concurrent build may have published this key; our local candidate is verified.
    print(f"已生成字体缓存：{entry}")
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True, help="原版 base.apk")
    parser.add_argument("--split", type=Path, required=True, help="原版 arm64 拆分 APK")
    parser.add_argument("--font", type=Path, help="complete-coverage SC font in OTF/TTF/TTC format")
    parser.add_argument("--font-patch-mode", choices=FONT_PATCH_MODES,
                        default=DEFAULT_PATCH_MODE, help="默认 module-font 由插件提供字体；兼容 add-font 向游戏资源加入字体")
    parser.add_argument("--lspatch", type=Path, help="JingMatrix LSPatch 的 JAR")
    parser.add_argument("--module", type=Path,
                        default=ROOT / "app/build/outputs/apk/debug/app-debug.apk")
    parser.add_argument("--keystore", type=Path, default=Path.home() / ".android/debug.keystore")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".analysis/signed")
    parser.add_argument("--font-cache", type=Path,
                        help="复用按原版 APK、字体、工具版本哈希绑定的本地字体缓存")
    parser.add_argument("--image-patches", type=Path,
                        help="可选的内置图集 PNG 替换清单；校验原资源及允许修改的区域")
    parser.add_argument("--preflight-only", action="store_true",
                        help="Check package, version and signatures without rebuilding")
    parser.add_argument("--allow-untested-version", action="store_true",
                        help="Allow testing a game version newer than the verified baseline")
    args = parser.parse_args()
    if not args.preflight_only and (args.font is None or args.lspatch is None):
        parser.error("构建测试 APK 需要 --font 和 --lspatch")
    required = (args.base, args.split) if args.preflight_only else (
        args.base, args.split, args.font, args.lspatch, args.module, args.keystore)
    for path in required:
        if not path.is_file():
            parser.error(f"找不到 {path}")
    sdk = Path(os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or "")
    if not sdk.is_dir():
        parser.error("请设置 ANDROID_HOME 或 ANDROID_SDK_ROOT")
    signer_candidates = sorted((sdk / "build-tools").glob("*/apksigner"))
    aapt_candidates = sorted((sdk / "build-tools").glob("*/aapt"))
    if not signer_candidates or not aapt_candidates:
        parser.error("Android SDK build-tools 中缺少 apksigner 或 aapt")
    signer = str(signer_candidates[-1])
    aapt = str(aapt_candidates[-1])
    java_home = os.environ.get("JAVA_HOME")
    java = str(Path(java_home) / "bin/java") if java_home else "java"
    env = os.environ.copy()
    env.setdefault("IDOLY_KEYSTORE_PASSWORD", "android")
    try:
        verified_version = preflight(args.base, args.split,
                                     None if args.preflight_only else args.module,
                                     aapt, signer, env,
                                     allow_untested_version=args.allow_untested_version)
    except ValueError as error:
        parser.error(str(error))
    print(f"原版 APK 预检通过：{verified_version}")
    if args.preflight_only:
        return
    try:
        verify_module_font_policy(args.module, args.font_patch_mode)
        if args.font_patch_mode in ("module-font", "add-font"):
            verify_module_font_bundle(args.module, args.font)
    except ValueError as error:
        parser.error(str(error))
    from font_coverage import audit_font, require_coverage
    from localize import write_json

    try:
        coverage = (audit_independent_font(args.font) if args.font_patch_mode in ("add-font", "module-font")
                    else audit_font(args.font, ROOT))
        require_coverage(coverage)
    except (OSError, ValueError) as error:
        parser.error(f"字体覆盖检查失败：{error}")
    write_json(args.output_dir / "font-coverage.json", coverage)
    source_note = "译文及游戏原文" if coverage["source_fields_included"] else "译文"
    if args.font_patch_mode in ("add-font", "module-font"):
        print(f"完整替换字体已覆盖 {coverage['fields']:,} 个{source_note}输入中的全部可见字符；"
              "实际字体选择遵循模块的 font_mode 与控件规则，显示效果待实机验证")
    else:
        print(f"字体已覆盖 {coverage['fields']:,} 个{source_note}输入中的全部可见字符")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="idoly-build-") as temporary:
        work = Path(temporary)
        modified = work / "base-font.apk"
        signed_modified = work / "base-font-signed.apk"
        try:
            if args.font_patch_mode == "module-font":
                shutil.copyfile(args.base, modified)
            else:
                prepare_font_apk(args.base, args.font, modified, args.font_cache, aapt, env, args.font_patch_mode)
        except ValueError as error:
            parser.error(str(error))
        if args.image_patches is not None:
            from patch_images import patch_apk as patch_image_apk
            image_modified = work / "base-images.apk"
            patch_image_apk(modified, args.image_patches, image_modified)
            modified = image_modified
        sign_args = ("--ks", str(args.keystore), "--ks-pass", "env:IDOLY_KEYSTORE_PASSWORD",
                     "--key-pass", "env:IDOLY_KEYSTORE_PASSWORD")
        run(signer, "sign", *sign_args, "--out", str(signed_modified), str(modified), env=env)
        patched_dir = work / "lspatch"
        patched_dir.mkdir()
        run(java, "-Xmx4g", "-jar", str(args.lspatch), "-f", "-d", "-l", "2",
            "-m", str(args.module), "-o", str(patched_dir),
            str(signed_modified), str(args.split), env=env)
        base_matches = list(patched_dir.glob("base-font-signed-*-lspatched.apk"))
        split_matches = list(patched_dir.glob(f"{args.split.stem}-*-lspatched.apk"))
        if len(base_matches) != 1 or len(split_matches) != 1:
            raise RuntimeError("LSPatch 输出文件不符合预期")
        base_out = args.output_dir / "base.apk"
        split_out = args.output_dir / args.split.name
        run(signer, "sign", *sign_args, "--out", str(base_out), str(base_matches[0]), env=env)
        run(signer, "sign", *sign_args, "--out", str(split_out), str(split_matches[0]), env=env)
        run(signer, "verify", str(base_out), env=env)
        run(signer, "verify", str(split_out), env=env)
        if signer_certificates(signer, base_out, env) != signer_certificates(
                signer, split_out, env):
            raise ValueError("Signed test base and split APK certificates differ")
        verify_embedded(base_out, args.module, args.font, args.font_patch_mode, args.base)
        if args.image_patches is not None:
            from patch_images import verify_apk as verify_image_apk
            with ZipFile(base_out) as wrapped:
                verify_image_apk(BytesIO(wrapped.read("assets/lspatch/origin.apk")),
                                 args.image_patches)
    print(f"测试 APK：{base_out}、{split_out}")
    print("仅安装到单独的测试模拟器；与原版游戏签名不同。")


if __name__ == "__main__":
    main()
