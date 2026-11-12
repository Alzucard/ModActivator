"""Where a binding's name and description come from, strongest first: your own
note, the mod's export, ModActivator.KnownMods.ini, the config comment, a
readme. Also: database entries make odd field names discoverable, and a mod
that writes hex key codes is read and written in hex."""
import ctypes, pathlib, shutil

root = pathlib.Path('build/description-tests').resolve()
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)
(root / 'ModActivator').mkdir()  # ModActivator's own files
dll = pathlib.Path('build/ModActivator-tests.asi').resolve()
m = ctypes.WinDLL(str(dll))
fn = m.ModActivatorDescribeTest
fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]

(root / 'ModActivator/ModActivator.KnownMods.ini').write_text(
    '; fixture database\n'
    '[PrivateStorageAnywhere.ini]\n'
    '@mod = Private Storage Anywhere | Opens storage from anywhere.\n'
    '@numbers = hex\n'
    'PrivateHotkey = menu | Private Storage | Opens your Private Storage.\n'
    '[DynamicScalify.ini]\n'
    'OpenMenu = menu | Scaling menu | Opens the scaling menu.\n'
    '[Combined.ini]\n'
    'Hotkey = toggle | From the database | The database wins over the comment.\n'
    '[Preset.ini]\n'
    'ToggleHotkey = toggle | Any section | Generic.\n'
    'UserPreset*/ToggleHotkey = toggle | Preset | Prefix section.\n'
    'Exact/ToggleHotkey = toggle | Exact | Exact section.\n'
    '[Nested/deep.ini]\n'
    'Hotkey = menu | Deep menu | Matched by its folder.\n'
    '[Compass.ini]\n'
    'Menu/HotkeyVk = menu | Compass menu | Ctrl from a flag. | Ctrl?HotkeyCtrl\n'
    'Menu/MapHotkeyVk = toggle | Minimap | Always with Ctrl. | Ctrl\n'
    'Menu/HotkeyCtrl = ignore\n'
    '[Flagless.ini]\n'
    'Menu/HotkeyVk = menu | Flag off | Ctrl only when the flag is set. | Ctrl?HotkeyCtrl\n'
    '[Linked.ini]\n'
    'OpenKey2Vk = menu | Second key | The modifier another setting names. | =OpenKey2WithVk\n',
    encoding='utf-8')
(root / 'Compass.ini').write_text('[Menu]\nHotkeyCtrl=1\nHotkeyVk=118\nMapHotkeyVk=119\n')
(root / 'Flagless.ini').write_text('[Menu]\nHotkeyCtrl=0\nHotkeyVk=118\n')
(root / 'Linked.ini').write_text('[General]\nOpenKey2WithVk=ALT\nOpenKey2Vk=F7\n')
(root / 'PrivateStorageAnywhere.ini').write_text('[Hotkeys]\nPrivateHotkey=73\nReloadKey=7A\n')
(root / 'DynamicScalify.ini').write_text('[General]\nOpenMenu=F6\n')
(root / 'Combined.ini').write_text('[Menu]\n; Opens the combined menu.\nHotkey=F2\n')
(root / 'Commented.ini').write_text('[Menu]\n; Opens the commented window.\nHotkey=F3\n')
(root / 'Preset.ini').write_text('[Main]\nToggleHotkey=V\n[UserPreset2]\nToggleHotkey=B\n[Exact]\nToggleHotkey=N\n')
(root / 'Nested').mkdir()
(root / 'Nested/deep.ini').write_text('[Menu]\nHotkey=F4\n')
(root / 'ReadmeMod.ini').write_text('[Menu]\nHotkey=F7\n[Other]\nBareKey=F8\n')
(root / 'ReadmeMod readme.txt').write_text(
    'ReadmeMod 1.0\n\nHotkey = F7 - opens the readme mod window\n\nBareKey:\nStarts the readme demo.\n')
