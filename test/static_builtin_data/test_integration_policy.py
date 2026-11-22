"""Actual source policy; recording/host evidence, NOT resolved SDK/target proof."""
import ast
import configparser
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
EXPERIMENT = 'sticky-static-builtins-experimental'


def option(c, section, key, default=''):
    import re
    value = c[section].get(key)
    if value is None:
        parent = c[section].get('extends')
        value = option(c, parent, key, default) if parent else default
    return re.sub(r'\$\{([^}]+)\}', lambda m: option(c, *m[1].rsplit('.', 1)), value)


class IntegrationPolicy(unittest.TestCase):
    def test_production_packing_sdk_delta_and_raw_controls(self):
        c = configparser.ConfigParser(interpolation=None)
        c.read(ROOT/'platformio.ini')
        self.assertEqual(c['platformio']['default_envs'], 'sticky')
        for section in ('crosspoint', 'crosssmudge', 'crossink'):
            self.assertEqual(c[section]['version'], '2.0.1')
        packed = ['sticky', 'default', 'sticky-owner-test', EXPERIMENT]
        for name in packed:
            self.assertEqual(option(c, 'env:'+name, 'custom_static_builtin_data', 'no'), 'yes')
            self.assertEqual(option(c, 'env:'+name, 'custom_i18n_builtin_langs'), 'all')
        for name in ['sticky-debug', 'debug', 'sticky-simulator', 'simulator']:
            self.assertEqual(option(c, 'env:'+name, 'custom_static_builtin_data', 'no'), 'no')
        self.assertEqual(option(c, 'base', 'custom_static_builtin_data', 'no'), 'no')
        self.assertEqual(option(c, 'firmware_tuned', 'custom_static_builtin_data', 'no'), 'no')
        expected = {'CONFIG_SPIRAM_BOOT_HW_INIT=y', 'CONFIG_SPIRAM_BOOT_INIT=y',
                    'CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION=y', 'CONFIG_SPIRAM_MEMTEST=y',
                    'CONFIG_SPIRAM_IGNORE_NOTFOUND=n', 'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y'}
        def settings(section):
            return {line.strip() for line in option(c, section, 'custom_sdkconfig').splitlines()
                    if line.strip() and not line.lstrip().startswith(';')}
        baseline = settings('env:sticky-debug')
        self.assertIn('CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=1024', baseline)
        self.assertIn('CONFIG_BT_ENABLED=n', baseline)
        for name in packed:
            candidate = settings('env:'+name)
            self.assertEqual(candidate-baseline, expected)
            self.assertFalse(baseline-candidate)
        for key in ['custom_sdkconfig', 'build_flags', 'extra_scripts', 'custom_sticky_project_lto', 'board',
                    'board_build.partitions', 'board_upload.maximum_size', 'platform', 'custom_firmware_device_type']:
            self.assertEqual(option(c, 'env:'+EXPERIMENT, key), option(c, 'env:sticky', key))
        self.assertEqual(option(c, 'env:sticky', 'board_upload.maximum_size'), '6553600')
        self.assertEqual(option(c, 'env:sticky', 'board_build.partitions'), 'partitions.csv')

    def test_earliest_setup_fence_and_one_owner(self):
        main = (ROOT/'src/main.cpp').read_text().split('void setup() {', 1)[1]
        self.assertTrue(main.startswith('\n#if defined(CROSSPOINT_STATIC_BUILTINS)'))
        self.assertLess(main.index('if (!static_builtin::hydrateBuiltins()) std::abort();'), main.index('SimulatorLifecycle::'))
        source = (ROOT/'lib/StaticBuiltinData/StaticBuiltinData.cpp').read_text()
        for filename in ['StaticBuiltinFonts.cpp', 'StaticBuiltinOwner.cpp']:
            self.assertEqual(source.count('#include "'+filename+'"'), 1)

    def test_required_effective_psram_guard_preprocessor(self):
        # Exercise the real leading target policy via C preprocessing only;
        # no host ABI masquerading as target ABI/layout qualification.
        source = (ROOT/'lib/StaticBuiltinData/StaticBuiltinData.cpp').read_text()
        prefix = source.split('#include "StaticBuiltinData.h"', 1)[0]
        required = ['CONFIG_SPIRAM_BOOT_HW_INIT', 'CONFIG_SPIRAM_BOOT_INIT',
                    'CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION', 'CONFIG_SPIRAM_MEMTEST',
                    'CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY', 'CONFIG_ESP_ALLOW_BSS_SEG_EXTERNAL_MEMORY']
        self.assertIn('supported early mandatory PSRAM', prefix)
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            header = Path(tmp)/'sdkconfig.h'
            for missing in [None, *required, 'ignore_notfound']:
                header.write_text('\n'.join('#define '+key+' 1' for key in required if key != missing) +
                                  ('\n#define CONFIG_SPIRAM_IGNORE_NOTFOUND 1\n' if missing == 'ignore_notfound' else '\n'))
                text = prefix + '\n#endif\n' # close outer feature guard
                proc = subprocess.run(['c++', '-E', '-x', 'c++', '-DCROSSPOINT_STATIC_BUILTINS=1', '-I'+tmp, '-'], input=text, text=True, capture_output=True)
                self.assertEqual(proc.returncode == 0, missing is None, proc.stderr)

    def test_experimental_actual_lto_recording_hooks(self):
        spec = importlib.util.spec_from_file_location('proposal_policy', ROOT/'test/static_builtin_data/test_proposal.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        lto_spec = importlib.util.spec_from_file_location('baseline_lto', ROOT/'test/crosspoint_migration/docs-ci/test_project_lto.py')
        module.LTO = importlib.util.module_from_spec(lto_spec)
        lto_spec.loader.exec_module(module.LTO)
        module.AFTER = module.config(ROOT/'platformio.ini')
        module.OVERLAY = ROOT
        module.Proposal('test_experiment_actual_lto_hooks_keep_sdk_and_object_policy').test_experiment_actual_lto_hooks_keep_sdk_and_object_policy()

    def test_full_bridge_wiring_synthetic_generator_recording(self):
        for name in ['sticky', 'default', 'sticky-owner-test', EXPERIMENT]:
            with self.subTest(name=name):
                self._exercise_bridge(None, name)

    def test_bridge_import_uses_project_dir_not_dunder_file(self):
        for name in ['sticky', 'default', 'sticky-owner-test', EXPERIMENT]:
            with self.subTest(name=name):
                self._exercise_bridge('/unrelated/scons-wrapper.py', name)

    def test_production_bridge_real_generation_without_file(self):
        # Execute the actual bridge and real generator. Redirect only the three
        # source-install writes to a private sink; never replace generation.
        import json
        import sys
        from unittest.mock import patch
        tree = ast.parse((ROOT/'scripts/gen_i18n.py').read_text())
        bridge = next(n for n in ast.walk(tree) if isinstance(n, ast.If) and isinstance(n.test, ast.BoolOp)
                      and 'custom_static_builtin_data' in ast.unparse(n.test))
        code = compile(ast.Module(body=[bridge], type_ignores=[]), '<actual-bridge-real-generation>', 'exec')
        c = configparser.ConfigParser(interpolation=None)
        c.read(ROOT/'platformio.ini')
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            build = Path(tmp)
            installed = {}
            targets = {ROOT/'lib/I18n'/name for name in ['I18nKeys.h', 'I18nStrings.h', 'I18nStrings.cpp']}
            original_write = Path.write_bytes
            def install(path, data):
                if path in targets:
                    installed[path.name] = data
                    return len(data)
                return original_write(path, data)
            class Env(dict):
                def GetProjectOption(self, key, default): return option(c, 'env:sticky', key, default)
                def subst(self, key): return str(ROOT if key == '$PROJECT_DIR' else build)
                def Prepend(self, **kwargs): self['prepend'] = kwargs
                def Append(self, **kwargs): self['append'] = kwargs
            env = Env(PIOENV='sticky')
            original_path = sys.path[:]
            try:
                with patch.object(Path, 'write_bytes', install):
                    exec(code, dict(env=env, Import=lambda *_: None, Path=Path, sys=sys))
            finally:
                sys.path[:] = original_path
            private = build/'static-builtins'
            fonts = json.loads((private/'font-inventory.json').read_text())
            languages = json.loads((private/'language-inventory.json').read_text())
            self.assertEqual(len(fonts), 76)
            self.assertEqual(len({font['source'] for font in fonts}), 38)
            self.assertEqual(len(languages), 35)
            self.assertEqual(len(list(private.glob('*.zlib'))), 110)  # English remains raw.
            self.assertEqual(set(installed), {path.name for path in targets})
            for name, data in installed.items():
                self.assertEqual(data, (private/name).read_bytes())
            self.assertEqual(env['prepend'], {'CPPPATH': [str(private)]})
            self.assertEqual(env['append'], {'CPPDEFINES': [('CROSSPOINT_STATIC_BUILTINS', 1)]})

    def _exercise_bridge(self, dunder_file, name):
        # Real bridge side effects with an explicitly synthetic generator double;
        # canonical generation/owner C++ is exercised separately by run.py.
        import sys
        import types
        from unittest.mock import patch
        tree = ast.parse((ROOT/'scripts/gen_i18n.py').read_text())
        bridge = next(n for n in ast.walk(tree) if isinstance(n, ast.If) and isinstance(n.test, ast.BoolOp)
                      and 'custom_static_builtin_data' in ast.unparse(n.test))
        code = compile(ast.Module(body=[bridge], type_ignores=[]), '<actual-bridge-recording>', 'exec')
        with tempfile.TemporaryDirectory(dir=os.environ['TMPDIR']) as tmp:
            root = Path(tmp); (root/'lib/I18n').mkdir(parents=True)
            calls = []
            def generate(project, private, profile):
                self.assertEqual(sys.path[0], str(root.resolve()/'scripts'))
                calls.append((project, private, profile))
                private.mkdir(parents=True)
                for filename in ['I18nKeys.h', 'I18nStrings.h', 'I18nStrings.cpp']:
                    (private/filename).write_bytes(b'SYNTHETIC bridge fixture '+filename.encode())
            class Env(dict):
                def GetProjectOption(self, key, default): return self.get(key, default)
                def subst(self, key): return str(root if key == '$PROJECT_DIR' else root/'build')
                def Prepend(self, **kwargs): self['prepend'] = kwargs
                def Append(self, **kwargs): self['append'] = kwargs
            env = Env(PIOENV=name, custom_static_builtin_data='yes')
            original_path = sys.path[:]
            try:
                with patch.dict(sys.modules, {'gen_static_builtin_data': types.SimpleNamespace(generate=generate)}):
                    scope = dict(env=env, Import=lambda *_: None, Path=Path, sys=sys)
                    if dunder_file is not None:
                        scope['__file__'] = dunder_file
                    exec(code, scope)
            finally:
                sys.path[:] = original_path
            private = root/'build/static-builtins'
            self.assertEqual(calls, [(root, private, 'esp32-le-v1')])
            self.assertEqual(env['prepend'], {'CPPPATH': [str(private)]})
            self.assertEqual(env['append'], {'CPPDEFINES': [('CROSSPOINT_STATIC_BUILTINS', 1)]})
            for filename in ['I18nKeys.h', 'I18nStrings.h', 'I18nStrings.cpp']:
                self.assertEqual((root/'lib/I18n'/filename).read_bytes(), (private/filename).read_bytes())

    def test_bridge_exact_optin_recording(self):
        tree = ast.parse((ROOT/'scripts/gen_i18n.py').read_text())
        bridge = [n for n in ast.walk(tree) if isinstance(n, ast.If) and isinstance(n.test, ast.BoolOp)
                  and 'custom_static_builtin_data' in ast.unparse(n.test)]
        self.assertEqual(len(bridge), 1)
        guards = []
        for node in bridge[0].body:
            if not isinstance(node, ast.If): break
            guards.append(node)
        self.assertEqual(len(guards), 2)
        bridge[0].body = guards + [ast.Assign(targets=[ast.Name(id='accepted', ctx=ast.Store())], value=ast.Constant(True))]
        code = compile(ast.fix_missing_locations(ast.Module(body=bridge, type_ignores=[])), '<actual-bridge-recording>', 'exec')
        class Env(dict):
            def GetProjectOption(self, key, default): return self.get(key, default)
        approved = [EXPERIMENT, 'sticky', 'default', 'sticky-owner-test']
        for name in [*approved, 'sticky-debug', 'debug', 'sticky-simulator', 'simulator',
                     'other-device', 'sticky-other', EXPERIMENT+'-other']:
            for optin in ['no', 'yes']:
                for langs in ['all', '', 'en']:
                    scope = dict(env=Env(PIOENV=name, custom_static_builtin_data=optin, custom_i18n_builtin_langs=langs), Import=lambda *_: None, accepted=False)
                    if optin == 'yes' and (name not in approved or langs == 'en'):
                        with self.assertRaises(RuntimeError): exec(code, scope)
                    else:
                        exec(code, scope)
                        self.assertEqual(scope['accepted'], optin == 'yes')


if __name__ == '__main__':
    unittest.main(verbosity=2)
