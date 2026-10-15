#include <tdk/UsbMassStorage.hpp>

namespace TDK
{
    namespace
    {
        constexpr uint32_t CommandSignature = 0x43425355u;
        constexpr uint32_t StatusSignature = 0x53425355u;
        constexpr uint32_t CommandBlockSize = 31u;
        constexpr uint32_t StatusBlockSize = 13u;
        constexpr uint32_t InquiryLength = 36u;
        constexpr uint32_t DeviceDescriptorSize = 18u;
        constexpr uint32_t ConfigurationDescriptorSize = 32u;
        constexpr uint32_t StringBufferSize =
            2u + UsbMassStorage::MaxStringCharacters * 2u;
        constexpr uint32_t IdlePollsBeforeEnable = 4u;
        constexpr uint32_t IdlePollsRepeat = 32u;

        constexpr Byte StatusPassed = 0u;
        constexpr Byte StatusFailed = 1u;
        constexpr Byte StatusPhaseError = 2u;

        constexpr Byte SenseNone = 0u;
        constexpr Byte SenseNotReady = 2u;
        constexpr Byte SenseMediumError = 3u;
        constexpr Byte SenseIllegalRequest = 5u;
        constexpr Byte SenseDataProtect = 7u;

        enum class Phase : uint8_t
        {
            Idle,
            DataIn,
            DataOut,
            Status,
            StatusSent
        };

        struct Transport
        {
            UsbIdentity identity;
            UsbStorage storage;
            bool hasStorage;
            bool ready;
            Phase phase;
            bool outputArmed;
            uint32_t idlePolls;
            uint32_t commandCount;

            uint32_t tag;
            uint32_t expected;
            uint32_t planned;
            uint32_t transferred;
            Byte opcode;
            Byte status;
            Byte senseKey;
            Byte senseCode;
            Byte senseQualifier;

            Byte scratch[64];
            Byte commandBlock[32];
            const Byte* source;
            uint32_t sourceRemaining;
            bool streaming;
            uint32_t block;
            uint32_t blocksLeft;
            uint32_t sectorPosition;
            Byte sector[UsbMassStorage::BlockSize];
            bool writing;
            bool discarding;
            uint32_t writeBytes;
        };

        Transport transport = {};
        Byte deviceDescriptor[DeviceDescriptorSize] = {};
        Byte configurationDescriptor[ConfigurationDescriptorSize] = {};
        Byte languageDescriptor[4] = {4u, 3u, 0x09u, 0x04u};
        Byte manufacturerDescriptor[StringBufferSize] = {};
        Byte productDescriptor[StringBufferSize] = {};
        Byte serialDescriptor[StringBufferSize] = {};
        const Byte* stringTable[4] = {};
        const Byte maxLogicalUnit[1] = {0u};

        void BuildString(Byte* destination, const char* text)
        {
            uint32_t count = 0u;

            while (text != nullptr && text[count] != '\0' &&
                   count < UsbMassStorage::MaxStringCharacters)
            {
                destination[2u + count * 2u] = static_cast<Byte>(text[count]);
                destination[3u + count * 2u] = 0u;
                ++count;
            }
            destination[0] = static_cast<Byte>(2u + count * 2u);
            destination[1] = 3u;
        }

