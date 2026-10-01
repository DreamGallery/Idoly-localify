"""Configurable checkout/data paths without adding sibling projects to sys.path."""
from __future__ import annotations

import importlib
from importlib.machinery import ModuleSpec
import os
from pathlib import Path
import sys
from types import ModuleType

ROOT = Path(__file__).resolve().parents[1]


def configured(name: str, default: Path) -> Path:
    value = os.environ.get(name)
    return Path(value).expanduser().resolve() if value else default


def toolkit_root(root: Path = ROOT) -> Path:
    return configured("IDOLY_TOOLKIT_DIR", root.parent / "HoshimiToolkit")


def translator_root(root: Path = ROOT) -> Path:
    return configured("IDOLY_TRANSLATOR_DIR", root.parent / "Hoshimi_Teleprompter")


def story_source_dir(root: Path = ROOT) -> Path:
    return configured("IDOLY_STORY_SOURCE_DIR", toolkit_root(root) / "cache/resource/adventure")


def story_csv_dir(root: Path = ROOT) -> Path:
    return configured("IDOLY_STORY_CSV_DIR", toolkit_root(root) / "cache/csv")


def story_output_dir(root: Path = ROOT) -> Path:
    return configured("IDOLY_STORY_OUTPUT_DIR", toolkit_root(root) / "cache/local-files/resource")


def asset_output_dir(root: Path = ROOT) -> Path:
    return configured("IDOLY_ASSET_OUTPUT_DIR", toolkit_root(root) / "cache/local-files/asset")


def load_module(project: str, module: str):
    """Load a checkout under its own package namespace, even if renamed/moved."""
    directory = {"HoshimiToolkit": toolkit_root,
                 "Hoshimi_Teleprompter": translator_root}[project]().resolve()
    if not (directory / "src").is_dir():
        raise ModuleNotFoundError(f"{project} checkout missing: {directory}", name=project)
    existing = sys.modules.get(project)
    if existing is None or list(getattr(existing, "__path__", [])) != [str(directory)]:
        for name in list(sys.modules):
            if name == project or name.startswith(project + "."):
                del sys.modules[name]
        package = ModuleType(project)
        package.__path__ = [str(directory)]
        package.__package__ = project
        package.__spec__ = ModuleSpec(project, loader=None, is_package=True)
        package.__spec__.submodule_search_locations = package.__path__
        sys.modules[project] = package
    return importlib.import_module(f"{project}.{module}")
