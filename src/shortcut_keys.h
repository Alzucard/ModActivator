#pragma once

// Symbolic OEM bindings in mod INIs use US key positions.
struct PunctuationKey {
  char symbol;
  unsigned scan;
};
static constexpr PunctuationKey punctuation[] = {
    {'-', 0x0C}, {'=', 0x0D}, {'[', 0x1A}, {']', 0x1B},
    {';', 0x27}, {'\'', 0x28}, {0x60, 0x29}, {'\\', 0x2B},
    {',', 0x33}, {'.', 0x34}, {'/', 0x35},
};
static HKL gameKeyboardLayout() {
  return GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
}
static int punctuationVk(unsigned scan, HKL layout = gameKeyboardLayout()) {
  return (int)MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
}
static bool isOemKey(int key) {
  return (key >= VK_OEM_1 && key <= VK_OEM_3) ||
         (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102;
}
static std::string localizedKey(int key, HKL layout = gameKeyboardLayout()) {
  auto character = MapVirtualKeyExW(key, MAPVK_VK_TO_CHAR, layout) & 0xffffu;
  if (character < 32)
    return {};
  wchar_t wide = (wchar_t)character;
  char utf8[4]{};
  const int n = WideCharToMultiByte(CP_UTF8, 0, &wide, 1, utf8, sizeof utf8,
                                   nullptr, nullptr);
  return std::string(utf8, n);
}
