#!/usr/bin/env python3
"""Read-only cold Sticky admission. No resolver, cleanup or firmware acceptance.

Pinned pioarduino Core 6.1.19: project/options.py directory metadata and
project/config.py getraw (including oldnames), read and get_default_path.
HOME on Linux comes from fs.expanduser -> os.path.expanduser.
Only fixed workflow commands are admitted, not arbitrary Core CLI arguments.
Freshness observes these paths; VM exclusivity is a launcher trust assumption.
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys

DIRECTORIES = frozenset('core globallib platforms packages cache build_cache workspace build libdeps include src lib data test boards monitor shared'.split())
# Finite metadata-derived controls, NOT a generic PLATFORMIO_* assertion.
OVERRIDES = frozenset('''BOARDS_DIR BUILD_CACHE_DIR BUILD_DIR BUILD_FLAGS
BUILD_SRC_FILTER BUILD_SRC_FLAGS BUILD_UNFLAGS CACHE_DIR CORE_DIR DATA_DIR
DEFAULT_ENVS ENV_DEFAULT EXTRA_SCRIPT EXTRA_SCRIPTS GLOBALLIB_DIR HOME_DIR
INCLUDE_DIR LIBDEPS_DIR LIB_DIR LIB_EXTRA_DIRS MONITOR_DIR PACKAGES_DIR
PLATFORMS_DIR SHARED_DIR SRC_BUILD_FLAGS SRC_DIR SRC_FILTER TEST_DIR
UPLOAD_FLAGS UPLOAD_PORT WORKSPACE_DIR'''.split())


class Rejected(ValueError):
    """Sanitized admission failure; never includes environment values."""


def directory_chain(path):
    """lstat every component, including absent roots and dangling symlinks."""
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise Rejected('noncanonical path')
    for component in [*reversed(path.parents), path]:
        try:
            mode = component.lstat().st_mode
        except FileNotFoundError:
            continue
        if not stat.S_ISDIR(mode):
            raise Rejected('directory chain is not real directories')


def absent(path):
    if os.path.lexists(path):
        raise Rejected('prior project state exists')


def snapshot(project, home):
    # Import only configuration APIs: no CLI invocation or package query.
    import platformio
    from platformio import app
    from platformio.project.config import ProjectConfig
    from platformio.project.options import ProjectOptions
    if platformio.__version__ != '6.1.19':
        raise Rejected('Core version mismatch')
    metadata = [o for o in ProjectOptions.values() if o.sysenvvar]
    controls = {n.removeprefix('PLATFORMIO_') for o in metadata
                for n in [o.sysenvvar] + ['PLATFORMIO_' + a.upper() for a in (o.oldnames or [])]}
    dirs = {o.name.removesuffix('_dir') for o in ProjectOptions.values() if o.group == 'directory'}
    if controls != OVERRIDES or dirs != DIRECTORIES:
        raise Rejected('pinned Core configuration schema mismatch')
    if any('PLATFORMIO_' + name in os.environ for name in OVERRIDES):
        raise Rejected('Core environment override present')
    if app.get_session_var('custom_project_conf') or Path.cwd() != project:
        raise Rejected('alternate project configuration context')
    # Do not follow injected configs even for inspection.
    config = ProjectConfig(str(project / 'platformio.ini'), parse_extra=False)
    if config.get('platformio', 'extra_configs', []) != ['platformio.local.ini']:
        raise Rejected('extra configuration injection')
    directory_options = {name + '_dir' for name in DIRECTORIES} | {'home_dir'}
    if config.has_section('platformio') and directory_options.intersection(config._parser.options('platformio')):
        raise Rejected('directory configuration override')
    roots = {name: config.get('platformio', name + '_dir') for name in ('core', 'packages', 'platforms')}
    expected = {'core': str(home / '.platformio'), 'packages': str(home / '.platformio/packages'), 'platforms': str(home / '.platformio/platforms')}
    if roots != expected:
        raise Rejected('Core root mismatch')
    # Extra config must remain absent after installation as well.
    absent(project / 'platformio.local.ini')
    return {'version': platformio.__version__, 'roots': roots, 'prefix': sys.prefix}


def admit(project, home, phase):
    project, home = Path(project), Path(home)
    directory_chain(home)
    directory_chain(project)
    if not home.is_dir() or not project.is_dir():
        raise Rejected('missing canonical home/project')
    config_path = project / 'platformio.ini'
    if not stat.S_ISREG(config_path.lstat().st_mode):
        raise Rejected('project config is not a real file')
    core = home / '.platformio'
    for root in [core, core / 'packages', core / 'platforms', core / '.cache', core / 'lib', core / 'penv']:
        directory_chain(root)
        if phase == 'fresh' and root != core and root.exists() and any(root.iterdir()):
            raise Rejected('nonempty cold payload/cache/runtime root')
    if phase == 'fresh':
        for name in ('.pio', 'sdkconfig.sticky', 'sdkconfig.defaults'):
            absent(project / name)
    return snapshot(project, home)


def coherent(outer, nested, home):
    expected = {'core': str(home / '.platformio'), 'packages': str(home / '.platformio/packages'), 'platforms': str(home / '.platformio/platforms')}
    for item in (outer, nested):
        if item.get('version') != '6.1.19' or item.get('roots') != expected:
            raise Rejected('outer/nested Core configuration mismatch')
    if nested.get('prefix') != str(home / '.platformio/penv'):
        raise Rejected('nested interpreter is not expected penv')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase', choices=['fresh', 'coherent', '_snapshot'])
    args = parser.parse_args()
    try:
        home = Path(pwd.getpwuid(os.getuid()).pw_dir)
        if os.environ.get('HOME') != str(home):
            raise Rejected('HOME is not canonical passwd home')
        project = Path.cwd()
        observed = admit(project, home, args.phase)
        if args.phase == '_snapshot':
            print(json.dumps(observed, sort_keys=True))
            return 0
        if args.phase == 'coherent':
            penv = home / '.platformio/penv'
            if not penv.is_dir():
                raise Rejected('missing expected penv')
            result = subprocess.run([str(penv / 'bin/python'), '-B', str(Path(__file__).absolute()), '_snapshot'], capture_output=True, timeout=30, check=False)
            if result.returncode or len(result.stdout) > 8192 or result.stderr:
                raise Rejected('nested Core snapshot failed')
            coherent(observed, json.loads(result.stdout), home)
        # Bounded fixed keys; no environment dump or package/config content.
        print(json.dumps({'phase': args.phase, 'version': observed['version'], 'roots': observed['roots'], 'nested_penv': str(home / '.platformio/penv'), 'publication_authorized': False}, sort_keys=True))
        return 0
    except Exception:
        # Core parse/import errors can embed config bytes and runner paths.
        # This CLI boundary always fails closed without echoing exception text.
        print('Cold Sticky state rejected; no state repaired or removed.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
