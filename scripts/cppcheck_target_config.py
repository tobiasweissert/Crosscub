"""Generate target Kconfig without changing the analyzer's Arduino metadata.

HybridCompile's package header can be stock or left over from another build.
Run the real project's configuration-only target in a disposable plain project
and private copies of the packages its hooks can write. Never compile firmware,
edit a package header, or synthesize CONFIG defines.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_config(command, *, cwd, env, log, timeout=600):
    # PIO launches SCons/CMake descendants. Reap and terminate the owned group
    # before deleting its probe, on timeout, interruption and normal exit alike.
    with Path(log).open('xb') as stream:
        os.chmod(log, 0o600)
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            status = process.wait(timeout=timeout)
            if status:
                raise subprocess.CalledProcessError(status, command)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()


def configured_header(project, core, environment, log):
    project, core = Path(project).resolve(), Path(core).resolve()
    if not environment or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in environment):
        raise RuntimeError('unsupported cppcheck environment identity')
    if not (project/'platformio.ini').is_file() or not (core/'platforms').is_dir():
        raise RuntimeError('configuration source and installed platform are required')
    # All writes, including sdkconfig.defaults markers and framework cache
    # requests, belong to this probe. It is not a Git clone or worktree.
    scratch = project/'.pio'
    scratch.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='cppcheck-config-', dir=scratch) as temporary:
        probe = Path(temporary)
        source = probe/'project'
        inputs = {}
        git_directories = {}

        def copy_input(src, dst):
            src = Path(src)
            if src.is_symlink() and not src.resolve().is_relative_to(project):
                raise RuntimeError('configuration input symlink escapes project')
            inputs[str(src.relative_to(project))] = digest(src)
            return shutil.copy2(src, dst)

        def ignore(directory, names):
            if '.git' in names:
                entry = Path(directory)/'.git'
                if entry.is_dir():
                    gitdir = entry.resolve()
                else:
                    value = entry.read_text().strip()
                    if not value.startswith('gitdir: '):
                        raise RuntimeError('unsupported project Git identity')
                    gitdir = (entry.parent/value[len('gitdir: '):]).resolve()
                git_directories[str(Path(directory).relative_to(project))] = gitdir
            excluded = {'.git', '.pio', '__pycache__', 'node_modules'}
            # Private analyzer logs can live in the checkout on CI; they are
            # outputs, never configuration inputs (and may still be open).
            evidence = os.environ.get('CROSSCUB_PRIVATE_EVIDENCE')
            if evidence:
                for name in names:
                    if (Path(directory)/name).resolve() == Path(evidence).resolve():
                        excluded.add(name)
            for name in set(names) - excluded:
                entry = Path(directory)/name
                if entry.is_symlink() and not entry.resolve().is_relative_to(project):
                    raise RuntimeError('configuration input symlink escapes project')
            return set(names) & excluded

        shutil.copytree(project, source, copy_function=copy_input, ignore=ignore)
        # Shipped hooks authenticate SDK HEAD and read product version identity.
        # Reference the original metadata read-only; do not clone repositories,
        # rewrite pins or pretend the copied working bytes are another commit.
        for relative, gitdir in git_directories.items():
            (source/relative/'.git').write_text('gitdir: ' + str(gitdir) + '\n')
        private = probe/'core'
        private.mkdir()
        shutil.copytree(core/'platforms', private/'platforms')
        if (core/'penv').is_dir():
            shutil.copytree(core/'penv', private/'penv', symlinks=True)
        packages = private/'packages'
        packages.mkdir()
        # These three packages and the platform are writable by shipped hooks
        # or HybridCompile configuration. Executable tools remain read-only
        # references to the installed, pinned packages.
        writable = {'framework-arduinoespressif32', 'framework-arduinoespressif32-libs', 'framework-espidf'}
        for package in (core/'packages').iterdir():
            if package.name in writable:
                shutil.copytree(package, packages/package.name)
            else:
                (packages/package.name).symlink_to(package.resolve(), target_is_directory=True)
        # A cached .orig means HybridCompile would reuse binaries and skip
        # Kconfig entirely. Restore only this private copy's original template
        # so envdump genuinely resolves the current project's requested values.
        for original in (packages/'framework-arduinoespressif32-libs').glob('*/sdkconfig.orig'):
            original.replace(original.with_name('sdkconfig'))
        child = os.environ.copy()
        child['PLATFORMIO_CORE_DIR'] = str(private)
        child['PLATFORMIO_WORKSPACE_DIR'] = str(source/'.pio')
        child['PLATFORMIO_BUILD_DIR'] = str(source/'.pio/build')
        child['PLATFORMIO_LIBDEPS_DIR'] = str(source/'.pio/libdeps')
        child['PLATFORMIO_PACKAGES_DIR'] = str(packages)
        child['PLATFORMIO_PLATFORMS_DIR'] = str(private/'platforms')
        child['GIT_OPTIONAL_LOCKS'] = '0'
        child['CROSSCUB_PROJECT_SCRIPTS'] = str(source/'scripts')
        child['CROSSCUB_PRIVATE_EVIDENCE'] = str(probe/'evidence')
        child.pop('GITHUB_STEP_SUMMARY', None)
        run_config([sys.executable, '-m', 'platformio', 'run', '-e', environment,
                    '-t', 'envdump'], cwd=source, env=child, log=log)
        for name, expected in inputs.items():
            if digest(project/name) != expected:
                raise RuntimeError('project input changed during target configuration')
        header = source/'.pio/build'/environment/'config/sdkconfig.h'
        if not header.is_file() or header.is_symlink():
            raise RuntimeError('Kconfig did not generate sdkconfig.h')
        data = header.read_bytes()
        if not data or b'CONFIG_IDF_TARGET' not in data:
            raise RuntimeError('invalid generated sdkconfig.h')
        return data, inputs


def prepare(tool, output, command):
    """Called only after Core has loaded the actual analyzer metadata."""
    if hasattr(tool, '_crosscub_target_config'):
        return tool._crosscub_target_config
    from ci_producer_capture import save_receipt
    metadata = {'defines': list(tool.cpp_defines), 'includes': list(tool.cpp_includes)}
    environment = tool.envname
    data, inputs = configured_header(tool.project_dir, tool.config.get('platformio', 'core_dir'), environment,
                                     output/'cppcheck-configure.log')
    if metadata != {'defines': list(tool.cpp_defines), 'includes': list(tool.cpp_includes)}:
        raise RuntimeError('analyzer metadata changed during target configuration')
    header = output/'cppcheck-sdkconfig.h'
    with header.open('xb') as stream:
        stream.write(data)
    save_receipt(output, 'cppcheck-target-config.json', {
        'environment': environment,
        'input_file_count': len(inputs),
        'input_manifest_sha256': hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest(),
        'metadata': metadata,
        'original_command': command,
        'cc_flags': tool.cc_flags, 'cxx_flags': tool.cxx_flags,
        'toolchain_defines': tool.toolchain_defines,
        'sdkconfig_sha256': hashlib.sha256(data).hexdigest(),
        'configuration_only': True, 'publication_authorized': False})
    tool._crosscub_target_config = header
    return header
