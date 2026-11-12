// The present paths: device-removal reports, ReShade's add-on events, and the
// Present, ResizeBuffers and SetColorSpace1 detours.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // On a device removal, dump DRED (Device Removed Extended Data): the GPU
    // breadcrumb trail (which queue/command list was mid-execution) and the page
    // fault (the faulting virtual address + the resource that owned it, if named).
    // This is what turns an opaque ACCESS_DENIED under DLSS-G Frame Generation into
    // "OUR command list faulted writing back buffer X" vs "a Streamline-owned
    // resource" - the difference decides the real fix. Requires DRED to have been
    // armed before device creation (EnableDredIfAvailable, from InstallDX12Hooks).
    static void DumpDred()
    {
        if (!g_device) return;

        ID3D12DeviceRemovedExtendedData* dred = nullptr;
        if (FAILED(g_device->QueryInterface(IID_PPV_ARGS(&dred))) || !dred)
        {
            LOG_ERR("DRED: interface unavailable on this device - fault cause not captured.");
            return;
        }

        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT bc = {};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&bc)) && bc.pHeadAutoBreadcrumbNode)
        {
            int n = 0;
            for (const D3D12_AUTO_BREADCRUMB_NODE* node = bc.pHeadAutoBreadcrumbNode;
                 node && n < 8; node = node->pNext, ++n)
            {
                const UINT last = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0;
                LOG_ERR("DRED breadcrumb[%d]: queue='%s' list='%s' completed %u/%u ops",
                        n,
                        node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "?",
                        node->pCommandListDebugNameA  ? node->pCommandListDebugNameA  : "?",
                        last, node->BreadcrumbCount);
            }
        }
        else
        {
            // No GPU breadcrumbs => the removal was NOT a GPU-side command failure.
            // Points at a DXGI/driver-level rejection (e.g. writing a read-only
            // shared back buffer during an FG swapchain reconfigure), not our submit.
            LOG_ERR("DRED: no GPU breadcrumbs - removal was not a GPU command fault.");
        }

        D3D12_DRED_PAGE_FAULT_OUTPUT pf = {};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&pf)) && pf.PageFaultVA)
        {
            LOG_ERR("DRED page fault VA=0x%llx", static_cast<unsigned long long>(pf.PageFaultVA));
            for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadExistingAllocationNode; a; a = a->pNext)
                LOG_ERR("  live alloc at fault: '%s'", a->ObjectNameA ? a->ObjectNameA : "?");
            for (const D3D12_DRED_ALLOCATION_NODE* a = pf.pHeadRecentFreedAllocationNode; a; a = a->pNext)
                LOG_ERR("  recently-freed alloc: '%s'", a->ObjectNameA ? a->ObjectNameA : "?");
        }
        else
        {
            LOG_ERR("DRED: no page fault recorded - not a bad-address access.");
        }

        dred->Release();
    }

    // Fail safe: if our overlay work removed the device, stop rendering instead
    // of spamming a dead GPU.
    static void PostPresentDeviceCheck(bool drew)
    {
        if (drew && g_device)
        {
            const HRESULT removed = g_device->GetDeviceRemovedReason();
            if (removed != S_OK)
            {
                LOG_ERR("Device removed (0x%08X) after overlay submit - overlay disabled.", removed);
                DumpDred();
                g_renderDisabled = true;
            }
        }
    }

    // Shared ResizeBuffers handling: retire our work + drop views, let the real
    // resize happen (caller), then rebuild from the post-resize truth.
    static void PreResizeCleanup()
    {
        if (!g_imguiReady) return;
        WaitForOverlayIdle();
        CleanupRenderTargets();
    }
    // The views were dropped before the resize. They are not rebuilt here,
    // inside the game's own ResizeBuffers call and outside the overlay's
    // exception guard, where a failed rebuild used to write through a null
    // RTV heap. Forgetting the chain makes the next drawn frame's
    // ReconcileSwapChain rebuild everything, with every step checked. Until
    // the menu is next drawn no back buffer is referenced at all.
    static void PostResizeRebuild(IDXGISwapChain3*)
    {
        if (!g_imguiReady) return;
        g_swapChain = nullptr;
    }

    // Restrict drawing to the actual game window, including when other
    // D3D12 windows are being presented by other add-ons.
    static bool IsGameWindow(HWND window)
    {
        return window && (window == g_hwnd ||
                          GetAncestor(window, GA_ROOT) == GetForegroundWindow());
    }

    // ReShade's "present" event. It fires before ReShade renders its effects,
    // so nothing is drawn here (issue #3). It is the one place on the ReShade
    // path that sees the api::swapchain, and with it the colour space the game
    // set: there is no SetColorSpace1 detour or swapchain wrapper on this path
    // to report it, so without this the menu was composited as SDR into HDR
    // back buffers (issue #2).
    static void OnReShadePresent(reshade_api::DeviceObject *,
                                 reshade_api::Swapchain *swapChain,
                                 const RECT *, const RECT *, uint32_t, const RECT *)
    {
        if (!swapChain)
            return;
        const auto *device = swapChain->get_device();
        if (!device || device->get_api() != reshade_api::d3d12)
            return;
        if (!IsGameWindow(static_cast<HWND>(swapChain->get_hwnd())))
            return;
        switch (swapChain->get_color_space())
        {
            case reshade_api::colorSpaceSrgb:
                OnColorSpaceChanged(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
                break;
            case reshade_api::colorSpaceScrgb:
                OnColorSpaceChanged(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
                break;
            case reshade_api::colorSpaceHdr10Pq:
                OnColorSpaceChanged(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                break;
            default:
                // Unknown, or HLG, which the composite pass has no mode for.
                break;
        }
    }

    // ReShade's "reshade_present" event: "Called after ReShade has rendered
    // its overlay", so the menu goes on top of the effects and ReShade's own
    // overlay rather than under them.
    static void OnReShadeEffectsPresent(reshade_api::EffectRuntime *runtime)
    {
        if (!runtime)
            return;
        const auto *device = runtime->get_device();
        if (!device || device->get_api() != reshade_api::d3d12)
            return;
        reshade_api::CommandQueue *queue = runtime->get_command_queue();
        if (!queue)
            return;
        auto *nativeQueue = reinterpret_cast<ID3D12CommandQueue *>(queue->get_native());
        auto *nativeSwapChain = reinterpret_cast<IDXGISwapChain *>(runtime->get_native());
        IDXGISwapChain3 *chain = nullptr;
        if (!nativeQueue || !nativeSwapChain ||
            FAILED(nativeSwapChain->QueryInterface(IID_PPV_ARGS(&chain))))
            return;

        DXGI_SWAP_CHAIN_DESC desc = {};
        if (SUCCEEDED(chain->GetDesc(&desc)) && IsGameWindow(desc.OutputWindow))
        {
            // On D3D12 ReShade records its effects and overlay into the
            // queue's immediate command list and only submits it after this
            // event returns. Submit it now, or the menu would still execute
            // on the GPU before the effects.
            queue->flush_immediate_command_list();
            const bool drew = RenderOverlay(chain, true, 0, nativeQueue);
            PostPresentDeviceCheck(drew);
        }
        else
        {
            CountNotGameWindow();
        }
        chain->Release();
    }

    static void OnReShadeDestroySwapChain(reshade_api::DeviceObject *swapChain, bool resize)
    {
        if (!g_imguiReady || !g_swapChain || !swapChain)
            return;
        const auto *device = swapChain->get_device();
        if (!device || device->get_api() != reshade_api::d3d12)
            return;
        auto *native = reinterpret_cast<IDXGISwapChain *>(swapChain->get_native());
        IDXGISwapChain3 *chain = nullptr;
        if (!native || FAILED(native->QueryInterface(IID_PPV_ARGS(&chain))))
            return;
        const bool ours = chain == g_swapChain;
        chain->Release();
        if (!ours)
            return;

        PreResizeCleanup();
        g_swapChain = nullptr; // Reconcile on the next real presentation.
        if (!resize)
        {
            EnterCriticalSection(&g_queueLock);
            if (g_presentQueue)
                g_presentQueue->Release();
            g_presentQueue = nullptr;
            g_presentQueuePinned = false;
            LeaveCriticalSection(&g_queueLock);
        }
    }

    // Native-swapchain Present/ResizeBuffers byte-detours (from the dummy vtable).
    // These are the drawing path ONLY when no wrapper is active (wrapping failed,
    // or a legacy no-proxy path). Once a wrapper owns drawing they forward
    // untouched - under DLSS-G the native present is Streamline's read-only frame.
    // Whether a chain reaching the stacked Present detours is the game's.
    // Those detours sit on the class, so every D3D12 swapchain in the process
    // comes through them, including other overlays' windows. Drawing on such
    // a chain made ReconcileSwapChain rebuild back and forth between the two
    // every frame. The chain already drawn on is the common case and costs a
    // compare; anything else is judged by its window: the one the game made
    // its first full-size chain for, or failing that the foreground window.
    static bool IsProbeChain(UINT w, UINT h);
    static bool IsGameChain(IDXGISwapChain* swapChain)
    {
        if (!swapChain) return false;
        if (g_swapChain && static_cast<IDXGISwapChain*>(g_swapChain) == swapChain) return true;
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow) return false;
        if (IsProbeChain(desc.BufferDesc.Width, desc.BufferDesc.Height)) return false;
        return g_gameHwnd ? desc.OutputWindow == g_gameHwnd : IsGameWindow(desc.OutputWindow);
    }

    static HRESULT WINAPI hkPresent(IDXGISwapChain3* swapChain, UINT syncInterval, UINT flags)
    {
        const bool drew = RenderOverlay(swapChain, !g_wrapperActive && IsGameChain(swapChain), flags);
        const HRESULT hr = oPresent(swapChain, syncInterval, flags);
        PostPresentDeviceCheck(drew);
        return hr;
    }

    typedef HRESULT (WINAPI* Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    static Present1_t oPresent1 = nullptr;
    static HRESULT WINAPI hkPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* pp)
    {
        // Only reached on the stacked path (see the factory hook), where the
        // chain is the game's own IDXGISwapChain4 and the cast holds.
        const bool drew = RenderOverlay(static_cast<IDXGISwapChain3*>(swapChain),
                                        !g_wrapperActive && IsGameChain(swapChain), flags);
        const HRESULT hr = oPresent1(swapChain, syncInterval, flags, pp);
        PostPresentDeviceCheck(drew);
        return hr;
    }
    static HRESULT WINAPI hkResizeBuffers(
        IDXGISwapChain3* swapChain, UINT bufferCount,
        UINT width, UINT height, DXGI_FORMAT format, UINT flags)
    {
        if (!g_imguiReady || g_wrapperActive)
            return oResizeBuffers(swapChain, bufferCount, width, height, format, flags);
        PreResizeCleanup();
        const HRESULT hr = oResizeBuffers(swapChain, bufferCount, width, height, format, flags);
        PostResizeRebuild(swapChain);
        LOG("[hook] ResizeBuffers through the stacked detour: %ux%u, %u buffers, fmt %d, hr 0x%08X; views released before and rebuilt after",
            width, height, bufferCount, static_cast<int>(format), static_cast<unsigned>(hr));
        return hr;
    }

    // The wrapper overrides ResizeBuffers1 as well as ResizeBuffers, and a
    // D3D12 game that passes a queue per buffer reconfigures through this
    // one. The stacked path had only the plain detour, so a reconfigure that
    // kept the size and count left the views on freed buffers.
    typedef HRESULT (WINAPI* ResizeBuffers1_t)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
    static ResizeBuffers1_t oResizeBuffers1 = nullptr;
    static HRESULT WINAPI hkResizeBuffers1(
        IDXGISwapChain3* swapChain, UINT bufferCount,
        UINT width, UINT height, DXGI_FORMAT format, UINT flags,
        const UINT* nodeMask, IUnknown* const* queues)
    {
        if (!g_imguiReady || g_wrapperActive)
            return oResizeBuffers1(swapChain, bufferCount, width, height, format, flags, nodeMask, queues);
        PreResizeCleanup();
        const HRESULT hr = oResizeBuffers1(swapChain, bufferCount, width, height, format, flags, nodeMask, queues);
        PostResizeRebuild(swapChain);
        LOG("[hook] ResizeBuffers1 through the stacked detour: %ux%u, %u buffers, fmt %d, hr 0x%08X; views released before and rebuilt after",
            width, height, bufferCount, static_cast<int>(format), static_cast<unsigned>(hr));
        return hr;
    }

    // Only reachable when wrapping failed (see g_wrapperActive) - the wrapper's
    // own SetColorSpace1 override covers the normal case.
    static HRESULT WINAPI hkSetColorSpace1(IDXGISwapChain3* swapChain, DXGI_COLOR_SPACE_TYPE colorSpace)
    {
        const HRESULT hr = oSetColorSpace1(swapChain, colorSpace);
        if (SUCCEEDED(hr)) OnColorSpaceChanged(colorSpace);
        return hr;
    }
