#pragma once

#include <cstdint>

namespace ml::hooks::reshade_api
{
    // https://github.com/crosire/reshade/tree/v6.8.0/include
    // ReShade 6.8 add-on API v20. These are the leading virtual methods
    // needed to identify D3D12 and obtain the native COM objects. ReShade
    // owns the objects. They are never constructed or destroyed here.
    //
    // ReShade refuses an add-on that asks for a newer API than it provides
    // (6.6.0 to 6.7.x have 18, 6.5.x has 17). InstallDX12Hooks falls back to
    // the legacy DirectX path in that case.
    inline constexpr uint32_t version = 20;
    inline constexpr uint32_t destroySwapchain = 8;
    // Fired before ReShade renders its effects. Used only to learn the swap
    // chain's colour space; drawing here would put the effects on the menu.
    inline constexpr uint32_t present = 74;
    // "Called after ReShade has rendered its overlay." The menu is drawn here.
    inline constexpr uint32_t reshadePresent = 75;
    inline constexpr uint32_t d3d12 = 0xc000;

    // api::color_space (reshade_api_format.hpp).
    inline constexpr uint32_t colorSpaceSrgb = 1;
    inline constexpr uint32_t colorSpaceScrgb = 2;
    inline constexpr uint32_t colorSpaceHdr10Pq = 3;

    // Slot order must match ReShade's declarations exactly. Placeholders keep
    // the later slots at the right vtable index and are never called.
    struct Object // api::api_object, slots 0-2
    {
        virtual uint64_t get_native() const = 0;
        virtual void get_private_data(const uint8_t guid[16], uint64_t *data) const = 0;
        virtual void set_private_data(const uint8_t guid[16], uint64_t data) = 0;
    };
    struct Device : Object // api::device, slot 3
    {
        virtual uint32_t get_api() const = 0;
    };
    struct DeviceObject : Object // api::device_object, slot 3
    {
        virtual Device *get_device() = 0;
    };
    struct CommandQueue : DeviceObject // api::command_queue
    {
        virtual uint32_t get_type() const = 0;                 // 4
        virtual void wait_idle() const = 0;                    // 5
        virtual void flush_immediate_command_list() const = 0; // 6
    };
    struct Swapchain : DeviceObject // api::swapchain
    {
        virtual void *get_hwnd() const = 0;                               // 4
        virtual uint64_t get_back_buffer(uint32_t index) = 0;             // 5
        virtual uint32_t get_back_buffer_count() const = 0;               // 6
        virtual uint32_t get_current_back_buffer_index() const = 0;       // 7
        virtual bool check_color_space_support(uint32_t space) const = 0; // 8
        virtual uint32_t get_color_space() const = 0;                     // 9
    };
    // api::effect_runtime derives from device_object, not swapchain. Its
    // get_native() returns the swap chain's native object.
    struct EffectRuntime : DeviceObject
    {
        virtual void *get_hwnd() const = 0;                         // 4
        virtual uint64_t get_back_buffer(uint32_t index) = 0;       // 5
        virtual uint32_t get_back_buffer_count() const = 0;         // 6
        virtual uint32_t get_current_back_buffer_index() const = 0; // 7
        virtual CommandQueue *get_command_queue() = 0;              // 8
    };

    using RegisterAddon = bool (*)(void *module, uint32_t version);
    using UnregisterAddon = void (*)(void *module);
    using RegisterEvent = void (*)(void *module, uint32_t event, void *callback);
}
