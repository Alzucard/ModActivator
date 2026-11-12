// ExampleMod: a minimal ASI mod that ModActivator understands fully.
//
// It reads two shortcuts from ExampleMod.ini beside it, written the way
// ModActivator finds and reads them without any help:
//
//   [Menu]
//   ; Opens or closes the Example Mod window.
//   MenuKey=Ctrl+F7
//   [Effect]
//   ; Turns the example effect on or off.
//   ToggleKey=F8
//   ToggleButtons=LB+Down
//
// and exports ModActivatorBindingsV1, so ModActivator also shows the names
// and descriptions it gives them, and puts each on the right tab.
//
// The "menu" and "effect" here only beep: the point is the config file and
// the export. Build (LLVM-MinGW or MSVC), from the kit's folder:
//
//   x86_64-w64-mingw32-clang++ -shared -O2 -std=c++17 -I include ^
//       -o ExampleMod.asi examples/ExampleMod/ExampleMod.cpp -luser32
//
// MIT-0: copy anything from here into your own mod, no attribution needed.
#include <Windows.h>

#include <string>

#include "ModActivatorBindings.h"

namespace {

// What ModActivator shows for the two shortcuts. Static storage: the strings
// must stay valid while the game runs.
const ModActivatorBindingV1 bindings[] = {
    {"ExampleMod.ini", "Menu", "MenuKey", "Example window",
     "Opens or closes the Example Mod window.", ModActivatorKindMenu},
    {"ExampleMod.ini", "Effect", "ToggleKey", "Example effect",
     "Turns the example effect on or off.", ModActivatorKindToggle},
};

std::string configPath(HMODULE self) {
  char path[MAX_PATH]{};
  GetModuleFileNameA(self, path, MAX_PATH);
  std::string p = path;
  return p.substr(0, p.find_last_of("\\/") + 1) + "ExampleMod.ini";
}

// A small reader for the values above: Ctrl/Shift/Alt plus one main key, an
// F key or a letter. A real mod would accept more; see the guide for the
// spellings ModActivator itself reads.
struct Shortcut {
  bool ctrl = false, shift = false, alt = false;
  int key = 0;
};
Shortcut parse(std::string text) {
  Shortcut s;
  for (auto &c : text)
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('+', start);
    const auto word = text.substr(start, end == text.npos ? text.npos : end - start);
    if (word == "CTRL") s.ctrl = true;
    else if (word == "SHIFT") s.shift = true;
    else if (word == "ALT") s.alt = true;
    else if (word.size() > 1 && word[0] == 'F') s.key = VK_F1 + atoi(word.c_str() + 1) - 1;
    else if (word.size() == 1) s.key = word[0];
    if (end == text.npos) break;
    start = end + 1;
  }
  return s;
}
Shortcut read(const std::string &config, const char *section, const char *key, const char *fallback) {
  char value[64]{};
  GetPrivateProfileStringA(section, key, fallback, value, sizeof value, config.c_str());
  return parse(value);
}

bool down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
// Exactly these modifiers, so Ctrl+F7 and F7 are different shortcuts.
bool held(const Shortcut &s) {
  return s.key && down(s.key) && down(VK_CONTROL) == s.ctrl && down(VK_SHIFT) == s.shift &&
         down(VK_MENU) == s.alt;
}

DWORD WINAPI run(void *self) {
  const auto config = configPath(static_cast<HMODULE>(self));
  const auto menu = read(config, "Menu", "MenuKey", "Ctrl+F7");
  const auto toggle = read(config, "Effect", "ToggleKey", "F8");
  bool menuWas = false, toggleWas = false, effect = false;
  for (;;) {
    // GetAsyncKeyState sees the keys ModActivator sends, as well as real ones.
    const bool menuNow = held(menu), toggleNow = held(toggle);
    if (menuNow && !menuWas)
      MessageBeep(MB_OK);
    if (toggleNow && !toggleWas) {
      effect = !effect;
      MessageBeep(effect ? MB_ICONASTERISK : MB_ICONHAND);
    }
    menuWas = menuNow;
    toggleWas = toggleNow;
    Sleep(15);
  }
}

} // namespace

extern "C" __declspec(dllexport) const ModActivatorBindingV1 *ModActivatorBindingsV1(unsigned *count) {
  *count = sizeof bindings / sizeof bindings[0];
  return bindings;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD why, LPVOID) {
  if (why == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    // The example's own key loop. Only in the game, never in a tool that
    // loads the DLL to look at it (such as ModActivator's tests).
    char exe[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char *name = strrchr(exe, '\\');
    if (!_stricmp(name ? name + 1 : exe, "CrimsonDesert.exe"))
      if (HANDLE thread = CreateThread(nullptr, 0, run, self, 0, nullptr))
        CloseHandle(thread);
  }
  return TRUE;
}
