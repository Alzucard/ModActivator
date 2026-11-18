"""Where a binding's name and description come from, strongest first: your own
note, the mod's export, ModActivator.KnownMods.ini, the config comment, a
readme. Also: database entries make odd field names discoverable, and a mod
that writes hex key codes is read and written in hex."""
import ctypes, pathlib, shutil

root = pathlib.Path('build/description-tests').resolve()
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)
(root / 'ModActivator').mkdir()  # the log folder
dll = pathlib.Path('build/ModActivator-tests.asi').resolve()
m = ctypes.WinDLL(str(dll))
fn = m.ModActivatorDescribeTest
fn.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]

(root / 'ModActivator.KnownMods.ini').write_text(
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
    'OpenKey2Vk = menu | Second key | The modifier another setting names. | =OpenKey2WithVk\n'
    '[Codes.ini]\n'
    'Codes*/Key2 = action | Code | From the pattern. | Shift =Key1\n'
    'CodesFree/Key2 = action | Free | | -\n',
    encoding='utf-8')
(root / 'Compass.ini').write_text('[Menu]\nHotkeyCtrl=1\nHotkeyVk=118\nMapHotkeyVk=119\n')
(root / 'Flagless.ini').write_text('[Menu]\nHotkeyCtrl=0\nHotkeyVk=118\n')
(root / 'Linked.ini').write_text('[General]\nOpenKey2WithVk=ALT\nOpenKey2Vk=F7\n')
(root / 'Codes.ini').write_text('[Codes]\nKey1=17\nKey2=9\n[CodesFree]\nKey1=17\nKey2=118\n')
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
(root / 'ModActivator.User.ini').write_text(
    '[BindingNames]\n' + mine + '=' + 'Mein Fenster'.encode().hex() + '\n'
    '[BindingNotes]\n' + mine + '=' + 'Meine Notiz\nöffnet es'.encode().hex() + '\n')
(root / 'ModActivator.ini').write_text(
    '[BindingNames]\ncombined.ini|menu|hotkey=' + 'Not read'.encode().hex() + '\n')

# Mods that say what they do only in their own file: a version resource, a
# config template, a settings schema, a log line, and a poller whose strings
# say enabled/disabled. Read from disk, never loaded.
for name in ['BinaryMod.asi', 'BinaryToggle.asi']:
    shutil.copy2(pathlib.Path('build') / name, root / name)
(root / 'BinaryMod.ini').write_text('[General]\nTemplateKey=F6\nPanelKey=F7\nPhotoKey=F8\n')
(root / 'BinaryToggle.ini').write_text('[General]\nHelperKey=F6\n')

