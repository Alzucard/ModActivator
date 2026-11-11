#pragma once

namespace ml::gui
{
    // Called once when ImGui is created, before the DX12 backend builds the font.
    void InitStyle(float scale);
    // A language change brings in characters the atlas was not built with. The
    // render hook asks between frames and rebuilds once its own work has
    // retired, since the backend releases the font texture to do it.
    bool FontsNeedRebuild();
    void RebuildFonts();
    // The height of the target the menu is drawn into, each frame. A height
    // that implies another UI scale schedules a rebuild through the two above.
    void SetDisplayHeight(float height);
    // Polled every frame from the render hook: menu key edge, Escape to close.
    void PollToggle();
    // Whether the menu needs drawing this frame.
    bool WantsDraw();
    // Hands ImGui the menu's own cursor and the input queued since the last
    // frame. Call between ImGui_ImplWin32_NewFrame and ImGui::NewFrame: later,
    // and the Win32 backend's own cursor poll overrides the virtual cursor.
    void FeedInput();
    // Draws the menu.
    void Render();
}
