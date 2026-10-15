#include <tdk/UsbDevice.hpp>

#include <tdk/Interrupts.hpp>
#include <tdk/Platform.hpp>
#include <tdk/TDK.hpp>

namespace TDK
{
    namespace
    {
        constexpr Address UsbConfig = 0x881b00c0u;
        constexpr Address UsbFunction = 0x881b00c4u;
        constexpr Address Ep0Data = 0x881b00ccu;
        constexpr Address BulkInData = 0x881b00d0u;
        constexpr Address InterruptInData = 0x881b00d8u;
        constexpr Address BulkOutData = 0x881b00d4u;
        constexpr Address EpEvent = 0x881b00dcu;
        constexpr Address IntEnable = 0x881b00e4u;
        constexpr Address IntFlags = 0x881b00e8u;
        constexpr Address StandardEnable = 0x881b00ecu;
        constexpr Address StandardFlags = 0x881b00f0u;
        constexpr Address AutoSet = 0x881b00f4u;
        constexpr Address SetStall = 0x881b00f8u;
        constexpr Address BufferClear = 0x881b00fcu;
        constexpr Address BulkOutCount = 0x881b0108u;
        constexpr Address BulkInPointer = 0x881b0110u;
        constexpr Address BulkOutPointer = 0x881b0114u;
        constexpr Address Ep0RequestType = 0x881b0118u;
        constexpr Address Ep0Request = 0x881b011cu;
        constexpr Address Ep0Value = 0x881b0120u;
        constexpr Address Ep0Index = 0x881b0124u;
        constexpr Address Ep0Length = 0x881b0128u;
        constexpr Address StallFlags = 0x881b0150u;
        constexpr Address DeviceConfig = 0x881b015cu;
        constexpr Address NullPacket = 0x881b0160u;
        constexpr Address DmaInterrupt = 0x881b0164u;

        constexpr Address PllConfig = 0x882100bcu;
        constexpr Address UsbClockConfig = 0x88210064u;
        constexpr Address CrystalEnable = 0x88210114u;
        constexpr Address DeviceSelect = 0x88200000u;
        constexpr Address TftOutputEnable = 0x88200018u;
        constexpr Address CsiGpioSetup = 0x88200024u;
        constexpr Address CsiGpioPull = 0x88200028u;
        constexpr Address UnknownGpio = 0x882000d0u;
        constexpr Address UsbGpioPort = 0x8820003cu;
        constexpr Address InterruptMask = 0x880a0024u;
        constexpr Address GpioInput = 0x88200074u;

        constexpr Word UsbPllEnable = 4u;
        constexpr Word TftOutputBit = 4u;
        constexpr Word DeviceClockOnly = 5u;
        constexpr Word DeviceSelectMask = 0xfffffcfcu;
        constexpr Word CsiOutputSetup = 0x40000u;
        constexpr Word CsiPullSetup = 0x30000u;
        constexpr Word UsbGpioSetup = 0x1010000u;
        constexpr Word DefaultEndpointTypes = 0xa10u;
        constexpr Word ConfigEnable = 0x818u;
        constexpr Word MaskBit = 1u << (45u - 32u);
        constexpr Word VbusBit = 1u << 8;

        constexpr Word EventSetupReady = 0x1u;
        constexpr Word EventInReady = 0x10u;
        constexpr Word EventStatusEnable = 0x40u;
        constexpr Word EventInterruptInReady = 0x4000u;
        constexpr Word EventBulkInReady = 0x100u;
        constexpr Word EventBulkInClear = 0x200u;
        constexpr Word EventBulkOutEnable = 0x800u;
        constexpr Word EventBulkOutReady = 0x1000u;
        constexpr Word EventBulkOutNackClear = 0x2000u;

