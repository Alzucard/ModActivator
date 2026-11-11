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

shutil.copy2(base / 'ModActivator.ini', out / 'ModActivator.ini')
# Stage distributable documentation and required third-party notices.
for name in ['README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md']:
    shutil.copy2(base / name, out / name)
shutil.copytree(base / 'licenses', out / 'licenses', dirs_exist_ok=True)
