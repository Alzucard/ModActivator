#pragma once

static HKL gameKeyboardLayout() {
  return GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), nullptr));
}

// The printable block uses US key names as its portable config spelling.
struct KeyRow {
  unsigned firstScan;
  const char *symbols;
};
static constexpr KeyRow keyRows[] = {
    {0x02, "1234567890-="},
    {0x10, "QWERTYUIOP[]"},
    {0x1E, "ASDFGHJKL;'"},
    {0x29, "\x60"},
    {0x2B, "\\ZXCVBNM,./"},
};
static unsigned scanForUsSymbol(char symbol) {
  constexpr char shifted[] = "!@#$%^&*()_+{}:\"~|<>?";
  constexpr char unshifted[] = "1234567890-=[];'\x60\\,./";
  if (const auto *at = strchr(shifted, symbol); symbol && at)
    symbol = unshifted[at - shifted];
  for (const auto &row : keyRows)
    if (const auto *at = strchr(row.symbols, symbol); symbol && at)
      return row.firstScan + (unsigned)(at - row.symbols);
  return 0;
}
static char usSymbolForScan(unsigned scan) {
  for (const auto &row : keyRows)
    if (scan >= row.firstScan && scan - row.firstScan < strlen(row.symbols))
      return row.symbols[scan - row.firstScan];
  return 0;
}
static int vkForScan(unsigned scan, HKL layout) {
  return (int)MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
}
static unsigned scanForVk(int key, HKL layout) {
  return MapVirtualKeyExW(key, MAPVK_VK_TO_VSC_EX, layout);
}
static bool isPrintableKey(int key) {
  return (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
         (key >= VK_OEM_1 && key <= VK_OEM_3) ||
         (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102;
}
static std::string localizedKey(int key, HKL layout = gameKeyboardLayout()) {
  BYTE keyboard[256]{};
  wchar_t wide[8]{};
  // Flag 4 keeps the call from changing the game's dead-key state.
  int count = ToUnicodeEx(key, scanForVk(key, layout), keyboard, wide,
                          IM_ARRAYSIZE(wide), 4, layout);
  if (count < 0)
    count = 1;
  if (!count || count > IM_ARRAYSIZE(wide) || wide[0] < 32)
    return {};
  CharUpperBuffW(wide, count);
  char utf8[sizeof wide * 2]{};
  const int n = WideCharToMultiByte(CP_UTF8, 0, wide, count, utf8, sizeof utf8,
                                   nullptr, nullptr);
  return std::string(utf8, n);
}

static wchar_t keySymbol(const std::string &token) {
  wchar_t wide[2]{};
  auto decode = [&](UINT page) {
    return MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, token.data(),
                               (int)token.size(), wide, 2);
  };
  int count = decode(CP_UTF8);
  if (!count)
    count = decode(CP_ACP);
  return count == 1 ? wide[0] : 0;
}
static unsigned scanForSymbol(wchar_t symbol, HKL layout) {
  const SHORT key = VkKeyScanExW(symbol, layout);
  // Ctrl/Alt/AltGr combinations can type a glyph on an unrelated key.
  return key == -1 || HIBYTE(key) > 1 ? 0 : scanForVk(LOBYTE(key), layout);
}
static unsigned scanForLocalizedSymbol(const std::string &token, HKL layout) {
  const auto symbol = keySymbol(token);
  if (symbol < 128)
    return 0;

  // These aliases have a dedicated position on the standard German and Spanish layouts.
  switch (symbol) {
  case L'\u00d6': case L'\u00f6': return 0x27; // O umlaut
  case L'\u00dc': case L'\u00fc': return 0x1A; // U umlaut
  case L'\u00c4': case L'\u00e4': return 0x28; // A umlaut
  case L'\u00df': case L'\u1e9e': return 0x0C; // Eszett
  case L'\u00d1': case L'\u00f1': return 0x27; // N with tilde
  }
  if (const unsigned scan = scanForSymbol(symbol, layout))
    return scan;

  const int size = GetKeyboardLayoutList(0, nullptr);
  std::vector<HKL> layouts(size);
  const int loaded = GetKeyboardLayoutList(size, layouts.data());
  unsigned match = 0;
  for (int i = 0; i < loaded; ++i) {
    const unsigned candidate = scanForSymbol(symbol, layouts[i]);
    if (!candidate)
      continue;
    if (match && match != candidate)
      return 0;
    match = candidate;
  }
  return match;
}
