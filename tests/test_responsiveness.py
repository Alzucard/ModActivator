import ctypes,pathlib
m=ctypes.WinDLL(str(pathlib.Path('dist/ModActivator.asi').resolve()))
assert m.ModActivatorResponsivenessTest()==0
print('PASS: cached Hotkeys rendering and toggle-window behavior.')
