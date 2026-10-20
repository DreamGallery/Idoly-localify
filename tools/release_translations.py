#!/usr/bin/env python3
"""Trusted scheduled release driver. Public collaboration files are data only.

Run from a reviewed, pinned private source checkout. --dry-run prepares and
validates all assets but does not upload, publish, or write Git history.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]

def run(command, cwd=None, capture=False):
    return subprocess.run([str(v) for v in command], cwd=cwd, check=True,
                          text=True, capture_output=capture).stdout

def release_exists(repo, tag):
    from release_followup import GitHub, find_release
    try:
        return find_release(GitHub(repo), tag)
    except RuntimeError:
        raise RuntimeError('Cannot inspect releases; check GitHub access before retrying') from None

TEXT_RELEASE_NOTES = '自动校验的本地化文本资源，更新后需重启游戏以载入 MasterDB 更新（如有）。\n'


def plugin_release_notes(project, version):
    """Require notes for the exact APK version from the trusted source checkout."""
    release = json.loads((project / 'config/plugin-release.json').read_text(encoding='utf-8'))
    notes = release.get('notes')
    if (release.get('version') != version or not isinstance(notes, list) or not notes
            or any(not isinstance(note, str) or not note.strip() for note in notes)):
        raise ValueError(f'Missing or invalid plugin release notes for {version}')
    return (f'## 插件 {version}\n\n' + '\n'.join('- ' + note.strip() for note in notes) +
            '\n\n### 安装与更新\n\n'
            '- LSPosed：安装本页 APK，启用模块并将作用域设为《偶像荣耀》，然后重启游戏。\n'
            '- LSPatch：使用新版插件重新修补游戏 APK，再覆盖安装。\n'
            '- 本页附件是本地化插件；文本资源请在游戏内通过插件更新。\n')


def publish(repo, tag, assets, latest, dry_run, *, notes=None, target_commit=None):
    if dry_run:
        return {'tag': tag, 'assets': [p.name for p in assets], 'published': False}
    if target_commit is not None:
        from release_followup import GitHub
        if not re.fullmatch(r'[0-9a-f]{40,64}', target_commit):
            raise ValueError('Text publication requires an exact main commit')
        client = GitHub(repo)
        tagged_ref = client.request('GET', f'git/ref/tags/{tag}', missing_ok=True)
        tagged = client.request('GET', f'commits/{tag}') if tagged_ref else None
        if tagged is not None and tagged.get('sha') != target_commit:
            raise ValueError('Existing text tag points to a different main commit')
    existing = release_exists(repo, tag)
    missing = list(assets)
    if existing and target_commit and existing.get('draft') and existing.get('target_commitish') != target_commit:
        raise ValueError('Existing text draft targets a different main commit')
    if existing:
        remote_names = {item['name'] for item in existing.get('assets', [])}
        expected_names = {asset.name for asset in assets}
        if remote_names - expected_names:
            raise ValueError(f'Unexpected assets on release {tag}')
        missing = [asset for asset in assets if asset.name not in remote_names]
        if missing and not existing.get('draft'):
            raise ValueError(f'Published release is incomplete: {tag}')
        with tempfile.TemporaryDirectory(prefix='idoly-published-') as temporary:
            for asset in assets:
                if asset.name not in remote_names: continue
                run(['gh', 'release', 'download', tag, '--repo', repo,
                     '--pattern', asset.name, '--dir', temporary])
                if digest(Path(temporary) / asset.name) != digest(asset):
                    raise ValueError(f'Existing release asset differs: {tag}/{asset.name}')
        if not existing.get('draft'):
            if latest:
                run(['gh', 'release', 'edit', tag, '--repo', repo, '--latest=true'])
            return {'tag': tag, 'unchanged': True}
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', delete=False) as stream:
        stream.write(TEXT_RELEASE_NOTES if notes is None else notes)
        notes_path = Path(stream.name)
    try:
        if not existing:
            run(['gh', 'release', 'create', tag, '--repo', repo, '--draft',
                 '--title', tag, '--notes-file', notes_path,
                 *(['--target', target_commit] if target_commit else [])])
        # Draft is invisible to clients until every asset uploads successfully.
        for asset in missing:
            run(['gh', 'release', 'upload', tag, asset, '--repo', repo])
        run(['gh', 'release', 'edit', tag, '--repo', repo, '--draft=false',
             '--latest=' + ('true' if latest else 'false'), '--notes-file', notes_path])
    finally:
        notes_path.unlink(missing_ok=True)
    return {'tag': tag, 'published': True}

def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''): value.update(block)
    return value.hexdigest()


def release_output(key, result, dry_run):
    """Expose only completed publications, even if the following publication fails."""
    if dry_run or not os.environ.get('GITHUB_OUTPUT'):
        return
    patterns = {'plugin_tag': r'v\d+\.\d+\.\d+(?:[-.][A-Za-z0-9]+)*',
                'text_tag': r'text-[0-9a-f]{16}'}
    tag = result.get('tag', '')
    completed = result.get('published') or (key == 'text_tag' and result.get('unchanged'))
    if not completed:
        return
    if key not in patterns or not re.fullmatch(patterns[key], tag):
        raise ValueError('Invalid completed release output')
    with Path(os.environ['GITHUB_OUTPUT']).open('a', encoding='utf-8') as stream:
        stream.write(f'{key}={tag}\n')


def publish_built_assets(repo, version, apk, apk_notes, output, text_version, dry_run, *, target_commit=None):
    from release_latest import publish_latest
    from story_catalog_payloads import release_catalog_assets
    results = []
    if apk:
        result = publish(repo, 'v' + version, [apk], False, dry_run, notes=apk_notes)
        results.append(result)
        release_output('plugin_tag', result, dry_run)
    result = publish(repo, 'text-' + text_version,
                     [output / 'text-update.zip', output / 'manifest.json'] +
                     release_catalog_assets(output, repo, 'text-' + text_version),
                     False, dry_run, target_commit=target_commit)
    results.append(result)
    results.append(publish_latest(repo, output, 'text-' + text_version, dry_run))
    # Retention starts only after the mutable entry points to this complete version.
    release_output('text_tag', result, dry_run)
    return results


def publish_plugin_update(repo, version, apk, apk_notes, dry_run):
    from release_latest import refresh_latest_notes
    if apk:
        result = publish(repo, 'v' + version, [apk], False, dry_run, notes=apk_notes)
        release_output('plugin_tag', result, dry_run)
    # Also refresh on retry, when the APK already exists and was not rebuilt.
    return refresh_latest_notes(repo, dry_run)


def resume_frozen_publication(repo, repository, work):
    """A committed batch is resumed before fetching sources or translating anew."""
    from frozen_release import load_receipt, receipt_commit, cached_assets
    from story_catalog_payloads import CATALOG_NAMES
    receipt = load_receipt(repository)
    if receipt is None: return False
    text_tag = 'text-' + receipt['text_version']
    text = release_exists(repo, text_tag)
    latest = release_exists(repo, 'latest')
    expected = {k: v for k, v in receipt['assets'].items()
                if k in ('manifest.json', 'text-update.zip', *CATALOG_NAMES)}
    def complete(release, alias=False):
        assets = {a['name']: a.get('digest') for a in (release or {}).get('assets', [])}
        required = {k: v for k, v in expected.items() if not alias or k not in CATALOG_NAMES}
        return bool(release and not release.get('draft') and all(assets.get(k) == 'sha256:' + v for k, v in required.items()))
    if complete(text) and complete(latest, alias=True):
        from release_followup import GitHub
        expected_commit = receipt_commit(repository)
        client = GitHub(repo)
        if client.request('GET', f'commits/{text_tag}').get('sha') != expected_commit:
            raise ValueError('Published text tag differs from its frozen receipt commit')
        if client.request('GET', 'commits/latest').get('sha') == expected_commit:
            return False
    output = cached_assets(work, receipt)
    apk = output / receipt['plugin_asset'] if receipt.get('plugin_asset') else None
    notes = (output / 'plugin-notes.txt').read_text() if apk else None
    commit = receipt_commit(repository)
    results = publish_built_assets(repo, receipt['plugin_version'], apk, notes,
        output, receipt['text_version'], False, target_commit=commit)
    print(json.dumps({'schema_version': 1, 'resumed': True, 'cutoff': receipt['cutoff'],
                      'source_commit': commit, 'releases': results}))
    return True


def plugin_version(project):
    source = (project / 'app/build.gradle').read_text()
    match = re.search(r"versionName\s+['\"]([0-9]+\.[0-9]+\.[0-9]+(?:[-.][A-Za-z0-9]+)*)['\"]", source)
    if not match: raise ValueError('Missing or unsafe plugin versionName')
    return match[1]

def needs_apk(existing, version):
    if not existing or existing.get('draft'): return True
    expected = f'idoly-localify-{version}.apk'
    if expected not in {asset['name'] for asset in existing.get('assets', [])}:
        raise ValueError(f'Published v{version} release has no expected APK')
    return False


def prepare_toolkit_cache(toolkit, work):
    cache = toolkit / 'cache'
    persistent = work / 'toolkit-cache'
    persistent.mkdir(parents=True, exist_ok=True)
    if cache.is_symlink():
        if cache.resolve() == persistent.resolve(): return cache
        raise ValueError('Toolkit cache points to a different persistent directory')
    if cache.exists():
        # Fresh Git checkouts contain cache/.gitkeep. Move it and any legitimate
        # existing cache into private persistent storage before creating the link.
        for item in cache.iterdir():
            destination = persistent / item.name
            if item.name == '.gitkeep':
                item.unlink()
            elif not destination.exists():
                shutil.move(str(item), destination)
            else:
                raise ValueError('Both checkout and persistent Toolkit cache contain data; reconcile explicitly')
        cache.rmdir()
    cache.symlink_to(persistent, target_is_directory=True)
    return cache


def prepare_story_csv(source, csv_dir, toolkit):
    # Previous runs end with human layers in the runtime CSV directory. Rebuild
    # the baseline from raw scripts so revoked human work cannot enter AI memory.
    if csv_dir.is_symlink(): raise ValueError('Story CSV work directory must not be a symlink')
    if csv_dir.exists(): shutil.rmtree(csv_dir)
    csv_dir.mkdir(parents=True)
    for path in sorted(source.glob('adv_*.txt')):
        if path.is_symlink(): raise ValueError('Symlinked original script')
        toolkit.export_file(path, csv_dir / (path.stem + '.csv'))


def replace_release_data(repository, project):
    """Replace the six data trees so intentional deletions cannot reappear."""
    from import_collaboration import copy_data
    from export_translation_sources import JSON_PATHS
    for directory in JSON_PATHS:
        source, target = repository / directory, project / 'translations' / directory
        if not source.is_dir() or source.is_symlink():
            raise ValueError('Missing or symlinked release data tree')
        if any(p.is_symlink() for p in (target, *target.parents) if p == project or project in p.parents):
            raise ValueError('Symlinked release workspace')
    for directory in JSON_PATHS:
        target = project / 'translations' / directory
        if target.exists(): shutil.rmtree(target)
    copy_data(repository, project)


def refresh_and_import_machine(repository, source, csv_dir, project, command, *,
                               offline=False, translate=True):
    from import_collaboration import import_stories
    if not offline:
        run(command, project)
    receipt = import_stories(repository, csv_dir, source, csv_dir, project,
                             apply_human_titles=False, rebase_ai=True, ai_only=True)
    translate_command = [*command, '--reuse-fetched']
    if translate: translate_command.append('--translate')
    run(translate_command, project)
    return receipt


def prepare_collaboration_stage(repository, source, csv_dir, work, migration_script=None):
    """Migrate a private copy; never mutate the real data checkout before validation."""
    import importlib.util
    from import_collaboration import read_csv, safe_files
    from story_csv_paths import classify
    from export_translation_sources import migration_data_path
    script = migration_script or repository / 'scripts/story_migration.py'
    if script.is_symlink() or not script.is_file():
        raise ValueError('Trusted story migration script is missing')
    spec = importlib.util.spec_from_file_location('release_story_migration', script)
    migration = importlib.util.module_from_spec(spec)
    # Execute the pinned trusted module without writing __pycache__ into data.
    exec(compile(script.read_bytes(), str(script), 'exec'), migration.__dict__)
    stage = work / 'collaboration-stage'
    view = work / 'classified-original-csv'
    for target in (stage, view):
        if target.is_symlink(): raise ValueError('Symlinked collaboration workspace')
        if target.exists(): shutil.rmtree(target)
    # Public checkout data is copied without executing any files from the stage.
    for path in repository.rglob('*'):
        if '.git' not in path.relative_to(repository).parts and path.is_symlink():
            raise ValueError('Symlinked collaboration checkout')
    shutil.copytree(repository, stage, ignore=shutil.ignore_patterns('.git'))
    view.mkdir()
    for path in safe_files(csv_dir, '*.csv'):
        stem, _, rows = read_csv(path)
        if not any(r['text'] for r in rows if r['id'] not in ('info', '译者')):
            continue
        target = view / classify(stem + '.txt')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(migration.encode_csv([{**r, 'trans': ''} for r in rows]))
    receipt, writes, deletes = migration.plan(source, view, stage)
    if receipt.get('schema_version') != 1 or set(receipt.get('changed_paths', [])) != set(writes) | set(deletes):
        raise ValueError('Migration receipt disagrees with planned data changes')
    if any(not migration_data_path(p) for p in set(writes) | set(deletes)):
        raise ValueError('Migration proposed a non-data path')
    baseline = {p: (repository / p).read_bytes() if (repository / p).is_file() else None
                for p in set(writes) | set(deletes)}
    migration.apply(stage, writes, deletes)
    (work / 'story-migration.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    return stage, receipt, baseline


def install_collaboration_stage(repository, stage, paths, migration_paths, baseline, apply=False):
    """Select exact differences; validate all paths and baselines before touching checkout."""
    from export_translation_sources import export_data_path, migration_data_path
    allowed_migrations = set(migration_paths)
    if any(not migration_data_path(p) for p in allowed_migrations):
        raise ValueError('Unsafe migration receipt path')
    changes = {}
    for relative in sorted(set(paths)):
        parts = Path(relative).parts
        if Path(relative).is_absolute() or '..' in parts or not (export_data_path(relative) or relative in allowed_migrations):
            raise ValueError('Unsafe staged localization path')
        old, new = repository / relative, stage / relative
        for root, target in ((repository, old), (stage, new)):
            if any(p.is_symlink() for p in (target, *target.parents) if p == root or root in p.parents):
                raise ValueError('Symlinked staged localization data')
        before = old.read_bytes() if old.is_file() else None
        if relative in baseline and before != baseline[relative]:
            raise ValueError('Collaboration data changed during release')
        after = new.read_bytes() if new.is_file() else None
        if before != after: changes[relative] = after
    if apply:
        for relative, data in changes.items():
            target = repository / relative
            if data is None:
                target.unlink()
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
                    temp = Path(stream.name); stream.write(data)
                temp.replace(target)
    return sorted(changes)


def configure_api(config):
    """Actions env (manual input/repository vars) wins over local compatibility config."""
    base = os.environ.get('OPENAI_API_BASE') or config.get('openai_api_base') or ''
    model = os.environ.get('OPENAI_MODEL') or config.get('openai_model') or ''
    if base:
        if not isinstance(base, str): raise ValueError('API base must be an HTTPS URL')
        try:
            url = urlsplit(base)
            valid = (url.scheme == 'https' and bool(url.hostname) and url.username is None and
                     url.password is None and not url.query and not url.fragment and
                     not any(ord(ch) < 33 for ch in base) and '\\' not in base)
        except ValueError:
            valid = False
        if not valid:
            # Never include the rejected URL: it may contain credentials.
            raise ValueError('API base must be HTTPS without credentials, query or fragment')
        os.environ['OPENAI_API_BASE'] = base
    if model:
        if (not isinstance(model, str) or model != model.strip() or len(model) > 256 or
                any(ord(ch) < 32 or ord(ch) == 127 for ch in model)):
            raise ValueError('Model must be nonempty single-line text of at most 256 characters')
        os.environ['OPENAI_MODEL'] = model
    return {'api_base': base, 'model': model}


def main():
    config_parser = argparse.ArgumentParser(add_help=False)
    config_parser.add_argument('--config', type=Path)
    known, _ = config_parser.parse_known_args()
    config = json.loads(known.config.read_text()) if known.config else {}
    if not isinstance(config, dict): raise ValueError('Runner config must be an object')
    configured_python = config.get('python')
    if configured_python and Path(configured_python).absolute() != Path(sys.executable).absolute():
        os.execv(configured_python, [configured_python, __file__, *sys.argv[1:]])
    parser = argparse.ArgumentParser(description=__doc__, parents=[config_parser])
    for flag in ('repository-dir', 'adv-dir', 'toolkit-dir', 'translator-dir', 'work-dir',
                 'solis-dir', 'notice-account', 'font-bundle', 'collaboration-dir'):
        parser.add_argument('--' + flag, type=Path, default=config.get(flag.replace('-', '_')))
    parser.add_argument('--repository', default='DreamGallery/Idoly-localify-translations')
    parser.add_argument('--project', type=Path, default=ROOT)
    parser.add_argument('--python', default=config.get('python', sys.executable))
    parser.add_argument('--api-python', default=config.get('api_python', sys.executable))
    parser.add_argument('--app-version', default=config.get('app_version'))
    parser.add_argument('--preflight-only', action='store_true')
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--offline', action='store_true', help='Use already refreshed workspace/cache')
    parser.add_argument('--no-translate', action='store_true')
    parser.add_argument('--initialize-collector-day', action='store_true',
                        default=config.get('initialize_collector_day', False))
    parser.add_argument('--data-branch', default='main', choices=['main'])
    parser.add_argument('--skip-apk', action='store_true', help='Explicit text-only run')
    args = parser.parse_args()
    # Child tools run in the project directory. Resolve caller-relative paths
    # before changing their working directory.
    for key in ('project', 'repository_dir', 'adv_dir', 'toolkit_dir', 'translator_dir',
                'work_dir', 'solis_dir', 'notice_account', 'font_bundle', 'collaboration_dir'):
        value = getattr(args, key)
        if value is not None:
            setattr(args, key, Path(value).expanduser().resolve())
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', args.repository):
        parser.error('Invalid release repository')
    for key in ('repository_dir', 'adv_dir', 'toolkit_dir', 'translator_dir', 'work_dir',
                'solis_dir', 'notice_account', 'font_bundle', 'app_version'):
        if not getattr(args, key): parser.error(f'--{key.replace("_", "-")} or runner config value required')
    if not args.preflight_only and not args.collaboration_dir:
        parser.error('--collaboration-dir is required for frozen releases')
    for name, key in [('IDOLY_TOOLKIT_CONFIG', 'toolkit_config'),
                      ('IDOLY_FIREBASE_SETTINGS_FILE', 'firebase_settings'),
                      ('IDOLY_OCTO_SETTINGS_FILE', 'octo_settings'),
                      ('ANDROID_HOME', 'android_home'), ('JAVA_HOME', 'java_home')]:
        if config.get(key): os.environ[name] = str(config[key])
    configure_api(config)
    for key in ('toolkit_config', 'firebase_settings'):
        path = config.get(key)
        if not path or not Path(path).is_file(): raise ValueError(f'Runner config requires existing {key}')
    for name in ('notice_account', 'font_bundle'):
        if not getattr(args, name).is_file(): raise ValueError(f'Missing configured {name}')
    signing_file = config.get('signing_env_file')
    if not args.skip_apk:
        if not signing_file: raise ValueError('Runner config requires signing_env_file')
        signing_path = Path(signing_file)
        if signing_path.stat().st_mode & 0o077: raise ValueError('Signing configuration must be mode 0600')
        signing = json.loads(signing_path.read_text())
        for key in ('IDOLY_KEYSTORE_FILE', 'IDOLY_KEYSTORE_PASSWORD', 'IDOLY_KEY_ALIAS', 'IDOLY_KEY_PASSWORD'):
            if not isinstance(signing.get(key), str) or not signing[key]: raise ValueError('Missing signing configuration')
            os.environ[key] = signing[key]
        if not Path(signing['IDOLY_KEYSTORE_FILE']).is_file(): raise ValueError('Signing keystore does not exist')
    for name in ('gh', 'git', 'java'):
        if not shutil.which(name): raise ValueError(f'Required runner program missing: {name}')
    run([args.python, '-c', 'import UnityPy, fontTools, py7zr, requests, Crypto, google.protobuf'])
    run([args.api_python, '-c', 'import grpc, google.protobuf'])
    if args.preflight_only:
        print(json.dumps({'preflight': 'passed', 'publishing': False}))
        return
    project, work = args.project.resolve(), args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if not args.dry_run and resume_frozen_publication(args.repository, args.repository_dir, work):
        return
    csv_dir, source = work / 'csv', work / 'source'
    source.mkdir(parents=True, exist_ok=True)
    for original in (args.adv_dir / 'Resource').glob('adv_*.txt'):
        if original.is_symlink(): raise ValueError('Symlinked original resource')
        target_source = source / original.name
        if not target_source.exists(): shutil.copyfile(original, target_source)
    # Preserve Octo cache across checkout cleanups. Toolkit resolves account paths
    # beneath its own cache; copy only the dedicated credential, never publish it.
    toolkit_cache = prepare_toolkit_cache(args.toolkit_dir, work)
    account = toolkit_cache / 'notice/collector-account.json'
    account.parent.mkdir(parents=True, exist_ok=True)
    if args.notice_account.resolve() != account.resolve():
        shutil.copyfile(args.notice_account, account)
    account.chmod(0o600)
    os.environ.update(IDOLY_TOOLKIT_DIR=str(args.toolkit_dir.resolve()),
                      IDOLY_TRANSLATOR_DIR=str(args.translator_dir.resolve()),
                      IDOLY_STORY_SOURCE_DIR=str(source), IDOLY_STORY_CSV_DIR=str(csv_dir),
                      IDOLY_STORY_OUTPUT_DIR=str(work / 'compiled-story'))
    from import_collaboration import import_stories, validate_story_coverage
    base_revision = run(['git', '-C', args.repository_dir, 'rev-parse', 'HEAD'], capture=True).strip()
    from frozen_release import stage_cutoff, commit_batch, cache_assets
    cutoff = run(['git', '-C', args.collaboration_dir, 'rev-parse', 'HEAD'], capture=True).strip()
    frozen = work / 'cutoff-data'
    collaboration_baseline = stage_cutoff(args.repository_dir, args.collaboration_dir, frozen)
    replace_release_data(frozen, project)
    from workspace_paths import load_module
    prepare_story_csv(source, csv_dir, load_module('HoshimiToolkit', 'src.adv_csv'))
    refresh_summary = work / 'refresh-summary.json'
    command = [args.python, project / 'tools/refresh_sources.py',
               '--toolkit-dir', args.toolkit_dir, '--translator-dir', args.translator_dir,
               '--solis-dir', args.solis_dir, '--app-version', args.app_version,
               '--translations-dir', project / 'translations', '--source-dir', source,
               '--csv-dir', csv_dir, '--work-dir', work / 'refresh', '--python', args.python,
               '--api-python', args.api_python, '--notice-account', account,
               '--summary', refresh_summary, '--accept-story-rebase']
    if args.initialize_collector_day: command.append('--initialize-collector-day')
    try:
        receipt = refresh_and_import_machine(frozen, source, csv_dir, project,
                                              command, offline=args.offline,
                                              translate=not args.no_translate)
        (work / 'collaboration-initial.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
    finally:
        if args.notice_account.resolve() != account.resolve() and account.is_file():
            shutil.copyfile(account, args.notice_account)
            args.notice_account.chmod(0o600)
            account.unlink()
    # Snapshot machine results before applying human title overrides for runtime.
    machine = work / 'machine-export'
    if machine.exists(): shutil.rmtree(machine)
    from export_translation_sources import JSON_PATHS, export_sources
    for relative in JSON_PATHS:
        origin = project / 'translations' / relative
        if origin.exists(): shutil.copytree(origin, machine / 'translations' / relative)
    shutil.copytree(csv_dir, machine / 'csv')
    skip_stories = [stem for stem, value in receipt['stories'].items() if value['layer'] != 'ai']
    # Executable migration code comes only from the trusted main checkout.
    original_migration = args.repository_dir / 'scripts/story_migration.py'
    if original_migration.is_symlink() or not original_migration.is_file():
        raise ValueError('Trusted story migration script is missing')
    migration_script = work / 'pinned-story-migration.py'
    shutil.copyfile(original_migration, migration_script)

    def finalize(snapshot_revision):
        # Apply the human layer only after the machine snapshot is complete.
        replace_release_data(machine / 'translations', project)
        if csv_dir.is_symlink(): raise ValueError('Symlinked release CSV directory')
        shutil.rmtree(csv_dir)
        shutil.copytree(machine / 'csv', csv_dir)
        stage, _, _ = prepare_collaboration_stage(
            frozen, source, csv_dir, work, migration_script=migration_script)
        receipt = import_stories(stage, csv_dir, source, csv_dir, project, human_only=True)
        validate_story_coverage(csv_dir)
        (work / 'collaboration-final.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n')
        run([args.python, project / 'tools/pipeline.py', 'build'], project)
        run([args.python, project / 'tools/pipeline.py', 'verify'], project)
        summary = json.loads(refresh_summary.read_text())
        if summary.get('complete') is not True or any(summary.get('pending_after', {}).values()):
            raise ValueError('Official source refresh incomplete; refusing release')
        master_tag = Path(summary['master_tag'])
        run([args.python, project / 'tools/master_blob_patches.py', '--master-tag', master_tag,
             '--solis-dir', args.solis_dir, '--all-tables'], project)
        from build_text_update import build
        output = project / '.analysis/scheduled-release'
        manifest = build(project, output, 'https://github.com/' + args.repository,
                         include_stories=True, github_repository=args.repository,
                         include_master_blobs=True, master_tag=master_tag)
        content_version = manifest['version']
        # Stable for the same frozen inputs; timestamps and refresh logs are not
        # part of the identity. Bind distinct data batches to distinct text tags.
        batch_version = hashlib.sha256((snapshot_revision + ':' + cutoff + ':' +
            content_version).encode()).hexdigest()[:16]
        manifest = build(project, output, 'https://github.com/' + args.repository, version=batch_version,
                         include_stories=True, github_repository=args.repository,
                         include_master_blobs=True, master_tag=master_tag)
        # Build every necessary asset before publishing anything.
        apk = None
        apk_notes = None
        version = plugin_version(project)
        existing_apk = None if args.dry_run or args.skip_apk else release_exists(args.repository, 'v' + version)
        if not args.skip_apk and (args.dry_run or needs_apk(existing_apk, version)):
            apk_notes = plugin_release_notes(project, version)
            if not args.font_bundle.is_file(): raise ValueError('Configured module font bundle is missing')
            # The persistent runner bundle is a schema template, not the selected
            # typeface. Rebuild it from the source-controlled, hash-pinned default.
            from prepare_module_font import prepare
            for bundle, font_config in (
                    ('resource-han-rounded.bundle', 'font-source.json'),
                    ('love-rounded.bundle', 'love-font-source.json')):
                prepare(args.font_bundle, project / 'app/src/main/assets/idoly-localify' / bundle,
                        cache=work / 'fonts', config=project / 'config' / font_config)
            run([project / 'gradlew', ':app:assembleRelease', '--no-daemon'], project)
            apk = output / f'idoly-localify-{version}.apk'
            shutil.copyfile(project / 'app/build/outputs/apk/release/app-release.apk', apk)
            run([args.python, project / 'tools/check_release_apk.py', apk], project)
        export_sources(machine, machine / 'csv', source, stage,
                                  skip_stories, summary, apply=True)
        from frozen_release import data_tree, projection, write_tree
        # Source migration may demote stale formal work into drafts. Those drafts
        # stay in collaboration; only valid completed layers reach published main.
        normalized, _ = projection(stage)
        write_tree(stage, {p: normalized.get(p) for p in set(data_tree(stage)) | set(normalized)})
        from frozen_release import unchanged_batch
        if unchanged_batch(args.repository_dir, stage, content_version):
            return {'unchanged': True, 'apk': apk, 'apk_notes': apk_notes, 'version': version}
        assets = cache_assets(work, manifest['version'], output, apk, apk_notes)
        batch = {'schema_version': 1, 'cutoff': cutoff, 'main_parent': snapshot_revision,
                 'collaboration_baseline': collaboration_baseline, 'published_data': {},
                 'text_version': manifest['version'], 'content_version': content_version, 'assets': assets,
                 'plugin_version': version, 'plugin_asset': apk.name if apk else None}
        data_commit, changed = (None, []) if args.dry_run else commit_batch(
            args.repository_dir, stage, snapshot_revision, batch, args.data_branch)
        return manifest, apk, apk_notes, data_commit, changed, output, version

    # Collaboration is never refreshed during compilation or publication. A main
    # race fails safely, with no force push and no implicit intake of newer work.
    finalized = finalize(base_revision)
    if isinstance(finalized, dict) and finalized.get('unchanged'):
        publish_plugin_update(args.repository, finalized['version'], finalized['apk'],
                              finalized['apk_notes'], args.dry_run)
        print(json.dumps({'schema_version': 1, 'unchanged': True, 'cutoff': cutoff, 'dry_run': args.dry_run}))
        return
    manifest, apk, apk_notes, data_commit, changed, output, version = finalized
    results = publish_built_assets(args.repository, version, apk, apk_notes,
                                  output, manifest['version'], args.dry_run,
                                  target_commit=data_commit)
    report = {'schema_version': 1, 'version': manifest['version'], 'dry_run': args.dry_run,
              'releases': results, 'source_changed_files': len(changed), 'source_commit': data_commit, 'archive_sha256': manifest['archive']['sha256']}
    (work / 'release-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
if __name__ == '__main__': main()
