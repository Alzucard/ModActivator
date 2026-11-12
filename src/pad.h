#pragma once
// Controller buttons, as mods write them. A combination is a set of buttons,
// kept as XInput's wButtons bits with the two triggers above them.
//
// Mods spell the same buttons many ways: XInput names (A, LB, Back), the
// PlayStation names (Cross, L1, Share), stick clicks (LS, L3, LeftStick),
// d-pad names (DPadDown, dpad_down, or Down in a controller setting), joined
// with + or , and in any case. Some write the XInput bits as a number
// instead: 0040 (hex, four digits) or 32768 (decimal).
#include <Xinput.h>

static constexpr unsigned padLeftTrigger = 1u << 16, padRightTrigger = 1u << 17;
static constexpr unsigned padTriggerThreshold = 64; // of 255: a quarter pull, firmer than XInput's 30

struct PadName {
  const char *name;
  unsigned bits;
};
static constexpr PadName padNames[] = {
    {"a", XINPUT_GAMEPAD_A}, {"cross", XINPUT_GAMEPAD_A},
    {"b", XINPUT_GAMEPAD_B}, {"circle", XINPUT_GAMEPAD_B},
    {"x", XINPUT_GAMEPAD_X}, {"square", XINPUT_GAMEPAD_X},
    {"y", XINPUT_GAMEPAD_Y}, {"triangle", XINPUT_GAMEPAD_Y},
    {"lb", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"l1", XINPUT_GAMEPAD_LEFT_SHOULDER},
    {"leftshoulder", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"leftbumper", XINPUT_GAMEPAD_LEFT_SHOULDER},
    {"rb", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"r1", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {"rightshoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"rightbumper", XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {"lt", padLeftTrigger}, {"l2", padLeftTrigger}, {"lefttrigger", padLeftTrigger},
    {"rt", padRightTrigger}, {"r2", padRightTrigger}, {"righttrigger", padRightTrigger},
    {"ls", XINPUT_GAMEPAD_LEFT_THUMB}, {"l3", XINPUT_GAMEPAD_LEFT_THUMB},
    {"leftstick", XINPUT_GAMEPAD_LEFT_THUMB}, {"leftthumb", XINPUT_GAMEPAD_LEFT_THUMB},
    {"rs", XINPUT_GAMEPAD_RIGHT_THUMB}, {"r3", XINPUT_GAMEPAD_RIGHT_THUMB},
    {"rightstick", XINPUT_GAMEPAD_RIGHT_THUMB}, {"rightthumb", XINPUT_GAMEPAD_RIGHT_THUMB},
    {"back", XINPUT_GAMEPAD_BACK}, {"view", XINPUT_GAMEPAD_BACK}, {"select", XINPUT_GAMEPAD_BACK},
    {"share", XINPUT_GAMEPAD_BACK},
    {"start", XINPUT_GAMEPAD_START}, {"menu", XINPUT_GAMEPAD_START}, {"options", XINPUT_GAMEPAD_START},
    {"up", XINPUT_GAMEPAD_DPAD_UP}, {"dpadup", XINPUT_GAMEPAD_DPAD_UP},
    {"down", XINPUT_GAMEPAD_DPAD_DOWN}, {"dpaddown", XINPUT_GAMEPAD_DPAD_DOWN},
    {"left", XINPUT_GAMEPAD_DPAD_LEFT}, {"dpadleft", XINPUT_GAMEPAD_DPAD_LEFT},
    {"right", XINPUT_GAMEPAD_DPAD_RIGHT}, {"dpadright", XINPUT_GAMEPAD_DPAD_RIGHT},
};

// The buttons a setting's value names, or 0 for none, off or anything not
// understood (a combination with one unknown word is not half-read).
static unsigned padButtonsFrom(std::string value) {
  std::string folded;
  for (char c : value)
    if (c != ' ' && c != '\t' && c != '_' && c != '-')
      folded += (char)tolower((unsigned char)c);
  if (folded.empty() || folded == "none" || folded == "0" || folded == "off")
    return 0;
  // The XInput bits as a number: 0x0040, 0040 (four hex digits), 32768.
  const bool hex4 = folded.size() == 4 &&
                    folded.find_first_not_of("0123456789abcdef") == folded.npos;
  // A lone digit is not one: it is a mod's on/off flag (PadOpen=1) far more
  // often than a d-pad bit.
  if (folded.compare(0, 2, "0x") == 0 || hex4 ||
      (folded.size() >= 2 && folded.find_first_not_of("0123456789") == folded.npos)) {
    char *end = nullptr;
    const auto bits = strtoul(folded.c_str(), &end, hex4 || folded[1] == 'x' ? 16 : 10);
    return end && !*end && bits <= 0xFFFF ? (unsigned)bits & 0xF3FF : 0;
  }
  unsigned bits = 0;
  size_t start = 0;
  while (start <= folded.size()) {
    const auto end = folded.find_first_of("+,", start);
    const auto word = folded.substr(start, end == folded.npos ? folded.npos : end - start);
    if (word.empty())
      return 0;
    const auto *found = std::find_if(std::begin(padNames), std::end(padNames),
                                     [&](const PadName &n) { return word == n.name; });
    if (found == std::end(padNames))
      return 0;
    bits |= found->bits;
    if (end == folded.npos)
      break;
    start = end + 1;
  }
  return bits;
}

// How the menu shows a combination, in the order the buttons are usually held.
static std::string padLabel(unsigned bits) {
  static constexpr PadName order[] = {
      {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER},
      {"LT", padLeftTrigger}, {"RT", padRightTrigger},
      {"Back", XINPUT_GAMEPAD_BACK}, {"Start", XINPUT_GAMEPAD_START},
      {"LS", XINPUT_GAMEPAD_LEFT_THUMB}, {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
      {"Up", XINPUT_GAMEPAD_DPAD_UP}, {"Down", XINPUT_GAMEPAD_DPAD_DOWN},
      {"Left", XINPUT_GAMEPAD_DPAD_LEFT}, {"Right", XINPUT_GAMEPAD_DPAD_RIGHT},
      {"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X},
      {"Y", XINPUT_GAMEPAD_Y}};
  std::string out;
  for (const auto &button : order)
    if (bits & button.bits) {
      if (!out.empty())
        out += " + ";
      out += button.name;
    }
  return out;
}

// The buttons held in a controller state, triggers included.
static unsigned padButtonsHeld(const XINPUT_GAMEPAD &pad) {
  return pad.wButtons | (pad.bLeftTrigger >= padTriggerThreshold ? padLeftTrigger : 0) |
         (pad.bRightTrigger >= padTriggerThreshold ? padRightTrigger : 0);
}

// Two combinations clash when they are the same (2), or when holding one
// presses the other as well (1): LB + A also presses A.
static int padConflict(unsigned a, unsigned b) {
  if (!a || !b)
    return 0;
  if (a == b)
    return 2;
  return (a & b) == a || (a & b) == b ? 1 : 0;
}