modules = ';'.join(['BinaryMod.asi', 'BinaryToggle.asi', 'PrivateStorageAnywhere.asi', 'DynamicScalify.asi', 'Combined.asi',
                    'Commented.asi', 'Preset.asi', 'Nested.asi', 'ReadmeMod.asi', dll.name,
                    'Compass.asi', 'Flagless.asi', 'Linked.asi', 'Codes.asi', 'Unrelated.asi',
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
# A =Field anywhere in the rule makes the value a key code (9 is Tab); an
# entry for one section takes the pattern's modifiers unless it says - (none).
assert rows['Codes.ini', 'Codes', 'Key2']['keys'] == '16,17,9,', rows['Codes.ini', 'Codes', 'Key2']
row = rows['Codes.ini', 'CodesFree', 'Key2']
assert row['keys'] == '118,' and row['name'] == 'Free' and row['description'] == 'From the pattern.', row
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

BINARY = 6
row = rows['BinaryMod.ini', 'General', 'TemplateKey']
assert row['source'] == BINARY and row['description'] == 'Opens the template window.', row
assert row['mod'] == 'Binary Fixture' and row['menu'] == 120, row
row = rows['BinaryMod.ini', 'General', 'PanelKey']
assert row['source'] == BINARY and row['menu'] == 120, row
assert row['description'].startswith('Key to open or close the binary panel'), row
assert 'Shows every fixture setting at once.' in row['description'], row
row = rows['BinaryMod.ini', 'General', 'PhotoKey']
assert row['source'] == BINARY and row['description'] == 'Snapshot saved to the log folder (PhotoKey).', row
assert ('BinaryMod.ini', 'General', 'Volume') not in rows
print("PASS: a mod's file names it and describes its keys: template, schema, then a sentence; complaints skipped.")

row = rows['BinaryToggle.ini', 'General', 'HelperKey']
assert row['feature'] == 120 and row['menu'] != 120, row
print('PASS: a single key read by a poller that reports enabled/disabled is a toggle.')

known_root = pathlib.Path('build/known-mods-test').resolve()
shutil.rmtree(known_root, ignore_errors=True)
known_root.mkdir(parents=True)
known = m.ModActivatorKnownModsTest
known.argtypes = [ctypes.c_char_p]
result = known((str(known_root) + '\\').encode())
assert result == 0, f'Known-mods check failed at step {result}'
print('PASS: the known-mods list is built in; a file beside the plugin only adds to it.')

# What a mod's file said is kept on disk until the mod changes.
assert (root / 'ModActivator' / 'ModActivator.ModCache.bin').exists(), 'the scan wrote no mod cache'
cache_root = pathlib.Path('build/mod-cache-test').resolve()
shutil.rmtree(cache_root, ignore_errors=True)
cache_root.mkdir(parents=True)
shutil.copy2(pathlib.Path('build') / 'BinaryMod.asi', cache_root / 'BinaryMod.asi')
cache = m.ModActivatorModCacheTest
cache.argtypes = [ctypes.c_char_p, ctypes.c_wchar_p]
result = cache((str(cache_root) + '\\').encode(), str(cache_root / 'BinaryMod.asi'))
assert result == 0, f'Mod cache check failed at step {result}'
print("PASS: a mod's file is read once, kept on disk, and read again only when it changes.")

# DesertLink's all-in-one menu keeps the shortcuts you assign in its own menu
# as [Shortcut.<action>] sections: Key1 a modifier (or 0), Key2 the key, both
# key codes. Names come from DesertLink's own titles where known, and are the
# raw action otherwise. A Key1 that is no modifier is never dropped: the
# shortcut is not listed rather than sent without it.
dl_root = pathlib.Path('build/desertlink-shortcuts').resolve()
shutil.rmtree(dl_root, ignore_errors=True)
(dl_root / 'ModActivator').mkdir(parents=True)
(dl_root / 'DesertLinkData' / 'ModMenu').mkdir(parents=True)
(dl_root / 'DesertLinkData' / 'ModMenu' / 'Settings.dat').write_bytes(
    ('\r\n[General]\r\nOpenKeyVk=45\r\n'
     '[Shortcut.cold_protection.toggle]\r\nKey1=17\r\nKey2=191\r\nPad=0\r\n'
     '[Shortcut.fat_stacks.toggle]\r\nKey1=17\r\nKey2=187\r\nPad=0\r\n'
     '[Shortcut.flight.hover]\r\nKey1=0\r\nKey2=118\r\nPad=0\r\n'
     '[Shortcut.storage.open.private]\r\nKey1=16\r\nKey2=119\r\nPad=0\r\n'
     '[Shortcut.max_bond.toggle]\r\nKey1=65\r\nKey2=120\r\nPad=0\r\n'
     '[Shortcut.just_take.toggle]\r\nKey1=65\r\nKey2=0\r\nPad=0\r\n'
     '[Shortcut.flight.cancel]\r\nKey1=17\r\nKey2=9\r\nPad=0\r\n'
     '[Shortcut.unlimited_health.toggle]\r\nKey1=0\r\nKey2=120\r\nPad=0\r\n'
     '[Shortcut.menu.open]\r\nKey1=0\r\nKey2=121\r\nPad=0\r\n'
     '[Shortcut.player.refill]\r\nKey1=65\r\nKey2=65\r\nPad=0\r\n'
     '[Shortcut.flight.jump]\r\nKey1=RCTRL\r\nKey2=118\r\nPad=0\r\n').encode('utf-16'))
def dl_scan():
    buf = ctypes.create_string_buffer(100000)
    r = fn((str(dl_root) + '\\').encode(), b'DesertLinkModMenu.asi', buf, len(buf))
    assert r >= 0, f'describe test failed with {r}'
    found = {}
    for line in buf.value.decode('utf-8').splitlines():
        f = line.split('|')
        found[f[1]] = dict(key=f[2], keys=f[3], name=f[4], menu=int(f[7]), feature=int(f[8]),
                           description=f[9])
    return found
dl = dl_scan()
print('\n'.join(f'{k}: {v}' for k, v in dl.items()))
row = dl['Shortcut.cold_protection.toggle']
assert row['keys'] == '17,191,' and row['name'] == 'Cold Protection' and row['feature'] == 150, row
row = dl['Shortcut.fat_stacks.toggle']
assert row['keys'] == '17,187,' and row['name'] == 'Unlimited Stacks (Fat Stacks)' and row['feature'] == 150, row
row = dl['Shortcut.flight.hover']
assert row['keys'] == '118,' and row['name'] == 'Hover' and not row['feature'] and not row['menu'], row
row = dl['Shortcut.storage.open.private']
assert row['keys'] == '16,119,' and row['name'] == 'storage.open.private', row
assert 'Shortcut.max_bond.toggle' not in dl and 'Shortcut.just_take.toggle' not in dl, dl
# Nor when Key1 is the same key as Key2, or a name ModActivator cannot read.
assert 'Shortcut.player.refill' not in dl and 'Shortcut.flight.jump' not in dl, dl
# Key codes, not digit keys: 9 is Tab. Every <module>.toggle is a toggle,
# named by the module where no title is known. Opening the menu is no second
# menu card. Entries named by hand take the modifier and the note from the
# general entry.
assert dl['Shortcut.flight.cancel']['keys'] == '17,9,', dl['Shortcut.flight.cancel']
row = dl['Shortcut.unlimited_health.toggle']
assert row['name'] == 'unlimited_health' and row['feature'] == 150, row
row = dl['Shortcut.menu.open']
assert not row['menu'] and not row['feature'], row
assert "DesertLink's own menu" in dl['Shortcut.cold_protection.toggle']['description'], dl
# A shortcut changed in DesertLink's menu shows at the next read.
settings = dl_root / 'DesertLinkData' / 'ModMenu' / 'Settings.dat'
settings.write_bytes(settings.read_bytes().decode('utf-16').replace('Key2=191', 'Key2=190').encode('utf-16'))
assert dl_scan()['Shortcut.cold_protection.toggle']['keys'] == '17,190,'
assert all(v['key'] == 'Key2' for k, v in dl.items() if k.startswith('Shortcut.')), dl
print("PASS: DesertLink's own shortcuts, named from its titles or by the raw action, never without their modifier.")
