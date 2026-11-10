# Dependencies and source provenance

- Overlay foundation: shin2344234/master-looter, commit `d08818df081cef091275d82588e38516f6dc3e97`.
- Dear ImGui: v1.91.5-docking, commit `368123ab06b2b573d585e52f84cd782c5c006697`.
- MinHook: commit `8af6b4acae5a9388fd742b56fa79ece89d96f823`.
- Tested compiler: LLVM-MinGW `20261104`, UCRT x86_64.

`src/hooks/`, the DX12 backend/shaders in `src/gui/`, `src/loot/farhook.*` (a low-level detour helper only), and `src/core/state.h` derive from the credited overlay foundation. ModActivator changes include keyboard passthrough tagging, menu state and the minimal settings/logging interfaces. No auto-loot engine or gameplay features are included.

`src/activator.cpp`, `src/discovery.h`, and `src/hotkey_editor.h` implement ModActivator features. Vendored dependencies include their upstream license notices. The overlay's original notice is retained in `licenses/UPSTREAM-NOTICES.md`; its references to the original loot engine describe that upstream project, not code included in this repository.
