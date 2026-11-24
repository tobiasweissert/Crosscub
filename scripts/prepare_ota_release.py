#!/usr/bin/env python3
"""Prepare an owner-approved release; never commit, push, build or dispatch locally."""
import argparse
import configparser
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

from validate_ota_release import REPOSITORY, validate_release, validate_version


def set_version(path, version):
    """Portable targeted edit; preserve comments and all other INI bytes."""
    validate_version(version)
    original = path.read_bytes()
    text = original.decode('utf-8')
    for section in ['crosssmudge', 'crossink']:
        pattern = rf'(^\[{section}\][^\r\n]*\r?\n)(.*?)(?=^\[|\Z)'
        matches = list(re.finditer(pattern, text, re.M | re.S))
        if len(matches) != 1:
            raise ValueError('expected exactly one [' + section + '] section')
        match = matches[0]
        body, count = re.subn(r'(?m)^(version\s*=\s*)[^\r\n]*', lambda m: m[1] + version, match[2])
        if count != 1:
            raise ValueError('expected exactly one version in [' + section + ']')
        text = text[:match.start(2)] + body + text[match.end(2):]
    path.write_bytes(text.encode('utf-8'))


def qualify_source(version, *, repository, event, ref, actor, publish, source_sha, cwd=Path('.')):
    validate_version(version)
    if repository != REPOSITORY:
        raise ValueError('release repository must be ' + REPOSITORY)
    if event not in ['workflow_dispatch', 'push']:
        raise ValueError('only explicit release dispatch or release tag push is supported')
    if event == 'workflow_dispatch' and ref != 'main':
        raise ValueError('manual releases must select main')
    if event == 'push' and ref != 'crosscub-v' + version:
        raise ValueError('tag/version mismatch')
    if publish and (event != 'workflow_dispatch' or actor != 'tobiasweissert'):
        raise ValueError('stable publication requires explicit repository-owner dispatch')
    def git(*args):
        result = subprocess.run(['git', *args], cwd=cwd, capture_output=True, text=True)
        if result.returncode:
            raise ValueError('release source/target check failed: git ' + ' '.join(args))
        return result.stdout.strip()
    if git('rev-parse', 'HEAD') != source_sha:
        raise ValueError('checkout must match the workflow source SHA exactly')
    git('merge-base', '--is-ancestor', source_sha, 'refs/remotes/origin/main')
    tag = 'refs/tags/crosscub-v' + version
    exists = subprocess.run(['git', 'show-ref', '--verify', '--quiet', tag], cwd=cwd).returncode
    if exists == 0 and git('rev-parse', tag + '^{commit}') != source_sha:
        raise ValueError('existing tag targets different source')
    if exists not in [0, 1]:
        raise ValueError('cannot inspect existing tag')
    config = configparser.ConfigParser(interpolation=None)
    config.read(Path(cwd) / 'platformio.ini')
    for section in ['crosssmudge', 'crossink']:
        if config.get(section, 'version', fallback=None) != version:
            raise ValueError('commit matching version fields before release: [' + section + ']')


def github(endpoint, *, fields=None, method=None):
    """Only exact HTTP 404 means absent; auth/rate-limit/network failures stop release."""
    command = ['gh', 'api', '--hostname', 'github.com', 'repos/' + REPOSITORY + '/' + endpoint]
    if fields is not None:
        command.extend(['--method', method or 'POST', '--input', '-'])
    result = subprocess.run(command, input=json.dumps(fields) if fields is not None else None,
                            text=True, capture_output=True)
    if result.returncode:
        if '(HTTP 404)' in result.stderr:
            return None
        raise ValueError('GitHub release lookup failed (not a confirmed 404)')
    return json.loads(result.stdout)


def check_remote(version, *, skip_published=False):
    validate_version(version)
    existing = github('releases/tags/crosscub-v' + version)
    if existing is not None:
        if (skip_published and existing.get('tag_name') == 'crosscub-v' + version
                and existing.get('draft') is False and existing.get('prerelease') is False):
            print('Already published stable release: no build, upload, redraft or replacement.')
            return False
        raise ValueError('release already exists; never overwrite or redraft it (including duplicate tag workflows)')
    # Authenticated tag lookup may return 404 for a draft. Enumerate bounded
    # pages before declaring admission; any malformed/incomplete response fails closed.
    for page in range(1, 11):
        releases = github(f'releases?per_page=100&page={page}')
        if not isinstance(releases, list):
            raise ValueError('cannot enumerate releases for draft admission')
        if any(not isinstance(r, dict) or not isinstance(r.get('tag_name'), str)
               or not r['tag_name'] for r in releases):
            raise ValueError('malformed release enumeration')
        if any(r['tag_name'] == 'crosscub-v' + version for r in releases):
            raise ValueError('release/draft already exists; explicit recovery required')
        if len(releases) < 100:
            break
    else:
        raise ValueError('release enumeration exceeded admission bound')
    latest = github('releases/latest')
    if latest is not None:
        tag = latest.get('tag_name', '')
        if not tag.startswith('crosscub-v'):
            raise ValueError('latest release does not follow Crosscub stable identity')
        old = validate_version(tag[len('crosscub-v'):])
        def numeric(value):
            parts = [int(part) for part in value.split('.')]
            return tuple(parts + [0] * (4 - len(parts)))
        if numeric(version) <= numeric(old):
            raise ValueError('release must be numerically newer than GitHub /latest')


