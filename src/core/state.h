#pragma once
#include <atomic>
#include <Windows.h>

namespace ml
{
    // Marks the key events ModActivator injects, so its own window procedure
    // passes them to the game instead of the open menu.
    inline constexpr ULONG_PTR shortcutInputTag = 0x4D414354;

    // Runtime flags shared by the render thread (menu), the window thread
    // (input) and the shortcut dispatch thread. Plain scalars only.
    struct State
    {
        std::atomic<bool> sendingShortcut{false};
        std::atomic<bool> menuOpen{false};
        std::atomic<DWORD> renderTid{0};     // thread that presents frames (set by the menu each frame)
        // ModActivator's controller shortcut (pad.h bits), kept from the game
        // while all of it is held. 0 when there is none.
        std::atomic<unsigned> padShortcut{0};

        // When the overlay last actually drew a frame. The menu can only be
        // opened or closed from inside the render path, so if drawing stops
        // while it is open there is no way left to close it.
        std::atomic<DWORD> lastDrawAt{0};

        // True while the menu owns keyboard, mouse and pad.
        //
        // Gated on the overlay having drawn recently. Without that, an overlay
        // that stops drawing with the menu open leaves the pad zeroed for the
        // rest of the session and no key can undo it, because the key that
        // would close the menu is read from the render path that has stopped.
        //
        // Half a second is far longer than a frame and far shorter than anyone
        // would spend wondering why the pad died.
        bool Captures() const
        {
            if (!menuOpen) return false;
            const DWORD drawn = lastDrawAt.load();
            return drawn && (GetTickCount() - drawn) < 500;
        }

        // The game window is in front. Hotkeys are read with GetAsyncKeyState,
        // which sees every window, so they are ignored while alt-tabbed.
        static bool ForegroundIsOurs()
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            return pid == GetCurrentProcessId();
        }

        static State& Get()
        {
            static State s;
            return s;
        }
    };
}
