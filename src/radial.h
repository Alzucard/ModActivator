// The radial menu: hold its key (or controller combination), aim at one of up
// to eight pinned cards with the mouse or the right stick, and let go to open
// it. Letting go with nothing chosen, or pressing Escape, closes it without
// doing anything.
//
// The mouse aims through the same virtual cursor as the panel (see
// input.h), so no cursor appears: the direction it has moved from the
// centre of the screen is the choice. While the ring is open it captures
// input like the panel does, so the camera stays still, and the game can be
// slowed or frozen (game_speed.h).
//
// Part of activator.cpp, included inside namespace ml::gui after
// menuSnapshot. Render thread only.

struct RadialEntry {
  Action action;
  bool usable = false; // loaded, not managed by ModHub, not waiting for a restart
  bool several = false;
};
static struct RadialState {
  bool open = false;
  bool viaPad = false;
  bool stickUsed = false;
  DWORD openedAt = 0, closedAt = 0;
  LARGE_INTEGER lastTick{};
  int selected = -1;
  float lastX = 0, lastY = 0;
  std::vector<RadialEntry> entries;
} radial;

static constexpr DWORD radialFadeMs = 120;

// The wedge a direction points at: wedge 0 is centred on straight up, and
// they go round clockwise. dx right, dy down.
static int radialWedge(float dx, float dy, int count) {
  if (count <= 0)
    return -1;
  constexpr float twoPi = 6.28318530718f;
  float angle = atan2f(dx, -dy);
  if (angle < 0)
    angle += twoPi;
  const float step = twoPi / count;
  return int((angle + step / 2) / step) % count;
}

static bool radialVisible() {
  return radial.open || (radial.closedAt && GetTickCount() - radial.closedAt < radialFadeMs);
}

// The hold key or the controller combination, all of it held now.
static bool radialHeld(bool *viaPad) {
  const unsigned code = radialKeyCode();
  const bool key = code && held(code & 0xff) && heldModifiers() == (code & ~0xffu) &&
                   !held(VK_LWIN) && !held(VK_RWIN);
  const unsigned pad = padRadial.load();
  const bool padHeld = pad && framePadConnected && (padButtonsHeld(framePad) & pad) == pad;
  *viaPad = padHeld && !key;
  return key || padHeld;
}

static float radialFrameSeconds() {
  LARGE_INTEGER now, frequency;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  const float seconds = radial.lastTick.QuadPart
                            ? float(now.QuadPart - radial.lastTick.QuadPart) / float(frequency.QuadPart)
                            : 1.0f / 60;
  radial.lastTick = now;
  return (std::min)(0.25f, seconds);
}

// The pinned cards, in the order you pinned them; cards that are gone (their
// mod is not loaded this time) are left out.
static void fillRadialEntries() {
  const auto &snap = menuSnapshot();
  radial.entries.clear();
  for (const auto &id : radialSlots)
    for (size_t i = 0; i < snap.actions.size(); ++i)
      if (cachedIdentity(snap.actions[i]) == id) {
        const auto &card = snap.cards[i];
        radial.entries.push_back(
            {snap.actions[i], card.loaded && !card.managed && !card.restart, card.several});
        break;
      }
}

static void openRadial(bool viaPad) {
  fillRadialEntries();
  radial.open = true;
  radial.viaPad = viaPad;
  radial.stickUsed = false;
  radial.selected = -1;
  radial.openedAt = GetTickCount();
  radial.closedAt = 0;
  radial.lastTick = {};
  input::CentreCursor();
  float w, h;
  input::VirtualCursor(&radial.lastX, &radial.lastY, &w, &h);
  State::Get().radialOpen = true;
  gameSpeed::begin(radialPause.load(), radialFrameSeconds());
  LOG("[radial] opened by %s, %zu of %zu pinned entries here.",
      viaPad ? "the controller" : "the keyboard", radial.entries.size(), radialSlots.size());
}

static void closeRadial(bool choose, const char *cause) {
  gameSpeed::end();
  radial.open = false;
  radial.closedAt = GetTickCount();
  State::Get().radialOpen = false;
  input::MenuClosed();
  const int chosen = radial.selected;
  if (choose && chosen >= 0 && chosen < (int)radial.entries.size()) {
    const auto &entry = radial.entries[chosen];
    if (!entry.usable)
      LOG("[radial] %s cannot be opened now (not loaded, managed by ModHub, or waiting for a restart).",
          UI::name(entry.action).c_str());
    else if (queueAction(entry.action, false))
      LOG("[radial] chose %s / %s.", UI::name(entry.action).c_str(), UI::actionName(entry.action).c_str());
    else
      LOG_ERR("[radial] %s could not be queued.", UI::name(entry.action).c_str());
  } else {
    LOG("[radial] closed by %s without a choice.", cause ? cause : "letting go");
  }
}

