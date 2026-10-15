#include <tdk/UsbLink.hpp>

#include <hslink.h>
#include <tdk/Interrupts.hpp>
#include <tdk/Platform.hpp>

namespace TDK
{
    namespace
    {
        constexpr uint32_t DescriptorStrings = 4u;
        constexpr uint32_t StringBytes = 2u + 2u * 30u;
        constexpr Word LinkEndpointTypes = 0x3a50u;
        constexpr Byte VendorCode = 0x21u;
        constexpr uint32_t IdlePollsBeforeEnable = 4u;
        constexpr uint32_t IdlePollsRepeat = 32u;

        enum class Phase : uint8_t
        {
            Header,
            Streaming,
            Buffered,
            Discard,
            Respond
        };

        struct Handler
        {
            Byte opcode;
            UsbLinkHandler function;
            void* context;
        };

        struct Link
        {
            UsbLinkConfig config;
            Phase phase;
            bool outputArmed;
            uint32_t idlePolls;
            Byte header[HSLINK_COMMAND_SIZE];
            uint32_t headerFill;
            UsbLinkRequest request;
            uint32_t payloadRemaining;
            uint32_t payloadFill;
            Address cursor;
            Byte status;
            Byte responseFlags;
            Byte responseHeader[HSLINK_RESPONSE_SIZE];
            uint32_t headerSent;
            const Byte* replyData;
            Address replyMemory;
            uint32_t replyRemaining;
            bool replyIsMemory;
            Address pendingExecute;
            bool executePending;
            uint32_t commands;
            volatile uint32_t logHead;
            volatile uint32_t logTail;
            bool logNotified;
        };

        Link link = {};
        Handler handlers[UsbLink::MaxCommands] = {};
        uint32_t handlerCount = 0u;

        const Handler* FindHandler(Byte opcode)
        {
            for (uint32_t index = 0u; index < handlerCount; ++index)
            {
                if (handlers[index].opcode == opcode)
                {
                    return &handlers[index];
                }
            }
            return nullptr;
        }
        Byte payloadBuffer[UsbLink::MaxPayload] = {};
        Byte replyBuffer[HSLINK_INFO_SIZE] = {};
        Byte crcBuffer[4] = {};
        Byte logRing[UsbLink::LogCapacity] = {};

        Byte deviceDescriptor[18] = {};
        Byte configurationDescriptor[39] = {};
        Byte languageDescriptor[4] = {4u, 3u, 0x09u, 0x04u};
        Byte stringStorage[DescriptorStrings - 1u][StringBytes] = {};
        const Byte* stringTable[DescriptorStrings] = {};
        Byte microsoftString[18] = {};
        Byte compatibleId[40] = {};

        void BuildString(Byte* destination, const char* text)
        {
            uint32_t count = 0u;

            while (text[count] != '\0' && count < 30u)
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
            const Byte device[18] = {18u,
                                     1u,
                                     0x10u,
                                     0x01u,
                                     0u,
                                     0u,
                                     0u,
                                     8u,
                                     static_cast<Byte>(HSLINK_VENDOR_ID & 0xffu),
                                     static_cast<Byte>(HSLINK_VENDOR_ID >> 8),
                                     static_cast<Byte>(HSLINK_PRODUCT_ID & 0xffu),
                                     static_cast<Byte>(HSLINK_PRODUCT_ID >> 8),
                                     0x00u,
                                     0x01u,
                                     1u,
                                     2u,
                                     3u,
                                     1u};
            const Byte configuration[39] = {9u,
                                            2u,
                                            39u,
                                            0u,
                                            1u,
                                            1u,
                                            0u,
                                            0xc0u,
                                            50u,
                                            9u,
                                            4u,
                                            0u,
                                            0u,
                                            3u,
                                            HSLINK_INTERFACE_CLASS,
                                            HSLINK_INTERFACE_SUBCLASS,
                                            HSLINK_INTERFACE_PROTOCOL,
                                            0u,
                                            7u,
                                            5u,
                                            HSLINK_EP_BULK_IN,
                                            2u,
                                            64u,
                                            0u,
                                            0u,
                                            7u,
                                            5u,
                                            HSLINK_EP_BULK_OUT,
                                            2u,
                                            64u,
                                            0u,
                                            0u,
                                            7u,
                                            5u,
                                            HSLINK_EP_NOTIFY_IN,
                                            3u,
                                            8u,
                                            0u,
                                            10u};
            const Byte compatible[40] = {40u, 0u,  0u,  0u,  0u, 1u, 4u, 0u, 1u,  0u,
                                         0u,  0u,  0u,  0u,  0u, 0u, 0u, 1u, 'W', 'I',
                                         'N', 'U', 'S', 'B', 0u, 0u, 0u, 0u, 0u,  0u,
                                         0u,  0u,  0u,  0u,  0u, 0u, 0u, 0u, 0u,  0u};
            const char signature[] = "MSFT100";

            for (uint32_t index = 0u; index < 18u; ++index)
            {
                deviceDescriptor[index] = device[index];
            }
            for (uint32_t index = 0u; index < 39u; ++index)
            {
                configurationDescriptor[index] = configuration[index];
            }
            for (uint32_t index = 0u; index < 40u; ++index)
            {
                compatibleId[index] = compatible[index];
            }
            BuildString(stringStorage[0], "7OS");
            BuildString(stringStorage[1], link.config.name != nullptr
                                              ? link.config.name
                                              : "Hyperscan Link");
            BuildString(stringStorage[2], "HSLINK0001");
            stringTable[0] = languageDescriptor;
            stringTable[1] = stringStorage[0];
            stringTable[2] = stringStorage[1];
            stringTable[3] = stringStorage[2];
            microsoftString[0] = 18u;
            microsoftString[1] = 3u;
            for (uint32_t index = 0u; index < 7u; ++index)
            {
                microsoftString[2u + index * 2u] = static_cast<Byte>(signature[index]);
                microsoftString[3u + index * 2u] = 0u;
            }
            microsoftString[16] = VendorCode;
            microsoftString[17] = 0u;
        }