        constexpr Word FlagSetup = 0x1u;
        constexpr Word FlagInClear = 0x8u;
        constexpr Word FlagInNack = 0x10u;
        constexpr Word FlagStatusClear = 0x20u;
        constexpr Word FlagBulkInClear = 0x80u;
        constexpr Word FlagBulkOutPacket = 0x200u;
        constexpr Word FlagBulkOutNack = 0x400u;
        constexpr Word FlagSuspend = 0x2000u;
        constexpr Word FlagResume = 0x4000u;
        constexpr Word FlagReset = 0x8000u;
        constexpr Word FlagsAll = 0xffffu;
        constexpr Word BaseEnable = FlagReset | FlagStatusClear | FlagInClear |
                                    FlagSetup | FlagBulkOutPacket | FlagBulkInClear;
        constexpr Word StandardAddress = 0x10u;
        constexpr Word StandardConfiguration = 0x4u;
        constexpr Word StandardEnableMask = StandardAddress | StandardConfiguration;

        constexpr Word StallEp0 = 1u;
        constexpr Word StallBulkInSet = 2u;
        constexpr Word StallBulkOutSet = 4u;
        constexpr Word StallSeenEp0 = 0x100u;
        constexpr Word StallSeenBulkIn = 0x200u;
        constexpr Word StallSeenBulkOut = 0x400u;
        constexpr Word BufferClearEp0 = 1u;

        constexpr uint32_t PllSettleIterations = 200000u;
        constexpr uint32_t StallWaitLimit = 40000u;
        constexpr uint32_t ServicePasses = 32u;
        constexpr uint32_t StuckPasses = 8u;
        constexpr uint32_t VbusTimeoutPolls = 180u;
        constexpr uint32_t IdleInterruptLimit = 2000u;
        constexpr uint32_t EventCapacity = 64u;

        struct State
        {
            UsbDescriptors descriptors;
            UsbClassHandler handler;
            bool initialized;
            bool bulkOutWanted;
            bool ep0Active;
            bool ep0Zlp;
            bool ep0NeedsZlp;
            bool notifyPending;
            Byte notifyData[8];
            uint32_t notifyLength;
            const Byte* ep0Data;
            uint32_t ep0Remaining;
            Byte configuration;
            Byte address;
            Word savedDeviceSelect;
            uint32_t idleInterrupts;
            bool interruptMasked;
            bool attached;
            bool vbusSeen;
            uint32_t waitingPolls;
            UsbStatus status;
        };

        State state = {};
        UsbEvent events[EventCapacity] = {};
        volatile uint32_t eventHead = 0u;
        volatile uint32_t eventTail = 0u;

        uint16_t Low16(Word value)
        {
            return static_cast<uint16_t>(value & 0xffffu);
        }

        Word Fetch(Address address)
        {
            Word value = 0u;

            ReadRegister(address, &value);
            return value;
        }

        void Put(Address address, Word value)
        {
            WriteRegister(address, value);
        }

        void Modify(Address address, Word setBits, Word clearBits)
        {
            Put(address, (Fetch(address) & ~clearBits) | setBits);
        }

        void PushEvent(UsbEventType type, Byte requestType, Byte request,
                       uint16_t value, uint16_t index, uint16_t length, uint32_t detail)
        {
            const uint32_t next = (eventHead + 1u) % EventCapacity;

            if (next == eventTail)
            {
                eventTail = (eventTail + 1u) % EventCapacity;
                ++state.status.droppedEvents;
            }
            UsbEvent& slot = events[eventHead];

            slot.type = type;
            slot.requestType = requestType;
            slot.request = request;
            slot.flag = 0u;
            slot.value = value;
            slot.index = index;
            slot.length = length;
            slot.reserved = 0u;
            slot.detail = detail;
            eventHead = next;
        }

        void ClearBulkState()
        {
            Put(BulkInPointer, 0u);
            Put(BulkOutPointer, 0u);
            Put(SetStall, 0u);
            Put(StallFlags, StallSeenEp0 | StallSeenBulkIn | StallSeenBulkOut);
            state.bulkOutWanted = false;
        }

