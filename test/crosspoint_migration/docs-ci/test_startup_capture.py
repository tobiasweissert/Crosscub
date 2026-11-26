"""Synthetic host qualification; no target build or installed SDK required."""
import importlib.util
from pathlib import Path
import unittest
import hashlib
import json
import os
import subprocess
import sys
import tempfile


def digest_tree(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file() and not p.is_symlink()}


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='startup-capture-', dir=os.environ.get('TMPDIR'))
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.project = self.base / 'project'
        self.home = self.base / 'home with spaces'
        self.out = self.base / 'evidence'
        self.project.mkdir()
        self.libs = self.home / '.platformio/packages/framework-arduinoespressif32-libs/esp32s3'
        self.platform = self.home / '.platformio/platforms/espressif32'
        self.header = self.libs / 'qio_opi/include/sdkconfig.h'
        self.dep = self.project / '.pio/build/sticky/src/main.cpp.d'
        self.write(self.project / 'platformio.ini', '[base]\nboard = actual-board\nplatform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip\n[env:sticky]\nextends = base\n')
        self.write(self.platform / 'boards/actual-board.json', json.dumps({'build': {'arduino': {'memory_type': 'qio_opi'}}}))
        self.write(self.platform / 'platform.json', '{"version":"55.03.311"}')

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def complete_fixture(self):
        module = load()
        self.assertIsNotNone(module)
        values = {k: 'n' for k in module.KEYS}
        values.update(CONFIG_SPIRAM='y', CONFIG_SPIRAM_SPEED='80', CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL='1024',
                      CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL='0')
        config = ''.join(k + '=' + v + '\n' for k, v in values.items())
        self.write(self.project / 'sdkconfig.sticky', config)
        self.write(self.libs / 'sdkconfig', config)
        self.write(self.header, ''.join('#define ' + k + ' ' + ('1' if v == 'y' else v) + '\n' for k, v in values.items() if v != 'n'))
        escaped = str(self.header).replace(' ', '\\ ')
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: src/main.cpp \\\n ' + escaped + '\n')

    def run_cache_hit_hook(self, explicit_path=''):
        """Replay the production pre-hook with cached package, fresh checkout."""
        import runpy
        from unittest.mock import patch
        builder = self.platform / 'builder/frameworks/arduino.py'
        self.write(builder, '''flag_any_custom_sdkconfig = (
    FRAMEWORK_LIB_DIR is not None
    and exists(str(Path(FRAMEWORK_LIB_DIR) / chip_variant / "sdkconfig.orig"))
)''')
        self.write(builder.with_name('component_manager.py'), '')
        self.write(self.libs / 'sdkconfig.orig', 'original template, not resolved\n')
        self.write(self.libs / 'sdkconfig.crosspoint', 'CONFIG_SPIRAM=y\n')
        owner = self

        class Environment:
            def PioPlatform(self):
                return self

            def get_dir(self):
                return str(owner.platform)

            def get_package_dir(self, name):
                return str(owner.libs.parent)

            def GetProjectOption(self, name, fallback=''):
                return 'CONFIG_SPIRAM=y' if name == 'custom_sdkconfig' else fallback

            def BoardConfig(self):
                return {'build.mcu': 'esp32s3', 'build.esp-idf.sdkconfig_path': explicit_path}

            def subst(self, value):
                return {'$PROJECT_DIR': str(owner.project), '$PIOENV': 'sticky'}[value]

        with patch.object(sys, 'argv', ['patch_pioarduino_cache.py']):
            runpy.run_path(str(ROOT / 'scripts/patch_pioarduino_cache.py'),
                           init_globals={'env': Environment(), 'Import': lambda name: None})

    def test_cache_hit_fresh_checkout_retains_exact_resolved_config(self):
        self.producer_fixture()
        resolved = self.project / 'sdkconfig.sticky'
        resolved.unlink()
        cached = (self.libs / 'sdkconfig').read_bytes()
        self.run_cache_hit_hook()
        self.assertTrue(resolved.is_file(), 'warm package skips Kconfig but fresh checkout needs resolved config')
        self.assertEqual(resolved.read_bytes(), cached)
        proc = self.run_capture()
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertTrue(self.receipt()['actual_resolved_config_captured'])

    def test_cache_hit_existing_config_conflict_still_fails(self):
        self.producer_fixture()
        resolved = self.project / 'sdkconfig.sticky'
        conflict = resolved.read_bytes() + b'CONFIG_UNRELATED=y\n'
        resolved.write_bytes(conflict)
        self.run_cache_hit_hook()
        self.assertEqual(resolved.read_bytes(), conflict)
        self.assert_incomplete()
        self.assertEqual(self.receipt()['configuration_error'], 'project-package-conflict')

    def test_cache_hit_explicit_config_path_is_not_materialized(self):
        self.complete_fixture()
        resolved = self.project / 'sdkconfig.sticky'
        resolved.unlink()
        self.run_cache_hit_hook(explicit_path='user-owned.sdkconfig')
        self.assertFalse(resolved.exists())
        self.assert_incomplete()
        self.assertEqual(self.receipt()['configuration_error'], 'required-config-missing')

    def test_cache_hit_preserves_dangling_project_symlink(self):
        self.complete_fixture()
        resolved = self.project / 'sdkconfig.sticky'
        resolved.unlink()
        foreign = self.base / 'must-not-create'
        resolved.symlink_to(foreign)
        self.run_cache_hit_hook()
        self.assertTrue(resolved.is_symlink())
        self.assertFalse(foreign.exists())

    def test_cache_hit_missing_package_config_does_not_create_empty_project(self):
        self.complete_fixture()
        resolved = self.project / 'sdkconfig.sticky'
        resolved.unlink()
        (self.libs / 'sdkconfig').unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_cache_hit_hook()
        self.assertFalse(resolved.exists())

    def test_cache_hit_does_not_waive_actual_header_config_check(self):
        self.producer_fixture()
        (self.project / 'sdkconfig.sticky').unlink()
        package = self.libs / 'sdkconfig'
        package.write_text(package.read_text().replace('CONFIG_SPIRAM_SPEED=80', 'CONFIG_SPIRAM_SPEED=40'))
        self.run_cache_hit_hook()
        self.assert_incomplete()
        self.assertEqual(self.receipt()['configuration_error'], 'header-config-conflict')

    def run_capture(self, mode=None):
        before = digest_tree(self.project), digest_tree(self.home)
        env = dict(os.environ, HOME=str(self.home), PYTHONDONTWRITEBYTECODE='1')
        mode = mode or getattr(self, 'capture_mode', '--legacy-dependencies')
        proc = subprocess.run([sys.executable, str(HELPER), mode, '--project', str(self.project), '--env', 'sticky', '--memory-type', 'qio_opi', '--out', str(self.out)],
                              env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual((digest_tree(self.project), digest_tree(self.home)), before, 'capture mutated source/SDK')
        self.assertNotIn(str(self.base), proc.stdout + proc.stderr)
        return proc

    def receipt(self):
        return json.loads((self.out / 'startup-config-manifest.json').read_text())

    def assert_incomplete(self):
        proc = self.run_capture()
        self.assertEqual(proc.returncode, 1, proc.stdout + proc.stderr)
        self.assertFalse(self.receipt()['actual_resolved_config_captured'])
        self.assertTrue(self.receipt()['diagnostics'])
        self.assertFalse(self.receipt()['publication_authorized'])

    def producer_fixture(self):
        self.complete_fixture()
        self.capture_mode = '--require-producer'
        self.dep.write_text('.pio/build/sticky/src/main.cpp.o: src/main.cpp\n')
        proof = '.pio/build/sticky/src/main.cpp.o: src/main.cpp ' + str(self.header).replace(' ', '\\ ') + '\n'
        self.out.mkdir()
        (self.out/'producer-main.d').write_text(proof)
        (self.out/'producer-main.macros').write_bytes(self.header.read_bytes())
        (self.out/'producer-application-sdkconfig.h').write_bytes(self.header.read_bytes())
        receipt = {'environment': 'sticky', 'boundary': 'inner-application-main', 'probe_exit': 0,
                   'compile_argv': ['/fake/g++', '-c', 'src/main.cpp', '-o', '.pio/build/sticky/src/main.cpp.o'],
                   'producer-main.d_sha256': hashlib.sha256(proof.encode()).hexdigest(),
                   'producer-main.macros_sha256': hashlib.sha256((self.out/'producer-main.macros').read_bytes()).hexdigest(),
                   'files': {'producer-application-sdkconfig.h': {'source': str(self.header), 'present': True,
                       'sha256': hashlib.sha256(self.header.read_bytes()).hexdigest()}}}
        receipt.update(dependency_exit=0, macro_exit=0, compile_env_sha256=hashlib.sha256(b'{}').hexdigest(),
                       dependency_argv=['/fake/g++', 'src/main.cpp', '-M', '-MF', str(self.out/'producer-main.d'),
                                        '-MT', '.pio/build/sticky/src/main.cpp.o'],
                       probe_argv=['/fake/g++', 'src/main.cpp', '-E', '-dM', '-o', '/dev/fd/8'])
        data = self.header.read_bytes()
        receipt['macro_projection'] = dict(raw_bytes=len(data), raw_sha256=hashlib.sha256(data).hexdigest(),
                                           config_records=data.count(b'\n'), records=data.count(b'\n'))
        (self.out/'producer-main-receipt.json').write_text(json.dumps(receipt))

    def test_public_failure_identifies_missing_producer_without_raw_config(self):
        self.complete_fixture()
        proc = self.run_capture('--require-producer')
        self.assertEqual(proc.returncode, 1)
        status = json.loads(proc.stdout)
        self.assertEqual(status.get('configuration_error'), 'producer-receipt-missing')
        self.assertEqual(self.receipt()['configuration_error'], status['configuration_error'])
        self.assertNotIn('CONFIG_', proc.stdout + proc.stderr)
        self.assertFalse(status['actual_resolved_config_captured'])

    def test_public_configuration_error_rejects_arbitrary_exception_text(self):
        module = load()
        for exc in (ValueError('secret-canary bearer https://private/config'),
                    KeyError('secret-canary'), TypeError('secret-canary'),
                    ValueError('producer-receipt-missing', 'secret-canary')):
            self.assertEqual(module.public_configuration_error(exc), 'configuration-incomplete')
        self.assertEqual(module.public_configuration_error(ValueError('header-config-conflict')),
                         'header-config-conflict')

    def test_missing_producer_cannot_certify_hosted_capture(self):
        self.complete_fixture()
        self.assertEqual(self.run_capture('--require-producer').returncode, 1)
        self.assertFalse(self.receipt()['actual_resolved_config_captured'])
        self.out = self.base / 'legacy-output'
        self.assertEqual(self.run_capture('--legacy-dependencies').returncode, 0)

    def test_producer_success_artifact_size_boundaries(self):
        for name in ('producer-main.macros', 'producer-main.d'):
            for size in (262143, 262144, 262145):
                with self.subTest(name=name, size=size):
                    self.out = self.base / f'{name}-{size}'
                    self.producer_fixture()
                    artifact = self.out / name
                    original = artifact.read_bytes()
                    # Pad the last line with harmless whitespace; keep all values intact.
                    data = original[:-1].ljust(size - 1, b' ') + b'\n'
                    self.assertEqual(len(data), size)
                    artifact.write_bytes(data)
                    path = self.out / 'producer-main-receipt.json'
                    receipt = json.loads(path.read_text())
                    digest = hashlib.sha256(data).hexdigest()
                    receipt[name + '_sha256'] = digest
                    if name == 'producer-main.macros':
                        receipt['macro_projection'].update(raw_bytes=size, raw_sha256=digest)
                    path.write_text(json.dumps(receipt))
                    if size == 262143:
                        self.assertEqual(self.run_capture().returncode, 0)
                        self.assertTrue(self.receipt()['actual_resolved_config_captured'])
                    else:
                        self.assert_incomplete()

    def test_projection_attestation_consistency(self):
        for field in ('raw_bytes', 'raw_sha256', 'records', 'mixed'):
            with self.subTest(field=field):
                self.out = self.base / field
                self.producer_fixture()
                path = self.out/'producer-main-receipt.json'
                receipt = json.loads(path.read_text())
                projection = receipt['macro_projection']
                if field == 'mixed':
                    raw = b'#define OTHER 1\n' + self.header.read_bytes()
                    projection.update(records=projection['records'] + 1, raw_bytes=len(raw),
                                      raw_sha256=hashlib.sha256(raw).hexdigest())
                elif field == 'raw_sha256':
                    projection[field] = '0' * 64
                else:
                    projection[field] += 1
                path.write_text(json.dumps(receipt))
                if field == 'mixed':
                    self.assertEqual(self.run_capture().returncode, 0)
                else:
                    self.assert_incomplete()

    def test_producer_dependency_closes_observed_mmd_system_header_gap(self):
        self.producer_fixture()
        self.assertEqual(self.run_capture().returncode, 0)
        self.assertTrue(self.receipt()['actual_resolved_config_captured'])

    def test_producer_complete_evidence_required(self):
        self.producer_fixture()
        original = self.out
        import shutil
        # Preserve the same absolute evidence paths for every independent run.
        template = self.base / 'template'
        shutil.copytree(original, template)
        for change in ({'dependency_exit': None}, {'macro_exit': 1}, {'compile_env_sha256': ''},
                       {'probe_error': 'OutputLimitExceeded'}, {'macro_projection': {}}, {'probe_argv': []}):
            with self.subTest(change=change):
                shutil.rmtree(original)
                shutil.copytree(template, original)
                path = original/'producer-main-receipt.json'
                receipt = json.loads(path.read_text())
                receipt.update(change)
                path.write_text(json.dumps(receipt))
                self.assert_incomplete()

    def test_producer_macro_header_disagreement_fails_closed(self):
        self.producer_fixture()
        path = self.out/'producer-main.macros'
        path.write_text(path.read_text().replace('#define CONFIG_SPIRAM 1', '#define CONFIG_SPIRAM 0'))
        receipt_path = self.out/'producer-main-receipt.json'
        receipt = json.loads(receipt_path.read_text())
        receipt['producer-main.macros_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        receipt['macro_projection']['raw_sha256'] = receipt['producer-main.macros_sha256']
        receipt_path.write_text(json.dumps(receipt))
        self.assert_incomplete()

    def test_producer_wrong_environment_or_tampered_dependency_fails_closed(self):
        self.producer_fixture()
        (self.out/'producer-main.d').write_text('tampered\n')
        self.assert_incomplete()

    def test_unsupported_generated_header_cannot_certify(self):
        for index, change in enumerate((
                lambda h: h + '#define CONFIG_SPIRAM_BOOT_INIT\n',
                lambda h: h + '#undef CONFIG_SPIRAM\n',
                lambda h: '#if 0\n' + h + '#endif\n',
                lambda h: '#define CONFIG_OTHER /* unterminated\n' + h)):
            with self.subTest(index=index):
                self.complete_fixture()
                self.out = self.base / ('unsupported-' + str(index))
                self.header.write_text(change(self.header.read_text()))
                self.assert_incomplete()
                self.assertEqual((self.out / 'startup-application-sdkconfig.h').read_bytes(), self.header.read_bytes())
                self.assertEqual((self.out / 'startup-main.cpp.d').read_bytes(), self.dep.read_bytes())

    def test_supported_generated_header_comments_pragma_and_guard(self):
        for index, wrapper in enumerate((
                lambda h: '/* Generated configuration */\n#pragma once\n' + h,
                lambda h: '#ifndef SDKCONFIG_H\n#define SDKCONFIG_H\n' + h + '#endif /* SDKCONFIG_H */\n')):
            with self.subTest(index=index):
                self.complete_fixture()
                self.out = self.base / ('supported-' + str(index))
                self.header.write_text(wrapper(self.header.read_text()))
                self.assertEqual(self.run_capture().returncode, 0)

    def test_candidate_metadata_failure_retains_actual_selection(self):
        for index, content in enumerate((
                '{"build":{"arduino":{"memory_type":"dio_opi"}}}',
                '{"build": []}', None, '{bad json')):
            with self.subTest(index=index):
                self.complete_fixture()
                self.out = self.base / ('metadata-' + str(index))
                board = self.platform / 'boards/actual-board.json'
                if content is None:
                    board.unlink()
                else:
                    board.write_text(content)
                other = self.dep.with_name('main.cpp.o.d')
                self.write(other, self.dep.read_text())
                self.assert_incomplete()
                for name, original in (('main.cpp.d', self.dep), ('main.cpp.o.d', other),
                                       ('application-sdkconfig.h', self.header)):
                    self.assertTrue((self.out / ('startup-' + name)).is_file(), name)
                    self.assertEqual((self.out / ('startup-' + name)).read_bytes(), original.read_bytes())
                self.assertEqual(self.receipt()['actual_header'], str(self.header))

    def test_expanded_receipt_has_compact_incomplete_fallback(self):
        self.complete_fixture()
        additions = ''.join('CONFIG_SPIRAM_X' + str(i) + '=n\n' for i in range(10000))
        for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
            path.write_text(path.read_text() + additions)
            self.assertLess(path.stat().st_size, 262144)
        # This is real JSON expansion, not an oversized raw input.
        expanded = {k: v for k, v in load().parse_config((self.libs / 'sdkconfig').read_text()).items()
                    if k.startswith('CONFIG_SPIRAM')}
        self.assertGreater(len(json.dumps(expanded, indent=2).encode()), 262144)
        proc = self.run_capture()
        self.assertEqual(proc.returncode, 1)
        manifest = self.out / 'startup-config-manifest.json'
        self.assertTrue(manifest.is_file(), 'bounded diagnostic receipt missing')
        receipt = self.receipt()
        self.assertFalse(receipt['actual_resolved_config_captured'])
        self.assertIn('receipt-detail-limit', receipt['diagnostics'])
        self.assertNotIn('all_spiram_resolved_values', receipt)
        self.assertLessEqual(manifest.stat().st_size, 262144)
        self.assertLessEqual(sum(p.stat().st_size for p in self.out.iterdir()), 2097152)
        for name, original in (('project-resolved.sdkconfig', self.project / 'sdkconfig.sticky'),
                               ('package-resolved.sdkconfig', self.libs / 'sdkconfig'),
                               ('application-sdkconfig.h', self.header), ('main.cpp.d', self.dep)):
            self.assertEqual((self.out / ('startup-' + name)).read_bytes(), original.read_bytes())
            self.assertEqual(receipt['files'][name]['sha256'], hashlib.sha256(original.read_bytes()).hexdigest())

    def test_inheritance_depth_and_cycle_are_private_bounded_failures(self):
        for cyclic in (False, True, 'short-cycle'):
            with self.subTest(cyclic=cyclic):
                self.complete_fixture()
                self.out = self.base / ('inheritance-' + str(cyclic))
                text = '[env:sticky]\nextends = level0\n'
                text += ''.join('[level' + str(i) + ']\nextends = level' + str(i + 1) + '\n'
                                for i in range(1100))
                text += '[level1100]\n' + ('extends = level0\n' if cyclic else 'board = actual-board\n')
                if cyclic == 'short-cycle':
                    text = '[env:sticky]\nextends = base\n[base]\nextends = env:sticky\n'
                self.write(self.project / 'platformio.ini', text)
                self.assertLess(len(text.encode()), 262144)
                proc = self.run_capture()
                self.assertEqual(proc.returncode, 1)
                self.assertEqual(proc.stderr, '', 'public traceback/diagnostic disclosure')
                self.assertLess(len(proc.stdout), 256)
                status = json.loads(proc.stdout)
                self.assertEqual(set(status), {'actual_resolved_config_captured', 'captured_bytes', 'captured_files', 'configuration_error'})
                self.assertEqual(status['configuration_error'], 'configuration-incomplete')
                self.assertFalse(self.receipt()['actual_resolved_config_captured'])
                self.assertTrue(self.receipt()['diagnostics'])
                self.assertIn('inheritance-cycle' if cyclic == 'short-cycle' else 'inheritance-depth-limit',
                              self.receipt()['diagnostics'])
                self.assertEqual((self.out / 'startup-main.cpp.d').read_bytes(), self.dep.read_bytes())
                self.assertEqual((self.out / 'startup-application-sdkconfig.h').read_bytes(), self.header.read_bytes())

    def invisible_fixture(self, **changes):
        self.complete_fixture()
        values = load().parse_config((self.libs / 'sdkconfig').read_text())
        values.update(CONFIG_SPIRAM_BOOT_HW_INIT='y',
                      CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY='y')
        values.update(changes)
        values.pop('CONFIG_SPIRAM_IGNORE_NOTFOUND')
        for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
            path.write_text(''.join(k + '=' + v + '\n' for k, v in values.items()))
        self.header.write_text(''.join('#define ' + k + ' ' + ('1' if v == 'y' else v) + '\n'
                                       for k, v in values.items() if v != 'n'))
        self.kconfig = self.home / '.platformio/packages/framework-espidf/components/esp_psram/Kconfig.spiram.common'
        self.write(self.kconfig, 'config SPIRAM_IGNORE_NOTFOUND\n'
                   '    bool "Ignore PSRAM when not found"\n    default "n"\n'
                   '    depends on SPIRAM_BOOT_HW_INIT && !SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY && !SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY\n'
                   '    help\n        Normally, if psram initialization is enabled during compile time but not found at runtime, it\n'
                   '        is seen as an error making the CPU panic.\n')

    def test_source_proven_invisible_ignore_preserves_raw_absence(self):
        for index, changes in enumerate(({}, {'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY': 'n',
                                              'CONFIG_SPIRAM_ALLOW_NOINIT_SEG_EXTERNAL_MEMORY': 'y'},
                                             {'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY': 'n',
                                              'CONFIG_SPIRAM_BOOT_HW_INIT': 'n'})):
            with self.subTest(index=index):
                self.invisible_fixture(**changes)
                self.out = self.base / ('invisible-' + str(index))
                self.assertEqual(self.run_capture().returncode, 0)
                receipt = self.receipt()
                key = 'CONFIG_SPIRAM_IGNORE_NOTFOUND'
                self.assertEqual(receipt['selected_resolved_values'][key], 'ABSENT_UNRESOLVED')
                proof = receipt['qualified_dependency_invisible'][key]
                self.assertEqual(proof['raw_header_state'], 'UNDEFINED')
                self.assertEqual(proof['raw_config_state'], 'ABSENT_UNRESOLVED')
                self.assertEqual(proof['source_sha256'], hashlib.sha256(self.kconfig.read_bytes()).hexdigest())
                self.assertEqual(proof['certainty'], 'source-proven-dependency-inactive')
                self.assertTrue(proof['false_dependency_terms'])
                self.assertFalse(receipt['publication_authorized'])

    def test_invisible_ignore_requires_exact_source_and_consistent_absence(self):
        for case in ('visible', 'missing-source', 'changed-dependency', 'extra-property',
                     'duplicate-definition', 'header-present', 'unproven-condition',
                     'contradictory-condition', 'unrelated-absence', 'enabled-ignore'):
            with self.subTest(case=case):
                self.invisible_fixture()
                self.out = self.base / case
                if case == 'missing-source':
                    self.kconfig.unlink()
                elif case == 'changed-dependency':
                    self.kconfig.write_text(self.kconfig.read_text().replace(' && !SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY', ''))
                elif case == 'extra-property':
                    self.kconfig.write_text(self.kconfig.read_text() + '    default y\n')
                elif case == 'duplicate-definition':
                    self.kconfig.write_text(self.kconfig.read_text() * 2)
                elif case == 'header-present':
                    self.header.write_text(self.header.read_text() + '#define CONFIG_SPIRAM_IGNORE_NOTFOUND 0\n')
                elif case == 'contradictory-condition':
                    self.header.write_text(self.header.read_text().replace('#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n', ''))
                elif case == 'enabled-ignore':
                    for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
                        path.write_text(path.read_text() + 'CONFIG_SPIRAM_IGNORE_NOTFOUND=y\n')
                    self.header.write_text(self.header.read_text() + '#define CONFIG_SPIRAM_IGNORE_NOTFOUND 1\n')
                else:
                    for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
                        text = path.read_text()
                        if case == 'visible':
                            text = text.replace('CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y', 'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=n')
                        elif case == 'unproven-condition':
                            text = text.replace('CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y\n', '')
                        else:
                            text = text.replace('CONFIG_SPIRAM_BOOT_INIT=n\n', '')
                        path.write_text(text)
                    if case in ('visible', 'unproven-condition'):
                        self.header.write_text(self.header.read_text().replace('#define CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY 1\n', ''))
                self.assert_incomplete()

    def test_observed_identifier_alias_and_chain_match_literals(self):
        for length in (1, 4):
            with self.subTest(length=length):
                self.complete_fixture()
                self.out = self.base / ('alias-' + str(length))
                first = 'CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY'
                chain = ['CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM'] + ['CONFIG_ALIAS_' + str(i) for i in range(length - 1)]
                for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
                    path.write_text(path.read_text() + first + '=y\n' + ''.join(k + '=y\n' for k in chain))
                self.header.write_text(self.header.read_text() + ''.join('#define ' + a + ' ' + b + '\n'
                    for a, b in zip([first] + chain, chain + ['1'])))
                self.assertEqual(self.run_capture().returncode, 0)

    def test_identifier_alias_undefined_cycles_conflicts_and_depth_fail_closed(self):
        for case in ('undefined', 'cycle', 'conflict', 'target-raw-conflict', 'expression', 'depth'):
            with self.subTest(case=case):
                self.complete_fixture()
                self.out = self.base / ('bad-alias-' + case)
                first = 'CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY'
                for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
                    path.write_text(path.read_text() + first + '=y\n' +
                                    ('CONFIG_ALIAS=n\n' if case == 'target-raw-conflict' else ''))
                tail = {'undefined': '', 'cycle': '#define CONFIG_ALIAS ' + first + '\n',
                        'target-raw-conflict': '#define CONFIG_ALIAS 1\n',
                        'conflict': '#define CONFIG_ALIAS 0\n', 'expression': '#define CONFIG_ALIAS (1)\n',
                        'depth': ''.join('#define CONFIG_ALIAS' + str(i) + ' CONFIG_ALIAS' + str(i + 1) + '\n' for i in range(70)) + '#define CONFIG_ALIAS70 1\n'}[case]
                target = 'CONFIG_ALIAS0' if case == 'depth' else 'CONFIG_ALIAS'
                self.header.write_text(self.header.read_text() + '#define ' + first + ' ' + target + '\n' + tail)
                self.assert_incomplete()

    def test_header_only_flag_is_conflict(self):
        self.complete_fixture()
        with self.header.open('a') as stream:
            stream.write('#define CONFIG_SPIRAM_INVENTED 1\n')
        self.assert_incomplete()

    def test_absent_selected_value_is_unresolved_not_disabled(self):
        self.complete_fixture()
        for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
            path.write_text(path.read_text().replace('CONFIG_SPIRAM_BOOT_INIT=n\n', ''))
        self.assert_incomplete()

    def test_candidate_matching_config_does_not_override_actual_header_path(self):
        self.complete_fixture()
        other = self.libs / 'dio_opi/include/sdkconfig.h'
        self.write(other, self.header.read_text())
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: ' + str(other).replace(' ', '\\ ') + '\n')
        self.assert_incomplete()
        self.assertEqual(self.receipt()['actual_header'], str(other))
        self.assertTrue((self.out / 'startup-application-sdkconfig.h').exists())

    def test_optional_oversize_is_diagnostic_not_success(self):
        self.complete_fixture()
        self.write(self.project / 'sdkconfig.defaults', 'x' * (262144 + 1))
        self.assert_incomplete()

    def test_optional_overwrite_is_refused_and_diagnostic(self):
        self.complete_fixture()
        self.out.mkdir()
        self.write(self.project / 'sdkconfig.defaults', 'baseline')
        self.write(self.out / 'startup-project-defaults.sdkconfig', 'untouched')
        self.assert_incomplete()
        self.assertEqual((self.out / 'startup-project-defaults.sdkconfig').read_text(), 'untouched')

    def test_duplicate_config_and_header_definitions_are_rejected(self):
        self.complete_fixture()
        for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
            with path.open('a') as stream:
                stream.write('CONFIG_SPIRAM_SPEED=40\nCONFIG_SPIRAM_SPEED=80\n')
        self.assert_incomplete()

    def test_duplicate_header_definition_is_rejected(self):
        self.complete_fixture()
        with self.header.open('a') as stream:
            stream.write('#define CONFIG_SPIRAM_SPEED 40\n#define CONFIG_SPIRAM_SPEED 80\n')
        self.assert_incomplete()

    def test_existing_local_override_cannot_be_ignored(self):
        self.complete_fixture()
        with (self.project / 'platformio.ini').open('a') as stream:
            stream.write('[platformio]\nextra_configs = platformio.local.ini\n')
        self.write(self.project / 'platformio.local.ini', '[env:sticky]\nboard = other-board\n')
        self.assert_incomplete()

    def test_path_traversal_in_dependency_is_not_normalized_to_authority(self):
        self.complete_fixture()
        path = self.header.parent / '..' / 'include/sdkconfig.h'
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: ' + str(path).replace(' ', '\\ ') + '\n')
        self.assert_incomplete()

    def test_ci44834357235_missing_header_in_retained_main_closure_is_explicit(self):
        # Authenticated CI44834357235 main.cpp.d contains SDK FreeRTOS headers,
        # but no sdkconfig.h prerequisite. Root paths alone cannot select a
        # memory variant; the matching board/config candidate is not authority.
        self.complete_fixture()
        prerequisite = self.libs / 'include/freertos/config/include/freertos/FreeRTOSConfig.h'
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: src/main.cpp \\\n ' +
                   str(prerequisite).replace(' ', '\\ ') + '\n')
        self.assert_incomplete()
        self.assertEqual(self.receipt().get('configuration_error'), 'dependency-header-missing')
        self.assertEqual((self.out / 'startup-main.cpp.d').read_bytes(), self.dep.read_bytes())
        self.assertFalse((self.out / 'startup-application-sdkconfig.h').exists())

    def test_missing_dependency_never_uses_board_candidate(self):
        self.complete_fixture()
        self.dep.unlink()
        self.assert_incomplete()
        self.assertFalse((self.out / 'startup-application-sdkconfig.h').exists())

    def test_alternate_object_dependency_filename(self):
        self.complete_fixture()
        self.dep.rename(self.dep.with_name('main.cpp.o.d'))
        self.assertEqual(self.run_capture().returncode, 0)
        self.assertTrue((self.out / 'startup-main.cpp.o.d').exists())

    def test_project_generated_header_is_accepted_only_if_dependency_selected(self):
        self.complete_fixture()
        generated = self.project / '.pio/build/sticky/config/sdkconfig.h'
        self.write(generated, self.header.read_text())
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: .pio/build/sticky/config/sdkconfig.h\n')
        self.assertEqual(self.run_capture().returncode, 0)
        self.assertEqual(self.receipt()['actual_header'], str(generated))

    def test_ambiguous_dependency_headers_are_incomplete(self):
        self.complete_fixture()
        with self.dep.open('a') as stream:
            stream.write('.pio/build/sticky/src/main.cpp.o: /foreign/sdkconfig.h\n')
        self.assert_incomplete()

    def test_two_dependency_files_must_agree(self):
        self.complete_fixture()
        self.write(self.dep.with_name('main.cpp.o.d'), '.pio/build/sticky/src/main.cpp.o: .pio/build/sticky/config/sdkconfig.h\n')
        self.assert_incomplete()
        self.assertTrue((self.out / 'startup-main.cpp.o.d').is_file())

    def test_foreign_header_is_not_copied(self):
        self.complete_fixture()
        foreign = self.project / 'sdkconfig.h'
        self.write(foreign, self.header.read_text())
        self.write(self.dep, '.pio/build/sticky/src/main.cpp.o: sdkconfig.h\n')
        self.assert_incomplete()
        self.assertFalse((self.out / 'startup-application-sdkconfig.h').exists())

    def test_false_and_numeric_header_conflicts(self):
        for flag in ('#define CONFIG_SPIRAM_BOOT_INIT 0\n', '#define CONFIG_SPIRAM_SPEED 40\n'):
            with self.subTest(flag=flag):
                self.complete_fixture()
                self.write(self.header, self.header.read_text().replace('#define CONFIG_SPIRAM_SPEED 80\n', '') + flag)
                self.assert_incomplete()
                self.out = self.base / ('evidence-' + str(len(flag)))

    def test_project_package_conflict(self):
        self.complete_fixture()
        with (self.libs / 'sdkconfig').open('a') as stream:
            stream.write('CONFIG_OTHER=y\n')
        self.assert_incomplete()

    def test_missing_required_config(self):
        self.complete_fixture()
        (self.project / 'sdkconfig.sticky').unlink()
        self.assert_incomplete()

    def test_header_symlink_not_followed(self):
        self.complete_fixture()
        original = self.base / 'private-header'
        self.header.rename(original)
        self.header.symlink_to(original)
        self.assert_incomplete()
        self.assertFalse((self.out / 'startup-application-sdkconfig.h').exists())

    def test_header_parent_symlink_not_followed(self):
        self.complete_fixture()
        original = self.base / 'private-include'
        self.header.parent.rename(original)
        self.header.parent.symlink_to(original, target_is_directory=True)
        self.assert_incomplete()

    def test_nonregular_required_file_is_rejected(self):
        self.complete_fixture()
        self.header.unlink()
        os.mkfifo(self.header)
        self.assert_incomplete()

    def test_total_capture_bound_is_fail_closed(self):
        self.complete_fixture()
        payload = '#' * 262144
        for path in (self.project / 'sdkconfig.defaults', self.libs / 'sdkconfig.orig',
                     self.libs / 'sdkconfig.crosspoint', self.libs / 'ld/memory.ld',
                     self.libs / 'ld/sections.ld', self.project / '.pio/build/sticky/config/sdkconfig.h',
                     self.project / '.pio/build/sticky/config/sdkconfig.json', self.project / '.pio/build/sticky/memory.ld'):
            self.write(path, payload)
        self.assert_incomplete()
        self.assertLessEqual(sum(p.stat().st_size for p in self.out.iterdir()), 2097152)

    def test_receipt_cannot_be_overwritten(self):
        self.complete_fixture()
        self.assertEqual(self.run_capture().returncode, 0)
        before = digest_tree(self.out)
        self.assertEqual(self.run_capture().returncode, 1)
        self.assertEqual(digest_tree(self.out), before)

    def test_selected_values_survive_missing_dependency(self):
        self.complete_fixture()
        self.dep.unlink()
        self.assert_incomplete()
        self.assertEqual(self.receipt().get('selected_resolved_values', {}).get('CONFIG_SPIRAM_SPEED'), '80')

    def test_selected_bt_flag_must_match_header(self):
        self.complete_fixture()
        with self.header.open('a') as stream:
            stream.write('#define CONFIG_BT_ENABLED 1\n')
        self.assert_incomplete()

    def test_matching_but_invalid_boolean_is_not_accepted(self):
        self.complete_fixture()
        for path in (self.project / 'sdkconfig.sticky', self.libs / 'sdkconfig'):
            path.write_text(path.read_text().replace('CONFIG_SPIRAM=y', 'CONFIG_SPIRAM=potato'))
        self.header.write_text(self.header.read_text().replace('#define CONFIG_SPIRAM 1', '#define CONFIG_SPIRAM potato'))
        self.assert_incomplete()

    def test_invalid_cli_tokens_are_rejected_without_path_disclosure(self):
        for token in ('../sticky', 'sticky/name', '', 'sticky.invalid'):
            with self.subTest(token=token):
                result = subprocess.run([sys.executable, str(HELPER), '--project', str(self.project), '--env', token,
                                         '--memory-type', 'qio_opi', '--out', str(self.out)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(self.out.exists())
                self.assertNotIn(str(self.base), result.stdout + result.stderr)

    def test_invalid_memory_type_is_rejected(self):
        result = subprocess.run([sys.executable, str(HELPER), '--project', str(self.project), '--env', 'sticky',
                                 '--memory-type', '../foreign', '--out', str(self.out)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.out.exists())

    def test_missing_pinned_board_manifest_is_incomplete(self):
        self.complete_fixture()
        (self.platform / 'boards/actual-board.json').unlink()
        self.assert_incomplete()

    def test_output_symlink_parent_never_creates_foreign_directory(self):
        self.complete_fixture()
        foreign = self.base / 'foreign'
        foreign.mkdir()
        link = self.base / 'linked'
        link.symlink_to(foreign, target_is_directory=True)
        self.out = link / 'new-evidence'
        proc = self.run_capture()
        self.assertEqual(proc.returncode, 1)
        self.assertEqual(list(foreign.iterdir()), [])

    def test_malformed_installed_version_is_private_diagnostic(self):
        self.complete_fixture()
        self.write(self.platform / 'platform.json', '{"version":123}')
        self.assert_incomplete()

    def test_actual_dependency_header_and_flat_hash_verified_capture(self):
        self.complete_fixture()
        proc = self.run_capture()
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertTrue((self.out / 'startup-config-manifest.json').exists(), 'capture receipt missing')
        receipt = self.receipt()
        self.assertTrue(receipt['actual_resolved_config_captured'])
        self.assertEqual(receipt['actual_header'], str(self.header))
        self.assertFalse(receipt['publication_authorized'])
        self.assertEqual(receipt['selected_resolved_values']['CONFIG_SPIRAM_SPEED'], '80')
        self.assertEqual((self.out / 'startup-main.cpp.d').read_bytes(), self.dep.read_bytes())
        for name, entry in receipt['files'].items():
            if entry['present']:
                payload = self.out / ('startup-' + name)
                self.assertTrue(payload.is_file())
                self.assertEqual(hashlib.sha256(payload.read_bytes()).hexdigest(), entry['sha256'])
        self.assertTrue(all(p.is_file() for p in self.out.iterdir()))

ROOT = Path(__file__).resolve().parents[3]
HELPER = ROOT / 'scripts/capture_sticky_startup_config.py'


def load():
    if not HELPER.exists():
        return None
    spec = importlib.util.spec_from_file_location('startup_capture', HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class StartupParserTests(unittest.TestCase):
    def test_identifier_alias_exact_depth_boundary(self):
        module = load()
        for edges in (32, 33):
            definitions = {'CONFIG_ALIAS' + str(i): 'CONFIG_ALIAS' + str(i + 1) for i in range(edges)}
            definitions['CONFIG_ALIAS' + str(edges)] = '1'
            if edges == 32:
                self.assertEqual(module.resolve_header_value(definitions, 'CONFIG_ALIAS0'), '1')
            else:
                with self.assertRaises(ValueError):
                    module.resolve_header_value(definitions, 'CONFIG_ALIAS0')

    def test_config_disabled_numeric_and_unknown(self):
        module = load()
        self.assertIsNotNone(module, 'capture helper missing')
        self.assertEqual(module.parse_config('CONFIG_SPIRAM=y\n# CONFIG_SPIRAM_BOOT_INIT is not set\nCONFIG_SPIRAM_SPEED=80\nunknown\n'),
                         {'CONFIG_SPIRAM': 'y', 'CONFIG_SPIRAM_BOOT_INIT': 'n', 'CONFIG_SPIRAM_SPEED': '80'})


class WorkflowTests(unittest.TestCase):
    def capture_block(self):
        workflow = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('startup_capture_status=0', workflow, 'sealed-stage capture missing')
        start = workflow.index('          startup_capture_status=0')
        end = workflow.index('          git diff --binary HEAD > sticky-evidence/source-after-build.patch', start)
        block = '\n'.join(line[10:] for line in workflow[start:end].splitlines())
        return workflow, block

    def test_capture_order_and_failure_does_not_skip_primary_or_hash_stage(self):
        workflow, block = self.capture_block()
        self.assertLess(workflow.index("trap 'test -f sticky-evidence/image-validation.json"), workflow.index('startup_capture_status=0'))
        self.assertLess(workflow.index('startup_capture_status=0'), workflow.index("(out/'artifact-sha256.json').write_text"))
        self.assertIn('          test "$startup_capture_status" -eq 0', workflow)
        self.assertNotIn('uses: actions/upload-artifact@', workflow)
        with tempfile.TemporaryDirectory(prefix='startup-stage-', dir=os.environ.get('TMPDIR')) as tmp:
            root = Path(tmp)
            (root / 'scripts').mkdir()
            (root / 'scripts/capture_sticky_startup_config.py').write_text('raise SystemExit(1)\n')
            (root / 'sticky-evidence').mkdir()
            primary = root / 'sticky-evidence/firmware.bin'
            primary.write_bytes(b'original-primary')
            result = subprocess.run(['bash', '-c', 'set -euo pipefail\n' + block +
                                     '\nprintf hash-stage > sticky-evidence/artifact-sha256.json\n' +
                                     'test "$startup_capture_status" -eq 0'], cwd=root, capture_output=True, text=True,
                                    env={**os.environ, 'PIO_ENV': 'sticky'})
            self.assertEqual(result.returncode, 1)
            self.assertEqual(primary.read_bytes(), b'original-primary')
            self.assertEqual((root / 'sticky-evidence/artifact-sha256.json').read_text(), 'hash-stage')


class DependencyTests(unittest.TestCase):
    def test_continuation_is_make_whitespace_not_token_concatenation(self):
        module = load()
        self.assertEqual(module.parse_dependencies('main.o: first\\\nsecond\n', 'main.o'), ['first', 'second'])

    def test_make_variables_foreign_rules_and_missing_target_are_rejected(self):
        module = load()
        for text in ('main.o: $(SDK)/sdkconfig.h\n', 'main.o: a\nother.o: b\n',
                     'other.o: a\n', 'main.o: a\\', 'main.o: a\nmain.o: b\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                module.parse_dependencies(text, 'main.o')

    def test_make_phony_rules_do_not_add_false_headers(self):
        module = load()
        self.assertEqual(module.parse_dependencies('main.o: actual.h\nwrong/sdkconfig.h:\n', 'main.o'), ['actual.h'])

    def test_continuation_and_make_escaped_paths(self):
        module = load()
        self.assertTrue(hasattr(module, 'parse_dependencies'), 'dependency parser missing')
        text = 'main.o: src/main.cpp \\\n /sdk/space\\ path/sdkconfig.h /sdk/hash\\#dollar$$/sdkconfig.h\n'
        self.assertEqual(module.parse_dependencies(text, 'main.o'),
                         ['src/main.cpp', '/sdk/space path/sdkconfig.h', '/sdk/hash#dollar$/sdkconfig.h'])


if __name__ == '__main__':
    unittest.main()
