"""Revision-bound SDK local fixes and source-bound cppcheck exceptions.

No wildcard, category, or vendor exemption. Unknown findings remain blocking.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess

SDK_REVISION = '634a3f8c067a3714a6a7c622015138af096ab5d2'
REGISTRY = Path(__file__).with_name('cppcheck-exceptions.json')
PATCHES = json.loads(Path(__file__).with_name('sdk-analyzer-patches.json').read_text())


def revision(sdk_root):
    try:
        actual = subprocess.check_output(['git', '-C', str(sdk_root), 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise RuntimeError('cppcheck SDK revision unavailable') from exc
    if actual != SDK_REVISION:
        raise RuntimeError('cppcheck SDK revision mismatch')


def prepare_patches(sdk_root, verify_revision=True):
    """Read-only preflight: authenticate all UI inputs and return final bytes."""
    if verify_revision:
        revision(sdk_root)
    outputs = {}
    for path, patch in PATCHES.items():
        current = (sdk_root/path).read_bytes()
        original = current
        # Invert only the exact installed output, then authenticate the preimage.
        if hashlib.sha256(current).hexdigest() != patch['sha256']:
            for old, new in reversed(patch['edits']):
                if original.count(new.encode()) != 1:
                    raise RuntimeError('Unknown SDK analyzer overlay bytes: '+path)
                original = original.replace(new.encode(), old.encode())
        if hashlib.sha256(original).hexdigest() != patch['sha256']:
            raise RuntimeError('Unknown SDK analyzer preimage: '+path)
        output = original
        for old, new in patch['edits']:
            if output.count(old.encode()) != 1:
                raise RuntimeError('SDK analyzer overlay anchor mismatch: '+path)
            output = output.replace(old.encode(), new.encode())
        if current not in (original, output):
            raise RuntimeError('Partial SDK analyzer overlay: '+path)
        outputs[path] = output
    return outputs


def apply_patches(sdk_root, verify_revision=True):
    # Authenticate every file before making any write.
    outputs = prepare_patches(sdk_root, verify_revision)
    for path, output in outputs.items():
        target = sdk_root/path
        if target.read_bytes() != output:
            target.write_bytes(output)


def validate_entry(root, entry):
    path = Path(entry['path'])
    if path.is_absolute() or '..' in path.parts or not re.fullmatch(r'[A-Za-z][A-Za-z0-9]*', entry['id']):
        raise RuntimeError('Invalid exact cppcheck exception')
    data = (root/path).read_bytes()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise RuntimeError('cppcheck exception source hash mismatch: '+str(path))
    lines = data.decode().splitlines()
    line = entry['line']
    if type(line) is not int or not 1 <= line <= len(lines) or lines[line-1] != entry['source']:
        raise RuntimeError('cppcheck exception line mismatch: '+str(path))
    if not entry['reason'] or not (root/entry['test']).is_file():
        raise RuntimeError('cppcheck exception regression missing')
    return '--suppress='+entry['id']+':'+str((root/path).resolve())+':'+str(line)


def suppressions(root):
    root = Path(root)
    registry = json.loads(REGISTRY.read_text())
    if registry['sdk_revision'] != SDK_REVISION:
        raise RuntimeError('cppcheck exception registry revision mismatch')
    revision(root/'freeink-sdk')
    return [validate_entry(root, entry) for entry in registry['exceptions']]
