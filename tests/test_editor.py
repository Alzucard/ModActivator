import pathlib,ctypes,shutil
p=pathlib.Path('build/editor-tests').resolve();shutil.rmtree(p,ignore_errors=True);p.mkdir(parents=True)
m=ctypes.WinDLL(str(pathlib.Path('build/ModActivator-tests.asi').resolve()));assert m.ModActivatorConflictTest()==0
fn=m.ModActivatorEditorTest;fn.argtypes=[ctypes.c_char_p]*7+[ctypes.c_int]
ON_DISK=object() # expected snapshot: the file as written, read by the editor itself
def edit(name,data,sec,key,new,expected=None):
 path=p/name;path.write_bytes(data);b=ctypes.create_string_buffer(1000);r=fn((str(p)+'\\').encode(),name.encode(),sec.encode(),key.encode(),new.encode(),None if expected is ON_DISK else data if expected is None else expected,b,len(b));return r,b.value.decode(),path.read_bytes()
data=b'; My config\r\n[Menu]\r\n  ActivationKeyVK = 0x77 ; Keep this note\r\nRequireCtrl=1\r\nEnabled=1\r\n'
r,msg,out=edit('Numeric.ini',data,'Menu','ActivationKeyVK','F6');assert r==0,(r,msg);assert out==data.replace(b'0x77',b'0x75');assert (p/'Numeric.ini.modactivator.bak').read_bytes()==data
r,msg,out=edit('Numeric.ini',data,'Menu','ActivationKeyVK','Ctrl+F6');assert r and out==data
route=b'[SettingsWindow]\nManageLocationsHotkey=F8|LeftStick,RightStick,A\nOther=hello\n'
r,msg,out=edit('Route.ini',route,'SettingsWindow','ManageLocationsHotkey','F4');assert not r and out==route.replace(b'F8|',b'F4|'),msg
r,msg,out=edit('Stale.ini',route,'SettingsWindow','ManageLocationsHotkey','F4',b'old');assert r and out==route
r,msg,out=edit('ReShade.ini',b'[INPUT]\nKeyOverlay=36,0,0,0\n','INPUT','KeyOverlay','F4');assert r
r,msg,out=edit('Duplicate.ini',b'[Menu]\nHotkey=F4\nHotkey=F5\n','Menu','Hotkey','F6');assert r
# UTF-16 bytes cannot pass through c_char_p, so the editor is told to use the
# file as it is (None) and must refuse it as a format it cannot edit.
unicode=b'[Menu]\nHotkey=F4\n'.decode().encode('utf-16')
r,msg,out=edit('Unicode.ini',unicode,'Menu','Hotkey','F6',ON_DISK)
assert r and 'unsupported' in msg and out==unicode,(r,msg)
print('PASS: exact/modifier conflicts; numeric/hex encoding; comments and CRLF; modifier retention; controller suffix; original backup; stale-edit refusal; duplicate/UTF16/ReShade protection.')

for new in ['F4+F5','F5+Ctrl','Ctrl+Ctrl+F5','119']:
 r,msg,out=edit('InvalidNamed.ini',b'[Menu]\nHotkey=Ctrl+F4\n','Menu','Hotkey',new)
 assert r and out==b'[Menu]\nHotkey=Ctrl+F4\n',(new,r,msg)
assert m.ModActivatorReliabilityTest()==0
print('PASS: malformed multi-main/repeated/reordered modifiers, ambiguous named numeric, strict ReShade arrays, whitespace and restart queue guard.')

from tempfile import TemporaryDirectory
with TemporaryDirectory(dir=p) as tmp:
    fixture = pathlib.Path(tmp)
    (fixture / 'First.ini').write_text('[Menu]\nHotkey=F8\n')
    (fixture / 'Second.ini').write_text('[Menu]\nHotkey=0x77\nRequireCtrl=1\n')
    settings = m.ModActivatorSettingsTest
    settings.argtypes = [ctypes.c_char_p]
    result = settings((str(fixture) + '\\').encode())
    assert result == 0, f'Settings regression check failed at step {result}'
with TemporaryDirectory(dir=p) as tmp:
    migration = m.ModActivatorMigrationTest
    migration.argtypes = [ctypes.c_char_p]
    result = migration((str(tmp) + '\\').encode())
    assert result == 0, f'Migration check failed at step {result}'
print('PASS: menu choices live in ModActivator.User.ini, moved once from an older ModActivator.ini.')
print('PASS: typed and captured chords, numeric modifier retention, keyboard layout mapping, saved opener, built-in and custom tab placement, stable binding labels and comments across type changes.')

commented = b'[Menu]\r\nHotkey=F8 ; note | kept verbatim\r\n'
r, msg, out = edit('InlineNote.ini', commented, 'Menu', 'Hotkey', 'F7')
assert not r and out == commented.replace(b'F8', b'F7'), (r, msg, out)
suffixed = b'[Menu]\nHotkey=F8|LeftStick,RightStick,A ; keep | note\n'
r, msg, out = edit('InlineController.ini', suffixed, 'Menu', 'Hotkey', 'F7')
assert not r and out == suffixed.replace(b'F8', b'F7'), (r, msg, out)
with TemporaryDirectory(dir=p) as tmp:
    fixture = pathlib.Path(tmp)
    config = fixture / 'Same.ini'
    contents = b'[Menu]\nHotkey=F8\n'
    config.write_bytes(contents)
    error = ctypes.create_string_buffer(1000)
    result = fn((str(fixture) + '\\').encode(), b'Same.ini', b'Menu',
                b'Hotkey', b'F8', contents, error, len(error))
    assert result == 0 and config.read_bytes() == contents
    assert not config.with_suffix('.ini.modactivator.bak').exists()
print('PASS: inline comment and controller suffix preservation, no-op edits.')

# A temporary file left by a save that died part way must not block saving.
stale = b'[Menu]\nHotkey=F8\n'
(p / 'Stale2.ini.modactivator.tmp').write_bytes(b'half a file')
r, msg, out = edit('Stale2.ini', stale, 'Menu', 'Hotkey', 'F7')
assert not r and out == stale.replace(b'F8', b'F7'), (r, msg, out)
assert not (p / 'Stale2.ini.modactivator.tmp').exists()
print('PASS: a leftover temporary file from an interrupted save is replaced.')
