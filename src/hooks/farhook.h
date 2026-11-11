#pragma once
#include <cstdint>

// Stacking on another mod's absolute-jump detour. Used once, by the swapchain
// creation watch in dx12_hook.cpp, when another mod writes its own detour
// over this one's on CreateSwapChainForHwnd.
namespace ml::farhook
{
    // For an entry another mod has patched with an absolute jump: `mov rax,
    // imm64; jmp rax`, or `jmp [rip+0]` with the address after it. Only the
    // eight address bytes change, to the detour, and *original jumps to the
    // address they held, so the other mod's detour still runs and the
    // original last. Other threads are suspended around the write.
    bool InstallOverAbsJump(const char* name, uintptr_t target, void* detour, void** original, char* why, unsigned whyLen);

    // What InstallOverAbsJump would take over: the address such a patch jumps
    // to, or 0 when the entry is not one.
    uintptr_t AbsJumpTarget(uintptr_t target);
}
