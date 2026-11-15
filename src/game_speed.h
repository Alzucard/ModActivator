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

// The override block:
//   cmp byte [flag], 1 ; jne past it            the switch
//   ... vcomiss xmm11, [rcx+delta] ; seta ; je  an applied delta already short
//   cmp byte [once], 1 ; jne past it            enough is kept, unless [once]
//   vmovss xmm0, [step] ; vmovss [rcx+delta], xmm0 ; mov byte [once], 0
// Trinity's bytes, with the struct offsets, a register and the short jumps
// left open: the November 2026 game moved the delta from +0x64 to +0x6C and
// its object from +0x58 to +0x68, which Trinity's exact bytes no longer find.
static constexpr const char *signature =
    "80 3D ?? ?? ?? ?? 01 75 ?? 48 8B 4F ?? 41 8B ?? C5 78 2F 59 ?? 0F 97 C0 "
    "85 C0 74 ?? 80 3D ?? ?? ?? ?? 01 75 ?? C5 FA 10 05 ?? ?? ?? ?? C5 FA 11 "
    "41 ?? C6 05 ?? ?? ?? ?? 00";
static constexpr size_t flagDisp = 2, flagEnd = 7;   // cmp cs:[flag], 1 (7 bytes)
static constexpr size_t onceDisp = 30, onceEnd = 35; // cmp cs:[once], 1 (7 bytes)
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
static uint8_t *once = nullptr;
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

// --- our own step --------------------------------------------------------------
// Since the November 2026 update the step instruction reads a read-only
// constant (1/60, shared with whatever else uses that number) instead of a
// variable. Writing it would change all of those, so the instruction is
// pointed at a float of ours instead: allocated within reach of its 32-bit
// displacement, holding the constant's value, so the game behaves exactly as
// before until the radial menu changes it.
static float *ownStep = nullptr;

// A page within 2 GB of `code`, below the game's image first, as MinHook
// allocates its trampolines.
static void *allocateNear(const uint8_t *code) {
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  const uintptr_t granularity = system.dwAllocationGranularity;
  const uintptr_t centre = reinterpret_cast<uintptr_t>(code);
  const uintptr_t reach = 0x7FF00000;
  const uintptr_t low = centre > reach ? centre - reach : granularity;
  for (uintptr_t at = (centre - granularity) & ~(granularity - 1); at > low; at -= granularity) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void *>(at), &info, sizeof info))
      break;
    if (info.State != MEM_FREE)
      continue;
    if (void *page = VirtualAlloc(reinterpret_cast<void *>(at), system.dwPageSize, MEM_RESERVE | MEM_COMMIT,
                                  PAGE_READWRITE))
      return page;
  }
  return nullptr;
}

// Replaces the 4-byte displacement at `at` with one atomic write, so a thread
// running the instruction meanwhile sees the old one or the new one, never a
// mix: the bytes must lie inside one aligned 8-byte word.
static bool patchDisplacement(uint8_t *at, int32_t value) {
  const uintptr_t address = reinterpret_cast<uintptr_t>(at);
  const uintptr_t word = address & ~uintptr_t(7);
  if (address + 4 > word + 8)
    return false;
  DWORD old = 0;
  if (!VirtualProtect(reinterpret_cast<void *>(word), 8, PAGE_EXECUTE_READWRITE, &old))
    return false;
  auto *target = reinterpret_cast<volatile LONG64 *>(word);
  LONG64 before = *target;
  for (;;) {
    LONG64 after = before;
    memcpy(reinterpret_cast<uint8_t *>(&after) + (address - word), &value, 4);
    const LONG64 seen = InterlockedCompareExchange64(target, after, before);
    if (seen == before)
      break;
    before = seen;
  }
  DWORD ignored = 0;
  VirtualProtect(reinterpret_cast<void *>(word), 8, old, &ignored);
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(word), 8);
  return true;
}

// Points the step instruction (at `instr`, 8 bytes, displacement in its last
// 4) at our own float. Null when it cannot.
static const char *redirectFailure = "";
static float *redirectStep(uint8_t *instr, const float *constant) {
  auto *own = static_cast<float *>(allocateNear(instr));
  if (!own) {
    redirectFailure = "no free memory was left within reach of it (other mods' hooks take that room too)";
    return nullptr;
  }
  *own = *constant;
  const intptr_t distance = reinterpret_cast<intptr_t>(own) - reinterpret_cast<intptr_t>(instr + stepLen);
  if (distance < INT32_MIN || distance > INT32_MAX || !patchDisplacement(instr + 4, static_cast<int32_t>(distance))) {
    redirectFailure = "the game's code there could not be changed";
    VirtualFree(own, 0, MEM_RELEASE);
    return nullptr;
  }
  return own;
}

