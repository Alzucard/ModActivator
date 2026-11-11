# Dependencies and source provenance

- Overlay foundation: shin2344234/master-looter, commit `d08818df081cef091275d82588e38516f6dc3e97`.
- Dear ImGui: v1.91.5-docking, commit `368123ab06b2b573d585e52f84cd782c5c006697`.
- MinHook: commit `8af6b4acae5a9388fd742b56fa79ece89d96f823`.
- Tested compiler: LLVM-MinGW `20261104`, UCRT x86_64.

`src/hooks/`, the DX12 backend/shaders in `src/gui/`, `src/hooks/farhook.*` (a low-level detour helper only), and `src/core/state.h` derive from the credited overlay foundation. ModActivator changes include keyboard passthrough tagging, menu state and the minimal settings/logging interfaces. No auto-loot engine or gameplay features are included.

`src/activator.cpp`, `src/discovery.h`, and `src/hotkey_editor.h` implement ModActivator features. Vendored dependencies include their upstream license notices. The overlay's original notice is retained in `licenses/UPSTREAM-NOTICES.md`; its references to the original loot engine and to its own scripts (such as `scripts/adapt_trinity_dx12.py`) describe that upstream project, not code or tools included in this repository.

The HLSL sources of the precompiled shaders are in `shaders/`: `imgui_vs.hlsl` and `imgui_ps.hlsl` are Dear ImGui's DirectX 12 backend shaders, verbatim, and `hdr_composite.hlsl` is Trinity's, unchanged. The regeneration commands are in `src/gui/imgui_dx12_shaders.h` and `src/hooks/hdr_composite_shader.h`.