        void BuildDescriptors()
        {
            const UsbIdentity& id = transport.identity;
            const Byte device[DeviceDescriptorSize] = {
                18u,
                1u,
                0x10u,
                0x01u,
                0u,
                0u,
                0u,
                8u,
                static_cast<Byte>(id.vendorId & 0xffu),
                static_cast<Byte>(id.vendorId >> 8),
                static_cast<Byte>(id.productId & 0xffu),
                static_cast<Byte>(id.productId >> 8),
                static_cast<Byte>(id.deviceVersion & 0xffu),
                static_cast<Byte>(id.deviceVersion >> 8),
                1u,
                2u,
                3u,
                1u};
            const Byte configuration[ConfigurationDescriptorSize] = {
                9u,  2u, 32u, 0u, 1u, 1u,    0u, 0xc0u, 50u, 9u,    4u,
                0u,  0u, 2u,  8u, 6u, 0x50u, 0u, 7u,    5u,  0x81u, 2u,
                64u, 0u, 0u,  7u, 5u, 0x02u, 2u, 64u,   0u,  0u};

            for (uint32_t index = 0u; index < DeviceDescriptorSize; ++index)
            {
                deviceDescriptor[index] = device[index];
            }
            for (uint32_t index = 0u; index < ConfigurationDescriptorSize; ++index)
            {
                configurationDescriptor[index] = configuration[index];
            }
            BuildString(manufacturerDescriptor, id.manufacturer);
            BuildString(productDescriptor, id.product);
            BuildString(serialDescriptor, id.serial);
            stringTable[0] = languageDescriptor;
            stringTable[1] = manufacturerDescriptor;
            stringTable[2] = productDescriptor;
            stringTable[3] = serialDescriptor;
        }

        uint32_t BigEndian32(const Byte* data)
        {
            return (static_cast<uint32_t>(data[0]) << 24) |
                   (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) | data[3];
        }

        void PutBigEndian32(Byte* data, uint32_t value)
        {
            data[0] = static_cast<Byte>(value >> 24);
            data[1] = static_cast<Byte>(value >> 16);
            data[2] = static_cast<Byte>(value >> 8);
            data[3] = static_cast<Byte>(value);
        }

        uint32_t LittleEndian32(const Byte* data)
        {
            return (static_cast<uint32_t>(data[3]) << 24) |
                   (static_cast<uint32_t>(data[2]) << 16) |
                   (static_cast<uint32_t>(data[1]) << 8) | data[0];
        }

        void PutLittleEndian32(Byte* data, uint32_t value)
        {
            data[0] = static_cast<Byte>(value);
            data[1] = static_cast<Byte>(value >> 8);
            data[2] = static_cast<Byte>(value >> 16);
            data[3] = static_cast<Byte>(value >> 24);
        }

        uint32_t Smaller(uint32_t a, uint32_t b)
        {
            return a < b ? a : b;
        }

        void SetSense(Byte key, Byte code, Byte qualifier)
        {
            transport.senseKey = key;
            transport.senseCode = code;
            transport.senseQualifier = qualifier;
        }

        void Fail(Byte key, Byte code, Byte qualifier)
        {
            transport.status = StatusFailed;
            SetSense(key, code, qualifier);
        }

        void CopyText(Byte* destination, const char* text, uint32_t width)
        {
            uint32_t index = 0u;

            for (; text != nullptr && text[index] != '\0' && index < width; ++index)
            {
                destination[index] = static_cast<Byte>(text[index]);
            }
            for (; index < width; ++index)
            {
                destination[index] = ' ';
            }
        }

        void ResetTransport()
        {
            transport.phase = Phase::Idle;
            transport.outputArmed = false;
            transport.idlePolls = 0u;
            transport.source = nullptr;
            transport.sourceRemaining = 0u;
            transport.streaming = false;
            transport.writing = false;
            transport.discarding = false;
            transport.planned = 0u;
            transport.transferred = 0u;
        }

        void Respond(uint32_t length)
        {
            transport.source = transport.scratch;
            transport.sourceRemaining = length;
            transport.planned = length;
        }

        bool MediumPresent()
        {
            return transport.hasStorage && transport.storage.blockCount != 0u &&
                   transport.storage.read != nullptr;
        }

        bool NeedMedium()
        {
            if (!MediumPresent())
            {
                Fail(SenseNotReady, 0x3au, 0u);
                return false;
            }
            return true;
        }

        void CommandInquiry(const Byte* cb)
        {
            const uint32_t allocation = cb[4];

            if ((cb[1] & 1u) != 0u)
            {
                Fail(SenseIllegalRequest, 0x24u, 0u);
                return;
            }
            for (uint32_t index = 0u; index < InquiryLength; ++index)
            {
                transport.scratch[index] = 0u;
            }
            transport.scratch[1] = 0x80u;
            transport.scratch[2] = 2u;
            transport.scratch[3] = 2u;
            transport.scratch[4] = 31u;
            CopyText(&transport.scratch[8], transport.identity.scsiVendor, 8u);
            CopyText(&transport.scratch[16], transport.identity.scsiProduct, 16u);
            CopyText(&transport.scratch[32], transport.identity.scsiRevision, 4u);
            Respond(Smaller(InquiryLength, allocation));
        }