def ensure_remote_tag(tag, source_sha):
    ref = github('git/ref/tags/' + tag)
    if ref is None:
        github('git/refs', fields={'ref': 'refs/tags/' + tag, 'sha': source_sha})
        # A draft release does not reliably create its tag until publication.
        # Create the immutable target explicitly, then verify the exact ref.
        ref = github('git/ref/tags/' + tag)
    obj = ref.get('object', {}) if ref else {}
    for _ in range(4):
        if obj.get('type') != 'tag':
            break
        annotation = github('git/tags/' + obj['sha'])
        obj = annotation.get('object', {}) if annotation else {}
    if obj.get('type') != 'commit' or obj.get('sha') != source_sha:
        raise ValueError('GitHub tag does not resolve to qualified source SHA')


def release_command(args):
    subprocess.run(['gh', 'release', *args], check=True, env={**os.environ, 'GH_HOST': 'github.com'})


def upload_release(version, firmware, source_sha, *, publish=False):
    """CI only: validate, create a NEW draft, verify target/asset, then opt-in publish."""
    report = validate_release(firmware, version)
    check_remote(version)
    tag = report['tag']
    ensure_remote_tag(tag, source_sha)
    created = github('releases', fields={'tag_name': tag, 'target_commitish': source_sha,
                     'draft': True, 'prerelease': False, 'make_latest': 'false',
                     'name': 'Crosscub Firmware v' + version,
                     'body': 'Owner-qualified Crosscub Sticky OTA update. SHA256: ' + report['sha256']})
    release_id = release_identity(created, tag, source_sha, True)
    subprocess.run(['gh', 'api', '--hostname', 'github.com', '--method', 'POST',
                    f'https://uploads.github.com/repos/{REPOSITORY}/releases/{release_id}/assets?name={report["filename"]}',
                    '-H', 'Content-Type: application/octet-stream', '--input', str(firmware)],
                   check=True, capture_output=True)
    verify_release(release_id, report, source_sha, draft=True)
    if publish:
        publish_verified(release_id, report, source_sha)
    return report


def release_identity(release, tag, source_sha, draft):
    release_id = release.get('id') if isinstance(release, dict) else None
    if (type(release_id) is not int or release_id <= 0
            or release.get('url') != f'https://api.github.com/repos/{REPOSITORY}/releases/{release_id}'
            or release.get('tag_name') != tag or release.get('target_commitish') != source_sha
            or release.get('draft') is not draft or release.get('prerelease') is not False):
        raise ValueError('GitHub release identity/state/source read-back mismatch')
    return release_id


def verify_release(release_id, report, source_sha, *, draft):
    import hashlib
    import urllib.request
    release = github('releases/' + str(release_id))
    if release_identity(release, report['tag'], source_sha, draft) != release_id:
        raise ValueError('GitHub release ID mismatch')
    assets = release.get('assets', [])
    if len(assets) != 1:
        raise ValueError('expected exactly one qualified asset')
    asset = assets[0]
    asset_id = asset.get('id')
    canonical = f'https://github.com/{REPOSITORY}/releases/download/{report["tag"]}/{report["filename"]}'
    url = asset.get('browser_download_url', '')
    draft_url = re.fullmatch(r'https://github\.com/' + re.escape(REPOSITORY) +
                             r'/releases/download/untagged-[A-Za-z0-9-]+/' + re.escape(report['filename']), url)
    if (type(asset_id) is not int or asset_id <= 0
            or asset.get('url') != f'https://api.github.com/repos/{REPOSITORY}/releases/assets/{asset_id}'
            or asset.get('name') != report['filename'] or asset.get('size') != report['size']
            or asset.get('digest') != 'sha256:' + report['sha256']
            or not (url == canonical or (draft and draft_url))):
        raise ValueError('GitHub uploaded asset name/size/digest/owner URL mismatch; do not publish')
    # Read authenticated draft bytes by exact asset ID, not its temporary URL.
    if draft:
        data = subprocess.run(['gh', 'api', '--hostname', 'github.com',
                              f'repos/{REPOSITORY}/releases/assets/{asset_id}',
                              '-H', 'Accept: application/octet-stream'], check=True, capture_output=True).stdout
    else:
        with urllib.request.urlopen(canonical, timeout=60) as response:
            data = response.read(report['size'] + 1)
    if len(data) != report['size'] or hashlib.sha256(data).hexdigest() != report['sha256']:
        raise ValueError('downloaded asset bytes/hash mismatch')
    # Read only: verification must never repair an absent tag.
    ref = github('git/ref/tags/' + report['tag'])
    obj = ref.get('object', {}) if ref else {}
    for _ in range(4):
        if obj.get('type') != 'tag':
            break
        annotation = github('git/tags/' + obj['sha'])
        obj = annotation.get('object', {}) if annotation else {}
    if obj.get('type') != 'commit' or obj.get('sha') != source_sha:
        raise ValueError('GitHub tag does not resolve to qualified source SHA')
    return release