        uint32_t Smaller(uint32_t a, uint32_t b)
        {
            return a < b ? a : b;
        }

        void DirectRead(Address address, Byte* destination, uint32_t count)
        {
            uint32_t done = 0u;

            if (((address | reinterpret_cast<uintptr_t>(destination)) & 3u) == 0u)
            {
                for (; done + 4u <= count; done += 4u)
                {
                    const uint32_t word = *reinterpret_cast<volatile uint32_t*>(
                        static_cast<uintptr_t>(address + done));

                    destination[done] = static_cast<Byte>(word);
                    destination[done + 1u] = static_cast<Byte>(word >> 8);
                    destination[done + 2u] = static_cast<Byte>(word >> 16);
                    destination[done + 3u] = static_cast<Byte>(word >> 24);
                }
            }
            for (; done < count; ++done)
            {
                destination[done] = *reinterpret_cast<volatile Byte*>(
                    static_cast<uintptr_t>(address + done));
            }
        }

        void DirectWrite(Address address, const Byte* source, uint32_t count)
        {
            uint32_t done = 0u;

            if ((address & 3u) == 0u)
            {
                for (; done + 4u <= count; done += 4u)
                {
                    const uint32_t word =
                        static_cast<uint32_t>(source[done]) |
                        (static_cast<uint32_t>(source[done + 1u]) << 8) |
                        (static_cast<uint32_t>(source[done + 2u]) << 16) |
                        (static_cast<uint32_t>(source[done + 3u]) << 24);

                    *reinterpret_cast<volatile uint32_t*>(
                        static_cast<uintptr_t>(address + done)) = word;
                }
            }
            for (; done < count; ++done)
            {
                *reinterpret_cast<volatile Byte*>(
                    static_cast<uintptr_t>(address + done)) = source[done];
            }
        }

        void CopyFromMemory(Address address, Byte* destination, uint32_t count)
        {
            if (link.config.memoryRead != nullptr)
            {
                link.config.memoryRead(link.config.context, address, destination,
                                       count);
                return;
            }
            DirectRead(address, destination, count);
        }

        void CopyToMemory(Address address, const Byte* source, uint32_t count)
        {
            if (link.config.memoryWrite != nullptr)
            {
                link.config.memoryWrite(link.config.context, address, source, count);
                return;
            }
            DirectWrite(address, source, count);
        }

        bool Validate(Address address, uint32_t length, bool write)
        {
            if (length != 0u && address + length < address)
            {
                return false;
            }
            return link.config.validate == nullptr ||
                   link.config.validate(link.config.context, address, length, write);
        }

        uint32_t Crc32(Address address, uint32_t length)
        {
            uint32_t crc = 0xffffffffu;
            Byte block[64];

            while (length != 0u)
            {
                const uint32_t count = Smaller(length, sizeof(block));

                CopyFromMemory(address, block, count);
                for (uint32_t index = 0u; index < count; ++index)
                {
                    crc ^= block[index];
                    for (uint32_t bit = 0u; bit < 8u; ++bit)
                    {
                        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
                    }
                }
                address += count;
                length -= count;
            }
            return ~crc;
        }

