#pragma once

#include <tdk/UsbDevice.hpp>

namespace TDK
{
    struct UsbIdentity
    {
        uint16_t vendorId;
        uint16_t productId;
        uint16_t deviceVersion;
        const char* manufacturer;
        const char* product;
        const char* serial;
        const char* scsiVendor;
        const char* scsiProduct;
        const char* scsiRevision;
    };

    struct UsbStorage
    {
        void* context;
        uint32_t blockCount;
        bool writable;
        bool (*read)(void* context, uint32_t block, Byte* data);
        bool (*write)(void* context, uint32_t block, const Byte* data);
    };

    class UsbMassStorage
    {
    public:
        static constexpr uint32_t BlockSize = 512u;
        static constexpr uint32_t MaxStringCharacters = 30u;

        static Result Initialize(const UsbIdentity& identity,
                                 const UsbStorage* storage);
        static Result Poll();
        static bool IsReady();
        static uint32_t GetCommandCount();
    };
}
