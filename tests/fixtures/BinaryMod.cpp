// A test mod that ModActivator never loads: tests/test_descriptions.py copies
// it beside configs and checks what ModActivator reads from the file itself.
// Built twice by scripts/build.py: BinaryMod.asi (version resource, a config
// template, a settings schema, a sentence) and, with BINARY_TOGGLE,
// BinaryToggle.asi (one key read by a poller that reports enabled/disabled).
#include <windows.h>

#ifndef BINARY_TOGGLE
// The default config the mod writes when none exists.
__attribute__((used)) static const char configTemplate[] =
    "[General]\n"
    "; Opens the template window.\n"
    "TemplateKey=F6\n"
    "; Not a key at all.\n"
    "Volume=5\n";
// A settings schema, as a settings menu would read it.
__attribute__((used)) static const char settingsSchema[] =
    "{\"sections\":{\"General\":{\"keys\":{\"PanelKey\":{\"label\":{\"de\":\"Taste\","
    "\"en\":\"Key to open or close the binary panel\"},\"help\":{\"en\":\"Shows every "
    "fixture setting at once.\"}}}}}}";
// A log line that names a key, and a complaint about it that says nothing.
__attribute__((used)) static const char logLine[] = "Snapshot saved to the log folder (PhotoKey). Done.";
__attribute__((used)) static const char complaint[] = "Invalid PhotoKey value, using F8.";
#else
static volatile LONG enabled;

__attribute__((noinline)) static void report(bool on) {
  OutputDebugStringA(on ? "Fixture helper enabled" : "Fixture helper disabled");
}

static DWORD WINAPI poll(void *) {
  for (bool down = false;; Sleep(16)) {
    const bool now = GetAsyncKeyState(VK_F6) & 0x8000;
    if (now && !down)
      report(InterlockedXor(&enabled, 1) == 0);
    down = now;
  }
}
#endif

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, void *) {
#ifdef BINARY_TOGGLE
  if (reason == DLL_PROCESS_ATTACH)
    CloseHandle(CreateThread(nullptr, 0, poll, nullptr, 0, nullptr));
#else
  (void)reason;
#endif
  return TRUE;
}
