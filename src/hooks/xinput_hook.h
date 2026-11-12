// Adapted for Master Looter from Trinity (https://github.com/XeTrinityz/Trinity),
// MIT License, Copyright (c) 2026 XeTrinityz. See THIRD_PARTY_NOTICES.md.
// Changes: Master Looter namespaces, logger, settings and menu hooks; icon atlas code removed.
#pragma once
#include <Windows.h>
#include <Xinput.h>

namespace ml::hooks
{
    // Detours XInputGetState so the pad is neutralised for the GAME while the
    // menu is open - stops nav buttons (A/B, d-pad, RB+X) leaking through.
    //
    // Safe to call every frame: it hooks whichever xinput module has since
    // loaded (the game often inits its input system after we do) and becomes a
    // no-op once every known module is accounted for.
    void EnsureXInputHooks();

    // The first connected controller as it really is, for ModActivator's own
    // use (its controller shortcut and menu navigation): not neutralised
    // while the menu is open, and not masked. False with no controller.
    // Disconnected slots are looked for again only every two seconds, since
    // asking XInput about an empty slot is slow.
    bool ReadPadRaw(XINPUT_GAMEPAD* out);
}