        void BeginReply(Byte status)
        {
            const uint32_t length = link.replyRemaining;

            if (link.logHead != link.logTail)
            {
                link.responseFlags |= HSLINK_FLAG_LOG_PENDING;
            }
            else
            {
                link.responseFlags &= static_cast<Byte>(~HSLINK_FLAG_LOG_PENDING);
            }
            link.status = status;
            hslink_put16(link.responseHeader, HSLINK_RESPONSE_MAGIC);
            link.responseHeader[2] = status;
            link.responseHeader[3] = link.responseFlags;
            hslink_put32(link.responseHeader + 4, link.request.tag);
            hslink_put32(link.responseHeader + 8, length);
            link.headerSent = 0u;
            link.phase = Phase::Respond;
        }

        void ReplyBuffer(Byte status, const Byte* data, uint32_t length)
        {
            link.replyData = data;
            link.replyRemaining = status == HSLINK_STATUS_OK ? length : 0u;
            link.replyIsMemory = false;
            BeginReply(status);
        }

        void ReplyMemory(Address address, uint32_t length)
        {
            link.replyMemory = address;
            link.replyRemaining = length;
            link.replyIsMemory = true;
            link.replyData = nullptr;
            BeginReply(HSLINK_STATUS_OK);
        }

        void Fail(Byte status)
        {
            ReplyBuffer(status, nullptr, 0u);
        }

        void CommandInfo()
        {
            const UsbLinkConfig& config = link.config;
            const char* name = config.name != nullptr ? config.name : "Hyperscan Link";

            for (uint32_t index = 0u; index < HSLINK_INFO_SIZE; ++index)
            {
                replyBuffer[index] = 0u;
            }
            hslink_put32(replyBuffer, HSLINK_PROTOCOL_VERSION);
            hslink_put32(replyBuffer + 4,
                         config.capabilities | HSLINK_CAP_MEMORY | HSLINK_CAP_LOG |
                             HSLINK_CAP_NOTIFY |
                             (config.execute != nullptr ? HSLINK_CAP_EXECUTE : 0u));
            hslink_put32(replyBuffer + 8, config.ramBase);
            hslink_put32(replyBuffer + 12, config.ramSize);
            hslink_put32(replyBuffer + 16, config.reservedBase);
            hslink_put32(replyBuffer + 20, config.reservedSize);
            hslink_put32(replyBuffer + 24, UsbLink::MaxPayload);
            hslink_put32(replyBuffer + 28, link.responseFlags);
            for (uint32_t index = 0u; index < 16u && name[index] != '\0'; ++index)
            {
                replyBuffer[32u + index] = static_cast<Byte>(name[index]);
            }
            ReplyBuffer(HSLINK_STATUS_OK, replyBuffer, HSLINK_INFO_SIZE);
        }

        void CommandLogRead()
        {
            uint32_t count = 0u;
            const uint32_t limit = Smaller(link.request.length, UsbLink::MaxPayload);

            while (link.logTail != link.logHead && count < limit)
            {
                payloadBuffer[count++] = logRing[link.logTail];
                link.logTail = (link.logTail + 1u) % UsbLink::LogCapacity;
            }
            if (link.logTail == link.logHead)
            {
                link.logNotified = false;
            }
            ReplyBuffer(HSLINK_STATUS_OK, payloadBuffer, count);
        }

        void RunUserCommand()
        {
            const Handler* handler = FindHandler(link.request.opcode);
            UsbLinkReply reply = {nullptr, 0u};

            if (handler == nullptr)
            {
                Fail(HSLINK_STATUS_BAD_COMMAND);
                return;
            }
            const Byte status = handler->function(handler->context, &link.request,
                                                  payloadBuffer, &reply);

            if (status == HSLINK_STATUS_OK && reply.length > HSLINK_MAX_REPLY)
            {
                Fail(HSLINK_STATUS_TOO_LARGE);
                return;
            }
            ReplyBuffer(status, reply.data, reply.length);
        }