        void StallControl()
        {
            Put(SetStall, StallEp0);
            for (uint32_t spin = 0u; spin < StallWaitLimit; ++spin)
            {
                if ((Fetch(StallFlags) & StallSeenEp0) != 0u)
                {
                    break;
                }
            }
            Put(StallFlags, StallSeenEp0);
            Put(SetStall, 0u);
            state.ep0Active = false;
            ++state.status.stalls;
            PushEvent(UsbEventType::Stall, 0u, 0u, 0u, 0u, 0u, 0u);
        }

        void SendZeroLengthPacket()
        {
            Put(NullPacket, 1u);
            Put(EpEvent, EventInReady);
            state.ep0Zlp = true;
        }

        void SendControlChunk()
        {
            if (state.ep0Remaining == 0u)
            {
                state.ep0Active = false;
                SendZeroLengthPacket();
                return;
            }

            const uint32_t count = state.ep0Remaining < UsbDevice::ControlPacketSize
                                       ? state.ep0Remaining
                                       : UsbDevice::ControlPacketSize;

            for (uint32_t index = 0u; index < count; ++index)
            {
                Put(Ep0Data, state.ep0Data[index]);
            }
            state.ep0Data += count;
            state.ep0Remaining -= count;
            Put(EpEvent, EventInReady);
            if (count == UsbDevice::ControlPacketSize &&
                (state.ep0Remaining != 0u || state.ep0NeedsZlp))
            {
                Modify(IntEnable, FlagInNack, 0u);
            }
            else
            {
                state.ep0Active = false;
            }
        }

        bool StandardDescriptor(const UsbSetupPacket& setup, const Byte** data,
                                uint16_t* length)
        {
            const Byte type = static_cast<Byte>(setup.value >> 8);
            const Byte index = static_cast<Byte>(setup.value & 0xffu);

            if (type == 1u && index == 0u && state.descriptors.device != nullptr)
            {
                *data = state.descriptors.device;
                *length = state.descriptors.device[0];
                return true;
            }
            if (type == 2u && index == 0u && state.descriptors.configuration != nullptr)
            {
                *data = state.descriptors.configuration;
                *length = state.descriptors.configurationLength;
                return true;
            }
            if (type == 3u && index < state.descriptors.stringCount &&
                state.descriptors.strings[index] != nullptr)
            {
                *data = state.descriptors.strings[index];
                *length = state.descriptors.strings[index][0];
                return true;
            }
            return false;
        }

        void HandleSetup()
        {
            UsbSetupPacket setup = {};
            const Byte* data = nullptr;
            uint16_t length = 0u;
            bool handled = false;

            Put(EpEvent, EventSetupReady);
            setup.requestType = static_cast<Byte>(Fetch(Ep0RequestType));
            setup.request = static_cast<Byte>(Fetch(Ep0Request));
            setup.value = Low16(Fetch(Ep0Value));
            setup.index = Low16(Fetch(Ep0Index));
            setup.length = Low16(Fetch(Ep0Length));
            ++state.status.setups;
            PushEvent(UsbEventType::Setup, setup.requestType, setup.request,
                      setup.value, setup.index, setup.length, 0u);

            state.ep0Active = false;
            state.ep0Zlp = false;
            Put(SetStall, 0u);

            if (setup.requestType == 0x80u && setup.request == 6u)
            {
                handled = StandardDescriptor(setup, &data, &length);
            }
            if (!handled && state.handler.controlRequest != nullptr)
            {
                handled = state.handler.controlRequest(state.handler.context, &setup,
                                                       &data, &length);
            }
            if (!handled || (setup.length != 0u && (setup.requestType & 0x80u) == 0u))
            {
                StallControl();
                return;
            }
            if ((setup.requestType & 0x80u) == 0u || setup.length == 0u)
            {
                SendZeroLengthPacket();
                return;
            }

            state.ep0Data = data;
            state.ep0Remaining = length < setup.length ? length : setup.length;
            state.ep0NeedsZlp = length < setup.length &&
                                state.ep0Remaining % UsbDevice::ControlPacketSize == 0u;
            state.ep0Active = true;
            Put(IntFlags, FlagInNack);
            Modify(IntEnable, FlagInNack, 0u);
        }

