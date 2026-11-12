/* ModActivatorBindings.h - describe your mod's shortcuts to ModActivator.
 *
 * ModActivator finds a mod's shortcuts in its config file and guesses what
 * each one does from its name and comments. A mod can say instead: export
 * one function, and ModActivator shows your names and descriptions, and puts
 * each shortcut on the right tab. Nothing else changes for your mod, and it
 * still works without ModActivator.
 *
 * Copy this header into your project (it is MIT-0, no attribution needed),
 * then:
 *
 *     static const ModActivatorBindingV1 bindings[] = {
 *         {"MyMod.ini", "Hotkeys", "MenuKey", "Settings menu",
 *          "Opens the My Mod settings window.", ModActivatorKindMenu},
 *         {nullptr, nullptr, "ToggleKey", "My Mod on/off",
 *          "Turns the effect on or off.", ModActivatorKindToggle},
 *     };
 *     extern "C" __declspec(dllexport) const ModActivatorBindingV1 *
 *     ModActivatorBindingsV1(unsigned *count) {
 *         *count = sizeof bindings / sizeof bindings[0];
 *         return bindings;
 *     }
 *
 * Rules:
 *  - The array and its strings must stay valid while the game runs (static
 *    storage, as above). ModActivator copies them on each refresh of its menu.
 *  - key is the setting's name in your config file, matched without regard
 *    to case. file (file name only) and section narrow it down, and may be
 *    null for any.
 *  - name and description are UTF-8, and either may be null. Keep name short
 *    ("Settings menu"); description is one or two sentences.
 *  - The function is called from a background thread, never from DllMain.
 *  - Your user can still rename and describe any shortcut in ModActivator,
 *    and their words win.
 */
#ifndef MODACTIVATOR_BINDINGS_H
#define MODACTIVATOR_BINDINGS_H

#ifdef __cplusplus
extern "C" {
#endif

enum ModActivatorKind {
  ModActivatorKindUnspecified = 0, /* let ModActivator decide */
  ModActivatorKindMenu = 1,        /* opens a window: the Mod menus tab */
  ModActivatorKindToggle = 2,      /* switches something on and off: the Toggles tab */
  ModActivatorKindAction = 3       /* anything else: listed under Hotkeys only */
};

typedef struct ModActivatorBindingV1 {
  const char *file;        /* config file name, e.g. "MyMod.ini"; null for any */
  const char *section;     /* INI section; null for any */
  const char *key;         /* INI key holding the shortcut; required */
  const char *name;        /* short display name, UTF-8; may be null */
  const char *description; /* what the shortcut does, UTF-8; may be null */
  int kind;                /* a ModActivatorKind */
} ModActivatorBindingV1;

typedef const ModActivatorBindingV1 *(*ModActivatorBindingsV1Fn)(unsigned *count);

#ifdef __cplusplus
}
#endif

#endif /* MODACTIVATOR_BINDINGS_H */
