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