        void HandleReset()
        {
            state.ep0Active = false;
            state.ep0Zlp = false;
            state.configuration = 0u;
            state.address = 0u;
            state.status.address = 0u;
            state.status.configuration = 0u;
            state.status.resetSeen = true;
            state.status.suspended = false;
            ++state.status.resets;
            Put(UsbFunction, 0u);
            ClearBulkState();
            Put(IntEnable, BaseEnable);
            PushEvent(UsbEventType::Reset, 0u, 0u, 0u, 0u, 0u, 0u);
            if (state.handler.configured != nullptr)
            {
                state.handler.configured(state.handler.context, 0u);
            }
        }

        void TrackFunction()
        {
            const Word function = Fetch(UsbFunction);
            const Byte address = static_cast<Byte>(function & 0x7fu);
            const Byte configuration = static_cast<Byte>((function >> 7) & 3u);

            if (address != state.address)
            {
                state.address = address;
                state.status.address = address;
                PushEvent(UsbEventType::Address, 0u, 0u, address, 0u, 0u, 0u);
            }
            if (configuration != state.configuration)
            {
                state.configuration = configuration;
                state.status.configuration = configuration;
                PushEvent(UsbEventType::Configuration, 0u, 0u, configuration, 0u, 0u,
                          0u);
                if (configuration == 0u)
                {
                    ClearBulkState();
                }
                if (state.handler.configured != nullptr)
                {
                    state.handler.configured(state.handler.context, configuration);
                }
            }
        }

        bool HandleEvents(Word* seen)
        {
            const Word flags = Fetch(IntFlags);
            const Word standard = Fetch(StandardFlags);
            bool worked = false;

            *seen = flags & BaseEnable;

            if (standard != 0u)
            {
                Put(StandardFlags, standard);
                PushEvent(UsbEventType::Standard, 0u, 0u,
                          static_cast<uint16_t>(standard), 0u, 0u, 0u);
                worked = true;
            }
            if ((flags & FlagReset) != 0u)
            {
                Put(IntFlags, FlagReset);
                HandleReset();
                return true;
            }
            if ((flags & FlagSuspend) != 0u)
            {
                Put(IntFlags, FlagSuspend);
                state.status.suspended = true;
                PushEvent(UsbEventType::Suspend, 0u, 0u, 0u, 0u, 0u, 0u);
                worked = true;
            }
            if ((flags & FlagResume) != 0u)
            {
                Put(IntFlags, FlagResume);
                state.status.suspended = false;
                PushEvent(UsbEventType::Resume, 0u, 0u, 0u, 0u, 0u, 0u);
                worked = true;
            }
            if ((flags & FlagSetup) != 0u)
            {
                Put(IntFlags, FlagSetup);
                HandleSetup();
                worked = true;
            }
            if ((flags & FlagInNack) != 0u)
            {
                Put(IntFlags, FlagInNack);
                Modify(IntEnable, 0u, FlagInNack);
                if (state.ep0Active)
                {
                    SendControlChunk();
                }
                worked = true;
            }
            if ((flags & FlagInClear) != 0u)
            {
                Put(IntFlags, FlagInClear);
                Put(EpEvent, EventStatusEnable);
                if (state.ep0Zlp)
                {
                    Put(NullPacket, 0u);
                    Put(BufferClear, BufferClearEp0);
                    state.ep0Zlp = false;
                }
                worked = true;
            }
            if ((flags & FlagStatusClear) != 0u)
            {
                Put(IntFlags, FlagStatusClear);
                worked = true;
            }
            if ((flags & FlagBulkOutNack) != 0u)
            {
                Put(IntFlags, FlagBulkOutNack);
                Modify(IntEnable, 0u, FlagBulkOutNack);
                Put(EpEvent, EventBulkOutNackClear);
                if (state.bulkOutWanted)
                {
                    state.bulkOutWanted = false;
                    Put(EpEvent, EventBulkOutEnable);
                }
                worked = true;
            }
            if ((flags & FlagBulkOutPacket) != 0u)
            {
                Put(IntFlags, FlagBulkOutPacket);
                ++state.status.bulkOutPackets;
                worked = true;
            }
            if ((flags & FlagBulkInClear) != 0u)
            {
                Put(IntFlags, FlagBulkInClear);
                Put(EpEvent, EventBulkInClear);
                ++state.status.bulkInPackets;
                worked = true;
            }
            return worked;
        }

