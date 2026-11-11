import ctypes, pathlib
from tempfile import TemporaryDirectory

m = ctypes.WinDLL(str(pathlib.Path('build/ModActivator-tests.asi').resolve()))
pathlib.Path('build').mkdir(exist_ok=True)
with TemporaryDirectory(dir='build') as tmp:
    text = m.ModActivatorTextTest
    text.argtypes = [ctypes.c_char_p]
    result = text((str(pathlib.Path(tmp).resolve()) + '\\').encode('mbcs'))
    assert result == 0, f'Text regression check failed at step {result}'
print('PASS: UTF-16, UTF-8 and ANSI comments; non-Latin glyphs merged from a Windows font; '
      'UI scale follows the resolution; the menu copies the catalog once per change.')