        void CommandRequestSense(const Byte* cb)
        {
            for (uint32_t index = 0u; index < 18u; ++index)
            {
                transport.scratch[index] = 0u;
            }
            transport.scratch[0] = 0x70u;
            transport.scratch[2] = transport.senseKey;
            transport.scratch[7] = 10u;
            transport.scratch[12] = transport.senseCode;
            transport.scratch[13] = transport.senseQualifier;
            Respond(Smaller(18u, cb[4]));
            SetSense(SenseNone, 0u, 0u);
        }

        void CommandModeSense6(const Byte* cb)
        {
            transport.scratch[0] = 3u;
            transport.scratch[1] = 0u;
            transport.scratch[2] = transport.storage.writable ? 0u : 0x80u;
            transport.scratch[3] = 0u;
            Respond(Smaller(4u, cb[4]));
        }

        void CommandModeSense10(const Byte* cb)
        {
            const uint32_t allocation = (static_cast<uint32_t>(cb[7]) << 8) | cb[8];

            for (uint32_t index = 0u; index < 8u; ++index)
            {
                transport.scratch[index] = 0u;
            }
            transport.scratch[1] = 6u;
            transport.scratch[3] = transport.storage.writable ? 0u : 0x80u;
            Respond(Smaller(8u, allocation));
        }

        void CommandReadFormatCapacities(const Byte* cb)
        {
            const uint32_t allocation = (static_cast<uint32_t>(cb[7]) << 8) | cb[8];
            const bool present = MediumPresent();

            for (uint32_t index = 0u; index < 12u; ++index)
            {
                transport.scratch[index] = 0u;
            }
            transport.scratch[3] = 8u;
            PutBigEndian32(&transport.scratch[4],
                           present ? transport.storage.blockCount : 0u);
            transport.scratch[8] = present ? 2u : 3u;
            transport.scratch[10] = static_cast<Byte>(UsbMassStorage::BlockSize >> 8);
            transport.scratch[11] = static_cast<Byte>(UsbMassStorage::BlockSize);
            Respond(Smaller(12u, allocation));
        }

        void CommandReadCapacity()
        {
            if (!NeedMedium())
            {
                return;
            }
            PutBigEndian32(&transport.scratch[0], transport.storage.blockCount - 1u);
            PutBigEndian32(&transport.scratch[4], UsbMassStorage::BlockSize);
            Respond(8u);
        }

        bool CheckRange(uint32_t block, uint32_t count)
        {
            if (!NeedMedium())
            {
                return false;
            }
            if (block > transport.storage.blockCount ||
                count > transport.storage.blockCount - block)
            {
                Fail(SenseIllegalRequest, 0x21u, 0u);
                return false;
            }
            return true;
        }

        void CommandRead(const Byte* cb)
        {
            const uint32_t block = BigEndian32(&cb[2]);
            const uint32_t count = (static_cast<uint32_t>(cb[7]) << 8) | cb[8];

            if (!CheckRange(block, count) || count == 0u)
            {
                return;
            }
            transport.streaming = true;
            transport.block = block;
            transport.blocksLeft = count;
            transport.sectorPosition = UsbMassStorage::BlockSize;
            transport.planned = count * UsbMassStorage::BlockSize;
        }

        void CommandWrite(const Byte* cb)
        {
            const uint32_t block = BigEndian32(&cb[2]);
            const uint32_t count = (static_cast<uint32_t>(cb[7]) << 8) | cb[8];

            if (!CheckRange(block, count))
            {
                return;
            }
            if (!transport.storage.writable || transport.storage.write == nullptr)
            {
                Fail(SenseDataProtect, 0x27u, 0u);
                return;
            }
            transport.writing = true;
            transport.block = block;
            transport.blocksLeft = count;
            transport.sectorPosition = 0u;
            transport.writeBytes = count * UsbMassStorage::BlockSize;
        }

