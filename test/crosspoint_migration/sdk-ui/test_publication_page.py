"""Exercise the build hook without SecureNet, then compile its exact output."""
import argparse
import hashlib
import os
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
UI = Path(os.environ.get('CROSSCUB_FREEINK_UI', ROOT / 'freeink-sdk/libs/ui/FreeInkUI')).resolve()
PAGE = Path('include/components/media/publication-page.h')
BASE = 'abae0d2776bdff43708dbbb7648b8aeae4036cc49c119f9ac83e5b2b137081d7'
FIXED = 'ece7046127282eb88f355d2e23cde6c60c441048488ea186fec79a400e8ef621'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sanitize', action='store_true', default=os.environ.get('SANITIZE') == '1')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='publication-page-') as temp:
        project = Path(temp)
        shadow = project / 'freeink-sdk/libs/ui/FreeInkUI'
        page = shadow / PAGE
        page.parent.mkdir(parents=True)

        class Env:
            def subst(self, key):
                assert key == '$PROJECT_DIR'
                return str(project)

        def install():
            runpy.run_path(str(ROOT / 'scripts/patch_publication_page.py'),
                           init_globals={'env': Env(), 'Import': lambda _: None})

        try:
            install()
        except FileNotFoundError as err:
            assert Path(err.filename) == page
        else:
            raise AssertionError('Missing page accepted')
        original = (UI / PAGE).read_bytes()
        if hashlib.sha256(original).hexdigest() == FIXED:
            original = original.replace(b'if (body.height <= 0)', b'if (body.empty())')
        assert hashlib.sha256(original).hexdigest() == BASE
        drift = original + b'// drift\n'
        page.write_bytes(drift)
        try:
            install()
        except RuntimeError as err:
            assert 'Unknown SDK publication page bytes' in str(err)
        else:
            raise AssertionError('Page drift accepted')
        assert page.read_bytes() == drift
        page.write_bytes(original)
        install()
        assert hashlib.sha256(page.read_bytes()).hexdigest() == FIXED
        stamp = page.stat().st_mtime_ns
        install()
        assert page.stat().st_mtime_ns == stamp and hashlib.sha256(page.read_bytes()).hexdigest() == FIXED

        # A private include view, not an overlay write to the SDK under test.
        for source in (UI / 'include').rglob('*'):
            dest = shadow / source.relative_to(UI)
            if source.is_dir():
                dest.mkdir(exist_ok=True, parents=True)
            elif dest != page:
                dest.symlink_to(source)
        output = project / 'probe'
        flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie'] if args.sanitize else []
        subprocess.run(['c++', '-std=c++17', '-pthread', *flags, '-I'+str(shadow/'include'),
                        '-I'+str(UI/'test/host'), '-MD', '-MF', str(project/'deps'),
                        str(UI/'src/FreeInkUI.cpp'), str(Path(__file__).with_name('publication_page.cpp')),
                        '-o', str(output)], check=True)
        assert str(page) in (project/'deps').read_text() and str(UI/PAGE) not in (project/'deps').read_text()
        subprocess.run([output], check=True)


if __name__ == '__main__':
    main()
