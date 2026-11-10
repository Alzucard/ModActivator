#pragma once
#include <atomic>
#include <Windows.h>
#include <cstring>

namespace ml
{
    // Runtime flags shared by the render thread (menu), the window thread
    // (input) and the loot worker. Plain scalars only.
    struct State
    {
        std::atomic<bool> keyboardPassthrough{false};
        std::atomic<bool> menuOpen{false};
        std::atomic<bool> menuWatch{false}; // menu drawn but every input goes to the game (watch mode)
        std::atomic<bool> textCapture{false}; // an ImGui text field has focus: every key belongs to the menu
        std::atomic<bool> rebindCapture{false}; // the General tab is waiting for the next key to bind
        std::atomic<bool> hooksOk{false};
        std::atomic<bool> overlayReady{false};
        std::atomic<DWORD> renderTid{0};     // thread that presents frames (set by the menu each frame)

        // A short on-screen notice ("auto-loot on"), drawn until `noticeUntil`.
        // An important one is drawn whether or not the toggle notice is wanted:
        // someone who turned that off still wants to hear about a full bag.
        char  notice[320]     = "";   // the pet filter names what it deleted, which runs past 200
        DWORD noticeUntil     = 0;
        bool  noticeImportant = false;

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
        // The symptom is a dead controller and no menu on screen, which looks
        // nothing like a mod problem and is entirely a mod problem.
        //
        // Half a second is far longer than a frame and far shorter than anyone
        // would spend wondering why the pad died.
        bool Captures() const
        {
            if (!menuOpen || menuWatch) return false;
            const DWORD now = GetTickCount();
            const DWORD drawn=lastDrawAt.load();return drawn && (now - drawn) < 500;
        }

        void Notify(const char* text, DWORD ms = 2500, bool important = false)
        {
            strncpy(notice, text, sizeof notice - 1);
            notice[sizeof notice - 1] = 0;
            noticeImportant = important;
            noticeUntil = GetTickCount() + ms;
        }

        // The game window is in front. Hotkeys are read with GetAsyncKeyState,
        // which sees every window, so they are ignored while alt-tabbed.
        static bool ForegroundIsOurs()
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            return pid == GetCurrentProcessId();
        }

        // Whether a keyboard hotkey of this mod may fire right now. Every key
        // here is bound bare, with no modifier, so a bare key is the only match:
        // with Ctrl or Alt held the press belongs to something else. Private
        // Storage Master binds Ctrl+F1 to Ctrl+F12, Ctrl+F10 among them, and
        // this mod's F10 read through it, so one press toggled both mods. Shift
        // is left out because the game sprints on it and nothing here is bound
        // with it; when Private Storage Master uses Shift itself, its own
        // PsmHidingKeys export says so and covers it. That export is optional
        // and looked up by name, so a build of that plugin without it changes
        // nothing. The pad chords are not affected.
        //
        // vk is the key being tested. A key bound to Ctrl or Alt itself is
        // always held with that modifier down, so the modifier it is does not
        // count against it, or it could never fire at all; trowieuk1 bound the
        // menu to Ctrl and it went dead in 1.6.27. The other modifier still
        // does. Private Storage Master never holds back a bare modifier, so a
        // modifier key does not ask it.
        static bool IsCtrlKey(int vk) { return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL; }
        static bool IsAltKey(int vk)  { return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU; }
        static bool HotkeysFree(int vk)
        {
            if (!IsCtrlKey(vk) && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) return false;
            if (!IsAltKey(vk) && (GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
            if (IsCtrlKey(vk) || IsAltKey(vk) || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return true;
            using HidingFn = int (*)(void);
            static std::atomic<HidingFn> s_hiding{nullptr};
            static std::atomic<DWORD> s_nextTry{0};
            HidingFn fn = s_hiding.load(std::memory_order_acquire);
            if (!fn)
            {
                const DWORD now = GetTickCount();
                if (static_cast<LONG>(now - s_nextTry.load(std::memory_order_relaxed)) >= 0)
                {
                    s_nextTry.store(now + 2000, std::memory_order_relaxed);
                    if (const HMODULE h = GetModuleHandleW(L"PrivateStorageMaster.asi"))
                        fn = reinterpret_cast<HidingFn>(GetProcAddress(h, "PsmHidingKeys"));
                    if (fn) s_hiding.store(fn, std::memory_order_release);
                }
            }
            return !(fn && fn() != 0);
        }

        static State& Get()
        {
            static State s;
            return s;
        }
    };
}
