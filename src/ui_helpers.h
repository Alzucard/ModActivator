#pragma once
static std::string keyName(int k) {
  if (k >= 112 && k <= 135)
    return "F" + std::to_string(k - 111);
  if (k >= 65 && k <= 90)
    return std::string(1, (char)k);
  if (k >= 48 && k <= 57)
    return std::string(1, (char)k);
  if (k >= 96 && k <= 105)
    return "NumPad" + std::to_string(k - 96);
  switch (k) {
  case 8:
    return "Backspace";
  case 9:
    return "Tab";
  case 13:
    return "Enter";
  case VK_SHIFT:
    return "Shift";
  case VK_CONTROL:
    return "Ctrl";
  case VK_MENU:
    return "Alt";
  case 19:
    return "Pause";
  case 20:
    return "CapsLock";
  case 27:
    return "Escape";
  case 32:
    return "Space";
  case 33:
    return "PageUp";
  case 34:
    return "PageDown";
  case 35:
    return "End";
  case 36:
    return "Home";
  case 37:
    return "Left";
  case 38:
    return "Up";
  case 39:
    return "Right";
  case 40:
    return "Down";
  case 44:
    return "PrintScreen";
  case 45:
    return "Insert";
  case 46:
    return "Delete";
  case 145:
    return "ScrollLock";
  case 192:
    return "Grave";
  }
  return {};
}
static std::string configKeyName(int key) {
  if (isPrintableKey(key)) {
    const auto scan = scanForVk(key, gameKeyboardLayout());
    const char symbol = usSymbolForScan(scan);
    if (symbol)
      return scan == 0x29 ? "Grave" : std::string(1, symbol);
    return localizedKey(key);
  }
  return keyName(key);
}
static std::string shortcutLabel(const std::vector<int> &keys) {
  std::string out;
  for (int k : keys) {
    if (!out.empty())
      out += " + ";
    auto label = isPrintableKey(k) ? localizedKey(k) : keyName(k);
    out += label.empty() ? "VK " + std::to_string(k) : label;
  }
  return out;
}

namespace UI {
static ImGuiID recording = 0;
static bool waitingForRelease = false;
static void beginShortcutCapture(ImGuiID id) {
  recording = id;
  waitingForRelease = true;
}
static std::vector<int> captureModifiers(bool (*isHeld)(int) = held) {
  std::vector<int> keys;
  for (int key : {VK_CONTROL, VK_SHIFT, VK_MENU})
    if (isHeld(key))
      keys.push_back(key);
  return keys;
}
static std::vector<int> captureShortcut(ImGuiID id, bool (*isHeld)(int) = held) {
  if (recording != id || ml::State::Get().sendingShortcut)
    return {};
  if (isHeld(VK_ESCAPE)) {
    recording = 0;
    return {};
  }
  int main = 0;
  for (int key = VK_BACK; key <= VK_OEM_102; ++key) {
    if ((keyName(key).empty() && !isPrintableKey(key)) ||
        key == VK_CONTROL || key == VK_SHIFT || key == VK_MENU || !isHeld(key))
      continue;
    if (main) {
      waitingForRelease = true;
      return {};
    }
    main = key;
  }
  if (waitingForRelease) {
    if (!main)
      waitingForRelease = false;
    return {};
  }
  if (!main || isHeld(VK_LWIN) || isHeld(VK_RWIN))
    return {};
  recording = 0;
  auto keys = captureModifiers(isHeld);
  keys.push_back(main);
  return keys;
}
static std::vector<int> recordShortcut(const char *label) {
  const auto id = ImGui::GetID(label);
  if (ImGui::Button(recording == id ? "Press shortcut...##Recording" : label))
    beginShortcutCapture(id);
  if (recording == id) {
    ImGui::SameLine();
    ImGui::TextDisabled("Esc cancels");
  }
  return captureShortcut(id);
}
static std::vector<int> shortcutField(const std::vector<int> &keys, float width) {
  const auto id = ImGui::GetID("##NewShortcut");
  const bool active = recording == id;
  std::string label;
  if (active) {
    label = shortcutLabel(captureModifiers());
    label += label.empty() ? "Press shortcut..." : " + ...";
  } else {
    label = keys.empty() ? "Click to set shortcut" : shortcutLabel(keys);
  }

  const auto &style = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button,
                        style.Colors[active ? ImGuiCol_FrameBgActive : ImGuiCol_FrameBg]);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_FrameBgActive]);
  if (ImGui::Button("##NewShortcut", ImVec2(width, ImGui::GetFrameHeight())))
    beginShortcutCapture(id);
  ImGui::PopStyleColor(3);
  const auto min = ImGui::GetItemRectMin();
  const auto max = ImGui::GetItemRectMax();
  auto *draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(min, max, true);
  draw->AddText(ImVec2(min.x + style.FramePadding.x, min.y + style.FramePadding.y),
                ImGui::GetColorU32(!active && keys.empty() ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                label.c_str());
  draw->PopClipRect();
  ImGui::SameLine();
  ImGui::TextUnformatted("New shortcut");
  if (recording == id) {
    ImGui::SameLine();
    ImGui::TextDisabled("Esc cancels");
  }
  return captureShortcut(id);
}
static float scale = 1.0f;
static char search[128] = "";
static const ImVec4 gold{0.84f, 0.69f, 0.43f, 1.0f},
    muted{0.57f, 0.59f, 0.62f, 1.0f};
