// The swapchain wrapper, which draws the menu before frame generation
// interpolates.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // --- COM wrapper: draw the overlay before Streamline interpolates --------
    // Forwards every IDXGISwapChain4 method to the real (Streamline-proxy)
    // swapchain, intercepting Present/Present1 to draw the overlay into the
    // writable game-facing back buffer first, and ResizeBuffers* to rebuild RTVs.
    class WrappedIDXGISwapChain final : public IDXGISwapChain4
    {
    public:
        explicit WrappedIDXGISwapChain(IDXGISwapChain4* inner) : m_inner(inner)
        {
        }

        // IUnknown ----------------------------------------------------------
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            if (!ppv) return E_POINTER;
            if (riid == __uuidof(IUnknown)               ||
                riid == __uuidof(IDXGIObject)            ||
                riid == __uuidof(IDXGIDeviceSubObject)   ||
                riid == __uuidof(IDXGISwapChain)         ||
                riid == __uuidof(IDXGISwapChain1)        ||
                riid == __uuidof(IDXGISwapChain2)        ||
                riid == __uuidof(IDXGISwapChain3)        ||
                riid == __uuidof(IDXGISwapChain4))
            {
                AddRef();
                *ppv = static_cast<IDXGISwapChain4*>(this);
                return S_OK;
            }
            return m_inner->QueryInterface(riid, ppv);
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG r = InterlockedDecrement(&m_ref);
            if (r == 0)
            {
                IDXGISwapChain4* inner = m_inner;
                const bool ours = (g_swapChain == static_cast<IDXGISwapChain3*>(inner));

                // Let go of the back buffers before the swapchain goes.
                //
                // This is the release the wrapper never did. CreateRenderTargets
                // calls GetBuffer for every back buffer and keeps the resources,
                // so while our views exist the swapchain has outstanding
                // references to its own buffers. The game reaching zero on its
                // reference does not end that. Every other place that touches the
                // buffers retires the GPU first and drops the views:
                // ReconcileSwapChain does it, PreResizeCleanup does it. Teardown
                // was the one path that did neither, and teardown is where the
                // game dies when frame generation is toggled.
                //
                // Only when this wrapper owns the chain the views describe. A
                // second wrapper being released must not tear down the live one's
                // resources.
                if (ours)
                {
                    WaitForOverlayIdle();     // our last submit may still be in flight
                    CleanupRenderTargets();
                    ReleaseOffscreenTarget();
                    g_swapChain = nullptr;    // and forget the identity: the
                                              // allocator can hand this address to
                                              // the next chain, and ReconcileSwapChain
                                              // compares by pointer first, so a
                                              // recycled one would read as unchanged.
                }
                delete this;
                inner->Release();
            }
            return r;
        }

        // IDXGIObject -------------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID n, UINT s, const void* d) override { return m_inner->SetPrivateData(n, s, d); }
        HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID n, const IUnknown* p) override { return m_inner->SetPrivateDataInterface(n, p); }
        HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID n, UINT* s, void* d) override { return m_inner->GetPrivateData(n, s, d); }
        HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return m_inner->GetParent(riid, pp); }

        // IDXGIDeviceSubObject ---------------------------------------------
        HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** pp) override { return m_inner->GetDevice(riid, pp); }

        // IDXGISwapChain ----------------------------------------------------
        HRESULT STDMETHODCALLTYPE Present(UINT syncInterval, UINT flags) override
        {
            const bool drew = RenderOverlay(m_inner, true, flags);
            const HRESULT hr = m_inner->Present(syncInterval, flags);
            PostPresentDeviceCheck(drew);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE GetBuffer(UINT i, REFIID riid, void** pp) override { return m_inner->GetBuffer(i, riid, pp); }
        HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fs, IDXGIOutput* t) override { return m_inner->SetFullscreenState(fs, t); }
        HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fs, IDXGIOutput** t) override { return m_inner->GetFullscreenState(fs, t); }
        HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override { return m_inner->GetDesc(d); }
        HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl) override
        {
            PreResizeCleanup();
            const HRESULT hr = m_inner->ResizeBuffers(bc, w, h, f, fl);
            PostResizeRebuild(m_inner);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* p) override { return m_inner->ResizeTarget(p); }
        HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** pp) override { return m_inner->GetContainingOutput(pp); }
        HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* p) override { return m_inner->GetFrameStatistics(p); }
        HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* p) override { return m_inner->GetLastPresentCount(p); }

        // IDXGISwapChain1 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* p) override { return m_inner->GetDesc1(p); }
        HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* p) override { return m_inner->GetFullscreenDesc(p); }
        HRESULT STDMETHODCALLTYPE GetHwnd(HWND* p) override { return m_inner->GetHwnd(p); }
        HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID riid, void** pp) override { return m_inner->GetCoreWindow(riid, pp); }
        HRESULT STDMETHODCALLTYPE Present1(UINT syncInterval, UINT flags, const DXGI_PRESENT_PARAMETERS* pp) override
        {
            const bool drew = RenderOverlay(m_inner, true, flags);
            const HRESULT hr = m_inner->Present1(syncInterval, flags, pp);
            PostPresentDeviceCheck(drew);
            return hr;
        }
        BOOL    STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return m_inner->IsTemporaryMonoSupported(); }
        HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** pp) override { return m_inner->GetRestrictToOutput(pp); }
        HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* p) override { return m_inner->SetBackgroundColor(p); }
        HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* p) override { return m_inner->GetBackgroundColor(p); }
        HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override { return m_inner->SetRotation(r); }
        HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* p) override { return m_inner->GetRotation(p); }

        // IDXGISwapChain2 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w, UINT h) override { return m_inner->SetSourceSize(w, h); }
        HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w, UINT* h) override { return m_inner->GetSourceSize(w, h); }
        HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT m) override { return m_inner->SetMaximumFrameLatency(m); }
        HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* m) override { return m_inner->GetMaximumFrameLatency(m); }
        HANDLE  STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return m_inner->GetFrameLatencyWaitableObject(); }
        HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* p) override { return m_inner->SetMatrixTransform(p); }
        HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* p) override { return m_inner->GetMatrixTransform(p); }

        // IDXGISwapChain3 ---------------------------------------------------
        UINT    STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return m_inner->GetCurrentBackBufferIndex(); }
        HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE c, UINT* s) override { return m_inner->CheckColorSpaceSupport(c, s); }
        HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE c) override
        {
            const HRESULT hr = m_inner->SetColorSpace1(c);
            if (SUCCEEDED(hr)) OnColorSpaceChanged(c);
            return hr;
        }
        HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT bc, UINT w, UINT h, DXGI_FORMAT f, UINT fl, const UINT* nodeMask, IUnknown* const* pQueues) override
        {
            PreResizeCleanup();
            const HRESULT hr = m_inner->ResizeBuffers1(bc, w, h, f, fl, nodeMask, pQueues);
            PostResizeRebuild(m_inner);
            return hr;
        }

        // IDXGISwapChain4 ---------------------------------------------------
        HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE t, UINT s, void* d) override { return m_inner->SetHDRMetaData(t, s, d); }

    private:
        IDXGISwapChain4* m_inner;
        LONG             m_ref = 1;
    };

    // Replace *pp (the real swapchain the game just created) with a wrapper that
    // owns it, so every Present routes through us first. hwnd is the window the
    // swapchain was created for - used to ignore auxiliary windows.
    // A probe is not the game. Crimson Route creates a 100 by 100 chain on a
    // window of its own to read vtables, OptiScaler does the same, and 1.6.5
    // took the first chain it saw as the game's: detours were read off it,
    // the queue was pinned to it, the main window was locked to Route's, and
    // Route was handed our wrapper back as its own probe. The game's real
    // chains then arrived on another window and were refused as auxiliary.
    // Nothing drew, Route saw no presents, and the menu key had nowhere to be
    // read. LuxDragon's 1.6.6 log, issue #47. The old guard let exactly 100
    // through. Nothing playable is under 256 on a side.
    static bool IsProbeChain(UINT w, UINT h)
    {
        return w < 256 || h < 256;
    }

    static void WrapSwapChain(IDXGISwapChain1** pp, HWND hwnd)
    {
        if (!pp || !*pp) return;

        // Skip tiny probe/overlay swapchains (matches OptiScaler) - never wrap our
        // own or a capability-probe surface.
        DXGI_SWAP_CHAIN_DESC1 d1 = {};
        if (SUCCEEDED((*pp)->GetDesc1(&d1)) && IsProbeChain(d1.Width, d1.Height))
            return;

        // Once we have locked onto the game's main window, ignore swapchains on any
        // other window: those are auxiliary surfaces (the engine/Streamline create
        // them) and wrapping them buys nothing while risking a fight with SL's own
        // bookkeeping.
        if (g_wrappedHwnd && hwnd && hwnd != g_wrappedHwnd)
            return;

        IDXGISwapChain4* inner = nullptr;
        if (FAILED((*pp)->QueryInterface(IID_PPV_ARGS(&inner))) || !inner)
            return; // need full IDXGISwapChain4 to wrap safely; else leave native

        // Transfer the caller's ref to the wrapper: QI added a ref (inner), drop
        // the raw ref, hand back the wrapper (ref=1) which owns inner.
        (*pp)->Release();
        auto* wrapper = new WrappedIDXGISwapChain(inner);
        *pp = static_cast<IDXGISwapChain1*>(wrapper);

        if (hwnd) g_wrappedHwnd = hwnd;

        // Log the layer on EVERY wrap - the pre-FG chain and the FG-active re-wrap
        // can be different objects. inner->Present in sl.*/nvngx/amd*/xess = a FG
        // proxy (correct, pre-interpolation, writable). dxgi.dll = native chain.
        // BufferCount jumping (e.g. to 6) + Flags reveal when MFG has taken over.
        void** innerVt = *reinterpret_cast<void***>(inner);
        LogHookedModule("Wrapped swapchain Present", innerVt[8]);

        if (!g_wrapperActive)
        {
            g_wrapperActive = true;
            LOG_OK("Swapchain wrapped - overlay composites before Frame Generation (%ux%u, %u buffers, flags 0x%X).",
                   d1.Width, d1.Height, d1.BufferCount, d1.Flags);
        }
        else
        {
            // A later recreation (FG toggle, resolution/settings change): expected.
            LOG("Swapchain re-wrapped after recreation (%ux%u, %u buffers, flags 0x%X).",
                d1.Width, d1.Height, d1.BufferCount, d1.Flags);
        }
    }