        void ExecuteCommand(const Byte* cb)
        {
            transport.status = StatusPassed;
            transport.source = nullptr;
            transport.sourceRemaining = 0u;
            transport.streaming = false;
            transport.writing = false;
            transport.planned = 0u;
            switch (cb[0])
            {
            case 0x00u:
                if (NeedMedium())
                {
                    SetSense(SenseNone, 0u, 0u);
                }
                break;
            case 0x03u:
                CommandRequestSense(cb);
                break;
            case 0x12u:
                CommandInquiry(cb);
                break;
            case 0x1au:
                CommandModeSense6(cb);
                break;
            case 0x5au:
                CommandModeSense10(cb);
                break;
            case 0x1bu:
            case 0x1eu:
            case 0x2fu:
            case 0x35u:
                break;
            case 0x23u:
                CommandReadFormatCapacities(cb);
                break;
            case 0x25u:
                CommandReadCapacity();
                break;
            case 0x28u:
                CommandRead(cb);
                break;
            case 0x2au:
                CommandWrite(cb);
                break;
            default:
                Fail(SenseIllegalRequest, 0x20u, 0u);
                break;
            }
        }

        bool HasOutput()
        {
            return transport.source != nullptr || transport.streaming;
        }

        void BeginStatus()
        {
            transport.phase = Phase::Status;
        }

        void FinishDataIn()
        {
            if (transport.transferred < transport.expected &&
                transport.transferred % UsbDevice::BulkPacketSize == 0u)
            {
                UsbDevice::StallBulkIn();
            }
            BeginStatus();
        }

        void FinishDataOut()
        {
            transport.writing = false;
            transport.discarding = false;
            BeginStatus();
        }

        void StartCommand(uint32_t length)
        {
            const Byte* command = transport.commandBlock;
            const bool hostIn = (command[12] & 0x80u) != 0u;
            const uint32_t signature = LittleEndian32(command);

            transport.tag = LittleEndian32(command + 4);
            transport.expected = LittleEndian32(command + 8);
            transport.opcode = command[15];
            transport.transferred = 0u;
            transport.status = StatusPassed;
            ++transport.commandCount;

            if (length != CommandBlockSize || signature != CommandSignature ||
                (command[12] & 0x7fu) != 0u || (command[13] & 0x0fu) != 0u ||
                command[14] == 0u || command[14] > 16u)
            {
                UsbDevice::Report(UsbEventType::Error, command[15],
                                  static_cast<uint16_t>(length), 0u, signature);
                transport.status = StatusPhaseError;
                transport.expected = 0u;
                UsbDevice::StallBulkIn();
                UsbDevice::StallBulkOut();
                ResetTransport();
                return;
            }

            Byte cb[16];

            for (uint32_t index = 0u; index < 16u; ++index)
            {
                cb[index] = command[15 + index];
            }
            ExecuteCommand(cb);
            if (HasOutput() && transport.planned == 0u)
            {
                transport.source = nullptr;
                transport.streaming = false;
            }

            if (HasOutput())
            {
                transport.planned = Smaller(transport.planned, transport.expected);
                if (transport.expected == 0u || !hostIn)
                {
                    transport.status = StatusPhaseError;
                    transport.planned = 0u;
                    transport.streaming = false;
                    transport.source = nullptr;
                    BeginStatus();
                    return;
                }
                transport.phase = Phase::DataIn;
                return;
            }
            if (transport.writing)
            {
                if (hostIn)
                {
                    transport.status = StatusPhaseError;
                    transport.writing = false;
                    UsbDevice::StallBulkIn();
                    BeginStatus();
                    return;
                }
                if (transport.expected == 0u)
                {
                    transport.status = StatusPhaseError;
                    transport.writing = false;
                    BeginStatus();
                    return;
                }
                if (transport.expected != transport.writeBytes)
                {
                    transport.status = StatusPhaseError;
                    transport.writing = false;
                    transport.discarding = true;
                }
                transport.phase = Phase::DataOut;
                transport.sectorPosition = 0u;
                UsbDevice::BulkOutEnable();
                return;
            }
            if (transport.expected != 0u)
            {
                if (hostIn)
                {
                    UsbDevice::StallBulkIn();
                    BeginStatus();
                }
                else
                {
                    transport.phase = Phase::DataOut;
                    transport.discarding = true;
                    transport.sectorPosition = 0u;
                    UsbDevice::BulkOutEnable();
                }
                return;
            }
            BeginStatus();
        }

