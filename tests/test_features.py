"""Cards for every chosen menu and toggle, conflicts with keys taken outside the
mods, controller bindings and their conflicts, the reload key, and the panel's
controller shortcut."""
import ctypes, pathlib, shutil

root = pathlib.Path('build/feature-tests').resolve()
shutil.rmtree(root, ignore_errors=True)
(root / 'ModActivator').mkdir(parents=True)
m = ctypes.WinDLL(str(pathlib.Path('build/ModActivator-tests.asi').resolve()))
fn = m.ModActivatorCatalogTest
fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]

(root / 'ModActivator/ModActivator.KnownMods.ini').write_text(
    '[@reserved]\n'
    'F12 = Steam: screenshot\n'
    'Alt+F4 = Windows: close the game\n'
    '[Storage.ini]\n'
    'Open = menu | Storage | Opens storage.\n'
    'Dresser = menu | Dresser | Opens the dresser.\n'
    'Ghost = toggle | Ghost | Ghost mode.\n'
    'Mark = action | Mark | A plain action.\n'
    '[Linked.ini]\n'
    'Jump = action | Jump | Its controller button is in a setting the list names. | | MyPad\n',
    encoding='utf-8')
(root / 'ModActivator/ModActivator.User.ini').write_text(
    '[BindingTypes]\nguess.ini|menu|togglekey=2\n'
    '[ReservedKeys]\nI = Game: inventory\n')
(root / 'Storage.ini').write_text('[Keys]\nOpen=F2\nDresser=F3\nGhost=F9\nMark=F11\n')
(root / 'Guess.ini').write_text('[Menu]\nHotkey=F5\nToggleKey=F6\n')
(root / 'Shot.ini').write_text('[Menu]\nHotkey=F12\n[Other]\nCtrlShotKey=Ctrl+F12\n')
(root / 'Inv.ini').write_text('[Menu]\nHotkey=I\n')
(root / 'Plain.ini').write_text('[Menu]\nHotkey=F4\n[Other]\nCloseKey=Ctrl+Alt+F4\n')
(root / 'Route.ini').write_text('[SettingsWindow]\nManageLocationsHotkey=F8|LeftStick,RightStick,A\n')
(root / 'Wiki.ini').write_text('[CDWiki]\nToggleKey=F7\nPadToggle=BACK+LS\n')
(root / 'Combat.ini').write_text('[Combat]\nStanceCycleKey=Grave\nStanceCycleButtons=L1+Down\n')
(root / 'Weather.ini').write_text('[General]\nHotkeyToggleEffect=F10\nControllerToggleEffect=dpad_down+a\n')
(root / 'Store.ini').write_text('[Hotkeys]\nPrivateHotkey=Insert\nPrivateControllerButton=0040\n')
(root / 'Bumpers.ini').write_text('[Pad]\nBothKey=Home\nBothButtons=LB+RB\n')
(root / 'Linked.ini').write_text('[Moves]\nJump=PageUp\nMyPad=Y\n')

modules = ';'.join(name + '.asi' for name in
                   ['Storage', 'Guess', 'Shot', 'Inv', 'Route', 'Wiki', 'Combat', 'Weather',
                    'Store', 'Bumpers', 'Linked', 'Plain'])
buf = ctypes.create_string_buffer(200000)
r = fn((str(root) + '\\').encode(), modules.encode(), buf, len(buf))
assert r >= 0, f'catalog test failed with {r}'
cards, rows = set(), {}
for line in buf.value.decode('utf-8').splitlines():
    f = line.split('|')
    if f[0] == 'card':
        cards.add((f[1], f[2], f[3]))
    else:
        rows[(f[1], f[2], f[3])] = dict(severity=int(f[4]), who=f[5], pad=f[6])
print('\n'.join(map(str, sorted(cards))))
print('\n'.join(f'{k}: {v}' for k, v in rows.items()))

# Every chosen menu and toggle is a card; a chosen action is not. A mod with
# nothing chosen keeps its one guessed card, plus the toggle you chose.
for key in ['Open', 'Dresser', 'Ghost']:
    assert ('Storage.ini', 'Keys', key) in cards, key
assert ('Storage.ini', 'Keys', 'Mark') not in cards
assert ('Guess.ini', 'Menu', 'ToggleKey') in cards and ('Guess.ini', 'Menu', 'Hotkey') in cards
print('PASS: every chosen menu and toggle gets a card; actions stay on the Hotkeys tab.')

shot = rows['Shot.ini', 'Menu', 'Hotkey']
assert shot['severity'] == 2 and 'Steam: screenshot' in shot['who'], shot
assert rows['Shot.ini', 'Other', 'CtrlShotKey']['severity'] == 1
inv = rows['Inv.ini', 'Menu', 'Hotkey']
assert inv['severity'] == 2 and 'Game: inventory' in inv['who'], inv
# F4 alone is not Alt+F4; Ctrl+Alt+F4 contains it.
assert 'Windows' not in rows['Plain.ini', 'Menu', 'Hotkey']['who'], rows['Plain.ini', 'Menu', 'Hotkey']
assert 'Windows: close the game' in rows['Plain.ini', 'Other', 'CloseKey']['who']
print('PASS: keys Steam, Windows or you reserved count as conflicts, exact and overlapping.')

assert rows['Route.ini', 'SettingsWindow', 'ManageLocationsHotkey']['pad'] == 'LS + RS + A'
assert rows['Wiki.ini', 'CDWiki', 'ToggleKey']['pad'] == 'Back + LS'
assert rows['Combat.ini', 'Combat', 'StanceCycleKey']['pad'] == 'LB + Down'
assert rows['Weather.ini', 'General', 'HotkeyToggleEffect']['pad'] == 'Down + A'
assert rows['Store.ini', 'Hotkeys', 'PrivateHotkey']['pad'] == 'LS'
assert rows['Linked.ini', 'Moves', 'Jump']['pad'] == 'Y'
assert rows['Storage.ini', 'Keys', 'Open']['pad'] == ''
print('PASS: controller buttons from a |suffix, a setting beside the key, XInput bits, or a named setting.')

store = rows['Store.ini', 'Hotkeys', 'PrivateHotkey']
assert store['severity'] == 1 and 'Wiki - CDWiki / ToggleKey (controller)' in store['who'], store
assert 'Route - SettingsWindow / ManageLocationsHotkey (controller)' in store['who'], store
both = rows['Bumpers.ini', 'Pad', 'BothKey']
assert both['severity'] == 1 and 'ModActivator (controller)' in both['who'], both
assert rows['Weather.ini', 'General', 'HotkeyToggleEffect']['severity'] == 0
print("PASS: controller combinations that press each other, or ModActivator's, are overlaps.")

ctl = m.ModActivatorControllerTest
ctl.argtypes = [ctypes.c_char_p]
tmp = root / 'controller'
tmp.mkdir()
result = ctl((str(tmp) + '\\').encode())
assert result == 0, f'controller check failed at step {result}'
print('PASS: controller spellings, the reload key lookup, and the saved controller shortcut.')
