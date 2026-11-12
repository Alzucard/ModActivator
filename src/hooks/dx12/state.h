// Shared state: the original functions, frame generation bookkeeping, the
// render resources and the HDR composite mode.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // --- Original function pointers -----------------------------------------
    using Present_t         = HRESULT (WINAPI*)(IDXGISwapChain3*, UINT, UINT);
    using ResizeBuffers_t   = HRESULT (WINAPI*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ExecuteCmdLists_t = void    (WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
    using SetColorSpace1_t  = HRESULT (WINAPI*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);

    static Present_t         oPresent             = nullptr;
    static ResizeBuffers_t   oResizeBuffers       = nullptr;
    static ExecuteCmdLists_t oExecuteCommandLists = nullptr;
    static SetColorSpace1_t  oSetColorSpace1      = nullptr;

    // --- Frame Generation (DLSS-G) support ----------------------------------
    // To draw over DLSS Frame Generation we must render on the swapchain the
    // GAME presents to - the writable, pre-interpolation one - NOT the native
    // swapchain Streamline pushes finished frames through (its buffers are
    // read-only to us => DXGI_ERROR_ACCESS_DENIED, device removed).
    //
    // The proven approach (OptiScaler / ReShade): WRAP, don't detour. We return a
    // WrappedIDXGISwapChain to the game that forwards every method to the real
    // (Streamline-proxy) swapchain but intercepts Present/Present1 to draw the
    // overlay first. The wrapper is handed out by a VirtualProtect vtable-slot
    // patch of the factory's CreateSwapChain*/CreateSwapChainForHwnd - NOT a
    // MinHook detour of the DXGI exports (that deadlocked startup and fought
    // Streamline's own interposer). See InstallSwapChainCreationPatch.
    using CSFH_t = HRESULT (STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

    static CSFH_t oFactoryCreateSwapChainForHwnd = nullptr;
    // MinHook's own trampoline for the in-place detour, kept when the
    // original above is moved onto another mod's detour. A nested entry
    // always takes this one: if that mod's trampoline leads back here, going
    // through it again would loop until the stack ran out.
    static CSFH_t g_factoryOwnTramp = nullptr;
    // What the creation report compares against: the factory vtable the
    // detour was installed from, the function in slot 15 at the time, and how
    // many times the detour has run.
    static void** g_factoryVt = nullptr;
    static void*  g_factoryFn = nullptr;
    static bool   g_factoryInPlace = false;
    static std::atomic<long> g_factoryCalls{0};

    // Set once a swapchain has been wrapped: the wrapper now does all overlay
    // drawing, so the native Present byte-hook must stop drawing (its buffers are
    // Streamline's read-only finished frames). Never cleared for the session.
    static bool g_wrapperActive = false;

    // Frames that reached this mod through any path: the wrapper's Present, the
    // stacked detours, either flavour. Zero when the game has been running for
    // a while means this mod has no way onto the screen, which OverlayBlindWatch
    // acts on rather than leaving the player with a menu key that does nothing.
    static std::atomic<unsigned> g_presentSeen{0};

    // Reentrancy guard for swapchain creation. When the game calls our patched
    // CreateSwapChainForHwnd, the real implementation (Streamline's interposer)
    // can itself create swapchains through the SAME class vtable slot we patched -
    // and it does exactly this every time Frame Generation is toggled, since SL is
    // spec-required to tear down and recreate the swapchain on an FG on/off. Only
    // the OUTERMOST call (the one the game made) may be wrapped; nested internal
    // ones are Streamline's own plumbing and must pass straight through, or we
    // recurse into the interposer mid-(re)creation and hang / remove the device.
    // This is the crash that appears "after a couple loads": the first swapchain
    // wraps cleanly, then an FG toggle re-enters us during SL's rebuild.
    static thread_local bool t_inSwapChainCreate = false;

    // Resets the guard however the call leaves, including through an exception
    // out of the interposer. A guard left set makes every later creation look
    // nested, so nothing is ever wrapped or re-pinned again for that thread.
    struct CreateGuard
    {
        bool wasNested;
        CreateGuard() : wasNested(t_inSwapChainCreate) { t_inSwapChainCreate = true; }
        ~CreateGuard() { if (!wasNested) t_inSwapChainCreate = false; }
    };

    // The window our wrapped (main) swapchain belongs to. The engine and Streamline
    // can spin up auxiliary swapchains on other windows; we only ever want the one
    // the game renders the world into. Locked on the first successful wrap so later
    // auxiliary creations are ignored.
    static HWND g_wrappedHwnd = nullptr;

    // The window the game's first full-size swapchain was created for, set on
    // both paths. g_wrappedHwnd is only ever set by WrapSwapChain, so on the
    // stacked path it stays null and nothing knows which window is the game's.
    static HWND g_gameHwnd = nullptr;

    // --- Rendering resources ------------------------------------------------
    struct FrameContext
    {
        ID3D12CommandAllocator*     commandAllocator = nullptr;
        ID3D12Resource*             renderTarget     = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle        = {};
        UINT64                      fenceValue       = 0; // GPU signal for this frame's overlay work
    };

    static ID3D12Device*              g_device      = nullptr;
    static ID3D12DescriptorHeap*      g_rtvHeap     = nullptr;
    static ID3D12DescriptorHeap*      g_srvHeap     = nullptr;
    static ID3D12GraphicsCommandList* g_commandList = nullptr;
    // SRV heap capacity: slot 0 the ImGui font, slot 1 the offscreen target
    // the composite pass samples. The rest is spare.
    static constexpr UINT             kSrvHeapSlots = 8;
    // The most-recent DIRECT queue confirmed to live on the swapchain's device
    // (i.e. the present queue). Published ONLY from hkExecuteCommandLists while
    // the queue argument is guaranteed alive, and AddRef'd - we never keep
    // un-refcounted queue pointers (the engine destroys temporary queues during
    // loading; a cached raw pointer is a use-after-free).
    static ID3D12CommandQueue*        g_presentQueue   = nullptr; // guarded by g_queueLock, AddRef'd
    static CRITICAL_SECTION           g_queueLock;
    static bool                       g_queueLockReady = false;
    // Set once we have the AUTHORITATIVE present queue - the one passed to
    // CreateSwapChainForHwnd, which for D3D12 IS the swapchain's present queue and
    // owns the back buffers. Once pinned, the ExecuteCommandLists heuristic must not
    // replace it: under Multi Frame Generation the busiest DIRECT queue there is
    // Streamline's present pacer, and submitting our overlay on the pacer queue is
    // rejected (ACCESS_DENIED -> device removed). This was the instant-crash cause.
    static bool                       g_presentQueuePinned = false;
    static ID3D12Fence*               g_fence          = nullptr;
    static HANDLE                     g_fenceEvent     = nullptr;
    // DrawOverlay's own, for waiting on one back buffer's work. Not shared
    // with g_fenceEvent, which WaitForOverlayIdle sets from other threads:
    // one auto-reset event for two waiters can wake the wrong one.
    static HANDLE                     g_frameFenceEvent = nullptr;
    static UINT64                     g_fenceValue     = 0;
    static std::vector<FrameContext>  g_frames;
    static UINT                       g_bufferCount    = 0;
    // The swapchain our render targets currently describe. Compared by identity
    // each Present so we can rebuild when Streamline (DLSS Frame Generation)
    // silently swaps the native swapchain out from under us - see
    // ReconcileSwapChain. Raw pointer: used ONLY for the identity compare,
    // never dereferenced when stale.
    static IDXGISwapChain3*           g_swapChain      = nullptr;
    static HWND                       g_hwnd           = nullptr;
    // Full back-buffer signature our render targets currently describe. A video
    // settings change (resolution, HDR) resizes the buffers IN PLACE - same
    // swapchain pointer, often the same count - so pointer+count alone can't tell
    // us the buffers moved. Track width/height/format too and rebuild on any
    // change, or our RTVs keep pointing at freed buffers => ACCESS_DENIED.
    static UINT                       g_scWidth        = 0;
    static UINT                       g_scHeight       = 0;
    static DXGI_FORMAT                g_scFormat       = DXGI_FORMAT_UNKNOWN;
    static bool                       g_imguiReady     = false;
    static bool                       g_initFailed     = false;
    static bool                       g_renderDisabled = false;

    // --- HDR-aware overlay compositing ---------------------------------------
    // ImGui always renders into an SDR (R8G8B8A8_UNORM, plain sRGB-gamma
    // numeric) offscreen target - decoupled from whatever pixel format/color
    // space the real back buffer is actually in. A small composite pass then
    // re-encodes that image into the back buffer's ACTUAL color space (scRGB
    // linear or HDR10 PQ) and alpha-blends it in. Without this an HDR back
    // buffer reads our plain 0-1 UI colors as if they were scene-referred /
    // PQ-encoded values directly, and the menu comes out blown-out and
    // oversaturated - the bug this whole section exists to fix.
    static ID3D12Resource*             g_offscreenTex     = nullptr;
    static ID3D12DescriptorHeap*       g_offscreenRtvHeap = nullptr;
    static D3D12_CPU_DESCRIPTOR_HANDLE g_offscreenRtv     = {};
    static UINT                        g_offscreenW       = 0;
    static UINT                        g_offscreenH       = 0;

    // Fixed SRV heap slot for the offscreen texture, right after the ImGui font.
    static constexpr UINT kOffscreenSrvSlot = 1;

    static ID3D12RootSignature* g_compositeRootSig = nullptr;
    static ID3D12PipelineState* g_compositePSO     = nullptr;
    static DXGI_FORMAT          g_compositeFormat  = DXGI_FORMAT_UNKNOWN;

    // SDR reference white targeted inside the HDR range (ITU-R BT.2408's
    // recommended "graphics white" for HDR overlays/subtitles). Not the peak
    // brightness - just how bright plain white UI text reads next to the
    // game's own HDR highlights.
    static constexpr float kPaperWhiteNits = 203.0f;

    // The color space the REAL swap chain is currently presenting in. DXGI has
    // no "get current color space" query - the only way to know it is to watch
    // every IDXGISwapChain3::SetColorSpace1 call the engine makes (native path
    // hkSetColorSpace1, wrapped path WrappedIDXGISwapChain::SetColorSpace1).
    // Defaults to plain SDR so nothing changes for players without HDR enabled,
    // or before the engine has told us otherwise.
    static DXGI_COLOR_SPACE_TYPE g_colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    // 0 = SDR passthrough (byte-identical to drawing straight on the back
    // buffer), 1 = scRGB linear, 2 = HDR10 / ST.2084 PQ. Any other color space
    // (e.g. wide-gamut SDR) falls back to passthrough rather than guessing.
    static UINT CompositeModeForColorSpace(DXGI_COLOR_SPACE_TYPE cs)
    {
        switch (cs)
        {
            case DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709:    return 1;
            case DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020: return 2;
            default:                                         return 0;
        }
    }

    static void OnColorSpaceChanged(DXGI_COLOR_SPACE_TYPE cs)
    {
        if (cs == g_colorSpace) return;
        g_colorSpace = cs;
        const char* name = CompositeModeForColorSpace(cs) == 2 ? "HDR10 (PQ)"
                          : CompositeModeForColorSpace(cs) == 1 ? "scRGB (linear)"
                          : "SDR";
        LOG("Swapchain color space changed to %s (%d) - overlay compositing %s.",
            name, static_cast<int>(cs),
            CompositeModeForColorSpace(cs) != 0 ? "HDR-adjusted" : "unadjusted");
    }