def publish_verified(release_id, report, source_sha):
    github('releases/' + str(release_id), method='PATCH',
           fields={'draft': False, 'prerelease': False, 'make_latest': 'true'})
    verify_release(release_id, report, source_sha, draft=False)
    latest = github('releases/latest')
    if release_identity(latest, report['tag'], source_sha, False) != release_id:
        raise ValueError('published release was not confirmed at GitHub /latest')


def recover_release(version, firmware, source_sha, *, release_id, publish=False):
    """Publish-only recovery: qualified local bytes, exact existing draft, no upload/build/tag mutation."""
    if not publish or type(release_id) is not int or release_id <= 0:
        raise ValueError('recovery requires explicit publication approval and positive release ID')
    report = validate_release(firmware, version)
    verify_release(release_id, report, source_sha, draft=True)
    latest = github('releases/latest')
    if latest is not None:
        old = validate_version(latest.get('tag_name', '').removeprefix('crosscub-v'))
        def numeric(v):
            values = [int(x) for x in v.split('.')]
            return tuple(values + [0] * (4-len(values)))
        if not latest.get('tag_name', '').startswith('crosscub-v') or numeric(version) <= numeric(old):
            raise ValueError('recovery must be newer than stable /latest')
    publish_verified(release_id, report, source_sha)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', nargs='?')
    parser.add_argument('--ci-check', action='store_true', help='CI: check owner/source/version/remote release before building')
    parser.add_argument('--ci-upload', type=Path, help='CI: qualify and upload this artifact; draft unless explicitly approved')
    parser.add_argument('--ci-recover-id', type=int, help='Explicit publish-only recovery of this exact existing draft ID; requires --ci-upload qualified local bytes and owner --publish')
    parser.add_argument('--publish', action='store_true', help='Print owner command selecting stable /latest publication; default is draft')
    parser.add_argument('--write-version', action='store_true', help='Explicitly update both tracked version fields; does not commit/tag/push')
    parser.add_argument('--config', type=Path, default=Path(__file__).resolve().parents[1] / 'platformio.ini')
    args = parser.parse_args()
    try:
        if args.ci_recover_id is not None and (not args.ci_upload or args.ci_check):
            raise ValueError('recovery requires --ci-upload and forbids --ci-check')
        if args.ci_check and args.ci_upload:
            raise ValueError('select exactly one CI operation')
        if args.ci_check or args.ci_upload:
            if os.environ.get('GITHUB_ACTIONS') != 'true' or args.write_version:
                raise ValueError('CI mode requires GitHub Actions and forbids changing source versions')
            event = os.environ.get('GITHUB_EVENT_NAME', '')
            ref = os.environ.get('GITHUB_REF_NAME', '')
            version = args.version or (os.environ.get('INPUT_VERSION', '') if event == 'workflow_dispatch' else ref.removeprefix('crosscub-v'))
            publish_value = os.environ.get('PUBLISH_RELEASE', 'false')
            if publish_value not in ['true', 'false', '']:
                raise ValueError('PUBLISH_RELEASE must be true or false')
            publish = args.publish or publish_value == 'true'
            qualify_source(version, repository=os.environ.get('GITHUB_REPOSITORY', ''), event=event,
                           ref=ref, actor=os.environ.get('GITHUB_ACTOR', ''), publish=publish,
                           source_sha=os.environ.get('GITHUB_SHA', ''))
            if args.ci_upload:
                if args.ci_recover_id is not None:
                    report = recover_release(version, args.ci_upload, os.environ['GITHUB_SHA'],
                                             release_id=args.ci_recover_id, publish=publish)
                else:
                    report = upload_release(version, args.ci_upload, os.environ['GITHUB_SHA'], publish=publish)
                print(json.dumps(report, indent=2))
            else:
                proceed = check_remote(version, skip_published=True)
                with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf-8') as output:
                    output.write('release_version=' + version + '\n')
                    output.write('skip_release=' + ('true' if proceed is False else 'false') + '\n')
            return
        validate_version(args.version)
        if args.write_version:
            set_version(args.config, args.version)
    except (ValueError, OSError, configparser.Error, subprocess.CalledProcessError) as exc:
        parser.exit(1, str(exc) + '\n')
    print('Review and commit the version and qualified source, then push approved main to ' + REPOSITORY + '.')
    print('No build, Git operation, workflow dispatch or publication was performed.')
    print('After approval, run explicitly:')
    print(shlex.join(['gh', 'workflow', 'run', 'release.yml', '--repo', REPOSITORY, '--ref', 'main',
                      '-f', 'version=' + args.version, '-f', 'publish=' + str(args.publish).lower()]))


if __name__ == '__main__':
    main()