        void Attach()
        {
            Put(DeviceConfig, state.descriptors.endpointTypes != 0u
                                  ? state.descriptors.endpointTypes
                                  : DefaultEndpointTypes);
            Put(UsbFunction, 0u);
            Put(AutoSet, 0u);
            Put(DmaInterrupt, 0u);
            Put(IntFlags, FlagsAll);
            Put(StandardFlags, 0xffu);
            Put(IntEnable, BaseEnable);
            Put(StandardEnable, StandardEnableMask);
            Put(InterruptMask, Fetch(InterruptMask) & ~MaskBit);
            Put(UsbConfig, ConfigEnable);
            state.attached = true;
            state.status.attached = true;
            state.interruptMasked = false;
            state.idleInterrupts = 0u;
            PushEvent(UsbEventType::Attach, 0u, 0u, 0u, 0u, 0u,
                      state.vbusSeen ? 1u : 0u);
        }

        void Detach()
        {
            Put(UsbConfig, 0u);
            Put(IntEnable, 0u);
            Put(StandardEnable, 0u);
            state.attached = false;
            state.status.attached = false;
            state.ep0Active = false;
            state.ep0Zlp = false;
            state.configuration = 0u;
            state.address = 0u;
            state.status.address = 0u;
            state.status.configuration = 0u;
            state.status.resetSeen = false;
            state.waitingPolls = 0u;
            PushEvent(UsbEventType::Detach, 0u, 0u, 0u, 0u, 0u, 0u);
            if (state.handler.configured != nullptr)
            {
                state.handler.configured(state.handler.context, 0u);
            }
        }

        void CheckVbus()
        {
            const bool vbus = (Fetch(GpioInput) & VbusBit) != 0u;

            state.status.vbus = vbus;
            if (vbus)
            {
                state.vbusSeen = true;
            }
            if (!state.attached)
            {
                if (vbus || ++state.waitingPolls >= VbusTimeoutPolls)
                {
                    Attach();
                }
            }
            else if (!vbus && state.vbusSeen)
            {
                state.vbusSeen = false;
                Detach();
            }
        }

        void FlushNotification()
        {
            if (!state.notifyPending || state.configuration == 0u ||
                (Fetch(EpEvent) & EventInterruptInReady) != 0u)
            {
                return;
            }
            for (uint32_t index = 0u; index < state.notifyLength; ++index)
            {
                Put(InterruptInData, state.notifyData[index]);
            }
            Put(EpEvent, EventInterruptInReady);
            state.notifyPending = false;
        }

        bool ServiceHardware(bool polled)
        {
            Word previous = 0u;
            uint32_t repeats = 0u;
            bool any = false;

            if (!state.initialized || !state.attached)
            {
                return false;
            }
            for (uint32_t pass = 0u; pass < ServicePasses; ++pass)
            {
                Word seen = 0u;
                const bool worked = HandleEvents(&seen);

                TrackFunction();
                FlushNotification();
                if (state.handler.service != nullptr && state.configuration != 0u)
                {
                    state.handler.service(state.handler.context, polled);
                }
                if (!worked)
                {
                    break;
                }
                any = true;
                if (seen != 0u && seen == previous && ++repeats >= StuckPasses)
                {
                    Modify(IntEnable, 0u, seen);
                    PushEvent(UsbEventType::Error, 0u, 0u, static_cast<uint16_t>(seen),
                              0u, 0u, 0x57acu);
                    repeats = 0u;
                }
                else if (seen != previous)
                {
                    repeats = 0u;
                }
                previous = seen;
            }
            return any;
        }