// Each frame from PollToggle. foreground: the game window is in front;
// accepts: no shortcut is being recorded and no text typed.
static void pollRadial(bool foreground, bool accepts) {
  static bool was = false;
  // The game's speed control is looked for once the game has been running a
  // while, so the first opening can already slow or freeze it. Not at
  // start: the game may still be unpacking its code then.
  static const DWORD firstPoll = GetTickCount();
  if (radialPause.load() && gameSpeed::status.load() == gameSpeed::notSearched &&
      GetTickCount() - firstPoll > 15000)
    gameSpeed::prepare();
  bool viaPad = false;
  const bool now = radialHeld(&viaPad);
  if (radial.open) {
    if (!foreground)
      closeRadial(false, "the game window losing focus");
    else if (held(VK_ESCAPE))
      closeRadial(false, "Escape");
    else if (!now)
      closeRadial(true, nullptr);
    else {
      const int count = (int)radial.entries.size();
      // The mouse: where the virtual cursor is from the centre, kept within
      // a circle so the direction answers at once however far it went.
      float x, y, w, h;
      input::VirtualCursor(&x, &y, &w, &h);
      const float dx = x - w / 2, dy = y - h / 2, dead = h * 0.035f, reach = h * 0.18f;
      if (x != radial.lastX || y != radial.lastY) {
        const float distance = sqrtf(dx * dx + dy * dy);
        radial.selected = distance > dead ? radialWedge(dx, dy, count) : -1;
        if (distance > reach) // back to the same direction, at the edge of the circle
          input::PlaceCursor(w / 2 + dx / distance * reach, h / 2 + dy / distance * reach);
        input::VirtualCursor(&radial.lastX, &radial.lastY, &w, &h);
      }
      // The right stick: a push past halfway chooses, and the choice stays
      // when the stick springs back, as on a weapon wheel.
      if (framePadConnected) {
        const float rx = framePad.sThumbRX, ry = framePad.sThumbRY;
        if (rx * rx + ry * ry > 16384.0f * 16384.0f) {
          radial.selected = radialWedge(rx, -ry, count);
          radial.stickUsed = true;
        }
      }
      gameSpeed::tick(radialFrameSeconds());
    }
  } else if (now && !was && foreground && accepts && !State::Get().menuOpen) {
    openRadial(viaPad);
  }
  was = now;
}

// Text cut to a width, with an ellipsis when it had to be cut.
static std::string fitText(ImFont *font, float size, std::string text, float width) {
  if (font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str()).x <= width)
    return text;
  while (!text.empty()) {
    do
      text.pop_back();
    while (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80);
    if (!text.empty() && (static_cast<unsigned char>(text.back()) & 0xC0) == 0xC0)
      text.pop_back(); // the lead byte of the character just cut
    const auto cut = text + "...";
    if (font->CalcTextSizeA(size, FLT_MAX, 0, cut.c_str()).x <= width)
      return cut;
  }
  return "...";
}

// Text wrapped to a width over at most maxLines lines, the last one cut with
// an ellipsis when the rest does not fit.
static std::vector<std::string> wrapText(ImFont *font, float size, const std::string &text, float width,
                                         int maxLines) {
  std::vector<std::string> lines;
  const char *p = text.c_str(), *end = p + text.size();
  while (p < end && (int)lines.size() < maxLines) {
    while (p < end && *p == ' ')
      ++p;
    if (p >= end)
      break;
    if ((int)lines.size() + 1 == maxLines) {
      lines.push_back(fitText(font, size, std::string(p, end), width));
      break;
    }
    const char *cut = font->CalcWordWrapPositionA(size / font->FontSize, p, end, width);
    if (cut == p)
      cut = p + 1;
    std::string line(p, cut);
    while (!line.empty() && line.back() == ' ')
      line.pop_back();
    lines.push_back(fitText(font, size, line, width));
    p = cut;
  }
  return lines;
}

