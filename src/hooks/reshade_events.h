#pragma once

#include <cstdint>

namespace ml::hooks::reshade_api
{
    // https://github.com/crosire/reshade/tree/v6.8.0/include
    // ReShade 6.8 add-on API v20. These are the leading virtual methods
    // needed to identify D3D12 and obtain the native COM objects. ReShade
    // owns the objects. They are never constructed or destroyed here.
    inline constexpr uint32_t version = 20;
    inline constexpr uint32_t present = 74;
    inline constexpr uint32_t destroySwapchain = 8;
    inline constexpr uint32_t d3d12 = 0xc000;

    struct Object
    {
        virtual uint64_t get_native() const = 0;
        virtual void get_private_data(const uint8_t guid[16], uint64_t *data) const = 0;
        virtual void set_private_data(const uint8_t guid[16], uint64_t data) = 0;
    };
    struct Device : Object
    {
        virtual uint32_t get_api() const = 0;
    };
    struct DeviceObject : Object
    {
        virtual Device *get_device() = 0;
    };

    using RegisterAddon = bool (*)(void *module, uint32_t version);
    using RegisterEvent = void (*)(void *module, uint32_t event, void *callback);
}