        void HandleInterrupt(uint32_t, void*)
        {
            ++state.status.interrupts;
            if (ServiceHardware(false))
            {
                state.idleInterrupts = 0u;
            }
            else if (++state.idleInterrupts >= IdleInterruptLimit &&
                     !state.interruptMasked)
            {
                state.interruptMasked = true;
                Put(InterruptMask, Fetch(InterruptMask) | MaskBit);
                PushEvent(UsbEventType::Error, 0u, 0u, 0u, 0u, 0u, 0x1d1eu);
            }
        }

        Result WaitStall(Word seenBit)
        {
            for (uint32_t spin = 0u; spin < StallWaitLimit; ++spin)
            {
                if ((Fetch(StallFlags) & seenBit) != 0u)
                {
                    return Result::Success;
                }
            }
            return Result::HardwareError;
        }
    }

    Result UsbDevice::Initialize(const UsbDescriptors& descriptors,
                                 const UsbClassHandler& handler)
    {
        const Platform* platform = GetPlatform();

        if (platform == nullptr)
        {
            return Result::NotInitialized;
        }
        if (descriptors.device == nullptr || descriptors.configuration == nullptr ||
            descriptors.configurationLength < 9u || descriptors.device[0] != 18u)
        {
            return Result::InvalidArgument;
        }
        if (platform->delayIterations == nullptr)
        {
            return Result::Unsupported;
        }

        state = {};
        eventHead = 0u;
        eventTail = 0u;
        state.descriptors = descriptors;
        state.handler = handler;

        if (Interrupts::Install(InterruptNumber, HandleInterrupt) != Result::Success)
        {
            return Result::HardwareError;
        }

        Modify(PllConfig, UsbPllEnable, 0u);
        Modify(TftOutputEnable, TftOutputBit, 0u);
        Put(UsbClockConfig, DeviceClockOnly);
        Put(CrystalEnable, 1u);
        platform->delayIterations(platform->context, PllSettleIterations);

        state.savedDeviceSelect = Fetch(DeviceSelect);
        Put(DeviceSelect, state.savedDeviceSelect & DeviceSelectMask);
        Put(CsiGpioSetup, CsiOutputSetup);
        Put(CsiGpioPull, CsiPullSetup);
        Put(UnknownGpio, 0u);
        Put(UsbGpioPort, UsbGpioSetup);

        Put(UsbConfig, 0u);
        state.initialized = true;
        CheckVbus();
        return Result::Success;
    }