static void drawRadial() {
  auto &io = ImGui::GetIO();
  auto *draw = ImGui::GetForegroundDrawList();
  ImFont *font = ImGui::GetFont();
  const float base = ImGui::GetFontSize();
  const DWORD now = GetTickCount();
  float alpha = radial.open ? (std::min)(1.0f, (now - radial.openedAt) / float(radialFadeMs))
                            : 1.0f - (std::min)(1.0f, (now - radial.closedAt) / float(radialFadeMs));
  alpha = alpha * alpha * (3 - 2 * alpha); // eased
  const auto colour = [&](int r, int g, int b, float a) {
    return IM_COL32(r, g, b, int(255 * a * alpha));
  };
  const ImVec2 centre(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  const float grow = 0.94f + 0.06f * alpha;
  const float outer = (std::min)(io.DisplaySize.x, io.DisplaySize.y) * 0.31f * grow;
  const float inner = outer * 0.40f;
  const auto at = [&](float angle, float radius) {
    return ImVec2(centre.x + cosf(angle) * radius, centre.y + sinf(angle) * radius);
  };
  const int count = (int)radial.entries.size();

  if (!count) {
    const char *line = radialSlots.empty() ? "Nothing pinned to the radial menu yet."
                                           : "None of your pinned mods are loaded.";
    const char *hint = "Pin mods from the panel: the ring button on a card.";
    const auto a = font->CalcTextSizeA(base, FLT_MAX, 0, line), b = font->CalcTextSizeA(base * 0.85f, FLT_MAX, 0, hint);
    const float width = (std::max)(a.x, b.x) + base * 2, height = a.y + b.y + base * 1.6f;
    const ImVec2 min(centre.x - width / 2, centre.y - height / 2), max(centre.x + width / 2, centre.y + height / 2);
    draw->AddRectFilled(min, max, colour(14, 14, 17, 0.88f), base * 0.6f);
    draw->AddRect(min, max, colour(214, 176, 110, 0.55f), base * 0.6f);
    draw->AddText(font, base, ImVec2(centre.x - a.x / 2, min.y + base * 0.7f), colour(214, 176, 110, 1), line);
    draw->AddText(font, base * 0.85f, ImVec2(centre.x - b.x / 2, min.y + base * 0.9f + a.y),
                  colour(150, 152, 158, 1), hint);
    return;
  }

  constexpr float pi = 3.14159265359f;
  // The wedges are filled in strips, without anti-aliasing, so no seams show
  // between the strips. Outlines and text keep it.
  const auto flags = draw->Flags;
  const float step = 2 * pi / count, gap = (std::min)(0.035f, step * 0.04f);
  for (int i = 0; i < count; ++i) {
    const auto &entry = radial.entries[i];
    const bool chosen = i == radial.selected;
    const float a0 = -pi / 2 + (i - 0.5f) * step + gap, a1 = -pi / 2 + (i + 0.5f) * step - gap;
    const int segments = (std::max)(8, int(48 / count) + 4);
    const float lift = chosen ? outer * 0.035f : 0;
    const float mid = (a0 + a1) / 2;
    const ImVec2 shift(cosf(mid) * lift, sinf(mid) * lift);
    const ImU32 fill = chosen ? colour(70, 56, 34, 0.92f) : colour(16, 16, 20, entry.usable ? 0.82f : 0.6f);
    draw->Flags = flags & ~ImDrawListFlags_AntiAliasedFill;
    for (int s = 0; s < segments; ++s) {
      const float t0 = a0 + (a1 - a0) * s / segments, t1 = a0 + (a1 - a0) * (s + 1) / segments;
      const auto o0 = at(t0, outer), o1 = at(t1, outer), n0 = at(t0, inner), n1 = at(t1, inner);
      draw->AddQuadFilled(ImVec2(o0.x + shift.x, o0.y + shift.y), ImVec2(o1.x + shift.x, o1.y + shift.y),
                          ImVec2(n1.x + shift.x, n1.y + shift.y), ImVec2(n0.x + shift.x, n0.y + shift.y), fill);
    }
    draw->Flags = flags;
    draw->PathArcTo(ImVec2(centre.x + shift.x, centre.y + shift.y), outer, a0, a1, segments);
    draw->PathArcTo(ImVec2(centre.x + shift.x, centre.y + shift.y), inner, a1, a0, segments);
    draw->PathStroke(chosen ? colour(214, 176, 110, 1) : colour(60, 60, 66, 0.9f), ImDrawFlags_Closed,
                     chosen ? 2.0f : 1.0f);

    // The mod's name, and what this entry does when the mod has more than
    // one on the ring or in the panel.
    const float labelRadius = (outer + inner) / 2;
    const ImVec2 spot(at(mid, labelRadius).x + shift.x, at(mid, labelRadius).y + shift.y);
    const float width = (std::min)((outer - inner) * 1.2f, 2 * labelRadius * sinf(step / 2) * 0.85f);
    const auto title = wrapText(font, base, UI::name(entry.action), width, 2);
    const auto detail = entry.several ? fitText(font, base * 0.8f, UI::actionName(entry.action), width) : "";
    const float lineHeight = font->CalcTextSizeA(base, FLT_MAX, 0, "Ag").y;
    const auto ds = detail.empty() ? ImVec2(0, 0) : font->CalcTextSizeA(base * 0.8f, FLT_MAX, 0, detail.c_str());
    const float toggleMark = entry.action.toggle ? base * 0.7f : 0;
    const float block = lineHeight * title.size() + (detail.empty() ? 0 : ds.y) +
                        (toggleMark ? toggleMark + base * 0.15f : 0);
    float y = spot.y - block / 2;
    const ImU32 text = !entry.usable ? colour(110, 112, 118, 1) : chosen ? colour(240, 214, 160, 1) : colour(232, 232, 226, 1);
    for (const auto &line : title) {
      const auto ts = font->CalcTextSizeA(base, FLT_MAX, 0, line.c_str());
      draw->AddText(font, base, ImVec2(spot.x - ts.x / 2, y), text, line.c_str());
      y += lineHeight;
    }
    if (!detail.empty()) {
      draw->AddText(font, base * 0.8f, ImVec2(spot.x - ds.x / 2, y), colour(150, 152, 158, 1), detail.c_str());
      y += ds.y;
    }
    if (toggleMark) {
      // A small switch: the entry turns something on or off.
      const float w = toggleMark * 1.8f, h = toggleMark * 0.9f;
      const ImVec2 min(spot.x - w / 2, y + base * 0.15f), max(spot.x + w / 2, y + base * 0.15f + h);
      draw->AddRect(min, max, text, h / 2, 0, 1.2f);
      draw->AddCircleFilled(ImVec2(max.x - h / 2, (min.y + max.y) / 2), h * 0.32f, text);
    }
  }

  // The centre: what the chosen entry is, or how to leave.
  draw->AddCircleFilled(centre, inner * 0.9f, colour(10, 10, 13, 0.9f), 48);
  draw->AddCircle(centre, inner * 0.9f, colour(214, 176, 110, 0.45f), 48, 1.0f);
  const float width = inner * 1.55f;
  struct Line {
    std::string text;
    ImU32 colour;
    float size;
  };
  std::vector<Line> lines;
  const float small = base * 0.82f;
  const auto add = [&](const std::string &text, ImU32 c, float size, float w, int most) {
    for (auto &line : wrapText(font, size, text, w, most))
      lines.push_back({std::move(line), c, size});
  };
  if (radial.selected >= 0 && radial.selected < count) {
    const auto &entry = radial.entries[radial.selected];
    add(UI::name(entry.action), colour(240, 214, 160, 1), base, width * 0.9f, 2);
    add(UI::actionName(entry.action), colour(232, 232, 226, 1), small, width, 1);
    if (!entry.usable)
      add("Not available now", colour(230, 120, 110, 1), small, width, 1);
    else if (!entry.action.description.empty()) {
      const auto &d = entry.action.description;
      add(d.substr(0, d.find('\n')), colour(150, 152, 158, 1), small, width * 0.9f, lines.size() > 2 ? 2 : 3);
    }
  } else {
    add("Choose a mod", colour(214, 176, 110, 1), base, width, 1);
    add(radial.viaPad || radial.stickUsed ? "Aim with the right stick" : "Move the mouse to aim",
        colour(150, 152, 158, 1), small, width, 1);
    add("Let go to close", colour(150, 152, 158, 1), small, width, 1);
  }
  float total = 0;
  for (const auto &line : lines)
    total += font->CalcTextSizeA(line.size, FLT_MAX, 0, line.text.c_str()).y;
  float y = centre.y - total / 2;
  for (const auto &line : lines) {
    const auto ts = font->CalcTextSizeA(line.size, FLT_MAX, 0, line.text.c_str());
    draw->AddText(font, line.size, ImVec2(centre.x - ts.x / 2, y), line.colour, line.text.c_str());
    y += ts.y;
  }
}