(root / 'ReadmeMod-LICENSE.txt').write_text('Hotkey grants you no rights at all.\n')
# A readme that could be anyone's, in the folder every mod shares, and one
# for a mod whose name merely starts the same: neither describes ReadmeMod or
# Unrelated. A generic readme in a mod's own folder does describe that mod.
(root / 'README.txt').write_text('Other tool\nHotkey = F9 - toggles the other tool overlay\n')
(root / 'ReadmeModPlus.txt').write_text('BareKey: belongs to another mod entirely.\n')
(root / 'Unrelated.ini').write_text('[Menu]\nHotkey=F3\n')
(root / 'Boxed').mkdir()
(root / 'Boxed/Boxed.ini').write_text('[Menu]\nHotkey=F5\n')
(root / 'Boxed/readme.txt').write_text('Hotkey: opens the boxed window.\n')
# Keys named only by the word, or ending in more than one suffix.
(root / 'Names.ini').write_text('[Menu]\nHotkey=F1\n[Other]\nHotkeyVk=113\nMapHotkeyVk=114\n')
# The mod export: the test DLL describes [Hotkeys] SecretKey in a config
# named after itself.
(root / 'ModActivator-tests.ini').write_text('[Hotkeys]\nSecretKey=F9\n; Comment loses.\nOtherKey=F10\n')
# Your own name and note, hex-encoded UTF-8, beat everything. They live in
# ModActivator.User.ini, which no release ships; the same sections in the
# shipped ModActivator.ini, which an update replaces, are not read.
mine = 'commented.ini|menu|hotkey'
(root / 'ModActivator/ModActivator.User.ini').write_text(
    '[BindingNames]\n' + mine + '=' + 'Mein Fenster'.encode().hex() + '\n'
    '[BindingNotes]\n' + mine + '=' + 'Meine Notiz\nöffnet es'.encode().hex() + '\n')
(root / 'ModActivator/ModActivator.ini').write_text(
    '[BindingNames]\ncombined.ini|menu|hotkey=' + 'Not read'.encode().hex() + '\n')

modules = ';'.join(['PrivateStorageAnywhere.asi', 'DynamicScalify.asi', 'Combined.asi',
                    'Commented.asi', 'Preset.asi', 'Nested.asi', 'ReadmeMod.asi', dll.name,
                    'Compass.asi', 'Flagless.asi', 'Linked.asi', 'Unrelated.asi',
                    'Boxed.asi', 'Names.asi', 'ModActivator.asi'])
buf = ctypes.create_string_buffer(100000)
r = fn((str(root) + '\\').encode(), modules.encode(), buf, len(buf))
assert r >= 0, f'describe test failed with {r}'

# A broken mod export (pointers to nothing, a count past its array) is
# refused entry by entry instead of crashing the game, and the good entry
# before it still counts.
assert m.ModActivatorForeignReadTest() == 0, 'foreign memory check failed'
m.ModActivatorBadExportTest(1)
bad = ctypes.create_string_buffer(100000)
r2 = fn((str(root) + '\\').encode(), dll.name.encode(), bad, len(bad))
m.ModActivatorBadExportTest(0)
assert r2 >= 0, f'describe with a broken export failed with {r2}'
assert 'Exported name' in bad.value.decode('utf-8'), bad.value
print('PASS: a broken mod export is refused, not read, and its good entries still count.')
rows = {}
for line in buf.value.decode('utf-8').splitlines():
    f = line.split('|')
    rows[(f[0], f[1], f[2])] = dict(keys=f[3], name=f[4], mod=f[5], source=int(f[6]),
                                    menu=int(f[7]), feature=int(f[8]), description=f[9])
print('\n'.join(f'{k}: {v}' for k, v in rows.items()))
README, COMMENT, KNOWN, MOD, USER = 1, 2, 3, 4, 5

# ModActivator's own folder is full of Key = value lines; none is a binding.
assert not [k for k in rows if k[0].lower().startswith('modactivator/')], rows.keys()
print("PASS: ModActivator's own files in its folder are not read as mod bindings.")

