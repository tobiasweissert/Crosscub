"""Portable orchestration contracts, not hosted build or performance qualification."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WorkflowReliability(unittest.TestCase):
    def test_release_admits_before_install_and_skips_expensive_tests(self):
        text = (ROOT / '.github/workflows/release.yml').read_text().split('  build-release:',1)[0]
        self.assertLess(text.index('--ci-check'), text.index('pioarduino==6.1.19'))
        for name in ['Install pinned OTA test Core prerequisite', 'Release tooling regression tests (synthetic images, never firmware)']:
            step = text.split('- name: '+name,1)[1].split('      - ',1)[0]
            self.assertIn("if: steps.version.outputs.skip_release != 'true'", step)


    def test_formatter_and_portable_policy_are_early_independent_jobs(self):
        text = (ROOT/'.github/workflows/ci.yml').read_text()
        formatter = text.split('  clang-format:', 1)[1].split('  cppcheck:', 1)[0]
        self.assertNotIn('submodules:', formatter)
        self.assertNotIn('llvm.sh', formatter)
        self.assertIn('clang-format==21.1.8', formatter)
        portable = text.split('  portable-policy:', 1)[1].split('  build:', 1)[0]
        self.assertNotIn('needs:', portable)
        self.assertIn('SCons==4.8.1', portable)
        self.assertIn('test/crosspoint_migration/docs-ci', portable)
        build = text.split('  build:', 1)[1].split('  required-tests:', 1)[0]
        self.assertNotIn('unittest discover -s test/crosspoint_migration/docs-ci', build)

    def test_optin_benchmark_is_nonpublishing_bounded_and_measured(self):
        text = (ROOT/'.github/workflows/ci.yml').read_text()
        self.assertIn('options: ["1", "2"]', text)
        self.assertIn('BUILD_JOBS:', text)
        self.assertIn('Cold qualification requires one worker', text)
        self.assertIn('command time -a', text)
        self.assertIn('elapsed_seconds=%e', text)
        self.assertIn('max_process_rss_kib=%M', text)
        self.assertIn('build-resources.txt', text)
        release = (ROOT/'.github/workflows/release.yml').read_text()
        self.assertIn('pio run -j1 -e sticky', release)

    def test_cache_identity_changes_with_mutable_inputs_not_release_version(self):
        import sys
        import tempfile
        sys.path.insert(0, str(ROOT / 'scripts'))
        self.assertTrue((ROOT / 'scripts/pio_cache_identity.py').exists(), 'mutable cache identity missing')
        import pio_cache_identity as cache
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'scripts').mkdir()
            ini = root/'platformio.ini'; ini.write_text('[crosssmudge]\nversion = 2.0.3\n[base]\ncustom_sdkconfig = CONFIG_TEST=y\n')
            patcher = root/'scripts/patch_pioarduino_cache.py'; patcher.write_text('patch-v1')
            def identity(**kw):
                return cache.identity(root, runner_os='Linux', runner_arch=kw.get('arch','X64'), sdk_sha='a'*40)
            original = identity()
            ini.write_text(ini.read_text().replace('2.0.3','2.0.4'))
            self.assertEqual(original, identity())
            self.assertNotEqual(original, identity(arch='ARM64'))
            ini.write_text(ini.read_text().replace('CONFIG_TEST=y','CONFIG_TEST=n'))
            self.assertNotEqual(original, identity())
            changed = identity(); patcher.write_text('patch-v2')
            self.assertNotEqual(changed, identity())


if __name__ == '__main__':
    unittest.main()
