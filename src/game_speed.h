#pragma once
// Slowing or freezing the game while the radial menu is open.
//
// The engine's per-frame timing update ends in a fixed-timestep override the
// game uses for its own video capture: while a flag byte is 1, the whole
// simulation (animation, physics, AI, timers) advances by a set number of
// seconds each frame instead of the real frame time. Setting that step to
// almost nothing freezes the world while it keeps rendering, so the ring
// still draws and aims; a fraction of the real frame time is slow motion.
//
// Found as Trinity by XeTrinityz (MIT, see THIRD_PARTY_NOTICES.md) finds it:
// by the bytes of that block in the running game, whose two RIP-relative
// operands are the flag and the step. Game updates can move or change that
// code. When the bytes are not found, the option says it is unavailable and
// the ring works without it. Searched for once, in the background, the first
// time the option is on.
namespace gameSpeed {

// The override block: cmp byte [flag], 1 ... vmovss xmm0, [step].
static constexpr const char *signature =
    "80 3D ?? ?? ?? ?? 01 75 30 48 8B 4F 58 41 8B C7 C5 78 2F 59 64 0F 97 C0 "
    "85 C0 74 09 80 3D ?? ?? ?? ?? 01 75 14 C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 "
    "41 64 C6 05 ?? ?? ?? ?? 00";
static constexpr size_t flagDisp = 2, flagEnd = 7;   // cmp cs:[flag], 1 (7 bytes)
static constexpr size_t stepInstr = 37, stepLen = 8; // vmovss xmm0, cs:[step]

// A signature as bytes, -1 for each ?? wildcard.
static std::vector<int> parseSignature(const char *text) {
  std::vector<int> out;
  std::istringstream words(text);
  for (std::string word; words >> word;)
    out.push_back(word == "??" ? -1 : (int)strtoul(word.c_str(), nullptr, 16));
  return out;
}
// The first match in [data, data + size), and how many there are (up to 2).
static const uint8_t *findSignature(const uint8_t *data, size_t size, const std::vector<int> &pattern,
                                    int *matches = nullptr) {
  const uint8_t *first = nullptr;
  int count = 0;
  if (pattern.empty() || pattern[0] < 0 || size < pattern.size()) {
    if (matches)
      *matches = 0;
    return nullptr;
  }
  const uint8_t lead = (uint8_t)pattern[0];
  const uint8_t *end = data + size - pattern.size() + 1;
  for (const uint8_t *p = data; p < end && count < 2; ++p) {
    p = static_cast<const uint8_t *>(memchr(p, lead, end - p));
    if (!p)
      break;
    size_t i = 1;
    while (i < pattern.size() && (pattern[i] < 0 || p[i] == (uint8_t)pattern[i]))
      ++i;
    if (i == pattern.size()) {
      if (!first)
        first = p;
      ++count;
    }
  }
  if (matches)
    *matches = count;
  return first;
}
static uint8_t *ripTarget(const uint8_t *instrEnd, const uint8_t *disp) {
  int32_t offset;
  memcpy(&offset, disp, sizeof offset);
  return const_cast<uint8_t *>(instrEnd) + offset;
}

enum Status { notSearched, searching, found, notFound };
static std::atomic<int> status{notSearched};
static uint8_t *flag = nullptr; // set before status becomes found
static float *step = nullptr;

static bool writable(const void *address, size_t size) {
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(address, &info, sizeof info) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
    return false;
  const DWORD rw = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  return (info.Protect & rw) &&
         reinterpret_cast<uintptr_t>(address) + size <=
             reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
}

static DWORD WINAPI search(void *) {
  const auto pattern = parseSignature(signature);
  const auto base = reinterpret_cast<const uint8_t *>(GetModuleHandleA(nullptr));
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  const uint8_t *imageEnd = base + nt->OptionalHeader.SizeOfImage;
  const uint8_t *match = nullptr;
  int matches = 0;
  // Every committed, readable, executable stretch of the game's image.
  for (const uint8_t *at = base; at < imageEnd && matches < 2;) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(at, &info, sizeof info))
      break;
    const uint8_t *regionEnd = static_cast<const uint8_t *>(info.BaseAddress) + info.RegionSize;
    const DWORD exec = PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    if (info.State == MEM_COMMIT && (info.Protect & exec) && !(info.Protect & PAGE_GUARD)) {
      const uint8_t *from = (std::max)(at, static_cast<const uint8_t *>(info.BaseAddress));
      const uint8_t *to = (std::min)(regionEnd, imageEnd);
      int here = 0;
      if (const auto *hit = findSignature(from, to - from, pattern, &here)) {
        if (!match)
          match = hit;
        matches += here;
      }
    }
    at = regionEnd;
  }
  if (match) {
    auto *f = ripTarget(match + flagEnd, match + flagDisp);
    auto *s = reinterpret_cast<float *>(ripTarget(match + stepInstr + stepLen, match + stepInstr + 4));
    if (f >= base && f < imageEnd && writable(f, 1) && writable(s, sizeof(float))) {
      flag = f;
      step = s;
      status.store(found);
      LOG("[radial] game speed found at %p (flag %p, step %p)%s: the game can be slowed or frozen "
          "while the radial menu is open.",
          static_cast<const void *>(match), static_cast<void *>(f), static_cast<void *>(s),
          matches > 1 ? ", the first of several matches" : "");
      return 0;
    }
  }
  status.store(notFound);
  LOG_ERR("[radial] the game's speed control was not found in this game version, so the radial "
          "menu cannot slow or freeze the game. It works without that.");
  return 0;
}
// Starts the search once, in the background: the game image is large.
static void prepare() {
  int expected = notSearched;
  if (status.compare_exchange_strong(expected, searching)) {
    if (HANDLE h = CreateThread(nullptr, 0, search, nullptr, 0, nullptr))
      CloseHandle(h);
    else
      status.store(notFound);
  }
}

// While the ring is open. What was there before is put back on close, so
// the game's own capture, or a mod using the same switch, gets its state
// back; if it was already on, it is someone else's and left alone.
static bool active = false;
static uint8_t savedFlag = 0;
static float savedStep = 0;
static int activeMode = 0;
static constexpr float slowFactor = 0.25f, frozenStep = 1.0e-5f;

static void apply(float realDelta) {
  const float value = activeMode == 2 ? frozenStep
                                      : (std::min)(0.1f, (std::max)(frozenStep, realDelta * slowFactor));
  *reinterpret_cast<volatile float *>(step) = value;
}
static void begin(int mode, float realDelta) {
  if (active || !mode || status.load() != found)
    return;
  const uint8_t now = *reinterpret_cast<volatile uint8_t *>(flag);
  if (now == 1) {
    LOG("[radial] the game's speed is already being set by the game or another mod, so the radial "
        "menu leaves it alone.");
    return;
  }
  savedFlag = now;
  savedStep = *reinterpret_cast<volatile float *>(step);
  activeMode = mode;
  active = true;
  apply(realDelta);                                 // the step first, then the switch,
  *reinterpret_cast<volatile uint8_t *>(flag) = 1;  // so no frame takes an old step
}
static void tick(float realDelta) {
  if (active && activeMode == 1)
    apply(realDelta);
}
static void end() {
  if (!active)
    return;
  *reinterpret_cast<volatile uint8_t *>(flag) = savedFlag;
  *reinterpret_cast<volatile float *>(step) = savedStep;
  active = false;
}
static const char *statusText() {
  switch (status.load()) {
  case found: return "available";
  case notFound: return "not found in this game version";
  case searching: return "looking for it...";
  default: return "checked when first switched on";
  }
}

} // namespace gameSpeed
