// Who owns a function: module names, detours other mods put first, whether to
// wrap, and the checks before each detour goes in.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // Log which module an address lives in - tells us from a live test whether the
    // factory we patched is the DLSS-G proxy (sl.*/nvngx*) or native (dxgi.dll).
    // Which DLL owns an address. On a clean install every one of these is a
    // system DLL; anything else means another mod is already on that code, and
    // that is the first thing to know when a launch crash is reported.
    static const char* OwningModule(void* addr, char* out, size_t n)
    {
        HMODULE m = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(addr), &m);
        char path[MAX_PATH] = "?";
        if (m) GetModuleFileNameA(m, path, MAX_PATH);
        const char* base = strrchr(path, '\\');
        strncpy(out, base ? base + 1 : path, n - 1);
        out[n - 1] = 0;
        return out;
    }

    // This plugin's own file name, as OwningModule reports it. Looked up from
    // a function inside it rather than written down, since the plugin can be
    // renamed and was once a different project entirely.
    static const char* OwnModuleName()
    {
        static const std::string name = [] {
            char b[64] = "";
            OwningModule(reinterpret_cast<void*>(&OwnModuleName), b, sizeof b);
            return std::string(b);
        }();
        return name.c_str();
    }

    // Forward declared: this wants to follow a detour, and the decoder is
    // defined below with the rest of the byte reading.
    static void DescribeDetour(const uint8_t* p, const void* at, char* out, size_t n);

    static void LogHookedModule(const char* what, void* addr)
    {
        char mod[64];
        OwningModule(addr, mod, sizeof mod);
        // The swapchain's own Present can be detoured too, and when it is, the
        // owning module is still the system DLL. Following the jump is the only
        // thing that names what is really in front of us.
        char into[96] = "";
        if (addr && !IsBadReadPtr(addr, 16))
        {
            uint8_t b[16];
            memcpy(b, addr, sizeof b);
            DescribeDetour(b, addr, into, sizeof into);
        }
        LOG("%s @ %p in %s%s", what, addr, mod, into);
    }

    // The DLLs these addresses belong to before anyone touches them. Anything
    // else and a second mod got there first, which is worth saying out loud.
    static bool IsSystemOwner(const char* mod)
    {
        return _stricmp(mod, "dxgi.dll") == 0 || _stricmp(mod, "d3d12.dll") == 0 ||
               _stricmp(mod, "d3d12core.dll") == 0;
    }

    // Whether the module that owns an address actually lives in the Windows
    // system directory, as against merely being called what a system DLL is
    // called.
    //
    // ReShade installs itself as dxgi.dll in the game folder, and so do other
    // overlays. Judged by name alone that proxy passes as the system library,
    // and this mod then writes an inline detour into another mod's code. That
    // was the whole of the hang reproduced for issue 34: with the swapchain
    // wrapper off the one hook still installed was ExecuteCommandLists, and it
    // had resolved to "dxgi.dll", which on that machine was ReShade's proxy in
    // bin64. On a clean machine the same function resolves to D3D12Core.dll.
    static bool ModuleInSystemDir(void* addr)
    {
        HMODULE m = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCSTR>(addr), &m) || !m)
            return false;
        char path[MAX_PATH] = "";
        if (!GetModuleFileNameA(m, path, MAX_PATH)) return false;
        char sys[MAX_PATH] = "";
        const UINT n = GetSystemDirectoryA(sys, MAX_PATH);
        if (!n || n >= MAX_PATH) return false;
        return _strnicmp(path, sys, n) == 0 && (path[n] == '\\' || path[n] == '/');
    }

    // A function whose first bytes are already a jump has been detoured in
    // place by someone else. The owning module stays the system DLL in that
    // case, so the module name alone cannot see it; only the bytes can.
    // Where that jump actually lands, so the log can name the mod that got
    // there first instead of only saying somebody did.
    //
    // Three of the open reports are some form of "it will not load alongside
    // these other mods", and every one of them arrived as a log saying Present
    // was already detoured, with no way to tell whether that was RenoDX,
    // ReShade, OptiScaler or a transmog mod without asking the reporter to
    // list their load order by hand.
    //
    // push/ret is left alone deliberately: its immediate is 32 bits, which
    // cannot address a module on x64, so resolving it would print a number
    // that means nothing.
    static bool DetourTarget(const uint8_t* p, const void* at, void** out)
    {
        const uintptr_t a = reinterpret_cast<uintptr_t>(at);
        if (p[0] == 0xE9)
        {
            int32_t rel = 0; memcpy(&rel, p + 1, sizeof rel);
            *out = reinterpret_cast<void*>(a + 5 + static_cast<intptr_t>(rel));
            return true;
        }
        if (p[0] == 0xEB)
        {
            *out = reinterpret_cast<void*>(a + 2 + static_cast<intptr_t>(static_cast<int8_t>(p[1])));
            return true;
        }
        if (p[0] == 0xFF && p[1] == 0x25)
        {
            // The displacement names a slot holding the address, not the
            // address, so this one needs a read that may fail.
            int32_t disp = 0; memcpy(&disp, p + 2, sizeof disp);
            void* slot = reinterpret_cast<void*>(a + 6 + static_cast<intptr_t>(disp));
            if (IsBadReadPtr(slot, sizeof(void*))) return false;
            memcpy(out, slot, sizeof(void*));
            return true;
        }
        if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0)
        {
            memcpy(out, p + 2, sizeof(void*));
            return true;
        }
        return false;
    }

    // ", into RenoDX.asi at 00007FF..." or empty when it cannot be followed.
    //
    // Up to four hops, because some hook libraries land the first jump on a
    // stub in the system DLL's own padding and only then leave for the module
    // that owns the hook. Stopping at hop one would name dxgi.dll, which is
    // the answer we already had. Anything that lands outside a system DLL is
    // the answer, so the walk stops there.
    static void DescribeDetour(const uint8_t* p, const void* at, char* out, size_t n)
    {
        out[0] = 0;
        void* dest = nullptr;
        if (!DetourTarget(p, at, &dest) || !dest) return;

        char owner[64];
        OwningModule(dest, owner, sizeof owner);
        // Keep walking while the destination is still not an answer.
        //
        // Two kinds of non-answer. A system DLL means we landed on a stub in
        // dxgi's own padding. A "?" means no loaded module owns the address at
        // all, which is a relay page the other mod allocated for itself, and
        // that is the common case: the first build of this reported "into ? at
        // 00007FFB007B04CA" for all three hooked functions, which is the same
        // amount of information as before.
        //
        // A relay is usually one more jump, often the rip-relative indirect
        // form, and that one does land in the module that owns the hook. Six
        // hops because a chain of two or three is normal and the cost of one
        // more is a read.
        for (int hop = 0; hop < 6 && (IsSystemOwner(owner) || owner[0] == '?'); ++hop)
        {
            if (IsBadReadPtr(dest, 16)) break;
            uint8_t b[16];
            memcpy(b, dest, sizeof b);
            void* next = nullptr;
            if (!DetourTarget(b, dest, &next) || !next || next == dest) break;
            dest = next;
            OwningModule(dest, owner, sizeof owner);
        }
        // Ending on "?" is still worth saying, but say what it means. An
        // address in no module is code somebody allocated, not a mystery.
        if (owner[0] == '?')
            snprintf(out, n, ", into an allocated stub at %p", dest);
        else
            snprintf(out, n, ", into %s at %p", owner, dest);
    }

    static const char* ExistingDetour(const uint8_t* p)
    {
        if (p[0] == 0xE9) return "jmp rel32";
        if (p[0] == 0xFF && p[1] == 0x25) return "jmp [rip+disp]";
        if (p[0] == 0xEB) return "jmp short";
        if (p[0] == 0x68 && p[5] == 0xC3) return "push/ret";
        if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0) return "mov rax, imm64; jmp rax";
        return nullptr;
    }

    // Which module's code an existing detour on fn lands in, following relay
    // stubs the way DescribeDetour does. False when fn is not detoured.
    static bool DetourOwner(void* fn, char* out, size_t n)
    {
        if (!fn || IsBadReadPtr(fn, 16)) return false;
        uint8_t b[16];
        memcpy(b, fn, sizeof b);
        if (!ExistingDetour(b)) return false;
        void* dest = nullptr;
        if (!DetourTarget(b, fn, &dest) || !dest) { strncpy(out, "?", n); out[n - 1] = 0; return true; }
        OwningModule(dest, out, n);
        for (int hop = 0; hop < 6 && (IsSystemOwner(out) || out[0] == '?'); ++hop)
        {
            if (IsBadReadPtr(dest, 16)) break;
            uint8_t c[16];
            memcpy(c, dest, sizeof c);
            void* next = nullptr;
            if (!DetourTarget(c, dest, &next) || !next) break;
            dest = next;
            OwningModule(dest, out, n);
        }
        return true;
    }

    // Whether the game's swapchain gets the wrapper. WrapSwapChain=1 and 0
    // decide it outright. Auto wraps when Streamline, the layer DLSS frame
    // generation runs in, is loaded: under it the present hook only sees
    // finished frames, which the menu cannot be drawn on, and the wrapper,
    // drawing before Streamline interpolates, is the only way it shows.
    // Without Streamline the present hook is the path with the longer record,
    // so it stays. The game loads sl.interposer.dll before its first
    // swapchain, which is where this is first asked. The answer is kept for
    // the session, so the detours InstallPass chose from it and every later
    // recreation agree, and it is said once in the log.
    static bool WrapWanted()
    {
        static int s_decided = -1;
        if (s_decided < 0)
        {
            const int setting = Settings::Get().wrapSwapChain;
            const bool streamline = GetModuleHandleA("sl.interposer.dll") != nullptr;
            const bool wrap = setting >= 0 ? setting != 0 : streamline;
            s_decided = wrap;
            if (setting < 0)
                LOG("WrapSwapChain=auto: Streamline (DLSS frame generation) is %s, so the swapchain is %s.",
                    streamline ? "loaded" : "not loaded",
                    wrap ? "wrapped and the menu draws before frame generation"
                         : "left alone and the menu draws through the present hook");
            else
                LOG("WrapSwapChain=%d: the swapchain is %s.", setting,
                    wrap ? "wrapped" : "left alone. The overlay draws through the present hook, which does not show under DLSS frame generation");
        }
        return s_decided != 0;
    }

    // Detours on Present that the wrapper has always lived alongside. Steam's
    // overlay draws and keys nothing; the frame generation interposer is the
    // proxy the wrapper was built to sit under. Anything else that got to
    // Present first is assumed to key its own work on the swapchain object it
    // was handed, which Crimson Route does, and such a mod must not be handed
    // the wrapper.
    static bool BenignPresentOwner(const char* mod)
    {
        if (!mod || !mod[0] || mod[0] == '?') return false;
        if (IsSystemOwner(mod)) return true;
        if (_stricmp(mod, "gameoverlayrenderer64.dll") == 0) return true;
        if (_stricmp(mod, OwnModuleName()) == 0) return true;
        if (_strnicmp(mod, "sl.", 3) == 0 || _strnicmp(mod, "nvngx", 5) == 0) return true;
        return false;
    }

    // A detour's prologue names only the mod that detoured Present last. One
    // that keys the swapchain but hooked before Steam's overlay is invisible
    // that way, so the mods known to key it are also checked by name.
    static const char* KeyingOverlayLoaded()
    {
        static const char* const kMods[] = { "CrimsonRoute.asi" };
        for (const char* m : kMods) if (GetModuleHandleA(m)) return m;
        return nullptr;
    }

    static bool LogHookTarget(const char* what, void* addr, bool stack = false, bool allowProxy = false)
    {
        char mod[64];
        OwningModule(addr, mod, sizeof mod);

        // First bytes, so a detour that was already there is on the record even
        // when this mod goes on to hook over the top of it.
        uint8_t b[16] = {};
        char bytes[64] = "";
        const char* detour = nullptr;
        if (addr && !IsBadReadPtr(addr, sizeof b))
        {
            memcpy(b, addr, sizeof b);
            int w = 0;
            for (int i = 0; i < 8; ++i) w += snprintf(bytes + w, sizeof bytes - w, "%02X ", b[i]);
            detour = ExistingDetour(b);
        }

        if (detour)
        {
            char into[96];
            DescribeDetour(b, addr, into, sizeof into);
            if (stack)
            {
                // Stacking is what every overlay does and what this mod
                // avoided; here it is the only way to draw without handing
                // another mod the wrapper. Ours runs first and calls theirs.
                LOG("[hook] %s @ %p in %s starts with %s (%s)%s: another mod detoured it first; stacking this mod's detour on top, so ours runs and then theirs",
                    what, addr, mod, detour, bytes, into);
            }
            else
            {
                LOG("[hook] %s @ %p in %s starts with %s (%s)%s: another mod detoured it first, so this one is left alone",
                    what, addr, mod, detour, bytes, into);
                return false;
            }
        }
        if (!IsSystemOwner(mod))
        {
            // A proxy does not have to be named after the library it replaces.
            // The stacking allowance below was written for OptiScaler, DLSS
            // Enabler and ReShade installed as dxgi.dll, and it sits behind this
            // test, which knows three names. susemi325's 1.6.14 log on 12
            // September 2026 has OptiScaler loaded as VERSION.dll with Present
            // and Present1 both inside it, so the call was refused here, a check
            // earlier than the one the retry relaxes. The retry ran, said it was
            // trying with a proxy allowed, and changed nothing: three mods deep
            // and the menu still dark, with the reason sitting in the log in
            // plain words that named the wrong rule.
            //
            // allowProxy is set for the present calls and nothing else, so
            // ExecuteCommandLists is still refused by name here, which is where
            // issue 34 was earned.
            if (!allowProxy)
            {
                LOG("[hook] %s @ %p in %s (%s), which is not a system DLL: another mod owns it, so this one is left alone", what, addr, mod, bytes);
                return false;
            }
            LOG("[hook] %s @ %p in %s (%s) belongs to another mod outright, and it owns every present call there is: "
                "stacking on it, because the alternative is a menu that never draws", what, addr, mod, bytes);
            return true;
        }
        // Right name, wrong place. A dxgi.dll that lives beside the game is a
        // proxy some other mod installed, and detouring it is detouring them.
        // D3D12Core.dll is the Agility SDK runtime, which a game ships in its
        // own folder on purpose, so the game-folder rule does not apply to it.
        // The detour check above has already ruled out another mod's jump on
        // it. Judged by place alone, ExecuteCommandLists was refused on
        // LuxDragon's machine and the queue had no fallback.
        if (!ModuleInSystemDir(addr) && _stricmp(mod, "d3d12core.dll") != 0)
        {
            // Unless there is nothing else left. OptiScaler, DLSS Enabler and
            // ReShade replace dxgi.dll outright, so the game's swapchain is
            // their object and every present call in its vtable is their code.
            // Refusing all of them leaves this mod with no way to draw at all,
            // which is what happened here on 11 September 2026: an OptiScaler
            // dxgi.dll went in beside the game, Crimson Route was loaded so the
            // wrapper was off, and the menu went silent on a plugin that
            // otherwise loaded and looted. Stacking on a proxy's present is the
            // same move this mod already makes on Steam's overlay: ours runs,
            // then theirs, then the real library. Issue 34's refusal stands
            // where it was earned, on anything that is not a present call.
            if (!allowProxy)
            {
                LOG("[hook] %s @ %p in %s (%s), a module named like a system DLL but living in the game folder, "
                    "so it is another mod's proxy: left alone", what, addr, mod, bytes);
                return false;
            }
            LOG("[hook] %s @ %p in %s (%s) is another mod's proxy in the game folder, and it owns every present "
                "call there is: stacking on it, because the alternative is a menu that never draws", what, addr, mod, bytes);
            return true;
        }
        LOG("[hook] %s @ %p in %s (%s)", what, addr, mod, bytes);
        return true;
    }
