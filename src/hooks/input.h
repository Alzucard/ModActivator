#pragma once
#include <Windows.h>

struct ImGuiIO;

namespace ml::input
{
    // Subclasses the game window. While the menu is open, keyboard and mouse
    // messages are queued for ImGui and withheld from the game (key releases still
    // pass so nothing sticks).
    //
    // The menu's cursor is virtual. The game clips the OS cursor to a point,
    // hides it and recentres it every frame, and it does so from more than one
    // thread, so the OS cursor is useless for a menu. Instead the raw mouse
    // deltas the game already receives (WM_INPUT) move a cursor of our own,
    // clamped to the window, and that is what ImGui is fed. The OS cursor is
    // left alone, so nothing has to be restored when the menu closes.
    void Init(HWND hwnd);

    void MenuOpened();   // centres the virtual cursor
    void MenuClosed();
    // The radial menu aims with the same virtual cursor, without a cursor on
    // screen and without feeding ImGui: centre it when the ring opens, then
    // read where the mouse has moved it, with the window's client size.
    void CentreCursor();
    void PlaceCursor(float x, float y);
    void VirtualCursor(float* x, float* y, float* w, float* h);
    // Render thread, before ImGui::NewFrame: submit the cursor and queued input.
    void FeedInput(ImGuiIO& io);
    // The size of the target the menu is drawn into, which is the swapchain's
    // and not always the window's. The virtual cursor is kept in window
    // coordinates, because that is what the raw deltas and the OS cursor are
    // in, and scaled into this on the way to ImGui.
    void SetOverlaySize(float w, float h);
}
