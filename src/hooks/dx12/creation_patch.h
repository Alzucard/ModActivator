// Hooking swapchain creation in the factory, and the report of where creation
// goes when no frame ever arrives.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // VirtualProtect-patch one vtable slot. Reentrancy-safe (no MinHook, no thread
    // suspend), so it is safe to leave installed while the game runs.
    static bool PatchVtableSlot(void** vtable, int index, void* newFn, void** origOut)
    {
        DWORD old = 0;
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old))
            return false;
        *origOut = vtable[index];
        vtable[index] = newFn;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
        return true;
    }

    // Whether following the jumps from dxgi's entry reaches this mod's
    // detour. MinHook puts an E9 on the entry and a jmp [rip+0] in a relay
    // page beside it, so two hops is normal and eight is plenty.
    static bool EntryReachesOurs(void* fn)
    {
        void* at = fn;
        for (int hop = 0; hop < 8; ++hop)
        {
            if (!at || IsBadReadPtr(at, 16)) return false;
            uint8_t b[16];
            memcpy(b, at, sizeof b);
            void* next = nullptr;
            if (!DetourTarget(b, at, &next) || !next) return false;
            if (next == reinterpret_cast<void*>(&hkFactoryCreateSwapChainForHwnd)) return true;
            at = next;
        }
        return false;
    }

    // Whether following jumps from `from` ever lands on `at`.
    static bool ChainPasses(void* from, void* at)
    {
        void* p = from;
        for (int hop = 0; hop < 8 && p; ++hop)
        {
            if (p == at) return true;
            if (IsBadReadPtr(p, 16)) return false;
            uint8_t b[16];
            memcpy(b, p, sizeof b);
            void* next = nullptr;
            if (!DetourTarget(b, p, &next) || !next) return false;
            p = next;
        }
        return p == at;
    }

    // Character Creator 9 MinHooks this same entry, and CreateSwapChain beside
    // it, from a thread of its own that starts in the same millisecond as
    // this mod's. It builds both trampolines first and enables them one after
    // the other, and every enable stops every thread in the process. Its log
    // on oasisezy's machine, 25 September 2026, spans 16:42:02.432 to .963
    // for that setup; this mod read the entry at .475 and put its detour in
    // at .752. A trampoline built from the entry before .752 goes straight to
    // Steam's hook, and an enable after .752 writes over this mod's jump, so
    // the game's creation reached Character Creator and never this mod. Read
    // from CharacterCreator.asi 9.0.9: both of its detours call their
    // original and nothing else, so a correct chain would have reached us.
    //
    // So the entry is watched until the game makes its chain. If it stops
    // leading here and now starts with a jump into another mod's
    // jmp [rip+0] relay, only the eight address bytes of that relay change,
    // to this mod's detour, and the original becomes the address they held.
    // This mod then runs first and calls theirs, which carries on to Steam's
    // hook and dxgi. When the chain was correct after all, this mod is
    // entered twice, and the inner entry goes on through its own MinHook
    // trampoline rather than back into theirs, so the loop closes there.
    static void WatchCreationDetour()
    {
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            // Once only. A second reclaim would move the original off the
            // first mod's detour, and that mod would drop out of the chain.
            int displaced = 0;
            bool saidUnstackable = false;
            for (int i = 0; i < 600; ++i)
            {
                Sleep(50);
                if (g_factoryCalls.load(std::memory_order_relaxed) > 0) break;
                void* fn = g_factoryFn;
                if (!fn || EntryReachesOurs(fn)) { displaced = 0; continue; }
                // Half a second displaced before acting. The other mod's
                // installer stops every thread for each hook it enables, and
                // so does the write below; two of those running at once can
                // each stop the other and hang the game. Its enables come
                // back to back, so once the entry has held still this long
                // it has finished with this function. That narrows the
                // window; nothing can close it, since the other mod's
                // installer takes no lock this mod can see.
                if (++displaced < 10) continue;
                uint8_t b[16] = {};
                __try { memcpy(b, fn, sizeof b); }
                __except (EXCEPTION_EXECUTE_HANDLER) { break; }
                void* relay = nullptr;
                char owner[64] = "?";
                if (b[0] != 0xE9 || !DetourTarget(b, fn, &relay) || !relay || !farhook::AbsJumpTarget(reinterpret_cast<uintptr_t>(relay)))
                {
                    // Logged once and watched on: the other mod may enable
                    // again a moment later in a form that can be stacked on.
                    if (!saidUnstackable)
                    {
                        if (!DetourOwner(fn, owner, sizeof owner)) snprintf(owner, sizeof owner, "nothing, the original bytes are back");
                        LOG_ERR("[hook] CreateSwapChainForHwnd no longer leads to this mod's detour and the entry now goes to %s, in a form this mod cannot stack on; "
                                "the menu may never get a frame", owner);
                        saidUnstackable = true;
                    }
                    displaced = 0;
                    continue;
                }
                OwningModule(reinterpret_cast<void*>(farhook::AbsJumpTarget(reinterpret_cast<uintptr_t>(relay))), owner, sizeof owner);
                // If that mod hooked first, MinHook built this mod's own
                // trampoline as a jump into the relay about to be patched, and
                // a nested entry going that way would come straight back here.
                // It goes to their detour instead, which ends at the original.
                const bool ownLeadsThere = ChainPasses(reinterpret_cast<void*>(g_factoryOwnTramp), relay);
                // The new original is their detour itself, published before
                // the relay write. Until the write lands this mod is either
                // out of the chain or reached through their trampoline, and
                // either way a call ends at the original; published after, a
                // call in between could go round through the relay forever.
                // On a failure nothing was written and the old pointers go
                // back. farhook's own original is not used: it writes null on
                // a failure, and this pointer is live.
                char why[96] = "";
                void* unused = nullptr;
                const CSFH_t oldOrig = oFactoryCreateSwapChainForHwnd, oldOwn = g_factoryOwnTramp;
                const CSFH_t theirs = reinterpret_cast<CSFH_t>(farhook::AbsJumpTarget(reinterpret_cast<uintptr_t>(relay)));
                oFactoryCreateSwapChainForHwnd = theirs;
                if (ownLeadsThere) g_factoryOwnTramp = theirs;
                if (theirs && farhook::InstallOverAbsJump("CreateSwapChainForHwnd", reinterpret_cast<uintptr_t>(relay),
                                                          reinterpret_cast<void*>(&hkFactoryCreateSwapChainForHwnd), &unused, why, sizeof why))
                {
                    LOG("[hook] %s wrote its own jump over this mod's CreateSwapChainForHwnd detour after it went in; "
                        "stacked on its relay, so this mod runs first and then %s", owner, owner);
                    break;
                }
                else
                {
                    oFactoryCreateSwapChainForHwnd = oldOrig;
                    g_factoryOwnTramp = oldOwn;
                    LOG_ERR("[hook] %s wrote over this mod's CreateSwapChainForHwnd detour and stacking on it failed (%s); "
                            "the menu may never get a frame", owner, why);
                    break;
                }
            }
            return 0;
        }, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }

    static void InstallSwapChainCreationPatch()
    {
        IDXGIFactory2* factory = nullptr;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory)
        {
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory)
            {
                LOG_ERR("FG: no factory to patch - overlay-over-FrameGen disabled.");
                return;
            }
        }

        // The function is detoured, never the slot. Until 1.6.9 this replaced
        // the vtable pointer with a function in this DLL, and Steam's overlay
        // refuses to hook any factory whose create slot "points to another
        // module": whenever Steam looked at the factory after this patch and
        // after its own hook reset, it skipped the factory the game then made
        // its swapchain through, never hooked Present, and never drew. Found
        // on 10 September 2026 with HookDX12=0 as the control: Steam's overlay
        // came back the moment this patch was absent. An inline detour on
        // dxgi's own function leaves the slot in dxgi.dll, Steam hooks the
        // slot above us, Streamline and Route patch the slot above us too, and
        // this mod is the innermost caller either way, which is the ordering
        // every working run has had (see the issue #47 note). Stacking on a
        // detour Route already put on the function is the same arrangement as
        // Present, ours first and theirs next.
        void** vt = *reinterpret_cast<void***>(factory);
        void* fn = vt[15];
        g_factoryVt = vt;
        g_factoryFn = fn;
        if (LogHookTarget("Factory CreateSwapChainForHwnd", fn, true) &&
            MH_CreateHook(fn, reinterpret_cast<void*>(&hkFactoryCreateSwapChainForHwnd),
                          reinterpret_cast<void**>(&oFactoryCreateSwapChainForHwnd)) == MH_OK &&
            MH_EnableHook(fn) == MH_OK)
        {
            g_factoryInPlace = true;
            g_factoryOwnTramp = oFactoryCreateSwapChainForHwnd;
            LOG("FG: CreateSwapChainForHwnd detoured in place - the factory slot still points into dxgi.dll, so Steam's overlay keeps its hooks.");
            WatchCreationDetour();
        }
        else
        {
            // The slot patch is the fallback, with the Steam cost named.
            LOG_ERR("FG: could not detour CreateSwapChainForHwnd in place; replacing the vtable slot instead, which Steam's overlay may refuse to hook over.");
            if (PatchVtableSlot(vt, 15, reinterpret_cast<void*>(&hkFactoryCreateSwapChainForHwnd),
                                reinterpret_cast<void**>(&oFactoryCreateSwapChainForHwnd)))
                LOG("FG: CreateSwapChainForHwnd slot patched.");
            else
                LOG_ERR("FG: VirtualProtect of CreateSwapChainForHwnd slot failed.");
        }

        factory->Release();
    }

    // Written when twenty seconds pass with no frame. oasisezy's log of 25
    // September 2026 installed the detour above, stacked on Steam's jump, and
    // then the game made its swapchain with Character Creator 9 loaded and
    // the detour never ran. Nothing said which way the chain was made
    // instead. These lines say whether the detour is still at the entry,
    // whether the slot still points at it, who owns the entries of the other
    // three ways a factory makes a chain, and how often the detour has run.
    // The vtable lives in dxgi.dll's image, so it outlives the factory it was
    // read from; nothing is created to read it.
    static void LogEntry(const char* what, void* fn)
    {
        char mod[64] = "?", bytes[40] = "", into[96] = "";
        if (!fn || IsBadReadPtr(fn, 16)) { LOG("[creation] %s @ %p: unreadable", what, fn); return; }
        OwningModule(fn, mod, sizeof mod);
        uint8_t b[16];
        memcpy(b, fn, sizeof b);
        int w = 0;
        for (int i = 0; i < 8; ++i) w += snprintf(bytes + w, sizeof bytes - w, "%02X ", b[i]);
        const char* detour = ExistingDetour(b);
        if (detour) DescribeDetour(b, fn, into, sizeof into);
        if (w > 0) bytes[w - 1] = 0;
        LOG("[creation] %s @ %p in %s (%s) %s%s%s", what, fn, mod, bytes,
            detour ? "starts with " : "not detoured", detour ? detour : "", into);
    }

    static void ReportCreationPath()
    {
        void** vt = g_factoryVt;
        if (vt && !IsBadReadPtr(vt, 25 * sizeof(void*)))
        {
            LOG("[creation] this mod's CreateSwapChainForHwnd detour has run %ld time(s); the slot %s the function it was read from",
                g_factoryCalls.load(std::memory_order_relaxed), vt[15] == g_factoryFn ? "still points at" : "no longer points at");
            char owner[64] = "";
            if (!g_factoryInPlace)
                LOG("[creation] the in-place detour never went in, so this mod relies on the slot patch");
            else if (DetourOwner(g_factoryFn, owner, sizeof owner))
                LOG("[creation] the entry it was installed on jumps into %s%s", owner[0] == '?' ? "an allocated stub" : owner,
                    _stricmp(owner, OwnModuleName()) == 0 ? ", so the detour is still first in line"
                                                              : ", so something detoured it after this mod did");
            else
                LOG("[creation] the entry it was installed on is not detoured any more: something put the original bytes back");
            LogEntry("slot 15 CreateSwapChainForHwnd", vt[15]);
            LogEntry("slot 10 CreateSwapChain", vt[10]);
            LogEntry("slot 16 CreateSwapChainForCoreWindow", vt[16]);
            LogEntry("slot 24 CreateSwapChainForComposition", vt[24]);
        }
        else
            LOG("[creation] the factory's CreateSwapChainForHwnd was never looked at, so there is no detour to compare");
        if (HMODULE dxgi = GetModuleHandleW(L"dxgi.dll"))
        {
            LogEntry("CreateDXGIFactory1", reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory1")));
            LogEntry("CreateDXGIFactory2", reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));
        }
        if (HMODULE d3d = GetModuleHandleW(L"d3d12.dll"))
            LogEntry("D3D12CreateDevice", reinterpret_cast<void*>(GetProcAddress(d3d, "D3D12CreateDevice")));
    }

    // Another mod can free a relay page between the readability test and the
    // read, so the whole report runs under a handler. It is a log line, and
    // losing the rest of it is better than taking the game down.
    void LogSwapChainCreationPath()
    {
        __try { ReportCreationPath(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { LOG("[creation] a read faulted part way through, so the lines above are all there is"); }
    }