        void RunBuiltIn()
        {
            const UsbLinkRequest& request = link.request;

            switch (request.opcode)
            {
            case HSLINK_OP_NOP:
                ReplyBuffer(HSLINK_STATUS_OK, nullptr, 0u);
                break;
            case HSLINK_OP_INFO:
                CommandInfo();
                break;
            case HSLINK_OP_READ_MEMORY:
                if (!Validate(request.argument, request.length, false))
                {
                    Fail(HSLINK_STATUS_FAULT);
                    break;
                }
                ReplyMemory(request.argument, request.length);
                break;
            case HSLINK_OP_WRITE_MEMORY:
                ReplyBuffer(HSLINK_STATUS_OK, nullptr, 0u);
                break;
            case HSLINK_OP_EXECUTE:
                if (link.config.execute == nullptr)
                {
                    Fail(HSLINK_STATUS_UNSUPPORTED);
                    break;
                }
                if (link.config.validateEntry != nullptr &&
                    !link.config.validateEntry(link.config.context, request.argument))
                {
                    Fail(HSLINK_STATUS_FAULT);
                    break;
                }
                link.pendingExecute = request.argument;
                link.executePending = true;
                ReplyBuffer(HSLINK_STATUS_OK, nullptr, 0u);
                break;
            case HSLINK_OP_CACHE:
                if (link.config.flushCache == nullptr)
                {
                    Fail(HSLINK_STATUS_UNSUPPORTED);
                    break;
                }
                if (!Validate(request.argument, request.length, false))
                {
                    Fail(HSLINK_STATUS_FAULT);
                    break;
                }
                link.config.flushCache(link.config.context, request.argument,
                                       request.length);
                ReplyBuffer(HSLINK_STATUS_OK, nullptr, 0u);
                break;
            case HSLINK_OP_CRC32:
                if (!Validate(request.argument, request.length, false))
                {
                    Fail(HSLINK_STATUS_FAULT);
                    break;
                }
                hslink_put32(crcBuffer, Crc32(request.argument, request.length));
                ReplyBuffer(HSLINK_STATUS_OK, crcBuffer, 4u);
                break;
            case HSLINK_OP_LOG_READ:
                CommandLogRead();
                break;
            default:
                RunUserCommand();
                break;
            }
        }

        void BeginCommand()
        {
            UsbLinkRequest& request = link.request;

            link.headerFill = 0u;
            if (hslink_get16(link.header) != HSLINK_COMMAND_MAGIC)
            {
                request.tag = 0u;
                Fail(HSLINK_STATUS_BAD_COMMAND);
                return;
            }
            request.opcode = link.header[2];
            request.flags = link.header[3];
            request.tag = hslink_get32(link.header + 4);
            request.argument = hslink_get32(link.header + 8);
            request.length = hslink_get32(link.header + 12);
            ++link.commands;

            if (request.opcode == HSLINK_OP_WRITE_MEMORY)
            {
                link.cursor = request.argument;
                link.payloadRemaining = request.length;
                if (request.length == 0u)
                {
                    RunBuiltIn();
                    return;
                }
                if (!Validate(request.argument, request.length, true))
                {
                    link.status = HSLINK_STATUS_FAULT;
                    link.phase = Phase::Discard;
                    return;
                }
                link.status = HSLINK_STATUS_OK;
                link.phase = Phase::Streaming;
                return;
            }
            if ((request.flags & HSLINK_CMDFLAG_PAYLOAD) != 0u && request.length != 0u)
            {
                link.payloadRemaining = request.length;
                link.payloadFill = 0u;
                if (FindHandler(request.opcode) == nullptr)
                {
                    link.status = HSLINK_STATUS_BAD_COMMAND;
                    link.phase = Phase::Discard;
                }
                else if (request.length > UsbLink::MaxPayload)
                {
                    link.status = HSLINK_STATUS_TOO_LARGE;
                    link.phase = Phase::Discard;
                }
                else
                {
                    link.phase = Phase::Buffered;
                }
                return;
            }
            RunBuiltIn();
        }

        void FinishPayload()
        {
            if (link.phase == Phase::Discard)
            {
                Fail(link.status);
                return;
            }
            if (link.phase == Phase::Buffered)
            {
                RunUserCommand();
                return;
            }
            RunBuiltIn();
        }

