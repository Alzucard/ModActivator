from pathlib import Path
import subprocess,concurrent.futures
import argparse,os
base=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--toolchain',default=os.environ.get('LLVM_MINGW'));args=parser.parse_args()
if not args.toolchain:parser.error('Pass --toolchain PATH or set LLVM_MINGW to your extracted LLVM-MinGW folder')
tc=Path(args.toolchain).resolve()/'bin';out=base/'dist';out.mkdir(exist_ok=True)
obj=base/'build';obj.mkdir(exist_ok=True)
sources=list((base/'src').rglob('*.cpp'))+[base/'vendor/imgui'/n for n in ['imgui.cpp','imgui_draw.cpp','imgui_widgets.cpp','imgui_tables.cpp','backends/imgui_impl_win32.cpp']]+list((base/'vendor/minhook/src').glob('*.c'))+[base/'vendor/minhook/src/hde/hde64.c']
incs=[base/'src',base/'vendor/imgui',base/'vendor/imgui/backends',base/'vendor/minhook/include',base/'vendor/minhook/src/hde']
def compile_one(pair):
 i,src=pair; target=obj/(str(i)+'.o'); cmd=[str(tc/('x86_64-w64-mingw32-clang.exe' if src.suffix=='.c' else 'x86_64-w64-mingw32-clang++.exe')),'-c',str(src),'-o',str(target),'-O2','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_CRT_SECURE_NO_WARNINGS','-fms-extensions']
 if src.suffix=='.cpp':cmd+=['-std=c++17']
 for inc in incs:cmd+=['-I',str(inc)]
 r=subprocess.run(cmd,capture_output=True,text=True);return target,r
results=list(concurrent.futures.ThreadPoolExecutor(max_workers=4).map(compile_one,enumerate(sources)))
failed=False
for target,r in results:
 if r.returncode:print(r.stdout+r.stderr);failed=True
if failed:raise SystemExit(1)
cmd=[str(tc/'x86_64-w64-mingw32-clang++.exe'),'-shared','-static','-o',str(out/'ModActivator.asi')]+[str(t) for t,r in results]+['-ld3d12','-ldxgi','-ldwmapi','-limm32','-lxinput9_1_0','-ldxguid','-luser32','-lgdi32']
subprocess.run(cmd,check=True)
print('Built '+str(out/'ModActivator.asi'))

import shutil
shutil.copy2(base/'ModActivator.ini',out/'ModActivator.ini')

# Stage distributable documentation and required third-party notices.
for name in ['README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md']:
    shutil.copy2(base/name,out/name)
shutil.copytree(base/'licenses',out/'licenses',dirs_exist_ok=True)
