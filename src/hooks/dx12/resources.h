// Render resources: back-buffer views, the offscreen target, the composite
// pipeline, keeping them in step with the swapchain, and Dear ImGui's setup.
//
// Part of dx12_hook.cpp, included inside its namespace in the order listed
// there. Not a header of its own: it relies on the parts before it.

    // ------------------------------------------------------------------------
    static void CleanupRenderTargets()
    {
        for (auto& f : g_frames)
        {
            if (f.renderTarget) { f.renderTarget->Release(); f.renderTarget = nullptr; }
        }
    }

    static bool CreateRenderTargets(IDXGISwapChain3* swapChain)
    {
        // A failed ResizeFrameResources leaves no heap, or fewer frames than
        // buffers. Refuse rather than write through a null heap.
        if (!g_device || !g_rtvHeap || g_frames.size() < g_bufferCount)
            return false;
        const UINT rtvSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < g_bufferCount; ++i)
        {
            ID3D12Resource* buffer = nullptr;
            if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffer))))
                return false;

            g_device->CreateRenderTargetView(buffer, nullptr, handle);
            g_frames[i].renderTarget = buffer;
            g_frames[i].rtvHandle    = handle;
            handle.ptr += rtvSize;
        }
        return true;
    }

    static void ReleaseOffscreenTarget()
    {
        if (g_offscreenTex)     { g_offscreenTex->Release();     g_offscreenTex     = nullptr; }
        if (g_offscreenRtvHeap) { g_offscreenRtvHeap->Release(); g_offscreenRtvHeap = nullptr; }
        g_offscreenW = 0;
        g_offscreenH = 0;
    }

    // (Re)creates the offscreen SDR target ImGui renders into, sized to match
    // the real back buffer. A no-op when the size hasn't changed. Requires
    // g_srvHeap to already exist (its SRV lands at the reserved fixed slot).
    static bool CreateOffscreenTarget(UINT width, UINT height)
    {
        if (g_offscreenTex && width == g_offscreenW && height == g_offscreenH)
            return true;

        ReleaseOffscreenTarget();
        if (width == 0 || height == 0)
            return false;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width            = width;
        desc.Height           = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = desc.Format; // Color left {0,0,0,0} - matches the per-frame clear.

        // Explicit initial state, not COMMON - this is a fresh resource we own
        // outright, and DrawOverlay's first barrier transitions FROM this state.
        if (FAILED(g_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear,
                IID_PPV_ARGS(&g_offscreenTex))))
            return false;
        g_offscreenTex->SetName(L"ModActivatorOverlayOffscreen");

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_offscreenRtvHeap))))
        {
            ReleaseOffscreenTarget();
            return false;
        }
        g_offscreenRtv = g_offscreenRtvHeap->GetCPUDescriptorHandleForHeapStart();
        g_device->CreateRenderTargetView(g_offscreenTex, nullptr, g_offscreenRtv);

        const UINT inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srv = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srv.ptr += static_cast<SIZE_T>(kOffscreenSrvSlot) * inc;
        g_device->CreateShaderResourceView(g_offscreenTex, nullptr, srv);

        g_offscreenW = width;
        g_offscreenH = height;
        return true;
    }

    // Builds the composite pass's PSO for the given back-buffer format (cheap:
    // no textures, no font atlas - just a tiny fullscreen-triangle pipeline) and
    // caches it. Rebuilds only when the format actually changes (HDR toggle,
    // which reallocates the swap chain with a different back-buffer format).
    // The root signature never depends on format, so it's created once and reused.
    static bool CreateCompositePipeline(DXGI_FORMAT rtvFormat)
    {
        if (!g_compositeRootSig)
        {
            D3D12_DESCRIPTOR_RANGE srvRange = {};
            srvRange.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRange.NumDescriptors                     = 1;
            srvRange.BaseShaderRegister                 = 0;
            srvRange.OffsetInDescriptorsFromTableStart  = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

            D3D12_ROOT_PARAMETER params[2] = {};
            params[0].ParameterType            = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            params[0].Constants.ShaderRegister = 0;
            params[0].Constants.Num32BitValues  = 4; // uint mode; float paperWhiteNits; float2 pad;
            params[0].ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;

            params[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            params[1].DescriptorTable.NumDescriptorRanges  = 1;
            params[1].DescriptorTable.pDescriptorRanges    = &srvRange;
            params[1].ShaderVisibility                     = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc   = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.NumParameters     = 2;
            rsDesc.pParameters       = params;
            rsDesc.NumStaticSamplers = 1;
            rsDesc.pStaticSamplers   = &sampler;

            ID3DBlob* sig = nullptr;
            ID3DBlob* err = nullptr;
            const HRESULT serialized = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
            if (err) err->Release();
            if (FAILED(serialized))
                return false;

            const HRESULT created = g_device->CreateRootSignature(
                0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_compositeRootSig));
            sig->Release();
            if (FAILED(created))
                return false;
        }

        if (g_compositePSO) { g_compositePSO->Release(); g_compositePSO = nullptr; }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
        pso.pRootSignature = g_compositeRootSig;
        pso.VS = { g_hdrCompositeVS, sizeof(g_hdrCompositeVS) };
        pso.PS = { g_hdrCompositePS, sizeof(g_hdrCompositePS) };
        // Same blend equation ImGui itself uses - straight (non-premultiplied)
        // alpha, so compositing the offscreen sprite reproduces exactly what
        // drawing ImGui directly onto the back buffer would have (mode 0).
        pso.BlendState.RenderTarget[0].BlendEnable    = TRUE;
        pso.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
        pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        pso.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.SampleMask                      = 0xFFFFFFFFu;
        pso.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.DepthStencilState.DepthEnable   = FALSE;
        pso.DepthStencilState.StencilEnable = FALSE;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets      = 1;
        pso.RTVFormats[0]         = rtvFormat;
        pso.SampleDesc.Count      = 1;

        return SUCCEEDED(g_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_compositePSO)));
    }

    static bool EnsureCompositePipeline(DXGI_FORMAT rtvFormat)
    {
        if (g_compositePSO && rtvFormat == g_compositeFormat)
            return true;
        if (!CreateCompositePipeline(rtvFormat))
            return false;
        g_compositeFormat = rtvFormat;
        return true;
    }

    // Block until our last overlay submit has retired so it is safe to release
    // the resources it referenced. Cheap when nothing is in flight.
    static void WaitForOverlayIdle()
    {
        if (g_fence && g_fenceValue && g_fenceEvent &&
            g_fence->GetCompletedValue() < g_fenceValue)
        {
            g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, 1000);
        }
    }

    // Rebuild the RTV heap and per-frame command allocators for a new back-buffer
    // count. The caller must have flushed our overlay work first (WaitForOverlayIdle)
    // and must call CreateRenderTargets afterwards to repopulate the views.
    static bool ResizeFrameResources(UINT newCount)
    {
        CleanupRenderTargets();
        for (auto& f : g_frames)
            if (f.commandAllocator) { f.commandAllocator->Release(); f.commandAllocator = nullptr; }
        if (g_rtvHeap) { g_rtvHeap->Release(); g_rtvHeap = nullptr; }

        // Zero until everything below exists. A count left at newCount after
        // a failure would make the next reconcile think nothing changed.
        g_bufferCount = 0;
        g_frames.clear();
        g_frames.resize(newCount);

        D3D12_DESCRIPTOR_HEAP_DESC d = {};
        d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        d.NumDescriptors = newCount;
        d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_rtvHeap))))
        {
            g_rtvHeap = nullptr;
            LOG_ERR("ResizeFrameResources: RTV heap (%u) creation failed.", newCount);
            return false;
        }

        for (UINT i = 0; i < newCount; ++i)
        {
            if (FAILED(g_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&g_frames[i].commandAllocator))))
            {
                g_frames[i].commandAllocator = nullptr;
                LOG_ERR("ResizeFrameResources: command allocator %u creation failed.", i);
                return false;
            }
        }
        g_bufferCount = newCount;
        return true;
    }

    // Keep our render targets pinned to whatever swapchain the game is actually
    // presenting. Streamline's DLSS Frame Generation destroys and recreates the
    // native swapchain when FG is toggled - a new pointer, and usually a larger
    // back-buffer count - WITHOUT routing through ResizeBuffers. Left unhandled,
    // our cached RTVs point at freed buffers and GetCurrentBackBufferIndex() can
    // run past g_frames => device removed. Detect a change by the full back-buffer
    // signature (identity, count, size, format) and rebuild in place. Returns false
    // (skip this frame) if the rebuild fails.
    static bool ReconcileSwapChain(IDXGISwapChain3* swapChain)
    {
        DXGI_SWAP_CHAIN_DESC desc = {};
        if (FAILED(swapChain->GetDesc(&desc)))
            return false;

        const UINT        newW   = desc.BufferDesc.Width;
        const UINT        newH   = desc.BufferDesc.Height;
        const DXGI_FORMAT newFmt = desc.BufferDesc.Format;

        // Compare the FULL back-buffer signature, not just pointer + count. A video
        // settings change (resolution, HDR on/off) reallocates the buffers in place
        // with the same swapchain and often the same count - detecting only
        // pointer/count leaves us drawing into freed buffers (ACCESS_DENIED). This
        // is what crashed on entering / applying game settings.
        if (swapChain == g_swapChain && desc.BufferCount == g_bufferCount &&
            newW == g_scWidth && newH == g_scHeight && newFmt == g_scFormat)
            return true; // unchanged - the common path

        // A different D3D device would invalidate our heaps/fence entirely; that
        // never happens for a DLSS-G toggle (Streamline reuses the device), so
        // rather than attempt a full teardown here we just skip the frame.
        ID3D12Device* dev = nullptr;
        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&dev))))
        {
            const bool sameDevice = (dev == g_device);
            dev->Release();
            if (!sameDevice)
            {
                static bool s_warned = false;
                if (!s_warned) { s_warned = true; LOG_ERR("Swapchain device changed - overlay paused this frame."); }
                return false;
            }
        }

        // Our last submit referenced the old buffers/allocators - retire it first.
        WaitForOverlayIdle();

        // The new signature is recorded only once everything below exists.
        // Recorded first, a failure here read as "unchanged" on the next
        // frame, which then drew with no render targets or offscreen texture.
        // On a failure the identity is forgotten instead, so the next frame
        // tries the whole rebuild again. Said once per failing signature, not
        // every frame.
        static UINT s_failedW = 0, s_failedH = 0, s_failedCount = 0;
        const auto fail = [&](const char* what) {
            CleanupRenderTargets();
            g_swapChain = nullptr;
            if (s_failedW != newW || s_failedH != newH || s_failedCount != desc.BufferCount)
            {
                s_failedW = newW; s_failedH = newH; s_failedCount = desc.BufferCount;
                LOG_ERR("ReconcileSwapChain: %s for %ux%u, %u buffers; the overlay retries next frame.",
                        what, newW, newH, desc.BufferCount);
            }
            return false;
        };

        if (desc.BufferCount != g_bufferCount)
        {
            if (!ResizeFrameResources(desc.BufferCount))
                return fail("frame resources could not be rebuilt");
        }
        else
        {
            CleanupRenderTargets(); // same count, fresh buffers
        }

        g_hwnd = desc.OutputWindow;
        if (!CreateRenderTargets(swapChain))
            return fail("render targets could not be rebuilt");

        // ImGui's own DX12 backend is baked for a FIXED SDR format (see
        // InitImGui) and never needs rebuilding here - only the offscreen
        // target it draws into has to track the back buffer's size.
        if (!CreateOffscreenTarget(newW, newH))
            return fail("the offscreen target could not be rebuilt");

        g_swapChain = swapChain;
        g_scWidth   = newW;
        g_scHeight  = newH;
        g_scFormat  = newFmt;
        s_failedW = s_failedH = s_failedCount = 0;

        LOG("Swapchain reconciled (%ux%u, %u buffers, fmt %d) - in-place reconfigure.",
            g_scWidth, g_scHeight, g_bufferCount, static_cast<int>(g_scFormat));
        return true;
    }

    static bool InitImGui(IDXGISwapChain3* swapChain)
    {
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&g_device))))
        {
            LOG_ERR("InitImGui: swapChain->GetDevice failed.");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc = {};
        swapChain->GetDesc(&desc);
        g_hwnd        = desc.OutputWindow;
        g_bufferCount = desc.BufferCount;
        g_scWidth     = desc.BufferDesc.Width;
        g_scHeight    = desc.BufferDesc.Height;
        g_scFormat    = desc.BufferDesc.Format;
        g_frames.clear();
        g_frames.resize(g_bufferCount);

        // NOTE: no queue selection here. Queues seen before init may already be
        // destroyed (the engine creates temporary devices/queues during loading),
        // so touching them is a use-after-free. The present queue is captured
        // live in hkExecuteCommandLists once g_device is known; DrawOverlay
        // simply skips frames until that has happened (typically 1 frame).

        // Shader-visible SRV heap: the ImGui font atlas and the offscreen target.
        {
            D3D12_DESCRIPTOR_HEAP_DESC d = {};
            d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            d.NumDescriptors = kSrvHeapSlots;
            d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_srvHeap))))
            {
                LOG_ERR("InitImGui: SRV heap creation failed.");
                return false;
            }
        }

        // RTV heap, one descriptor per back buffer.
        {
            D3D12_DESCRIPTOR_HEAP_DESC d = {};
            d.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            d.NumDescriptors = g_bufferCount;
            d.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(g_device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&g_rtvHeap))))
            {
                LOG_ERR("InitImGui: RTV heap creation failed.");
                return false;
            }
        }

        // One command allocator per frame + a single command list.
        for (UINT i = 0; i < g_bufferCount; ++i)
        {
            if (FAILED(g_device->CreateCommandAllocator(
                    D3D12_COMMAND_LIST_TYPE_DIRECT,
                    IID_PPV_ARGS(&g_frames[i].commandAllocator))))
            {
                LOG_ERR("InitImGui: command allocator %u creation failed.", i);
                return false;
            }
        }

        if (FAILED(g_device->CreateCommandList(
                0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_frames[0].commandAllocator, nullptr,
                IID_PPV_ARGS(&g_commandList))))
        {
            LOG_ERR("InitImGui: command list creation failed.");
            return false;
        }
        g_commandList->Close();
        // Name it so DRED (DumpDred) can tell a fault in OUR overlay submit apart
        // from a Streamline / engine command list.
        g_commandList->SetName(L"ModActivatorOverlayCmdList");

        // Fence so we never reset an allocator the GPU is still using.
        if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        {
            LOG_ERR("InitImGui: fence creation failed.");
            return false;
        }
        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_frameFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent || !g_frameFenceEvent)
        {
            LOG_ERR("InitImGui: fence event creation failed.");
            return false;
        }

        if (!CreateRenderTargets(swapChain))
        {
            LOG_ERR("InitImGui: render target creation failed.");
            return false;
        }

        if (!CreateOffscreenTarget(g_scWidth, g_scHeight))
        {
            LOG_ERR("InitImGui: offscreen target creation failed.");
            return false;
        }

        // ImGui.
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't litter the game folder
        // ImGui's recoverable-error path draws a tooltip, and a tooltip is a
        // window, so an error raised outside a frame (a font file that is
        // not there, in InitStyle below) is a Begin() with no frame and a
        // null read. Log the error and nothing else.
        io.ConfigErrorRecoveryEnableTooltip = false;
        io.ConfigErrorRecoveryEnableAssert  = false;

        gui::InitStyle(static_cast<float>(desc.BufferDesc.Height) / 1080.0f);

        ImGui_ImplWin32_Init(g_hwnd);
        // Always a fixed SDR format, independent of the real back buffer's
        // format/color space - see the "HDR-aware overlay compositing" comment
        // above. The composite pass (EnsureCompositePipeline) is what actually
        // targets g_scFormat.
        ImGui_ImplDX12_Init(
            g_device, g_bufferCount, DXGI_FORMAT_R8G8B8A8_UNORM, g_srvHeap,
            g_srvHeap->GetCPUDescriptorHandleForHeapStart(),
            g_srvHeap->GetGPUDescriptorHandleForHeapStart());


        input::Init(g_hwnd);

        g_swapChain = swapChain; // baseline for ReconcileSwapChain

        LOG_OK("Overlay ready - %ux%u, %u back buffers.",
               desc.BufferDesc.Width, desc.BufferDesc.Height, g_bufferCount);
        return true;
    }