    Result UsbDevice::Shutdown()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        state.initialized = false;
        state.attached = false;
        Put(UsbConfig, 0u);
        Put(IntEnable, 0u);
        Put(StandardEnable, 0u);
        Put(UsbClockConfig, 0u);
        Modify(PllConfig, 0u, UsbPllEnable);
        Put(DeviceSelect, state.savedDeviceSelect);
        Interrupts::Uninstall(InterruptNumber);
        return Result::Success;
    }

    Result UsbDevice::PollLocked()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        CheckVbus();
        ServiceHardware(true);
        return Result::Success;
    }

    Result UsbDevice::NotifyWrite(const Byte* data, uint32_t length)
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        if (data == nullptr || length == 0u || length > 8u)
        {
            return Result::InvalidArgument;
        }
        if (state.configuration == 0u)
        {
            return Result::Unsupported;
        }
        for (uint32_t index = 0u; index < length; ++index)
        {
            state.notifyData[index] = data[index];
        }
        state.notifyLength = length;
        state.notifyPending = true;
        FlushNotification();
        return Result::Success;
    }

    bool UsbDevice::NotifyBusy()
    {
        return state.notifyPending || (Fetch(EpEvent) & EventInterruptInReady) != 0u;
    }

    Result UsbDevice::Poll()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        Interrupts::Disable();
        CheckVbus();
        ServiceHardware(true);
        Interrupts::Enable();
        return Result::Success;
    }

    bool UsbDevice::IsInitialized()
    {
        return state.initialized;
    }

    bool UsbDevice::IsConfigured()
    {
        return state.initialized && state.configuration != 0u;
    }

    bool UsbDevice::GetEvent(UsbEvent* event)
    {
        bool found = false;

        if (event == nullptr)
        {
            return false;
        }
        Interrupts::Disable();
        if (eventTail != eventHead)
        {
            *event = events[eventTail];
            eventTail = (eventTail + 1u) % EventCapacity;
            found = true;
        }
        Interrupts::Enable();
        return found;
    }

    Result UsbDevice::GetStatus(UsbStatus* status)
    {
        if (status == nullptr)
        {
            return Result::InvalidArgument;
        }
        Interrupts::Disable();
        *status = state.status;
        Interrupts::Enable();
        return Result::Success;
    }

    void UsbDevice::Report(UsbEventType type, Byte request, uint16_t value,
                           uint16_t length, uint32_t detail)
    {
        if (type == UsbEventType::Command)
        {
            ++state.status.commands;
        }
        PushEvent(type, 0u, request, value, 0u, length, detail);
    }

    Result UsbDevice::BulkOutWait()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        Put(EpEvent, EventBulkOutNackClear);
        state.bulkOutWanted = true;
        Modify(IntEnable, FlagBulkOutNack, 0u);
        return Result::Success;
    }

    Result UsbDevice::BulkOutEnable()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        Put(EpEvent, EventBulkOutEnable);
        return Result::Success;
    }

    bool UsbDevice::BulkOutPending()
    {
        return state.initialized && (Fetch(EpEvent) & EventBulkOutReady) != 0u;
    }

    uint32_t UsbDevice::BulkOutRead(Byte* destination, uint32_t wanted,
                                    uint32_t fallback)
    {
        uint32_t count = Fetch(BulkOutCount) & 0x7fu;
        uint32_t stored = 0u;

        if (count == 0u)
        {
            count = fallback < BulkPacketSize ? fallback : BulkPacketSize;
        }
        if (count > BulkPacketSize)
        {
            count = BulkPacketSize;
        }
        for (uint32_t index = 0u; index < count; ++index)
        {
            const Byte value = static_cast<Byte>(Fetch(BulkOutData));

            if (destination != nullptr && index < wanted)
            {
                destination[index] = value;
                ++stored;
            }
        }
        Put(EpEvent, EventBulkOutReady);
        PushEvent(UsbEventType::BulkOut, 0u, 0u, static_cast<uint16_t>(count), 0u, 0u,
                  0u);
        return stored;
    }

    bool UsbDevice::BulkInBusy()
    {
        return (Fetch(EpEvent) & EventBulkInReady) != 0u;
    }

    Result UsbDevice::BulkInWrite(const Byte* data, uint32_t length)
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        if (data == nullptr || length == 0u || length > BulkPacketSize)
        {
            return Result::InvalidArgument;
        }
        if (BulkInBusy())
        {
            return Result::HardwareError;
        }
        for (uint32_t index = 0u; index < length; ++index)
        {
            Put(BulkInData, data[index]);
        }
        Put(EpEvent, EventBulkInReady);
        PushEvent(UsbEventType::BulkIn, 0u, 0u, static_cast<uint16_t>(length), 0u, 0u,
                  0u);
        return Result::Success;
    }

    Result UsbDevice::StallBulkIn()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        Put(SetStall, StallBulkInSet);
        const Result seen = WaitStall(StallSeenBulkIn);

        Put(StallFlags, StallSeenBulkIn);
        Put(SetStall, 0u);
        ++state.status.stalls;
        PushEvent(UsbEventType::Stall, 0u, 0x81u, 0u, 0u, 0u, 0u);
        return seen;
    }

    Result UsbDevice::StallBulkOut()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        Put(SetStall, StallBulkOutSet);
        const Result seen = WaitStall(StallSeenBulkOut);

        Put(StallFlags, StallSeenBulkOut);
        Put(SetStall, 0u);
        ++state.status.stalls;
        PushEvent(UsbEventType::Stall, 0u, 0x02u, 0u, 0u, 0u, 0u);
        return seen;
    }

    Result UsbDevice::ResetBulk()
    {
        if (!state.initialized)
        {
            return Result::NotInitialized;
        }
        ClearBulkState();
        return Result::Success;
    }
}