        uint32_t FillPacket(Byte* packet, uint32_t wanted)
        {
            uint32_t filled = 0u;

            if (transport.source != nullptr)
            {
                filled = Smaller(wanted, transport.sourceRemaining);
                for (uint32_t index = 0u; index < filled; ++index)
                {
                    packet[index] = transport.source[index];
                }
                transport.source += filled;
                transport.sourceRemaining -= filled;
                return filled;
            }
            while (filled < wanted)
            {
                if (transport.sectorPosition >= UsbMassStorage::BlockSize)
                {
                    if (transport.blocksLeft == 0u)
                    {
                        break;
                    }
                    if (!transport.storage.read(transport.storage.context,
                                                transport.block, transport.sector))
                    {
                        Fail(SenseMediumError, 0x11u, 0u);
                        transport.blocksLeft = 0u;
                        break;
                    }
                    ++transport.block;
                    --transport.blocksLeft;
                    transport.sectorPosition = 0u;
                }
                const uint32_t chunk =
                    Smaller(wanted - filled,
                            UsbMassStorage::BlockSize - transport.sectorPosition);

                for (uint32_t index = 0u; index < chunk; ++index)
                {
                    packet[filled + index] =
                        transport.sector[transport.sectorPosition + index];
                }
                transport.sectorPosition += chunk;
                filled += chunk;
            }
            return filled;
        }

        void PumpDataIn()
        {
            Byte packet[UsbDevice::BulkPacketSize];

            if (UsbDevice::BulkInBusy())
            {
                return;
            }
            if (transport.transferred < transport.planned)
            {
                const uint32_t count = FillPacket(
                    packet, Smaller(UsbDevice::BulkPacketSize,
                                    transport.planned - transport.transferred));

                if (count != 0u)
                {
                    UsbDevice::BulkInWrite(packet, count);
                    transport.transferred += count;
                    return;
                }
                transport.planned = transport.transferred;
            }
            FinishDataIn();
        }

        void StoreSector()
        {
            if (!transport.discarding && transport.writing)
            {
                if (!transport.storage.write(transport.storage.context, transport.block,
                                             transport.sector))
                {
                    Fail(SenseMediumError, 0x03u, 0u);
                    transport.discarding = true;
                }
                ++transport.block;
                if (transport.blocksLeft != 0u)
                {
                    --transport.blocksLeft;
                }
            }
            transport.sectorPosition = 0u;
        }

        void PumpDataOut()
        {
            Byte packet[UsbDevice::BulkPacketSize];

            if (!UsbDevice::BulkOutPending())
            {
                return;
            }
            const uint32_t count = UsbDevice::BulkOutRead(
                packet,
                Smaller(UsbDevice::BulkPacketSize,
                        transport.expected - transport.transferred),
                0u);

            for (uint32_t index = 0u; index < count; ++index)
            {
                if (transport.writing && !transport.discarding &&
                    transport.blocksLeft != 0u)
                {
                    transport.sector[transport.sectorPosition] = packet[index];
                    if (++transport.sectorPosition == UsbMassStorage::BlockSize)
                    {
                        StoreSector();
                    }
                }
            }
            transport.transferred += count;
            if (transport.transferred >= transport.expected || count == 0u)
            {
                FinishDataOut();
                return;
            }
            UsbDevice::BulkOutEnable();
        }

