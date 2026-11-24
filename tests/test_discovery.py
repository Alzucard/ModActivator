import ctypes,pathlib,shutil
root=pathlib.Path('build/discovery-tests').resolve();shutil.rmtree(root,ignore_errors=True);root.mkdir(parents=True)
(root/'NewUI.ini').write_text('[General]\nActivationKeyVK=118\n[Menu]\nHotkey=Ctrl+7\nRequireAlt=1\n[Gameplay]\nStanceCycleKey=F3\nHotkeyToggleEffect=F10\n')
(root/'ToggleOnly.ini').write_text('[General]\nHotkeyToggleEffect=F10\n[Hotkeys]\nToggleKey=121\nDisableKey=122\n')
(root/'Ghost.ini').write_text('[Menu]\nHotkey=F6\n')
(root/'MissingUI.ini').write_text('[Menu]\nEnabled=1\n')
(root/'Disabled.ini').write_text('[Menu]\nHotkey=None\n')
(root/'Nested').mkdir(exist_ok=True);(root/'Nested/startup.ini').write_text('[Startup]\nInstantResumeHotkey=Home\n[Menu]\nHotkey=F5\n')
m=ctypes.WinDLL(str(pathlib.Path('build/ModActivator-tests.asi').resolve()));assert m.ModActivatorSelfTest()==0
fn=m.ModActivatorDiscoveryTest;fn.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int]
def scan(path,mods):
 b=ctypes.create_string_buffer(100000);r=fn((str(path)+'\\').encode(),mods.encode(),b,len(b));assert r>=0,r;return b.value.decode()
r=scan(root,'NewUI.asi;ToggleOnly.asi;MissingUI.asi;Disabled.asi;Nested.asi');print(r)
assert len(r.strip().splitlines())==2
assert 'NewUI.asi|NewUI.ini|Menu|Hotkey|18,17,55,' in r
assert 'Nested.asi|Nested/startup.ini|Menu|Hotkey|116,' in r
assert all(x not in r for x in ['Ghost','ToggleOnly','MissingUI','Disabled','Stance','Effect','Resume'])
(root/'LateMod.ini').write_text('[UI]\nOpenKey=F8\n')
assert 'LateMod' not in scan(root,'NewUI.asi')
assert 'LateMod.asi' in scan(root,'NewUI.asi;LateMod.asi')
(root/'NewUI.ini').write_text('[Menu]\nHotkey=\n[Gameplay]\nToggleKey=F7\n')
assert scan(root,'NewUI.asi')==''

fn=m.ModActivatorToggleDiscoveryTest;fn.argtypes=[ctypes.c_char_p,ctypes.c_char_p,ctypes.c_char_p,ctypes.c_int]
r=scan(root,'ToggleOnly.asi;Nested.asi');assert len(r.strip().splitlines())==1 and '|HotkeyToggleEffect|121,' in r
print('PASS: UI discovery, one UI per mod, no fallback, unloaded filtering, nested configs, modifiers, late mods and toggle discovery.')

# The system dxgi.dll is loaded in every D3D12 game. A leftover ReShade.ini
# must not be attributed to it when ReShade itself is not loaded (issue #4).
fn=m.ModActivatorDiscoveryTest
(root/'ReShade.ini').write_text('[INPUT]\nKeyOverlay=36,0,0,0\n')
assert 'ReShade.ini' not in scan(root,'dxgi.dll;NewUI.asi')
(root/'ReShade.ini').unlink()
fn=m.ModActivatorToggleDiscoveryTest
print('PASS: a leftover ReShade.ini without ReShade loaded is not listed.')

