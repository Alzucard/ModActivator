// Installing the detours from the game's own swapchain, and the swapchain
// creation hook that decides between wrapping and stacking.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // The detour targets, read off the game's own objects. Slot numbers are the
    // interface layout, which is fixed: Present is IDXGISwapChain[8], Present1
    // [22], ResizeBuffers [13], SetColorSpace1 is IDXGISwapChain3[38],
    // ResizeBuffers1 [39], and ExecuteCommandLists is ID3D12CommandQueue[10].
    //
    // They are cached because a later pass may fill in what the first one
    // refused. What is cached is six function addresses inside modules, never a
    // swapchain pointer: holding a reference to the chain is what kept it from
    // dying in issue #30, and a raw one would dangle the moment the game
    // replaced it.
    struct HookSlot
    {
        const char* name;
        void*       addr;
        void*       detour;
        void**      orig;
        bool        mayStack;   // another mod's jump may be stacked on
        bool        mayProxy;   // a game-folder proxy may be stacked on as a last resort
        bool        draws;      // installing this one gives the overlay a way in
        bool        installed;
    };
    static HookSlot g_targets[6] = {};
    static bool g_targetsRead = false;
    static bool g_proxyPassDone = false;

    static bool AnyDrawPathHooked()
    {
        for (const HookSlot& t : g_targets)
            if (t.installed && t.draws) return true;
        return false;
    }

    // One pass over whatever is still uninstalled. Returns how many it added.
    static int InstallPass(bool stack, bool allowProxy, bool wrapperWillDraw = true)
    {
        int added = 0, skipped = 0;
        for (HookSlot& t : g_targets)
        {
            if (t.installed || !t.addr) { if (!t.installed) ++skipped; continue; }
            if (!LogHookTarget(t.name, t.addr, stack && t.mayStack, allowProxy && t.mayProxy)) { ++skipped; continue; }
            if (MH_CreateHook(t.addr, t.detour, t.orig) != MH_OK) { LOG_ERR("MH_CreateHook failed for %s.", t.name); ++skipped; continue; }
            t.installed = true;
            ++added;
        }
        if (added && MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
            LOG_ERR("MH_EnableHook failed; the DirectX detours are not active.");
        if (skipped && !allowProxy && wrapperWillDraw)
            LOG("%d of 6 DirectX functions were left to whoever hooked them first. The overlay comes from the wrapped swapchain instead; if that does not happen it will not draw, which is better than two mods fighting over one function.", skipped);
        else if (skipped && !allowProxy)
            LOG("%d of 6 DirectX functions belong to something else and the wrapper is off.", skipped);
        return added;
    }

    // Nothing is drawing and nothing is hooked that could: try again with a
    // game-folder proxy allowed. Says what it is doing either way, because a
    // silent overlay with no explanation is the report this exists to prevent.
    static void EscalateToProxy(const char* why)
    {
        if (g_proxyPassDone || !g_targetsRead) return;
        g_proxyPassDone = true;
        LOG("[overlay] %s. Trying again with a present call in another mod's proxy allowed.", why);
        if (InstallPass(true, true) == 0)
            LOG_ERR("[overlay] there is still nothing this mod can hook to draw the menu. The [hook] lines above name who owns each call. "
                    "Other mods are unaffected; the menu needs one of those mods out of the folder, or WrapSwapChain=1 with no swapchain-keying overlay loaded.");
    }

    // Started once, when the targets are first read. Every path that can draw
    // counts a frame, so a count still at zero means no path exists, whatever
    // the reason: the hooks were all refused, or the wrapper was expected and
    // never happened because the chain failed its interface check or arrived on
    // another window. A machine can be slow to its first frame (twenty seconds
    // on a 5060 Ti), so this waits and then acts on the count rather than on
    // the clock alone.
    static DWORD WINAPI OverlayBlindWatch(LPVOID)
    {
        for (int i = 0; i < 90 && g_presentSeen.load(std::memory_order_relaxed) == 0; ++i)
            Sleep(200);
        if (g_presentSeen.load(std::memory_order_relaxed) == 0 && !g_wrapperActive && !AnyDrawPathHooked())
            EscalateToProxy("eighteen seconds and no frame has reached this mod, with nothing hooked that could bring one");
        return 0;
    }

    static void InstallDetoursFrom(IDXGISwapChain1* chain, IUnknown* queueUnk, bool stack)
    {
        if (g_targetsRead || !chain) return;
        g_targetsRead = true;

        void** scVt = *reinterpret_cast<void***>(chain);
        void* presentAddr  = scVt[8];
        void* present1Addr = scVt[22];
        void* resizeAddr   = scVt[13];
        void* colorSpaceAddr = nullptr;
        void* resize1Addr    = nullptr;
        IDXGISwapChain3* sc3 = nullptr;
        if (SUCCEEDED(chain->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3)
        {
            colorSpaceAddr = (*reinterpret_cast<void***>(sc3))[38];
            resize1Addr    = (*reinterpret_cast<void***>(sc3))[39];
            sc3->Release();
        }
        void* execAddr = nullptr;
        ID3D12CommandQueue* q = nullptr;
        if (queueUnk && SUCCEEDED(queueUnk->QueryInterface(IID_PPV_ARGS(&q))) && q)
        {
            execAddr = (*reinterpret_cast<void***>(q))[10];
            q->Release();
        }

        // ExecuteCommandLists is never stacked and never taken from a proxy:
        // the queue comes from the creation pin, Trinity sits on that function,
        // and a proxy's copy of it was the hang in issue #34.
        const HookSlot targets[6] = {
            { "Present",             presentAddr,    reinterpret_cast<void*>(&hkPresent),             reinterpret_cast<void**>(&oPresent),             true,  true,  true,  false },
            { "Present1",            present1Addr,   reinterpret_cast<void*>(&hkPresent1),            reinterpret_cast<void**>(&oPresent1),            true,  true,  true,  false },
            { "ResizeBuffers",       resizeAddr,     reinterpret_cast<void*>(&hkResizeBuffers),       reinterpret_cast<void**>(&oResizeBuffers),       true,  true,  false, false },
            { "ResizeBuffers1",      resize1Addr,    reinterpret_cast<void*>(&hkResizeBuffers1),      reinterpret_cast<void**>(&oResizeBuffers1),      true,  true,  false, false },
            { "ExecuteCommandLists", execAddr,       reinterpret_cast<void*>(&hkExecuteCommandLists), reinterpret_cast<void**>(&oExecuteCommandLists), false, false, false, false },
            { "SetColorSpace1",      colorSpaceAddr, reinterpret_cast<void*>(&hkSetColorSpace1),      reinterpret_cast<void**>(&oSetColorSpace1),      true,  true,  false, false },
        };
        for (int i = 0; i < 6; ++i) g_targets[i] = targets[i];

        // Whether anything at all can draw is knowable here, without waiting:
        // the wrapper is off when a keying overlay is loaded or the ini says
        // so, and the present calls are about to be refused or taken.
        const bool willWrap = !stack && WrapWanted();
        InstallPass(stack, false, willWrap);

        if (!willWrap && !AnyDrawPathHooked())
            EscalateToProxy("every present call belongs to another mod's proxy in the game folder and the swapchain wrapper is off, so nothing can draw the menu");

        CloseHandle(CreateThread(nullptr, 0, &OverlayBlindWatch, nullptr, 0, nullptr));
    }

    static HRESULT STDMETHODCALLTYPE hkFactoryCreateSwapChainForHwnd(
        IDXGIFactory2* self, IUnknown* device, HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fsDesc,
        IDXGIOutput* restrictOut, IDXGISwapChain1** ppSwapChain)
    {
        // Only the outermost (game-issued) creation is wrapped. If we are already
        // inside a creation on this thread, this is Streamline recreating the chain
        // through the same patched slot during an FG toggle - let it complete
        // untouched, otherwise we recurse into the interposer mid-rebuild.
        CreateGuard guard;
        g_factoryCalls.fetch_add(1, std::memory_order_relaxed);

        // Without a wrapper, nothing lets go of the back buffers when the game
        // replaces its swapchain. The wrapper's destructor does that on the
        // wrapped path, at the game's last Release. On the stacked path the
        // views stayed on the old chain's buffers, so the chain could not
        // finish dying, and the replacement the game then asked for on the
        // same window failed, which is the null pointer the game reads when
        // frame generation or a display setting is applied. The first hint
        // that this creation is a replacement is this call, so the release
        // has to happen here, before the original runs. A full-size chain on
        // another window is another mod's, and is left alone.
        if (!guard.wasNested && !g_wrapperActive && g_imguiReady && g_swapChain &&
            desc && !IsProbeChain(desc->Width, desc->Height) &&
            (!g_gameHwnd || hwnd == g_gameHwnd))
        {
            LOG("[hook] the game is replacing its %ux%u swapchain with a %ux%u one and there is no wrapper to let go of the old buffers: releasing them before the new chain is made",
                g_scWidth, g_scHeight, desc->Width, desc->Height);
            WaitForOverlayIdle();
            CleanupRenderTargets();
            ReleaseOffscreenTarget();
            g_swapChain = nullptr;
        }

        const CSFH_t next = guard.wasNested && g_factoryOwnTramp ? g_factoryOwnTramp : oFactoryCreateSwapChainForHwnd;
        const HRESULT hr = next(self, device, hwnd, desc, fsDesc, restrictOut, ppSwapChain);
        if (!guard.wasNested && FAILED(hr))
            LOG_ERR("[hook] CreateSwapChainForHwnd failed (0x%08X) for a %ux%u chain on window %p; if the game dies next, this is why",
                    static_cast<unsigned>(hr), desc ? desc->Width : 0u, desc ? desc->Height : 0u, static_cast<void*>(hwnd));
        if (guard.wasNested || FAILED(hr))
            return hr;

        // The game's real chain and queue, the first time they exist. Tiny
        // chains are probes and not the game's window; skip those the way
        // WrapSwapChain does.
        if (desc && IsProbeChain(desc->Width, desc->Height))
        {
            LOG("[hook] a %ux%u swapchain on window %p (%s) is another mod's probe, not the game's: no detours, no queue, no wrapper",
                desc->Width, desc->Height, static_cast<void*>(hwnd), hwnd && IsWindowVisible(hwnd) ? "visible" : "hidden");
            return hr;
        }
        if (!g_gameHwnd && hwnd) g_gameHwnd = hwnd;
        // Who owns Present on the chain the game is about to use, decided once.
        // Steam's overlay and the frame generation interposer coexist with the
        // wrapper. Anything else that detoured Present first is assumed to key
        // its own work on the object it is handed, which Crimson Route does:
        // above this mod at the factory it keyed the wrapper, met the real
        // chain in its detour, and drew nothing. For such a mod the wrapper is
        // skipped and this mod's detours stack on top of its instead, ours
        // first, theirs next, dxgi last. The overlay then draws where it drew
        // before the wrapper existed, after frame generation. Issue #47.
        static bool s_decided = false, s_stack = false;
        if (!s_decided && ppSwapChain && *ppSwapChain)
        {
            s_decided = true;
            void** vt = *reinterpret_cast<void***>(*ppSwapChain);
            char owner[64] = "";
            if (DetourOwner(vt[8], owner, sizeof owner) && !BenignPresentOwner(owner))
            {
                // Whether that mod minds being handed the wrapper is a guess
                // drawn from Crimson Route, which does. WrapSwapChain=1 asks
                // for the wrapper outright, so it wins over the guess; the
                // mods known to mind are still checked by name below.
                if (Settings::Get().wrapSwapChain == 1 && !KeyingOverlayLoaded())
                    LOG("[hook] %s detoured Present before this mod, which would normally mean stacking instead of wrapping. "
                        "WrapSwapChain=1 asks for the wrapper, so it is used anyway. If %s stops working, set WrapSwapChain back to auto.",
                        owner, owner);
                else
                {
                    s_stack = true;
                    LOG("[hook] %s detoured Present before this mod and would be handed the wrapper: not wrapping; stacking this mod's detours on top of its instead%s",
                        owner, Settings::Get().wrapSwapChain == 1 ? "" : ". WrapSwapChain=1 would wrap anyway");
                }
            }
            else if (const char* keying = KeyingOverlayLoaded())
            {
                s_stack = true;
                LOG("[hook] %s is loaded and keys its work on the swapchain object it is handed: not wrapping; stacking this mod's detours instead", keying);
            }
            // Stacking skips WrapWanted, so its line never comes. What the
            // frame generation fix would have done is said here instead.
            if (s_stack)
                LOG("[hook] so WrapSwapChain does not apply: the menu draws on the finished frames%s.",
                    GetModuleHandleA("sl.interposer.dll")
                        ? ", and Streamline (DLSS frame generation) is loaded. Should the menu not show with frame generation on, that is why"
                        : "; Streamline (DLSS frame generation) is not loaded");
        }
        if (ppSwapChain && *ppSwapChain)
            InstallDetoursFrom(*ppSwapChain, device, s_stack);

        // Replacements are wrapped, the same as the first one. What once
        // killed the game on a frame generation toggle was not the proxy but
        // render target views still holding the dying chain's back buffers;
        // the wrapper's Release drops those now.
        //
        // A full-size chain on some other window, once the game's is known, is
        // another mod's surface: no wrapper and no queue pin, since submitting
        // on that mod's queue is not ours to do. Said once per window.
        if (g_wrappedHwnd && hwnd && hwnd != g_wrappedHwnd)
        {
            static HWND s_said = nullptr;
            if (s_said != hwnd)
            {
                s_said = hwnd;
                LOG("[hook] a %ux%u swapchain on window %p, which is not the game's %p: another mod's surface, left alone",
                    desc ? desc->Width : 0u, desc ? desc->Height : 0u, static_cast<void*>(hwnd), static_cast<void*>(g_wrappedHwnd));
            }
            return hr;
        }
        if (g_wrapperActive)
            LOG("Swapchain replaced; wrapping the new one.");

        // For D3D12 the first argument is the swapchain's present command
        // queue - the queue that owns the back buffers. Pin it as the queue
        // we submit overlay work on, so we never submit on Streamline's MFG
        // pacer queue (which is rejected). Re-pins on each recreation too.
        ID3D12CommandQueue* pq = nullptr;
        if (device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&pq))) && pq)
        {
            PublishPresentQueue(pq, true);
            pq->Release();
        }
        if (!s_stack && WrapWanted())
        {
            WrapSwapChain(ppSwapChain, hwnd);
            // InstallPass left the present calls other mods own alone because
            // the wrapper was to draw. When it declined the game's chain
            // instead, nothing would: take the present calls after all.
            if (!g_wrapperActive && !AnyDrawPathHooked())
                EscalateToProxy("the swapchain wrapper declined the game's swapchain and no present call is hooked, so nothing can draw the menu");
        }
        return hr;
    }
