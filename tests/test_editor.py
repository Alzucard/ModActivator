import pathlib,ctypes
p=pathlib.Path('build/editor-tests').resolve();p.mkdir(exist_ok=True)
m=ctypes.WinDLL(str(pathlib.Path('dist/ModActivator.asi').resolve()));assert m.ModActivatorConflictTest()==0
fn=m.ModActivatorEditorTest;fn.argtypes=[ctypes.c_char_p]*7+[ctypes.c_int]
def edit(name,data,sec,key,new,expected=None):
 path=p/name;path.write_bytes(data);b=ctypes.create_string_buffer(1000);r=fn((str(p)+'\\').encode(),name.encode(),sec.encode(),key.encode(),new.encode(),data if expected is None else expected,b,len(b));return r,b.value.decode(),path.read_bytes()
data=b'; My config\r\n[Menu]\r\n  ActivationKeyVK = 0x77 ; Keep this note\r\nRequireCtrl=1\r\nEnabled=1\r\n'
r,msg,out=edit('Numeric.ini',data,'Menu','ActivationKeyVK','F6');assert r==0,(r,msg);assert out==data.replace(b'0x77',b'0x75');assert (p/'Numeric.ini.modactivator.bak').read_bytes()==data
r,msg,out=edit('Numeric.ini',data,'Menu','ActivationKeyVK','Ctrl+F6');assert r and out==data
route=b'[SettingsWindow]\nManageLocationsHotkey=F8|LeftStick,RightStick,A\nOther=hello\n'
r,msg,out=edit('Route.ini',route,'SettingsWindow','ManageLocationsHotkey','F4');assert not r and out==route.replace(b'F8|',b'F4|'),msg
r,msg,out=edit('Stale.ini',route,'SettingsWindow','ManageLocationsHotkey','F4',b'old');assert r and out==route
r,msg,out=edit('ReShade.ini',b'[INPUT]\nKeyOverlay=36,0,0,0\n','INPUT','KeyOverlay','F4');assert r
r,msg,out=edit('Duplicate.ini',b'[Menu]\nHotkey=F4\nHotkey=F5\n','Menu','Hotkey','F6');assert r
r,msg,out=edit('Unicode.ini','[Menu]\nHotkey=F4\n'.encode('utf-16'),'Menu','Hotkey','F6');assert r
print('PASS: exact/modifier conflicts; numeric/hex encoding; comments and CRLF; modifier retention; controller suffix; original backup; stale-edit refusal; duplicate/UTF16/ReShade protection.')

for new in ['F4+F5','F5+Ctrl','Ctrl+Ctrl+F5','119']:
 r,msg,out=edit('InvalidNamed.ini',b'[Menu]\nHotkey=Ctrl+F4\n','Menu','Hotkey',new)
 assert r and out==b'[Menu]\nHotkey=Ctrl+F4\n',(new,r,msg)
assert m.ModActivatorReliabilityTest()==0
print('PASS: malformed multi-main/repeated/reordered modifiers, ambiguous named numeric, strict ReShade arrays, whitespace and restart queue guard.')