# A field called ToggleKey can open a UI. A comment attached to a different
# setting must not affect its classification.
(root/'WikiFixture.ini').write_text('[Wiki]\n; Keyboard key to open/close the wiki\nToggleKey=F7\n')
(root/'Opaque.ini').write_text('[Input]\n; Show the menu at startup\nStartupHint=1\nToggleKey=F6\n')
(root/'ModActivator').mkdir(exist_ok=True);(root/'ModActivator/ModActivator.User.ini').write_text('')
toggle_scan = fn
fn = m.ModActivatorDiscoveryTest
assert 'WikiFixture.asi|WikiFixture.ini|Wiki|ToggleKey|118,' in scan(root,'WikiFixture.asi')
assert scan(root,'Opaque.asi') == ''
fn = toggle_scan
assert 'Opaque.asi|Opaque.ini|Input|ToggleKey|117,' in scan(root,'Opaque.asi')
assert scan(root,'WikiFixture.asi') == ''
(root/'ModActivator/ModActivator.User.ini').write_text('[BindingTypes]\nopaque.ini|input|togglekey=1\nwikifixture.ini|wiki|togglekey=2\n')
assert 'WikiFixture.asi' in scan(root,'WikiFixture.asi')
assert scan(root,'Opaque.asi') == ''
fn = m.ModActivatorDiscoveryTest
assert 'Opaque.asi' in scan(root,'Opaque.asi')
assert scan(root,'WikiFixture.asi') == ''
(root/'ModActivator/ModActivator.User.ini').write_text('[BindingTypes]\nopaque.ini|input|togglekey=3\n')
assert scan(root,'Opaque.asi') == ''
fn = toggle_scan
assert scan(root,'Opaque.asi') == ''
print('PASS: nearby comments, ambiguous names, and persisted binding-type overrides.')

# DesertLink's all-in-one menu keeps its INI text, UTF-16 with a BOM, in
# DesertLinkData/ModMenu/Settings.dat: no .ini, and no folder named like the
# mod. A .dat that is not INI text is never read as settings.
(root/'ModActivator/ModActivator.User.ini').write_text('')
menu=root/'DesertLinkData'/'ModMenu';menu.mkdir(parents=True,exist_ok=True)
(menu/'Settings.dat').write_bytes('\r\n[EditorPanel]\r\nOpenKeyVk=NONE\r\nOpenKey2Vk=113\r\nOpenKey2WithVk=16\r\n[FlashControls]\r\nToggleKey=F10\r\n[General]\r\nOpenKeyVk=45\r\n'.encode('utf-16'))
(menu/'recovery.dat').write_bytes(b'\x00\x01[General]\nOpenKeyVk=46\n')
(root/'DesertLinkModMenu.dat').write_bytes(b'\x89PNG[General]\nOpenKeyVk=47\n')
fn=m.ModActivatorDiscoveryTest
r=scan(root,'DesertLinkModMenu.asi')
assert r=='DesertLinkModMenu.asi|DesertLinkData/ModMenu/Settings.dat|General|OpenKeyVk|45,\n',r
fn=toggle_scan
for known in (1,0): # the folders find the file without the known-mods entry
 m.ModActivatorUseKnownModsTest(known)
 assert 'DesertLinkModMenu.asi|DesertLinkData/ModMenu/Settings.dat|FlashControls|ToggleKey|121,' in scan(root,'DesertLinkModMenu.asi'),known
m.ModActivatorUseKnownModsTest(0)
fn=m.ModActivatorDiscoveryTest
# A [General] OpenKeyVk is a menu key only where the known-mods list says so.
assert scan(root,'DesertLinkModMenu.asi')==''
m.ModActivatorUseKnownModsTest(1)
shutil.rmtree(root/'DesertLinkData');(root/'DesertLinkModMenu.dat').unlink()
# Near misses: one FooData folder, folders that spell only part of the name,
# or more than it, belong to no mod. A .cfg with INI text is read.
fn=toggle_scan
for path in ['SpellData/a.ini','SpellModData/a.ini','Desert/LinkMore/a.ini','Desert/LinkData/b.ini','Spell/a.cfg']:
 f=root/path;f.parent.mkdir(parents=True,exist_ok=True);f.write_text('[Hotkeys]\nToggleKey=F9\n')
r=scan(root,'Spell.asi;SpellMod.asi;DesertLink.asi;DesertLinkMoreX.asi')
assert r=='DesertLink.asi|Desert/LinkData/b.ini|Hotkeys|ToggleKey|120,\nSpell.asi|Spell/a.cfg|Hotkeys|ToggleKey|120,\n',r
for d in ['SpellData','Spell','SpellModData','Desert']:shutil.rmtree(root/d)
print('PASS: INI text in a .dat or .cfg, found through two folders that spell the mod name, and no near misses.')