row = rows['PrivateStorageAnywhere.ini', 'Hotkeys', 'PrivateHotkey']
assert row['keys'] == '115,' and row['name'] == 'Private Storage' and row['source'] == KNOWN, row
assert row['mod'] == 'Private Storage Anywhere' and row['menu'] == 150, row
assert rows['PrivateStorageAnywhere.ini', 'Hotkeys', 'ReloadKey']['keys'] == '122,'
print('PASS: database names, kinds and hex key codes.')

assert rows['Compass.ini', 'Menu', 'HotkeyVk']['keys'] == '17,118,'
assert rows['Compass.ini', 'Menu', 'MapHotkeyVk']['keys'] == '17,119,'
assert ('Compass.ini', 'Menu', 'HotkeyCtrl') not in rows  # its 1 is not the 1 key
assert rows['Flagless.ini', 'Menu', 'HotkeyVk']['keys'] == '118,'
assert rows['Linked.ini', 'General', 'OpenKey2Vk']['keys'] == '18,118,'
print('PASS: modifiers kept in separate settings, and a flag that only looks like a key.')

row = rows['DynamicScalify.ini', 'General', 'OpenMenu']
assert row['keys'] == '117,' and row['name'] == 'Scaling menu', row
print('PASS: a database entry makes a field named OpenMenu a shortcut.')

row = rows['Combined.ini', 'Menu', 'Hotkey']
assert row['source'] == KNOWN and row['feature'] == 150 and row['menu'] == 0, row
assert row['name'] == 'From the database', row  # not the decoy in ModActivator.ini
row = rows['Commented.ini', 'Menu', 'Hotkey']
assert row['source'] == USER and row['name'] == 'Mein Fenster', row
assert row['description'] == 'Meine Notiz\\nöffnet es', row
print('PASS: the database beats the comment, and your note beats everything.')

assert rows['Preset.ini', 'Main', 'ToggleHotkey']['name'] == 'Any section'
assert rows['Preset.ini', 'UserPreset2', 'ToggleHotkey']['name'] == 'Preset'
assert rows['Preset.ini', 'Exact', 'ToggleHotkey']['name'] == 'Exact'
assert rows['Nested/deep.ini', 'Menu', 'Hotkey']['name'] == 'Deep menu'
print('PASS: exact sections beat prefixes beat any section; folders match.')

row = rows['ReadmeMod.ini', 'Menu', 'Hotkey']
assert row['source'] == README and row['description'] == 'opens the readme mod window', row
row = rows['ReadmeMod.ini', 'Other', 'BareKey']
assert row['source'] == README and row['description'] == 'Starts the readme demo.', row
print('PASS: readme lines, on the same line and the next, and licences ignored.')

assert rows['Unrelated.ini', 'Menu', 'Hotkey']['source'] != README, rows['Unrelated.ini', 'Menu', 'Hotkey']
assert rows['ReadmeMod.ini', 'Other', 'BareKey']['description'] == 'Starts the readme demo.'
row = rows['Boxed/Boxed.ini', 'Menu', 'Hotkey']
assert row['source'] == README and row['description'] == 'opens the boxed window.', row
print("PASS: in bin64 only a readme named after the mod counts; in a mod's own folder any readme does.")

assert rows['Names.ini', 'Menu', 'Hotkey']['name'] == 'Hotkey'
assert rows['Names.ini', 'Other', 'HotkeyVk']['name'] == 'Hotkey'
assert rows['Names.ini', 'Other', 'MapHotkeyVk']['name'] == 'Map'
print('PASS: keys named only Hotkey or HotkeyVk keep that name instead of "Hot".')

row = rows[dll.stem + '.ini', 'Hotkeys', 'SecretKey']
assert row['source'] == MOD and row['name'] == 'Exported name' and row['feature'] == 150, row
assert row['description'] == 'Described by the mod itself.', row
assert rows[dll.stem + '.ini', 'Hotkeys', 'OtherKey']['source'] == COMMENT
print('PASS: the mod export names, describes and classifies its binding.')
