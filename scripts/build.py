"""Builds dist/ModActivator.asi, and build/ModActivator-tests.asi for the test suites.

The test build is the same plugin plus the exported entry points the tests in
tests/ call. Those entry points are compiled only with MODACTIVATOR_TESTS, so
the release build ships none of them.
"""
from pathlib import Path
import argparse
import concurrent.futures
import os
import shutil
import subprocess

base = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--toolchain', default=os.environ.get('LLVM_MINGW'))
args = parser.parse_args()
if not args.toolchain:
    parser.error('Pass --toolchain PATH or set LLVM_MINGW to your extracted LLVM-MinGW folder')
tc = Path(args.toolchain).resolve() / 'bin'
out = base / 'dist'
out.mkdir(exist_ok=True)
obj = base / 'build'
obj.mkdir(exist_ok=True)

imgui = base / 'vendor/imgui'
minhook = base / 'vendor/minhook/src'
sources = (sorted((base / 'src').rglob('*.cpp'))
           + [imgui / n for n in ['imgui.cpp', 'imgui_draw.cpp', 'imgui_widgets.cpp',
                                  'imgui_tables.cpp', 'backends/imgui_impl_win32.cpp']]
           + sorted(minhook.glob('*.c')) + [minhook / 'hde/hde64.c'])
# Sources holding test entry points; compiled a second time for the test build.
with_tests = {base / 'src/activator.cpp', base / 'src/hooks/input.cpp'}
includes = [base / 'src', imgui, imgui / 'backends', base / 'vendor/minhook/include', minhook / 'hde']
flags = ['-O2', '-DNDEBUG', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_CRT_SECURE_NO_WARNINGS',
         '-fms-extensions']


def compile_one(job):
    src, tests = job
    name = src.relative_to(base).as_posix().replace('/', '_') + ('.tests' if tests else '') + '.o'
    target = obj / name
    compiler = 'x86_64-w64-mingw32-clang.exe' if src.suffix == '.c' else 'x86_64-w64-mingw32-clang++.exe'
    cmd = [str(tc / compiler), '-c', str(src), '-o', str(target)] + flags
    if src.suffix == '.cpp':
        cmd += ['-std=c++17']
    if tests:
        cmd += ['-DMODACTIVATOR_TESTS']
    for inc in includes:
        cmd += ['-I', str(inc)]
    return src, tests, target, subprocess.run(cmd, capture_output=True, text=True)


jobs = [(s, False) for s in sources] + [(s, True) for s in sources if s in with_tests]
results = list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(compile_one, jobs))
failed = False
for _, _, _, r in results:
    if r.returncode:
        print(r.stdout + r.stderr)
        failed = True
if failed:
    raise SystemExit(1)

libs = ['-ld3d12', '-ldxgi', '-ldwmapi', '-limm32', '-ldxguid', '-luser32', '-lgdi32']


def link(path, tests):
    objects = [str(t) for s, t_flag, t, _ in results
               if t_flag == tests or (not t_flag and s not in with_tests)]
    # With no dllexport in it, the release build would otherwise have every
    # global symbol exported automatically: over a thousand of them.
    subprocess.run([str(tc / 'x86_64-w64-mingw32-clang++.exe'), '-shared', '-static',
                    '-Wl,--exclude-all-symbols', '-o', str(path)] + objects + libs, check=True)
    print('Built ' + str(path))


link(out / 'ModActivator.asi', False)
link(obj / 'ModActivator-tests.asi', True)

# dist/ mirrors bin64: the plugin at the top, where the ASI loader looks, and
# everything else in a ModActivator folder beside it. What earlier builds
# staged at the top goes, so the folder can be copied into bin64 as it is.
for name in ['ModActivator.ini', 'ModActivator.KnownMods.ini', 'README.md', 'LICENSE',
             'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md']:
    (out / name).unlink(missing_ok=True)
for name in ['include', 'licenses', 'ModActivator']:
    shutil.rmtree(out / name, ignore_errors=True)
data = out / 'ModActivator'
data.mkdir()
shutil.copy2(base / 'ModActivator.ini', data / 'ModActivator.ini')
shutil.copy2(base / 'data/ModActivator.KnownMods.ini', data / 'ModActivator.KnownMods.ini')
# For mod authors who want to describe their shortcuts (see the header).
(data / 'include').mkdir()
shutil.copy2(base / 'include/ModActivatorBindings.h', data / 'include/ModActivatorBindings.h')
# Distributable documentation and required third-party notices.
for name in ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md']:
    shutil.copy2(base / name, data / name)
shutil.copytree(base / 'licenses', data / 'licenses')
