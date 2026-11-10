import ctypes,pathlib
m=ctypes.WinDLL(str(pathlib.Path('dist/ModActivator.asi').resolve()))
assert m.ModActivatorResponsivenessTest()==0
print('PASS: cached Hotkeys rendering and toggle-window behavior.')

assert m.ModActivatorUISearchTest()==0
print("PASS: shortcut and formatted-name search.")

assert m.ModActivatorScrollTest() == 0, "Wheel must scroll the list over a card"
print("PASS: wheel over a mod card scrolls its parent list.")