        void Consume(const Byte* data, uint32_t count)
        {
            uint32_t offset = 0u;

            while (offset < count && link.phase != Phase::Respond)
            {
                const uint32_t left = count - offset;

                if (link.phase == Phase::Header)
                {
                    const uint32_t take =
                        Smaller(left, HSLINK_COMMAND_SIZE - link.headerFill);

                    for (uint32_t index = 0u; index < take; ++index)
                    {
                        link.header[link.headerFill + index] = data[offset + index];
                    }
                    link.headerFill += take;
                    offset += take;
                    if (link.headerFill == HSLINK_COMMAND_SIZE)
                    {
                        BeginCommand();
                    }
                    continue;
                }
                const uint32_t take = Smaller(left, link.payloadRemaining);

                if (link.phase == Phase::Streaming)
                {
                    CopyToMemory(link.cursor, data + offset, take);
                    link.cursor += take;
                }
                else if (link.phase == Phase::Buffered)
                {
                    for (uint32_t index = 0u; index < take; ++index)
                    {
                        payloadBuffer[link.payloadFill + index] = data[offset + index];
                    }
                    link.payloadFill += take;
                }
                link.payloadRemaining -= take;
                offset += take;
                if (link.payloadRemaining == 0u)
                {
                    FinishPayload();
                }
            }
        }

        void ResetLink()
        {
            link.phase = Phase::Header;
            link.headerFill = 0u;
            link.outputArmed = false;
            link.idlePolls = 0u;
            link.payloadRemaining = 0u;
            link.replyRemaining = 0u;
            link.headerSent = HSLINK_RESPONSE_SIZE;
        }

        void SendPacket()
        {
            Byte packet[HSLINK_PACKET_SIZE];
            uint32_t filled = 0u;

            if (UsbDevice::BulkInBusy())
            {
                return;
            }
            while (link.headerSent < HSLINK_RESPONSE_SIZE &&
                   filled < HSLINK_PACKET_SIZE)
            {
                packet[filled++] = link.responseHeader[link.headerSent++];
            }
            if (filled < HSLINK_PACKET_SIZE && link.replyRemaining != 0u)
            {
                const uint32_t take =
                    Smaller(HSLINK_PACKET_SIZE - filled, link.replyRemaining);

                if (link.replyIsMemory)
                {
                    CopyFromMemory(link.replyMemory, packet + filled, take);
                    link.replyMemory += take;
                }
                else
                {
                    for (uint32_t index = 0u; index < take; ++index)
                    {
                        packet[filled + index] = link.replyData[index];
                    }
                    link.replyData += take;
                }
                link.replyRemaining -= take;
                filled += take;
            }
            if (filled != 0u)
            {
                UsbDevice::BulkInWrite(packet, filled);
            }
            if (link.headerSent == HSLINK_RESPONSE_SIZE && link.replyRemaining == 0u)
            {
                link.phase = Phase::Header;
                link.headerFill = 0u;
                link.outputArmed = false;
            }
        }

        void ServiceLink(void*, bool polled)
        {
            for (uint32_t pass = 0u; pass < 4u; ++pass)
            {
                if (link.phase == Phase::Respond)
                {
                    if (UsbDevice::BulkInBusy())
                    {
                        return;
                    }
                    SendPacket();
                    continue;
                }
                if (UsbDevice::BulkOutPending())
                {
                    Byte packet[HSLINK_PACKET_SIZE];
                    const uint32_t count =
                        UsbDevice::BulkOutRead(packet, HSLINK_PACKET_SIZE, 0u);

                    link.outputArmed = false;
                    Consume(packet, count);
                    if (link.phase == Phase::Respond)
                    {
                        continue;
                    }
                    if (link.phase == Phase::Header && link.headerFill == 0u)
                    {
                        UsbDevice::BulkOutWait();
                        link.outputArmed = true;
                        link.idlePolls = 0u;
                    }
                    else
                    {
                        UsbDevice::BulkOutEnable();
                    }
                    continue;
                }
                if (!link.outputArmed)
                {
                    if (link.phase == Phase::Header && link.headerFill == 0u)
                    {
                        UsbDevice::BulkOutWait();
                    }
                    else
                    {
                        UsbDevice::BulkOutEnable();
                    }
                    link.outputArmed = true;
                    link.idlePolls = 0u;
                    return;
                }
                if (polled)
                {
                    ++link.idlePolls;
                    if (link.idlePolls == IdlePollsBeforeEnable ||
                        (link.idlePolls > IdlePollsBeforeEnable &&
                         link.idlePolls % IdlePollsRepeat == 0u))
                    {
                        UsbDevice::BulkOutEnable();
                    }
                }
                return;
            }
        }

