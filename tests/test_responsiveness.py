import ctypes,pathlib
m=ctypes.WinDLL(str(pathlib.Path('dist/ModActivator.asi').resolve()))
assert m.ModActivatorResponsivenessTest()==0
print('PASS: cached Hotkeys rendering and toggle-window behavior.')

assert m.ModActivatorUISearchTest()==0
print("PASS: shortcut and formatted-name search.")

assert m.ModActivatorScrollTest() == 0, "Wheel must scroll the list over a card"
print("PASS: wheel over a mod card scrolls its parent list.")

from tempfile import TemporaryDirectory

with TemporaryDirectory(dir='build') as tmp:
    drag = m.ModActivatorDragTest
    drag.argtypes = [ctypes.c_char_p]
    result = drag((str(pathlib.Path(tmp).resolve()) + '\\').encode())
    assert result == 0, f'Drag-and-drop regression check failed at step {result}'
print("PASS: mouse drag reorders cards, saves the result, and supports moving back up.")

result = m.ModActivatorGroupsTest()
assert result == 0, f'Grouped bindings regression check failed at step {result}'
print("PASS: groups collapse by default, expand on search, and keep related bindings together.")

result = m.ModActivatorInputQueueTest()
assert result == 0, f'Input queue regression check failed at step {result}'
print("PASS: window-thread input is handed off in order, and menu close and overflow discard stale input.")
