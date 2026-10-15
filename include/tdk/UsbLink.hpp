#pragma once

#include <tdk/UsbDevice.hpp>

#ifndef TDK_USBLINK_PAYLOAD
#define TDK_USBLINK_PAYLOAD 4096u
#endif
#ifndef TDK_USBLINK_LOG
#define TDK_USBLINK_LOG 4096u
#endif
#ifndef TDK_USBLINK_COMMANDS
#define TDK_USBLINK_COMMANDS 32u
#endif

namespace TDK
{
    struct UsbLinkRequest
    {
        Byte opcode;
        Byte flags;
        uint32_t tag;
        uint32_t argument;
        uint32_t length;
    };

    struct UsbLinkReply
    {
        const Byte* data;
        uint32_t length;
    };

    using UsbLinkHandler = Byte (*)(void* context, const UsbLinkRequest* request,
                                    const Byte* payload, UsbLinkReply* reply);

    struct UsbLinkConfig
    {
        void* context;
        const char* name;
        uint32_t ramBase;
        uint32_t ramSize;
        uint32_t reservedBase;
        uint32_t reservedSize;
        uint32_t capabilities;
        bool (*validate)(void* context, Address address, uint32_t length, bool write);
        bool (*validateEntry)(void* context, Address entry);
        void (*execute)(void* context, Address entry);
        void (*flushCache)(void* context, Address address, uint32_t length);
        void (*memoryRead)(void* context, Address address, Byte* destination,
                           uint32_t length);
        void (*memoryWrite)(void* context, Address address, const Byte* source,
                            uint32_t length);
    };

    class UsbLink
    {
    public:
        static constexpr uint32_t MaxPayload = TDK_USBLINK_PAYLOAD;
        static constexpr uint32_t LogCapacity = TDK_USBLINK_LOG;
        static constexpr uint32_t MaxCommands = TDK_USBLINK_COMMANDS;

        static Result Initialize(const UsbLinkConfig& config);
        static Result RegisterCommand(Byte opcode, UsbLinkHandler handler,
                                      void* context);
        static void Log(const char* text, uint32_t length);
        static Result Notify(Byte type, uint16_t value, uint32_t detail);
        static void SetFlags(Byte setBits, Byte clearBits);
        static void RunPending();
        static void CancelPending();
        static bool IsIdle();
        static uint32_t GetCommandCount();
        static void ReadMemory(Address address, Byte* destination, uint32_t length);
        static void WriteMemory(Address address, const Byte* source, uint32_t length);
    };
}