static std::string name(const Action &a) {
  std::string raw = std::filesystem::path(a.module).stem().string(), out;
  for (size_t i = 0; i < raw.size(); ++i) {
    unsigned char c = raw[i];
    if (i && isupper(c) && islower((unsigned char)raw[i - 1]))
      out += ' ';
    out += c == '_' ? ' ' : c;
  }
  return out;
}
static std::string actionName(const Action &a) {
  // Keep the INI action separate from classification and tab placement.
  std::string raw = a.key;
  auto folded = lower(raw);
  for (std::string_view prefix : {"hotkey", "key"}) {
    if (raw.size() > prefix.size() && folded.compare(0, prefix.size(), prefix) == 0 &&
        (raw[prefix.size()] == '_' || isupper((unsigned char)raw[prefix.size()]))) {
      raw.erase(0, prefix.size());
      folded.erase(0, prefix.size());
      break;
    }
  }
  for (std::string_view suffix : {"hotkey", "keycode", "keyvk", "key"}) {
    if (raw.size() > suffix.size() &&
        folded.compare(raw.size() - suffix.size(), suffix.size(), suffix) == 0) {
      raw.resize(raw.size() - suffix.size());
      break;
    }
  }
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    if (i && isupper((unsigned char)raw[i]) &&
        islower((unsigned char)raw[i - 1]))
      out += ' ';
    out += raw[i] == '_' ? ' ' : raw[i];
  }
  out = trimConfigLine(out);
  if (out.empty())
    return a.key;
  out.front() = (char)toupper((unsigned char)out.front());
  return out;
}
static std::string shortcut(const Action &a) {
  auto out = shortcutLabel(a.cachedKeys);
  return out.empty() ? a.cachedValue : out;
}
static bool matches(const Action &a) {
  return !search[0] ||
         lower(a.module + " " + name(a) + " " + a.label + " " + actionName(a) +
               " " + a.description + " " + a.cachedValue + " " + shortcut(a))
                 .find(lower(search)) != std::string::npos;
}
static void bindingTooltip(const Action &a, const std::string &detail = {}) {
  if (!ImGui::BeginTooltip())
    return;
  const auto title = name(a) + " / " + actionName(a);
  ImGui::TextUnformatted(title.c_str());
  ImGui::TextDisabled("%s", (detail.empty() ? shortcut(a) : detail).c_str());
  ImGui::TextDisabled("%s / [%s] %s", a.file.c_str(), a.section.c_str(), a.key.c_str());
  if (!a.description.empty()) {
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * scale);
    ImGui::TextUnformatted(a.description.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::EndTooltip();
}
static void badge(const char *text, ImVec4 color) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * .20f, color.y * .20f,
                                                color.z * .20f, 1));
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::SmallButton(text);
  ImGui::PopStyleColor(2);
}
} // namespace UI