        void Configured(void*, Byte configuration)
        {
            ResetLink();
            if (configuration != 0u)
            {
                Byte ready[HSLINK_NOTIFY_SIZE] = {
                    HSLINK_NOTIFY_READY, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

                UsbDevice::NotifyWrite(ready, HSLINK_NOTIFY_SIZE);
            }
        }

        bool ControlRequest(void*, const UsbSetupPacket* setup, const Byte** data,
                            uint16_t* length)
        {
            if (setup->requestType == 0x80u && (setup->value >> 8) == 3u &&
                (setup->value & 0xffu) == 0xeeu)
            {
                *data = microsoftString;
                *length = sizeof(microsoftString);
                return true;
            }
            if (setup->requestType == 0xc0u && setup->request == VendorCode &&
                setup->index == 4u)
            {
                *data = compatibleId;
                *length = sizeof(compatibleId);
                return true;
            }
            return false;
        }
    }

    Result UsbLink::Initialize(const UsbLinkConfig& config)
    {
        link = {};
        link.config = config;
        link.headerSent = HSLINK_RESPONSE_SIZE;
        BuildDescriptors();

        const UsbDescriptors descriptors = {
            deviceDescriptor, configurationDescriptor, sizeof(configurationDescriptor),
            stringTable,      DescriptorStrings,       LinkEndpointTypes};
        const UsbClassHandler handler = {nullptr, ControlRequest, Configured,
                                         ServiceLink};

        return UsbDevice::Initialize(descriptors, handler);
    }

    Result UsbLink::RegisterCommand(Byte opcode, UsbLinkHandler handler, void* context)
    {
        if (opcode < HSLINK_OP_DEBUG_STATE || handler == nullptr)
        {
            return Result::InvalidArgument;
        }
        for (uint32_t index = 0u; index < handlerCount; ++index)
        {
            if (handlers[index].opcode == opcode)
            {
                handlers[index] = {opcode, handler, context};
                return Result::Success;
            }
        }
        if (handlerCount == MaxCommands)
        {
            return Result::Unsupported;
        }
        handlers[handlerCount++] = {opcode, handler, context};
        return Result::Success;
    }

    void UsbLink::Log(const char* text, uint32_t length)
    {
        const bool wasEmpty = link.logHead == link.logTail;

        for (uint32_t index = 0u; index < length; ++index)
        {
            const uint32_t next = (link.logHead + 1u) % LogCapacity;

            if (next == link.logTail)
            {
                link.logTail = (link.logTail + 1u) % LogCapacity;
            }
            logRing[link.logHead] = static_cast<Byte>(text[index]);
            link.logHead = next;
        }
        if (wasEmpty && length != 0u && !link.logNotified &&
            UsbDevice::IsConfigured() && !UsbDevice::NotifyBusy())
        {
            Byte notice[HSLINK_NOTIFY_SIZE] = {
                HSLINK_NOTIFY_LOG, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

            link.logNotified =
                UsbDevice::NotifyWrite(notice, HSLINK_NOTIFY_SIZE) == Result::Success;
        }
    }

    Result UsbLink::Notify(Byte type, uint16_t value, uint32_t detail)
    {
        Byte notice[HSLINK_NOTIFY_SIZE] = {type, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

        hslink_put16(notice + 2, value);
        hslink_put32(notice + 4, detail);
        return UsbDevice::NotifyWrite(notice, HSLINK_NOTIFY_SIZE);
    }

    void UsbLink::SetFlags(Byte setBits, Byte clearBits)
    {
        link.responseFlags =
            static_cast<Byte>((link.responseFlags | setBits) & ~clearBits);
    }

    void UsbLink::RunPending()
    {
        Address entry = 0u;
        bool run = false;

        if (!link.executePending)
        {
            return;
        }
        Interrupts::Disable();
        if (link.executePending && link.phase == Phase::Header &&
            link.headerFill == 0u && !UsbDevice::BulkInBusy() &&
            link.config.execute != nullptr)
        {
            entry = link.pendingExecute;
            link.executePending = false;
            run = true;
        }
        if (!run)
        {
            Interrupts::Enable();
            return;
        }
        link.config.execute(link.config.context, entry);
        Interrupts::Enable();
    }

    void UsbLink::CancelPending()
    {
        link.executePending = false;
    }

    bool UsbLink::IsIdle()
    {
        return link.phase == Phase::Header && link.headerFill == 0u;
    }

    uint32_t UsbLink::GetCommandCount()
    {
        return link.commands;
    }

    void UsbLink::ReadMemory(Address address, Byte* destination, uint32_t length)
    {
        DirectRead(address, destination, length);
    }

    void UsbLink::WriteMemory(Address address, const Byte* source, uint32_t length)
    {
        DirectWrite(address, source, length);
    }
}
