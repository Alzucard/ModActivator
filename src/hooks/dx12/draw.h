// Drawing one frame: the present queue, recording and submitting the menu, the
// per-frame diagnostics, and the gate every present path goes through.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // --- Hooks --------------------------------------------------------------
    // Adopt q as the queue we submit overlay work on. authoritative=true is for the
    // queue passed to CreateSwapChainForHwnd - for D3D12 that first argument IS the
    // swapchain's present queue, the one that owns the back buffers - which we pin so
    // the ExecuteCommandLists heuristic can never later swap in Streamline's MFG
    // present-pacer queue (submitting there is rejected -> device removed).
    static void PublishPresentQueue(ID3D12CommandQueue* q, bool authoritative)
    {
        if (!q || !g_queueLockReady) return;
        EnterCriticalSection(&g_queueLock);
        if (authoritative || !g_presentQueuePinned)
        {
            if (q != g_presentQueue)
            {
                q->AddRef();
                if (g_presentQueue) g_presentQueue->Release();
                g_presentQueue = q;
            }
            if (authoritative && !g_presentQueuePinned)
            {
                g_presentQueuePinned = true;
                LOG("Present queue pinned from swapchain creation - owns the back buffers.");
            }
        }
        LeaveCriticalSection(&g_queueLock);
    }

    static void WINAPI hkExecuteCommandLists(
        ID3D12CommandQueue* queue, UINT numLists, ID3D12CommandList* const* lists)
    {
        // Fallback queue discovery ONLY until the authoritative present queue is
        // pinned from swapchain creation. Once pinned, never touch it here - under
        // MFG the busiest DIRECT queue is Streamline's pacer, not ours.
        if (!g_presentQueuePinned && g_queueLockReady && g_device && queue &&
            queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            EnterCriticalSection(&g_queueLock);
            if (queue != g_presentQueue)
            {
                ID3D12Device* d = nullptr;
                if (SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&d))))
                {
                    if (d == g_device)
                    {
                        // New most-recent DIRECT queue on our device: take a ref,
                        // drop the ref on the previous one.
                        queue->AddRef();
                        if (g_presentQueue)
                            g_presentQueue->Release();
                        g_presentQueue = queue;

                        static bool s_logged = false;
                        if (!s_logged)
                        {
                            s_logged = true;
                            LOG("Present queue captured (heuristic - creation queue not yet seen).");
                        }
                    }
                    d->Release();
                }
            }
            LeaveCriticalSection(&g_queueLock);
        }

        oExecuteCommandLists(queue, numLists, lists);
    }

    // Per-frame counts for the [frames] log lines (see FrameReport).
    struct FrameStats
    {
        std::atomic<unsigned> presents{0}, notGameWindow{0}, notGameFacing{0}, busy{0},
            closed{0}, drawn{0}, notDrawn{0}, chainSwitches{0}, threadSwitches{0}, gpuWaits{0};
    };
    static FrameStats g_frameStats;

    // Records + submits the overlay. Kept as its own function so hkPresent can
    // wrap the call in __try/__except without object-unwinding conflicts.
    static bool DrawOverlay(IDXGISwapChain3* swapChain)
    {
        // Pin our render targets to the live swapchain before touching them.
        // A DLSS Frame Generation toggle silently swaps it; if the rebuild
        // fails, sit this frame out rather than draw into freed buffers.
        if (!ReconcileSwapChain(swapChain) || !g_offscreenTex || !g_rtvHeap)
            return false;

        if (!EnsureCompositePipeline(g_scFormat))
            return false;

        // Hold our own ref on the present queue for the duration of this frame
        // so a concurrent republish can't pull it out from under us. Not
        // captured yet? Skip - it arrives within a frame via
        // hkExecuteCommandLists.
        ID3D12CommandQueue* submitQueue = nullptr;
        EnterCriticalSection(&g_queueLock);
        if (g_presentQueue)
        {
            submitQueue = g_presentQueue;
            submitQueue->AddRef();
        }
        LeaveCriticalSection(&g_queueLock);
        if (!submitQueue)
            return false;

        // A language change needs glyphs the atlas does not hold. Rebuild
        // between frames, and only once our last submit has retired: the
        // backend releases the font texture to recreate it and the GPU may
        // still be reading the old one. The NewFrame below builds both again.
        // A resolution change moves the UI scale, which InitStyle once took
        // from the first swapchain only.
        gui::SetDisplayHeight(static_cast<float>(g_scHeight));
        if (gui::FontsNeedRebuild())
        {
            WaitForOverlayIdle();
            ImGui_ImplDX12_InvalidateDeviceObjects();
            gui::RebuildFonts();
        }

        // Before the backends, so the GetCursorPos detour knows which thread is
        // drawing when the Win32 backend polls the cursor a line below.
        State::Get().renderTid = GetCurrentThreadId();

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        // The Win32 backend takes the display size from the window's client
        // rect. ImGui renders into the offscreen target, which is the size of
        // the swapchain. Where the game runs at one resolution inside a window
        // of another those two differ, and since the DX12 backend builds its
        // viewport from the display size, everything past the target's own
        // width and height is dropped by the rasterizer with nothing to say so.
        // 4istoryBR's log of 17 September 2026 is a 1600x900 chain in a
        // 2560x1440 window: the menu opened with its cursor at 1280,720 and the
        // cursor disappeared every time it crossed 1600 across or 900 down.
        // Building the frame in the target's own size fixes it, and the rest of
        // the menu already assumes that space, since InitStyle takes the UI
        // scale from the swapchain height.
        if (g_scWidth && g_scHeight)
        {
            ImGuiIO& io = ImGui::GetIO();
            const float tw = static_cast<float>(g_scWidth), th = static_cast<float>(g_scHeight);
            if (io.DisplaySize.x != tw || io.DisplaySize.y != th)
            {
                static UINT s_saidW = 0, s_saidH = 0;
                if (s_saidW != g_scWidth || s_saidH != g_scHeight)
                {
                    s_saidW = g_scWidth; s_saidH = g_scHeight;
                    LOG("[overlay] the window is %.0fx%.0f and the target this mod draws into is %ux%u, so the menu is built at the target's size and stretched with the rest of the frame.",
                        io.DisplaySize.x, io.DisplaySize.y, g_scWidth, g_scHeight);
                }
                io.DisplaySize = ImVec2(tw, th);
            }
            input::SetOverlaySize(tw, th);
        }
        gui::FeedInput();   // after the backend's own cursor poll, before the frame is built
        ImGui::NewFrame();

        gui::Render();

        ImGui::Render();

        const UINT idx = swapChain->GetCurrentBackBufferIndex();
        if (idx >= g_frames.size() || !g_frames[idx].renderTarget)
        {
            // Live index outran our (just-reconciled) frame array - never index
            // out of bounds. Should not happen after ReconcileSwapChain, but a
            // freed device is worse than a dropped overlay frame.
            submitQueue->Release();
            return false;
        }
        FrameContext& frame = g_frames[idx];

        // The allocator cannot be reset while the GPU still uses it. It used
        // to skip the menu for this frame then, which with DLSS frame
        // generation and two back buffers happened on up to half the frames
        // and made the menu blink. Like Trinity, it waits for that work
        // instead: our own submission from a frame or two ago, normally a
        // fraction of a millisecond away. Only after 100 ms is the frame
        // given up, so a stuck GPU cannot hold the game.
        auto completed = g_fence->GetCompletedValue();
        if (completed != UINT64_MAX && completed < frame.fenceValue &&
            SUCCEEDED(g_fence->SetEventOnCompletion(frame.fenceValue, g_frameFenceEvent)))
        {
            g_frameStats.gpuWaits.fetch_add(1, std::memory_order_relaxed);
            WaitForSingleObject(g_frameFenceEvent, 100);
            completed = g_fence->GetCompletedValue();
        }
        if (completed < frame.fenceValue || completed == UINT64_MAX ||
            FAILED(frame.commandAllocator->Reset()) ||
            FAILED(g_commandList->Reset(frame.commandAllocator, nullptr)))
        {
            submitQueue->Release();
            return false;
        }

        // --- Pass 1: ImGui draws into the offscreen SDR target -------------
        D3D12_RESOURCE_BARRIER offscreenBarrier = {};
        offscreenBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        offscreenBarrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        offscreenBarrier.Transition.pResource   = g_offscreenTex;
        offscreenBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        offscreenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        offscreenBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &offscreenBarrier);

        const float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_commandList->OMSetRenderTargets(1, &g_offscreenRtv, FALSE, nullptr);
        g_commandList->ClearRenderTargetView(g_offscreenRtv, transparent, 0, nullptr);
        g_commandList->SetDescriptorHeaps(1, &g_srvHeap);


        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList);

        offscreenBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        offscreenBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        g_commandList->ResourceBarrier(1, &offscreenBarrier);

        // --- Pass 2: composite the offscreen image onto the real back buffer,
        // re-encoding for its actual color space (see CreateCompositePipeline).
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = frame.renderTarget;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &barrier);
        g_commandList->OMSetRenderTargets(1, &frame.rtvHandle, FALSE, nullptr);

        const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(g_scWidth), static_cast<float>(g_scHeight), 0.0f, 1.0f };
        const D3D12_RECT     scissor  = { 0, 0, static_cast<LONG>(g_scWidth), static_cast<LONG>(g_scHeight) };
        g_commandList->RSSetViewports(1, &viewport);
        g_commandList->RSSetScissorRects(1, &scissor);

        g_commandList->SetGraphicsRootSignature(g_compositeRootSig);
        g_commandList->SetPipelineState(g_compositePSO);
        g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        const struct { UINT mode; float paperWhiteNits; float pad[2]; } params =
            { CompositeModeForColorSpace(g_colorSpace), kPaperWhiteNits, { 0.0f, 0.0f } };
        g_commandList->SetGraphicsRoot32BitConstants(0, 4, &params, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE offscreenSrvGpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
        offscreenSrvGpu.ptr += static_cast<UINT64>(kOffscreenSrvSlot) *
            g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_commandList->SetGraphicsRootDescriptorTable(1, offscreenSrvGpu);

        g_commandList->DrawInstanced(3, 1, 0, 0);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);

        if (FAILED(g_commandList->Close()))
        {
            submitQueue->Release();
            return false;
        }

        ID3D12CommandList* toExec[] = { g_commandList };
        submitQueue->ExecuteCommandLists(1, toExec);
        const auto signal = submitQueue->Signal(g_fence, g_fenceValue + 1);
        submitQueue->Release();
        if (FAILED(signal))
        {
            // Without a fence this allocator cannot be reused safely.
            LOG_ERR("Overlay fence signal failed (0x%08X) - overlay disabled.", signal);
            g_renderDisabled = true;
            return false;
        }
        frame.fenceValue = ++g_fenceValue;
        return true;
    }

    // --- What happens to each frame while the menu is open ---------------------
    // A menu that blinks is drawn on some presented frames and not on others,
    // or opens and closes by itself. Neither showed in the log, so for 20
    // seconds after each opening a line a second says how many frames came
    // through, how many the menu was drawn on, why the others were not, and
    // how often the GPU had to be waited for. At most 120 such lines a
    // session. The frame generation libraries loaded are written at each
    // opening when they changed. These lines found the blinking under DLSS
    // frame generation (see DrawOverlay).

    // Counted by the ReShade path, which refuses other windows before
    // RenderOverlay is reached.
    static void CountNotGameWindow() { g_frameStats.notGameWindow.fetch_add(1, std::memory_order_relaxed); }

    static void LogFrameGenerationModules()
    {
        static const char* const kModules[] = {
            "sl.interposer.dll", "sl.dlss_g.dll", "nvngx_dlssg.dll",
            "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_framegeneration_dx12.dll",
            "libxess_fg.dll" };
        std::string loaded;
        for (const char* m : kModules)
            if (GetModuleHandleA(m))
                loaded += (loaded.empty() ? "" : ", ") + std::string(m);
        static std::string s_last = "?";
        if (loaded == s_last)
            return;
        s_last = loaded;
        LOG("[frames] frame generation libraries loaded: %s.",
            loaded.empty() ? "none" : loaded.c_str());
    }

    // Called once a frame from RenderOverlayFrame, after the toggle is read.
    static void FrameReport(bool open)
    {
        static bool s_wasOpen = false;
        static DWORD s_until = 0, s_last = 0;
        static unsigned s_lines = 0;
        const DWORD now = GetTickCount();
        auto& f = g_frameStats;
        const auto take = [](std::atomic<unsigned>& n) { return n.exchange(0, std::memory_order_relaxed); };
        if (open && !s_wasOpen)
        {
            LogFrameGenerationModules();
            if (!s_until)
            {
                // A fresh window: what was counted while nothing reported
                // belongs to no line.
                for (auto* n : { &f.presents, &f.notGameWindow, &f.notGameFacing, &f.busy, &f.closed,
                                 &f.drawn, &f.notDrawn, &f.chainSwitches, &f.threadSwitches, &f.gpuWaits })
                    take(*n);
                s_last = now;
            }
            s_until = now + 20000;
        }
        s_wasOpen = open;
        if (!s_until || static_cast<LONG>(now - s_last) < 1000)
            return;
        const unsigned presents = take(f.presents), notGameWindow = take(f.notGameWindow),
                       notGameFacing = take(f.notGameFacing), busy = take(f.busy),
                       closed = take(f.closed), drawn = take(f.drawn), notDrawn = take(f.notDrawn),
                       chains = take(f.chainSwitches), threads = take(f.threadSwitches),
                       waits = take(f.gpuWaits);
        if (s_lines < 120)
        {
            ++s_lines;
            LOG("[frames] %.1f s: %u presents; menu drawn on %u, draw failed on %u, menu closed on %u, "
                "skipped: %u another window, %u not the game's frames, %u still drawing the last one; "
                "%u swapchain switches, %u thread switches; waited for the GPU on %u.%s",
                (now - s_last) / 1000.0, presents, drawn, notDrawn, closed, notGameWindow, notGameFacing, busy,
                chains, threads, waits, s_lines == 120 ? " No more of these lines this session." : "");
        }
        s_last = now;
        if (static_cast<LONG>(now - s_until) >= 0)
            s_until = 0;
    }

    // Init-if-needed and draw the overlay into swapChain's current back buffer.
    // Returns whether a frame was actually submitted (for the post-present device
    // check). isGameFacing is false for native presents while a wrapper owns the
    // drawing - those are Streamline's read-only finished frames, left untouched.
    static bool RenderOverlayFrame(IDXGISwapChain3* swapChain, bool isGameFacing, UINT flags,
                              ID3D12CommandQueue* presentQueue = nullptr)
    {
        // Counted before anything can refuse: every path that could draw comes
        // through here, so this is the one number that says whether this mod is
        // on the screen at all. OverlayBlindWatch reads it.
        g_presentSeen.fetch_add(1, std::memory_order_relaxed);

        // For the [frames] lines. RenderOverlay lets one call in at a time,
        // so the two remembered values need no lock.
        auto& f = g_frameStats;
        f.presents.fetch_add(1, std::memory_order_relaxed);
        static IDXGISwapChain3* s_chain = nullptr;
        static DWORD s_thread = 0;
        const DWORD thread = GetCurrentThreadId();
        if (s_chain && s_chain != swapChain)
            f.chainSwitches.fetch_add(1, std::memory_order_relaxed);
        if (s_thread && s_thread != thread)
            f.threadSwitches.fetch_add(1, std::memory_order_relaxed);
        s_chain = swapChain;
        s_thread = thread;

        if (!isGameFacing || (flags & DXGI_PRESENT_TEST))
        {
            g_frameStats.notGameFacing.fetch_add(1, std::memory_order_relaxed);
            FrameReport(gui::WantsDraw());
            return false;
        }

        if (g_initFailed || g_renderDisabled)
            return false;

        // Other graphics mods may be creating devices and resizing their
        // swapchains at startup. Don't take back-buffer references until the
        // player requests the overlay.
        gui::PollToggle();
        const bool open = gui::WantsDraw();
        FrameReport(open);
        if (!open)
        {
            g_frameStats.closed.fetch_add(1, std::memory_order_relaxed);
            if (g_imguiReady)
                gui::FeedInput();
            return false;
        }

        bool drew = false;
        __try
        {
            if (!g_imguiReady)
            {
                DXGI_SWAP_CHAIN_DESC desc = {};
                if (FAILED(swapChain->GetDesc(&desc)) || !desc.OutputWindow ||
                    !desc.BufferCount ||
                    GetAncestor(desc.OutputWindow, GA_ROOT) != GetForegroundWindow())
                    return false;

                LOG("[overlay] Menu requested; initialising the renderer.");
                if (!InitImGui(swapChain))
                {
                    g_initFailed = true;
                    State::Get().menuOpen = false;
                    CleanupRenderTargets();
                    ReleaseOffscreenTarget();
                    LOG_ERR("ModActivator overlay initialization failed.");
                    return false;
                }
                g_imguiReady = true;
            }

            if (presentQueue)
                PublishPresentQueue(presentQueue, true);
            // This also covers XInput modules loaded after the first opening.
            hooks::EnsureXInputHooks();
            drew = DrawOverlay(swapChain);
            (drew ? g_frameStats.drawn : g_frameStats.notDrawn).fetch_add(1, std::memory_order_relaxed);
            // Proof the render path is alive. State::Captures reads this so an
            // overlay that stops drawing releases the pad instead of holding it
            // for the rest of the session.
            if (drew)
                State::Get().lastDrawAt = GetTickCount();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LOG_ERR("Overlay crashed (exception 0x%08X) - overlay disabled, game continues.",
                    GetExceptionCode());
            g_renderDisabled = true;
            State::Get().menuOpen = false;
        }
        return drew;
    }

    static bool RenderOverlay(IDXGISwapChain3* swapChain, bool isGameFacing, UINT flags,
                              ID3D12CommandQueue* presentQueue = nullptr)
    {
        // A second swapchain can present while the first is still drawing.
        // Skip that frame rather than accessing the same ImGui context twice.
        static std::atomic_flag busy = ATOMIC_FLAG_INIT;
        if (busy.test_and_set(std::memory_order_acquire))
        {
            g_frameStats.busy.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        struct UnlockOnReturn
        {
            std::atomic_flag& flag;
            ~UnlockOnReturn() { flag.clear(std::memory_order_release); }
        } unlock{busy};
        return RenderOverlayFrame(swapChain, isGameFacing, flags, presentQueue);
    }