        void SendStatus()
        {
            Byte csw[StatusBlockSize];

            if (UsbDevice::BulkInBusy())
            {
                return;
            }
            const uint32_t residue = transport.expected > transport.transferred
                                         ? transport.expected - transport.transferred
                                         : 0u;

            PutLittleEndian32(csw, StatusSignature);
            PutLittleEndian32(csw + 4, transport.tag);
            PutLittleEndian32(csw + 8, residue);
            csw[12] = transport.status;
            UsbDevice::BulkInWrite(csw, StatusBlockSize);
            UsbDevice::Report(UsbEventType::Command, transport.opcode, transport.status,
                              static_cast<uint16_t>(transport.expected),
                              (static_cast<uint32_t>(transport.senseKey) << 16) |
                                  (static_cast<uint32_t>(transport.senseCode) << 8) |
                                  transport.senseQualifier);
            transport.phase = Phase::StatusSent;
        }

        void PumpIdle(bool polled)
        {
            if (UsbDevice::BulkOutPending())
            {
                const uint32_t count = UsbDevice::BulkOutRead(
                    transport.commandBlock, CommandBlockSize, CommandBlockSize);

                transport.outputArmed = false;
                StartCommand(count);
                return;
            }
            if (!transport.outputArmed)
            {
                UsbDevice::BulkOutWait();
                transport.outputArmed = true;
                transport.idlePolls = 0u;
                return;
            }
            if (polled)
            {
                ++transport.idlePolls;
                if (transport.idlePolls == IdlePollsBeforeEnable ||
                    (transport.idlePolls > IdlePollsBeforeEnable &&
                     transport.idlePolls % IdlePollsRepeat == 0u))
                {
                    UsbDevice::BulkOutEnable();
                }
            }
        }

        void ServiceTransport(void*, bool polled)
        {
            switch (transport.phase)
            {
            case Phase::Idle:
                PumpIdle(polled);
                break;
            case Phase::DataIn:
                PumpDataIn();
                break;
            case Phase::DataOut:
                PumpDataOut();
                break;
            case Phase::Status:
                SendStatus();
                break;
            case Phase::StatusSent:
                if (!UsbDevice::BulkInBusy())
                {
                    ResetTransport();
                }
                break;
            }
        }

        void Configured(void*, Byte configuration)
        {
            ResetTransport();
            transport.ready = configuration != 0u;
            if (configuration == 0u)
            {
                SetSense(SenseNone, 0u, 0u);
            }
        }

        bool ControlRequest(void*, const UsbSetupPacket* setup, const Byte** data,
                            uint16_t* length)
        {
            if (setup->requestType == 0xa1u && setup->request == 0xfeu &&
                setup->value == 0u)
            {
                *data = maxLogicalUnit;
                *length = 1u;
                return true;
            }
            if (setup->requestType == 0x21u && setup->request == 0xffu &&
                setup->value == 0u)
            {
                ResetTransport();
                UsbDevice::ResetBulk();
                *data = nullptr;
                *length = 0u;
                return true;
            }
            return false;
        }
    }

    Result UsbMassStorage::Initialize(const UsbIdentity& identity,
                                      const UsbStorage* storage)
    {
        transport = {};
        transport.identity = identity;
        if (storage != nullptr)
        {
            transport.storage = *storage;
            transport.hasStorage = true;
        }
        SetSense(SenseNone, 0u, 0u);
        BuildDescriptors();

        const UsbDescriptors descriptors = {deviceDescriptor,
                                            configurationDescriptor,
                                            ConfigurationDescriptorSize,
                                            stringTable,
                                            4u,
                                            0u};
        const UsbClassHandler handler = {nullptr, ControlRequest, Configured,
                                         ServiceTransport};

        return UsbDevice::Initialize(descriptors, handler);
    }

    Result UsbMassStorage::Poll()
    {
        return UsbDevice::Poll();
    }

    bool UsbMassStorage::IsReady()
    {
        return transport.ready;
    }

    uint32_t UsbMassStorage::GetCommandCount()
    {
        return transport.commandCount;
    }
}