static bool readable(DWORD protect) {
  if ((protect & PAGE_GUARD) || protect == PAGE_NOACCESS || protect == PAGE_EXECUTE)
    return false;
  return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                     PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
// The committed, readable stretches of the game's image, neighbours joined so
// a match across two regions is not missed. Not only the executable ones: a
// protected game's code is not always marked executable where it is read.
static std::vector<std::pair<const uint8_t *, const uint8_t *>> readableSpans(const uint8_t *base,
                                                                             const uint8_t *imageEnd) {
  std::vector<std::pair<const uint8_t *, const uint8_t *>> spans;
  for (const uint8_t *at = base; at < imageEnd;) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(at, &info, sizeof info) || !info.RegionSize)
      break;
    const auto *from = static_cast<const uint8_t *>(info.BaseAddress);
    const uint8_t *to = (std::min)(from + info.RegionSize, imageEnd);
    if (info.State == MEM_COMMIT && readable(info.Protect)) {
      if (!spans.empty() && spans.back().second == from)
        spans.back().second = to;
      else
        spans.emplace_back(from, to);
    }
    at = from + info.RegionSize;
  }
  return spans;
}

static DWORD WINAPI search(void *) {
  const auto pattern = parseSignature(signature);
  const auto base = reinterpret_cast<const uint8_t *>(GetModuleHandleA(nullptr));
  const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
  const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
  const uint8_t *imageEnd = base + nt->OptionalHeader.SizeOfImage;
  const uint8_t *match = nullptr;
  int matches = 0;
  size_t scanned = 0;
  const auto spans = readableSpans(base, imageEnd);
  for (const auto &[from, to] : spans) {
    scanned += to - from;
    int here = 0;
    if (const auto *hit = findSignature(from, to - from, pattern, &here)) {
      if (!match)
        match = hit;
      matches += here;
      if (matches >= 2)
        break;
    }
  }
  if (match) {
    auto *f = ripTarget(match + flagEnd, match + flagDisp);
    auto *o = ripTarget(match + onceEnd, match + onceDisp);
    auto *s = reinterpret_cast<float *>(ripTarget(match + stepInstr + stepLen, match + stepInstr + 4));
    const bool flagsWritable = f >= base && f < imageEnd && writable(f, 1) && writable(o, 1);
    // A step that is a variable (the game Trinity was written for) is used
    // as it is; a constant gets our own float in its place.
    const bool stepWritable = writable(s, sizeof(float));
    float *own = flagsWritable && !stepWritable
                     ? redirectStep(const_cast<uint8_t *>(match) + stepInstr, s)
                     : nullptr;
    if (flagsWritable && (stepWritable || own)) {
      flag = f;
      once = o;
      step = stepWritable ? s : own;
      ownStep = own;
      status.store(found);
      LOG("[radial] game speed found at %p (flag %p, step %p%s)%s: the game can be slowed or frozen "
          "while the radial menu is open.",
          static_cast<const void *>(match), static_cast<void *>(f), static_cast<void *>(step),
          own ? ", our own in place of the game's constant" : "",
          matches > 1 ? ", the first of several matches" : "");
      return 0;
    }
    status.store(notFound);
    LOG_ERR("[radial] the game's speed control was found at %p, but %s%s, so the radial menu cannot slow or "
            "freeze the game.",
            static_cast<const void *>(match),
            !flagsWritable ? "its switches cannot be written"
                           : "its step is a constant and the instruction reading it could not be pointed elsewhere: ",
            flagsWritable ? redirectFailure : "");
    return 0;
  }
  status.store(notFound);
  LOG_ERR("[radial] the game's speed control was not found in this game version (%zu MB in %zu "
          "readable parts of the game searched), so the radial menu cannot slow or freeze the game. "
          "It works without that.",
          scanned >> 20, spans.size());
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
static uint8_t savedFlag = 0, savedOnce = 0;
static float savedStep = 0;
static int activeMode = 0;
static constexpr float slowFactor = 0.25f, frozenStep = 1.0e-5f;

// The step, then both switches, every frame: the game clears [once] each
// time it applies the step, and without it a frame whose own delta is
// already short is left alone. Re-arming the switch too, as Trinity does,
// takes it back if the game's capture code cleared it.
static void apply(float realDelta) {
  const float value = activeMode == 2 ? frozenStep
                                      : (std::min)(0.1f, (std::max)(frozenStep, realDelta * slowFactor));
  *reinterpret_cast<volatile float *>(step) = value;
  *reinterpret_cast<volatile uint8_t *>(once) = 1;
  *reinterpret_cast<volatile uint8_t *>(flag) = 1;
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
  savedOnce = *reinterpret_cast<volatile uint8_t *>(once);
  savedStep = *reinterpret_cast<volatile float *>(step);
  activeMode = mode;
  active = true;
  apply(realDelta); // the step before the switches, so no frame takes an old step
}
static void tick(float realDelta) {
  if (active)
    apply(realDelta);
}
static void end() {
  if (!active)
    return;
  *reinterpret_cast<volatile uint8_t *>(flag) = savedFlag;
  *reinterpret_cast<volatile uint8_t *>(once) = savedOnce;
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
