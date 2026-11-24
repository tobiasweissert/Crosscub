"""Producer-boundary preparation only: never runs a real compiler/PlatformIO."""
import ast
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
HELPER = ROOT / 'scripts/ci_producer_capture.py'
PRODUCER = Path.home()/'.platformio/platforms/espressif32/builder/frameworks/espidf.py'
if not PRODUCER.is_file():
    PRODUCER = Path('/root/.hermes/task-state/crosscub-owner-next-release/plugin-hub/target-44834357235/selected/startup-builder-espidf.py')


def load():
    spec = importlib.util.spec_from_file_location('producer_capture', HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProducerTests(unittest.TestCase):
    def test_real_producer_preserves_before_cleanup_and_forwards_verbose(self):
        self.assertTrue(HELPER.exists(), 'missing producer-boundary implementation')
        if not PRODUCER.exists():
            self.skipTest('authenticated producer source available only in preparation workspace')
        m = load()
        original = PRODUCER.read_bytes()
        patched = m.overlay(original)
        self.assertEqual(m.overlay(patched), patched)
        tree = ast.parse(patched)
        function = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == 'idf_lib_copy')
        # Actual producer body, fake external build only; no invented producer model.
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            build = root / '.pio/build/sticky'
            (build / 'config').mkdir(parents=True)
            (build / 'config/sdkconfig.h').write_text('#define CONFIG_SPIRAM 1\n')
            (build / 'config/sdkconfig.json').write_text('{}')
            (root / 'sdkconfig.sticky').write_text('CONFIG_SPIRAM=y\n')
            libs = root / 'libs'
            (libs / 'esp32s3').mkdir(parents=True)
            (libs / 'esp32s3/sdkconfig').write_text('old')
            calls = []
            class Env(dict):
                def subst(self, value):
                    return {'$PROJECT_DIR': str(root), '$PYTHONEXE': '/fake/python'}[value]
                def Execute(self, command):
                    calls.append(command)
                    self_test.assertTrue((root/'evidence/producer-idf-sdkconfig.h').is_file())
                    self_test.assertFalse((build/'config').exists())
                    return 0
                def VerboseAction(self, command, label):
                    return command
                def Exit(self, code):
                    calls.append(code)
            self_test = self
            env = Env(PROJECT_BUILD_DIR=str(root/'.pio/build'), PIOENV='sticky')
            import shutil
            namespace = dict(Path=Path, shutil=shutil, os=os, ARDUINO_FRMWRK_LIB_DIR=str(libs),
                             chip_variant='esp32s3', board={}, sdk_config={},
                             copy_idf_component_archives=lambda *a: None, IS_WINDOWS=False,
                             get_requested_cli_targets=lambda: [], ARGUMENTS={}, idf_variant='esp32s3',
                             flag_custom_component_add=False, flag_custom_component_remove=False)
            exec(compile(ast.Module(body=[function], type_ignores=[]), '<actual-producer>', 'exec'), namespace)
            from unittest.mock import patch
            with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence'), CROSSCUB_PROJECT_SCRIPTS=str(ROOT/'scripts')):
                namespace['idf_lib_copy']([], [], env)
            self.assertIn('run --verbose -e "sticky"', calls[0])
            self.assertEqual((root/'evidence/producer-idf-sdkconfig.h').read_bytes(), b'#define CONFIG_SPIRAM 1\n')
            self.assertEqual(calls[-1], 0)

    def test_post_registration_skips_outer_dummy_graph(self):
        import sys
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        from unittest.mock import patch
        m = load()
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            env = Environment(tools=['g++'])
            env.Replace(PROJECT_DIR=str(root), BUILD_DIR=str(root/'build'))
            dummy = env.Object(target=str(root/'build/.dummy/sketch.cpp.o'), source=str(root/'.dummy/sketch.cpp'))[0]
            with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')):
                m.install_main(env)
            self.assertFalse(env.File('$BUILD_DIR/src/main.cpp.o').has_builder())
            self.assertEqual(dummy.get_executor().post_actions, [])
            self.assertFalse((root/'evidence').exists())

    def test_opt_in_schedules_only_post_scope_preserving_scripts(self):
        import sys
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        from unittest.mock import patch
        env = Environment(tools=[])
        env.AddMethod(lambda e,scope:['existing-'+scope], 'GetExtraScripts')
        m = load()
        with patch.dict(os.environ, {}, clear=True):
            m.install(env)
        self.assertEqual(env.GetExtraScripts('post'), ['existing-post'])
        with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE='/unused-synthetic'):
            m.install(env)
        self.assertEqual(env.GetExtraScripts('pre'), ['existing-pre'])
        self.assertEqual(env.GetExtraScripts('post'), ['existing-post', str(HELPER)])

    def test_actual_main_callback_runs_fake_external_preprocessor(self):
        self.assertTrue(HELPER.exists(), 'missing producer-boundary implementation')
        m = load()
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            (root/'src').mkdir()
            (root/'src/main.cpp').write_text('// synthetic main input\n')
            header = root/'.pio/build/sticky/config/sdkconfig.h'
            header.parent.mkdir(parents=True)
            header.write_text('#define CONFIG_SPIRAM 1\n')
            fake = root/'fake-driver'
            import sys
            fake.write_text('#!'+sys.executable+'\nimport sys,pathlib\na=sys.argv\n'
                'assert "-c" not in a\n'
                'if "-M" in a:\n'
                ' pathlib.Path(a[a.index("-MF")+1]).write_text(a[a.index("-MT")+1]+": src/main.cpp '+str(header)+'\\n")\n'
                'else:\n'
                ' assert "-dM" in a and pathlib.Path("evidence/producer-application-sdkconfig.h").is_file()\n'
                ' with open(a[a.index("-o")+1], "wb") as f:\n'
                '  f.write(b"#define UNRELATED 1\\n"*15000 + b"#define CONFIG_SPIRAM 1\\n")\n')
            fake.chmod(0o700)
            class Node:
                def srcnode(self): return self
                def get_abspath(self): return str(root/'src/main.cpp')
            import sys
            sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
            from SCons.Script import Environment
            env = Environment(tools=['g++'], ENV={'PATH': os.defpath})
            env.Replace(PIOENV='sticky', PROJECT_DIR=str(root), BUILD_DIR=str(root/'.pio/build/sticky'),
                        CXX=str(fake), CXXCOM='$CXX -DAPP=1 -c $SOURCE -o $TARGET')
            env.Object(target=str(root/'.pio/build/sticky/src/main.cpp.o'), source=str(root/'src/main.cpp'))
            env.AddMethod(lambda self, target, callback: setattr(self, 'action', callback), 'AddPostAction')
            from unittest.mock import patch
            with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')):
                m.install_main(env)
                self.assertEqual(env.action([env.File('src/main.cpp')], [env.File('.pio/build/sticky/src/main.cpp.o')], env), 0)
            receipt = json.loads((root/'evidence/producer-main-receipt.json').read_text())
            self.assertEqual(receipt['probe_exit'], 0)
            projection = receipt['macro_projection']
            full = b'#define UNRELATED 1\n'*15000 + header.read_bytes()
            self.assertGreater(len(full), m.MAX_DIAGNOSTIC)
            self.assertEqual(projection, dict(raw_sha256=hashlib.sha256(full).hexdigest(), raw_bytes=len(full),
                                              records=15001, config_records=1))
            self.assertEqual((root/'evidence/producer-main.macros').read_bytes(), header.read_bytes())
            self.assertEqual((root/'evidence/producer-application-sdkconfig.h').read_bytes(), header.read_bytes())
            self.assertIn('-DAPP=1', receipt['probe_argv'])
            self.assertFalse(receipt['publication_authorized'])

    def test_real_scons_graph_post_phase_survives_object_lto_executor_replacement(self):
        import sys
        import subprocess
        import textwrap
        import SCons
        self.assertEqual(SCons.__version__, '4.8.1')
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            (root/'src').mkdir()
            (root/'src/main.cpp').write_text('// synthetic graph input\n')
            # Actual SCons Object creation and project LTO callback. Only SPAWN
            # is synthetic; neither a compiler nor PlatformIO is launched.
            program = textwrap.dedent('''\
                import os,runpy,shlex
                from pathlib import Path
                import SCons.Platform.posix
                root=Path(Dir('.').get_abspath())
                def no_compiler(shell,escape,cmd,args,execution_env):
                    argv=shlex.split(' '.join(args))
                    p=Path(argv[argv.index('-o')+1]);p.parent.mkdir(parents=True,exist_ok=True)
                    p.write_text('NOT AN OBJECT')
                    return 0
                SCons.Platform.posix.subprocess_spawn=no_compiler
                env=Environment(tools=['g++'],ENV={'PATH':os.defpath})
                env.Replace(SPAWN=no_compiler,PROJECT_DIR=str(root),BUILD_DIR=str(root/'build'),
                    PROJECT_SRC_DIR=str(root/'src'),PIOENV='sticky-owner-test',CCFLAGS=['-Os'],
                    CXX='/nonexistent-python-only-graph-driver')
                env.AddMethod(lambda e,k,default=None: {'custom_sticky_project_lto':'yes',
                    'custom_firmware_device_type':'sticky'}.get(k,default),'GetProjectOption')
                env.AddMethod(lambda e,scope: [],'GetExtraScripts')
                callbacks=[]
                env.AddMethod(lambda e,fn,pattern=None:callbacks.append(fn),'AddBuildMiddleware')
                helper=runpy.run_path(HELPER)
                helper['install'](env)
                runpy.run_path(LTO,init_globals={'env':env,'Import':lambda *a:None})
                # Exact Core CollectBuildFiles -> middleware -> BuildSources
                # ordering: middleware receives source before Object construction.
                # Focused suffix control; full pinned replay separately executes
                # Core's authenticated scons_patched_match_splitext body.
                env.Replace(OBJSUFFIX='.cpp.o')
                env.VariantDir(str(root/'build/src'),str(root/'src'),duplicate=False)
                node=env.File(str(root/'build/src/main.cpp'))
                for fn in callbacks: node=fn(env,node)
                obj=node[0]
                assert obj.get_abspath()==str(root/'build/src/main.cpp.o')
                for script in env.GetExtraScripts('post'):
                    env.SConscript(script,exports={'env':env})
                assert len(obj.get_executor().post_actions)==1
                Default(obj)
                ''')
            # Project SConscript import resolves through PROJECT_DIR, no __file__.
            (root/'scripts').symlink_to(ROOT/'scripts',target_is_directory=True)
            (root/'SConstruct').write_text('HELPER='+repr(str(HELPER))+'\nLTO='+repr(str(ROOT/'scripts/sticky_project_lto_pre.py'))+'\n'+program)
            result = subprocess.run([sys.executable,'-m','SCons','-Q'],cwd=root,
                env=dict(os.environ,CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')),
                capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            receipt=json.loads((root/'evidence/producer-main-receipt.json').read_text())
            self.assertIn('-Oz',receipt['compile_argv'])
            self.assertIn('-flto=1',receipt['compile_argv'])
            self.assertNotIn('-Os',receipt['compile_argv'])
            # No actual diagnostic driver exists in this graph fixture. The
            # original object action succeeded via no-op SPAWN, callback ran,
            # and bounded diagnostic failure remains private/incomplete.
            self.assertIn('probe_error',receipt)
            self.assertFalse(receipt['publication_authorized'])

    def test_overlay_rejects_unknown_bytes(self):
        self.assertTrue(HELPER.exists(), 'missing producer-boundary implementation')
        with self.assertRaises(ValueError):
            load().overlay(b'unknown producer')

    def test_probe_uses_exact_compile_arguments_without_compiling(self):
        self.assertTrue(HELPER.exists(), 'missing producer-boundary implementation')
        args = ['/fake/g++', '-o', 'main.o', '-c', '-MMD', '-MF', 'main.d', '-DVALUE=2', '-isystem', '/sdk', 'src/main.cpp']
        command = load().preprocessing_base(args)
        self.assertEqual(command, ['/fake/g++', '-DVALUE=2', '-isystem', '/sdk', 'src/main.cpp'])
        self.assertEqual(args[1:3], ['-o', 'main.o'])

class BoundedProducerTests(unittest.TestCase):
    def invoke(self, root, body, deadline=0.25, inject_exception=False):
        import sys
        import shlex
        from unittest.mock import patch
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        (root/'src').mkdir(exist_ok=True)
        (root/'src/main.cpp').write_text('// fixture')
        (root/'.pio/build/sticky/config').mkdir(parents=True, exist_ok=True)
        driver = root/'python driver.py'
        driver.write_text('import sys,os,pathlib,time,subprocess\n'
                          'assert os.environ["ONLY_CONSTRUCTION"] == "synthetic-env-sentinel"\n'
                          'assert \'-DCROSSCUB_APP_VERSION="release"\' in sys.argv\n'+body)
        env = Environment(tools=['g++'], ENV={'PATH': os.defpath, 'ONLY_CONSTRUCTION': 'synthetic-env-sentinel'})
        env.Replace(PIOENV='sticky', PROJECT_DIR=str(root), BUILD_DIR=str(root/'build'),
                    CXXCOM=shlex.quote(sys.executable)+' '+shlex.quote(str(driver))+r' -DCROSSCUB_APP_VERSION=\"release\" -c $SOURCE -o $TARGET')
        env.Object(target=str(root/'build/src/main.cpp.o'), source=str(root/'src/main.cpp'))
        env.AddMethod(lambda self, target, fn: setattr(self, 'action', fn), 'AddPostAction')
        module = load()
        import selectors
        select = selectors.DefaultSelector.select
        def fault(selector, timeout=None):
            if inject_exception and (root/'pid').exists():
                raise OSError('synthetic-monitor-failure')
            return select(selector, timeout)
        # Public callback, real SCons expansion and Python-only driver.
        with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')), patch.object(module, 'PROBE_TIMEOUT', deadline, create=True), patch.object(selectors.DefaultSelector, 'select', fault):
            module.install_main(env)
            self.assertEqual(env.action([env.File(str(root/'src/main.cpp'))], [env.File(str(root/'build/main.o'))], env), 0)
        receipt = json.loads((root/'evidence/producer-main-receipt.json').read_text())
        expected_env_digest = hashlib.sha256(json.dumps(dict(env['ENV']), sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(receipt['compile_env_sha256'], expected_env_digest)
        for path in (root/'evidence').iterdir():
            self.assertNotIn(b'synthetic-env-sentinel', path.read_bytes(), 'environment value leaked into evidence')
            self.assertNotIn(b'ONLY_CONSTRUCTION', path.read_bytes(), 'environment dump leaked into evidence')
        return receipt

    def test_raw_header_identity_required_before_retention_or_macro_phase(self):
        allowed = '.pio/build/sticky/config/sdkconfig.h'
        for candidate in ('.pio/build/sticky/config/unused/../sdkconfig.h', 'other/sdkconfig.h'):
            with self.subTest(candidate=candidate), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                header = root / (allowed if '..' in Path(candidate).parts else candidate)
                header.parent.mkdir(parents=True)
                header.write_text('// fixture')
                paths = (allowed + ' ' if '..' in Path(candidate).parts else '') + candidate
                body = ('if "-M" in sys.argv:\n pathlib.Path(sys.argv[sys.argv.index("-MF")+1]).write_text('
                        'sys.argv[sys.argv.index("-MT")+1]+' + repr(': ' + paths + '\n') + ')\n'
                        'else:\n pathlib.Path("macro-start").touch()')
                receipt = self.invoke(root, body)
                self.assertEqual(receipt['dependency_exit'], 0)
                self.assertIn('probe_error', receipt)
                self.assertFalse((root/'evidence/producer-application-sdkconfig.h').exists())
                self.assertFalse((root/'macro-start').exists())

    def test_shared_oversize_receipt_preserves_caller_result(self):
        module = load()
        argv = ['fixture', 'x' * (module.MAX_DIAGNOSTIC // 2)]
        def finish(out, result):
            module.save_receipt(out, 'receipt.json', {'argv': argv, 'probe_argv': argv, 'exit': result[0]})
            return result
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            out = Path(temp)
            original = (7, 'synthetic diagnostic')
            self.assertIs(finish(out, original), original)
            raw = (out/'receipt.json').read_bytes()
            self.assertLessEqual(len(raw), module.MAX_DIAGNOSTIC)
            self.assertEqual(json.loads(raw), {'probe_error': 'OutputLimitExceeded', 'publication_authorized': False})

    def test_each_diagnostic_output_is_bounded(self):
        for channel in ('stdout', 'stderr', 'dependency', 'macro'):
            with self.subTest(channel=channel), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                body = {'stdout': 'os.write(1,b"x"*300000)', 'stderr': 'os.write(2,b"x"*300000)',
                        'dependency': 'pathlib.Path(sys.argv[sys.argv.index("-MF")+1]).write_bytes(b"x"*300000)',
                        'macro': 'if "-M" in sys.argv:\n pathlib.Path(".pio/build/sticky/config/sdkconfig.h").write_text("// test")\n pathlib.Path(sys.argv[sys.argv.index("-MF")+1]).write_text(sys.argv[sys.argv.index("-MT")+1]+": .pio/build/sticky/config/sdkconfig.h\\n")\nelse:\n pathlib.Path(sys.argv[sys.argv.index("-o")+1]).write_bytes(b"#define CONFIG_A 1\\n"*20000)'}[channel]
                receipt = self.invoke(root, body)
                self.assertEqual(receipt.get('probe_error'), 'OutputLimitExceeded')
                self.assertNotEqual(receipt.get('probe_exit'), 0)
                for path in (root/'evidence').iterdir():
                    self.assertLessEqual(path.stat().st_size, 262144, path.name)
                self.assertNotIn('producer-main.macros_sha256', receipt)

    def test_macro_transfer_limit_is_distinct_from_retention_limit(self):
        import io
        module = load()
        self.assertEqual(module.MAX_DIAGNOSTIC, 262144)
        output = io.BytesIO()
        projection = module.ConfigProjection(output)
        # Real transfer ceiling, without a large fixture/file or a long line.
        chunk = b'#define OUTSIDE '+b'x'*(65536-17)+b'\n'
        self.assertEqual(len(chunk), 65536)
        for _ in range(module.MAX_MACRO_STREAM // len(chunk)):
            projection.feed(chunk)
        with self.assertRaises(module.OutputLimitExceeded):
            projection.feed(b'#define CONFIG_A 1\n')
        self.assertEqual(output.getvalue(), b'')

    def test_macro_phase_rejects_partial_record_nonzero_exit_and_malformed_stream(self):
        dep = ('if "-M" in sys.argv:\n pathlib.Path(".pio/build/sticky/config/sdkconfig.h").write_text("#define CONFIG_A 1")\n'
               ' pathlib.Path(sys.argv[sys.argv.index("-MF")+1]).write_text(sys.argv[sys.argv.index("-MT")+1]+": .pio/build/sticky/config/sdkconfig.h\\n")\n')
        for tail in ('os.write(f,b"#define E")', 'sys.exit(3)', 'os.write(f,b"not-a-definition\\n")',
                     'os.write(f,b"#define BIG "+b"x"*262144+b"\\n")'):
            with self.subTest(tail=tail), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                receipt = self.invoke(root, dep+'else:\n f=os.open(sys.argv[sys.argv.index("-o")+1],os.O_WRONLY)\n os.write(f,b"#define CONFIG_A 1\\n")\n '+tail)
                self.assertEqual(receipt['dependency_exit'], 0)
                self.assertTrue(receipt['files']['producer-application-sdkconfig.h']['present'])
                self.assertIn('probe_error', receipt)
                self.assertNotIn('probe_exit', receipt)
                self.assertNotIn('producer-main.macros_sha256', receipt)

    def test_descendants_cannot_write_after_callback(self):
        import time
        import sys
        for mode in ('timeout', 'normal', 'closed-pipes', 'overflow', 'exception'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                sentinel = root/'late-write'
                pidfile = root/'pid'
                child = 'import pathlib,time;time.sleep(0.65);pathlib.Path('+repr(str(sentinel))+').write_text("late")'
                body = 'p=subprocess.Popen([sys.executable,"-c",'+repr(child)+'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\npathlib.Path('+repr(str(pidfile))+').write_text(str(p.pid))\n'
                if mode == 'closed-pipes':
                    body += 'os.close(1);os.close(2)\n'
                if mode == 'overflow':
                    body += 'os.write(2,b"x"*300000)\n'
                if mode != 'normal':
                    body += 'time.sleep(5)\n'
                receipt = self.invoke(root, body, inject_exception=mode == 'exception')
                expected_error = {'timeout': 'TimeoutExpired', 'closed-pipes': 'TimeoutExpired',
                                  'overflow': 'OutputLimitExceeded', 'exception': 'OSError'}
                if mode in expected_error:
                    self.assertEqual(receipt.get('probe_error'), expected_error[mode])
                status = Path('/proc')/pidfile.read_text()/'stat'
                self.assertTrue(not status.exists() or status.read_text().split()[2] == 'Z', 'live descendant at callback return')
                time.sleep(0.8)
                self.assertFalse(sentinel.exists(), 'descendant wrote after callback returned')
                self.assertFalse(status.exists(), 'descendant not reaped by host init')

class EffectiveArgvTests(unittest.TestCase):
    def test_hosted_quoted_board_with_default_spawn_and_object_lto(self):
        import sys
        import shlex
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        import SCons
        from SCons.Script import Environment, Action
        from SCons.Platform.posix import subprocess_spawn
        # Authenticated hosted inner-main argv, both 44861002849 and 44861453556:
        # exact board/version values; raw SCons quoting built by its own defines.
        board = 'Espressif ESP32-S3-DevKitC-1-N16R8V (16 MB Flash Quad, 8 MB PSRAM Octal)'
        env = Environment(tools=['g++'], ENV={'PATH': os.defpath,
                          'ONLY_CONSTRUCTION': 'synthetic-env-not-hosted'})
        env.Replace(CXX='/not-executed/cxx', CCFLAGS=['-Os'], CPPDEFINES=[
            ('CROSSCUB_APP_VERSION', r'\"2.0.0\"'),
            ('ARDUINO_BOARD', '\\"'+board+'\\"')])
        obj = env.Object(target='hosted-main.o', source='hosted-main.cpp',
                         CCFLAGS=['-flto=1', '-Oz'])[0]
        effective = obj.get_executor().get_build_env()
        self.assertEqual(SCons.__version__, '4.8.1')
        self.assertIs(effective['SPAWN'], subprocess_spawn)
        self.assertEqual(Path(effective['SHELL']).name, 'sh')
        seen = []
        def no_op(shell, escape, cmd, args, execution_env):
            seen.append((shlex.split(' '.join(args)), dict(execution_env)))
            return 0
        self.assertEqual(Action('$CXXCOM').execute([obj], obj.sources,
                         effective.Override({'SPAWN': no_op})), 0)
        expected, expected_env = seen[0]
        self.assertIn('-DARDUINO_BOARD="'+board+'"', expected)
        self.assertIn('-DCROSSCUB_APP_VERSION="2.0.0"', expected)
        self.assertIn('-Oz', expected)
        self.assertNotIn('-Os', expected)
        argv, execution_env = load().effective_compile(effective, [obj], obj.sources)
        self.assertEqual(argv, expected)
        self.assertEqual(execution_env, expected_env)
        self.assertIs(effective['SPAWN'], subprocess_spawn)
        self.assertEqual(env['CCFLAGS'], ['-Os'])

    def test_quote_removal_literals_and_escapes_without_shell_execution(self):
        import sys
        from unittest.mock import patch
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        env = Environment(tools=['g++'], ENV={'PATH': os.defpath})
        # Independent literal expectations, including POSIX double-quote
        # backslash rules which shlex.split does not implement for escaped $/`.
        cases = [
            (r'''-DVALUE='()[]*?~#;&|<>`' ''', '-DVALUE=()[]*?~#;&|<>`'),
            (r'''-DVALUE="()[]*?~#;&|<>" ''', '-DVALUE=()[]*?~#;&|<>'),
            (r'''-DVALUE='owner'"'"'s "board" \path' ''', '-DVALUE=owner\'s "board" \\path'),
            (r'''-DVALUE="a\qb\$c\`d\"e\\f" ''', '-DVALUE=a\\qb$c`d"e\\f'),
            (r'''-DVALUE=escaped\ space\(x\)\[y\] ''', '-DVALUE=escaped space(x)[y]'),
            (r'''"" ''', ''),
        ]
        m = load()
        self.assertEqual(m.literal_posix_argv("cxx '()[]*?~#;&|<>$`'"),
                         ['cxx', '()[]*?~#;&|<>$`'])
        with patch('subprocess.Popen', side_effect=AssertionError('must not spawn')):
            for raw, expected in cases:
                with self.subTest(raw=raw):
                    env.Replace(CXXCOM='/not-executed/cxx '+raw.replace('$', '$$'))
                    argv, execution_env = m.effective_compile(env, [], [])
                    self.assertEqual(argv, ['/not-executed/cxx', expected])
                    self.assertEqual(execution_env, dict(env['ENV']))

    def test_unsafe_posix_forms_denied_without_execution(self):
        import sys
        from unittest.mock import patch
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        env = Environment(tools=['g++'])
        m = load()
        unsafe = [';', '&&', '|', '>', '<', '(', ')', '*', '?', '[x]', '~root', '#comment',
                  '$NAME', '${NAME}', '$(id)', '`id`', '"$NAME"', '"$(id)"', '"`id`"',
                  "'literal'$(id)", '"literal";bad', "'unterminated", '"unterminated',
                  'trailing\\', 'a\nb', 'a\rb', "'a\nb'", '"a\nb"', 'a\x00b', 'a\x01b',
                  'a\\\nb', 'a\x7fb']
        with patch('subprocess.Popen', side_effect=AssertionError('must not spawn')):
            for raw in unsafe:
                with self.subTest(raw=raw):
                    env.Replace(CXXCOM='/not-executed/cxx '+raw.replace('$', '$$'))
                    with self.assertRaises(ValueError):
                        m.effective_compile(env, [], [])

    def test_pre_admission_guards_have_independent_constant_stages(self):
        import sys
        from unittest.mock import patch
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        import SCons
        from SCons.Script import Environment
        env = Environment(tools=['g++'])
        env.Replace(CXXCOM='/not-executed/cxx')
        m = load()
        # Each isolated guard fails before even constructing the action.
        with patch('SCons.Action.Action', side_effect=AssertionError('action reached')):
            with patch.object(SCons, '__version__', 'unsupported'):
                with self.assertRaises(ValueError) as failure:
                    m.effective_compile(env, [], [])
                self.assertEqual(failure.exception.stage, 'execution-version')
            for changes, stage in [({'SPAWN': lambda *a: self.fail('unknown spawn executed')}, 'execution-spawn'),
                                   ({'SHELL': '/bin/bash'}, 'execution-shell')]:
                with self.subTest(stage=stage), self.assertRaises(ValueError) as failure:
                    m.effective_compile(env.Override(changes), [], [])
                self.assertEqual(failure.exception.stage, stage)

    def test_callback_retains_only_allowlisted_admission_stage(self):
        import sys
        from unittest.mock import patch
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        m = load()
        for stage in ('execution-version', 'execution-spawn', 'execution-shell',
                      'execution-platform', 'shell-lexical', 'compile-action', None):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
                root = Path(temp)
                env = Environment(tools=['g++'])
                env.Replace(PROJECT_DIR=str(root), BUILD_DIR=str(root/'build'), PIOENV='sticky')
                obj = env.Object(target=str(root/'build/src/main.cpp.o'), source=str(root/'src/main.cpp'))[0]
                env.AddMethod(lambda e, node, fn: setattr(e, 'action', fn), 'AddPostAction')
                failure = m.CompileAdmissionError(stage) if stage else ValueError('SECRET-ERROR-DO-NOT-RETAIN')
                with patch.dict(os.environ, CROSSCUB_PRIVATE_EVIDENCE=str(root/'evidence')), \
                     patch.object(m, 'effective_compile', side_effect=failure):
                    m.install_main(env)
                    self.assertEqual(env.action(obj.sources, [obj], env), 0)
                text = (root/'evidence/producer-main-receipt.json').read_text()
                receipt = json.loads(text)
                self.assertEqual(receipt['probe_error'], 'ValueError')
                self.assertEqual(receipt.get('admission_stage'), stage)
                self.assertNotIn('SECRET', text)
                self.assertNotIn('compile_argv', receipt)
                self.assertNotIn('compile_env_sha256', receipt)
        with self.assertRaises(ValueError):
            m.CompileAdmissionError('SECRET-ERROR-DO-NOT-RETAIN')

    def test_real_scons_spawn_semantics_no_compiler(self):
        import sys
        import shlex
        import subprocess
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        import SCons
        from SCons.Script import Environment, Action
        self.assertEqual(SCons.__version__, '4.8.1')
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as temp:
            root = Path(temp)
            # No compiler: the real SCons SPAWN boundary is intercepted, then
            # its POSIX shell interpretation is checked with a Python argv echo.
            echo = root/"echo driver's with spaces.py"
            echo.write_text('import sys,json,os; print(json.dumps([sys.argv[1:],os.getenv("ONLY_CONSTRUCTION")]))')
            env = Environment(tools=['g++'], ENV={'PATH': os.defpath, 'ONLY_CONSTRUCTION': 'synthetic-env-sentinel'})
            env.Replace(CXXCOM=shlex.quote(sys.executable)+' '+shlex.quote(str(echo))+
                        r''' -DCROSSCUB_APP_VERSION=\"release\" -I"include with spaces" -DNAME='quoted value' $SOURCE -o $TARGET''')
            source = [env.File(str(root/'main input.cpp'))]
            target = [env.File(str(root/'main output.o'))]
            seen = []
            def spawn(shell, escape, cmd, args, execution_env):
                seen.append((args, dict(execution_env)))
                return 0
            oracle = env.Clone(SPAWN=spawn)
            self.assertEqual(Action('$CXXCOM').execute(target, source, oracle), 0)
            raw, execution_env = seen[0]
            actual = json.loads(subprocess.check_output([env['SHELL'], '-c', ' '.join(raw)], env=execution_env))
            original_spawn = env['SPAWN']
            original_env = dict(env['ENV'])
            m = load()
            argv, effective_env = m.effective_compile(env, target, source)
            replay = json.loads(subprocess.check_output(argv, env=effective_env))
            self.assertEqual(replay, actual)
            self.assertIn('-DCROSSCUB_APP_VERSION="release"', replay[0])
            self.assertIn('-Iinclude with spaces', replay[0])
            self.assertIn('-DNAME=quoted value', replay[0])
            self.assertEqual(replay[1], 'synthetic-env-sentinel')
            self.assertIn(str(source[0]), replay[0])
            self.assertIn(str(target[0]), replay[0])
            self.assertIs(env['SPAWN'], original_spawn)
            self.assertEqual(dict(env['ENV']), original_env)

    def test_object_local_override_executor_preserves_effective_arguments(self):
        import sys
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        env = Environment(tools=['g++'], ENV={'PATH': os.defpath})
        env.Replace(CXXCOM='/not-executed/cxx $CCFLAGS -c $SOURCE -o $TARGET', CCFLAGS=['-Os'])
        obj = env.Object(target='override-main.o', source='override-main.cpp', CCFLAGS=['-flto=1', '-Oz'])[0]
        effective_env = obj.get_executor().get_build_env()
        argv, execution_env = load().effective_compile(effective_env, [obj], obj.sources)
        self.assertIn('-Oz', argv)
        self.assertIn('-flto=1', argv)
        self.assertNotIn('-Os', argv)
        self.assertEqual(env['CCFLAGS'], ['-Os'])
        self.assertEqual(effective_env['CCFLAGS'], ['-flto=1', '-Oz'])
        self.assertEqual(execution_env, dict(effective_env['ENV']))

    def test_shell_expansion_and_unknown_spawn_fail_closed_without_execution(self):
        import sys
        sys.path.insert(0, str(Path.home()/'.platformio/packages/tool-scons/scons-local-4.8.1'))
        from SCons.Script import Environment
        env = Environment(tools=['g++'])
        module = load()
        for command in ('/fake/cxx *.cpp', '/fake/cxx ; echo bad', '/fake/cxx `id`', '/fake/cxx ~root'):
            env.Replace(CXXCOM=command)
            with self.subTest(command=command), self.assertRaises(ValueError):
                module.effective_compile(env, [env.File('output.o')], [env.File('input.cpp')])
        env.Replace(SPAWN=lambda *args: self.fail('unknown spawn executed'))
        with self.assertRaises(ValueError):
            module.effective_compile(env, [], [])

if __name__ == '__main__':
    unittest.main()
