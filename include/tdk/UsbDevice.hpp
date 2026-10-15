#pragma once

#include <tdk/Types.hpp>

namespace TDK
{
    struct UsbSetupPacket
    {
        Byte requestType;
        Byte request;
        uint16_t value;
        uint16_t index;
        uint16_t length;
    };

    struct UsbDescriptors
    {
        const Byte* device;
        const Byte* configuration;
        uint16_t configurationLength;
        const Byte* const* strings;
        uint8_t stringCount;
        Word endpointTypes;
    };

    struct UsbClassHandler
    {
        void* context;
        bool (*controlRequest)(void* context, const UsbSetupPacket* setup,
                               const Byte** data, uint16_t* length);
        void (*configured)(void* context, Byte configuration);
        void (*service)(void* context, bool polled);
    };

    enum class UsbEventType : uint8_t
    {
        Reset,
        Setup,
        Address,
        Configuration,
        Stall,
        Suspend,
        Resume,
        Standard,
        BulkOut,
        BulkIn,
        Command,
        Error,
        Attach,
        Detach
    };

    struct UsbEvent
    {
        UsbEventType type;
        Byte requestType;
        Byte request;
        Byte flag;
        uint16_t value;
        uint16_t index;
        uint16_t length;
        uint16_t reserved;
        uint32_t detail;
    };

    struct UsbStatus
    {
        uint32_t interrupts;
        uint32_t resets;
        uint32_t setups;
        uint32_t stalls;
        uint32_t bulkOutPackets;
        uint32_t bulkInPackets;
        uint32_t commands;
        uint32_t droppedEvents;
        Byte address;
        Byte configuration;
        bool resetSeen;
        bool suspended;
        bool vbus;
        bool attached;
    };

    class UsbDevice
    {
    public:
        static constexpr uint32_t InterruptNumber = 45u;
        static constexpr uint32_t ControlPacketSize = 8u;
        static constexpr uint32_t BulkPacketSize = 64u;

        static Result Initialize(const UsbDescriptors& descriptors,
                                 const UsbClassHandler& handler);
        static Result Shutdown();
        static Result Poll();
        static bool IsInitialized();
        static bool IsConfigured();
        static bool GetEvent(UsbEvent* event);
        static Result GetStatus(UsbStatus* status);
        static void Report(UsbEventType type, Byte request, uint16_t value,
                           uint16_t length, uint32_t detail);

        static Result PollLocked();
        static Result NotifyWrite(const Byte* data, uint32_t length);
        static bool NotifyBusy();
        static Result BulkOutWait();
        static Result BulkOutEnable();
        static bool BulkOutPending();
        static uint32_t BulkOutRead(Byte* destination, uint32_t wanted,
                                    uint32_t fallback);
        static bool BulkInBusy();
        static Result BulkInWrite(const Byte* data, uint32_t length);
        static Result StallBulkIn();
        static Result StallBulkOut();
        static Result ResetBulk();
    };
}
