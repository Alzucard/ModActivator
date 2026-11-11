#include "farhook.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <cstdio>
#include <cstring>

namespace ml::farhook
{
    // The trampolines share one executable page, and the creation watch may
    // run while the game itself is starting threads, so calls are serialised.
    static SRWLOCK g_lock = SRWLOCK_INIT;
    struct Held
    {
        Held() { AcquireSRWLockExclusive(&g_lock); }
        ~Held() { ReleaseSRWLockExclusive(&g_lock); }
    };

    static unsigned char* g_page = nullptr;
    static unsigned g_used = 0;

    static unsigned char* Alloc(unsigned n)
    {
        if (!g_page || g_used + n > 4096)
        {
            g_page = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            g_used = 0;
            if (!g_page) return nullptr;
        }
        unsigned char* p = g_page + g_used;
        g_used += (n + 15) & ~15u;
        return p;
    }

    // Suspends every other thread; reports whether one of them sits inside
    // the bytes about to change, so the caller can retry.
    static int SuspendOthers(HANDLE* handles, int max, uintptr_t lo, uintptr_t hi, bool* inside)
    {
        *inside = false;
        int n = 0;
        const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return 0;
        THREADENTRY32 te; te.dwSize = sizeof te;
        if (Thread32First(snap, &te))
        {
            do
            {
                if (te.th32OwnerProcessID != pid || te.th32ThreadID == me || n >= max) continue;
                HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                if (!h) continue;
                if (SuspendThread(h) == static_cast<DWORD>(-1)) { CloseHandle(h); continue; }
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_CONTROL;
                if (GetThreadContext(h, &ctx) && ctx.Rip >= lo && ctx.Rip < hi) *inside = true;
                handles[n++] = h;
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
        return n;
    }
    static void ResumeAll(HANDLE* handles, int n)
    {
        for (int i = 0; i < n; ++i) { ResumeThread(handles[i]); CloseHandle(handles[i]); }
    }

    // Fails rather than patch under a thread that keeps sitting inside the
    // bytes: a thread resumed halfway through a rewritten instruction crashes
    // the game, while a missing hook only costs the menu.
    static bool WriteCode(uintptr_t dst, const void* src, unsigned n, char* why, unsigned whyLen)
    {
        DWORD old;
        if (!VirtualProtect(reinterpret_cast<void*>(dst), n, PAGE_EXECUTE_READWRITE, &old)) { snprintf(why, whyLen, "VirtualProtect failed"); return false; }
        HANDLE handles[512]; bool inside = false; int cnt = 0;
        for (int attempt = 0; attempt < 40; ++attempt)
        {
            cnt = SuspendOthers(handles, 512, dst, dst + n, &inside);
            if (!inside) break;
            ResumeAll(handles, cnt);
            cnt = 0;
            Sleep(1);
        }
        if (inside)
        {
            VirtualProtect(reinterpret_cast<void*>(dst), n, old, &old);
            snprintf(why, whyLen, "a thread stayed inside the patched bytes");
            return false;
        }
        memcpy(reinterpret_cast<void*>(dst), src, n);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(dst), n);
        ResumeAll(handles, cnt);
        VirtualProtect(reinterpret_cast<void*>(dst), n, old, &old);
        return true;
    }

    // `mov rax, imm64; jmp rax` keeps the address at +2; `jmp [rip+0]` at +6.
    static unsigned AbsJumpAt(const unsigned char* at)
    {
        if (at[0] == 0x48 && at[1] == 0xB8 && at[10] == 0xFF && at[11] == 0xE0) return 2;
        if (at[0] == 0xFF && at[1] == 0x25 && at[2] == 0 && at[3] == 0 && at[4] == 0 && at[5] == 0) return 6;
        return 0;
    }

    uintptr_t AbsJumpTarget(uintptr_t target)
    {
        if (!target) return 0;
        __try
        {
            const auto* at = reinterpret_cast<const unsigned char*>(target);
            const unsigned off = AbsJumpAt(at);
            if (!off) return 0;
            uintptr_t to;
            memcpy(&to, at + off, 8);
            return to;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool InstallOverAbsJump(const char* name, uintptr_t target, void* detour, void** original, char* why, unsigned whyLen)
    {
        Held held;
        (void)name;
        why[0] = 0;
        if (!target) { snprintf(why, whyLen, "no target"); return false; }
        const auto* at = reinterpret_cast<const unsigned char*>(target);
        const unsigned off = AbsJumpAt(at);
        if (!off) { snprintf(why, whyLen, "the entry is not an absolute jump"); return false; }
        uintptr_t theirs;
        memcpy(&theirs, at + off, 8);

        // The original: straight on to where the other mod's patch went.
        unsigned char* tramp = Alloc(14);
        if (!tramp) { snprintf(why, whyLen, "trampoline page allocation failed"); return false; }
        tramp[0] = 0xFF; tramp[1] = 0x25; tramp[2] = tramp[3] = tramp[4] = tramp[5] = 0;
        memcpy(tramp + 6, &theirs, 8);

        // Trampoline first, then the write. The eight bytes are the operand of
        // one instruction and no thread can be stopped inside them, so a
        // thread before the write takes ours and one past it takes theirs.
        *original = tramp;
        if (!WriteCode(target + off, &detour, 8, why, whyLen)) { *original = nullptr; return false; }
        return true;
    }
}
