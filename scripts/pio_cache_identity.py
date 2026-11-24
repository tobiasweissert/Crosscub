#!/usr/bin/env python3
"""Exact compatibility identity for mutable patched PlatformIO packages/platforms.

No fallback prefix: framework rebuilds and installed builder edits are not immutable
archives. Version-only edits do not change framework compatibility. All other parsed
INI values, patch scripts/data, SDK gitlink and partition config remain conservative.
"""
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess

SCHEMA = 'mutable-pio-v2-core6.1.19-py3.14'


def identity(root, *, runner_os, runner_arch, sdk_sha):
    inputs = {}
    for name in ('platformio.ini', 'platformio.local.example.ini', 'platformio.local.ini'):
        path = root / name
        if not path.exists():
            continue
        config = configparser.ConfigParser(interpolation=None)
        config.read_string(path.read_text())
        # Only display/descriptor versions are independent of installed SDK state.
        for section in ('crosspoint', 'crosssmudge', 'crossink'):
            if config.has_option(section, 'version'):
                config.set(section, 'version', '<release-version>')
        inputs[name] = {s: dict(config[s]) for s in config.sections()}
    for path in sorted((root/'scripts').rglob('*')):
        if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
            inputs[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    for name in ('partitions.csv', '.gitmodules'):
        if (root/name).is_file():
            inputs[name] = hashlib.sha256((root/name).read_bytes()).hexdigest()
    payload = [SCHEMA, runner_os, runner_arch, sdk_sha, inputs]
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest()


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    sdk_sha = subprocess.check_output(['git', 'ls-tree', 'HEAD', 'freeink-sdk'], cwd=root, text=True).split()[2]
    key = identity(root, runner_os=os.environ['RUNNER_OS'], runner_arch=os.environ['RUNNER_ARCH'], sdk_sha=sdk_sha)
    with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
        output.write('identity=' + key + '\n')
