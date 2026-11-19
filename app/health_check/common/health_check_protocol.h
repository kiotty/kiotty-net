#ifndef KIOTTY_HEALTH_CHECK_PROTOCOL_H
#define KIOTTY_HEALTH_CHECK_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace health_check
{
    const size_t   kHeaderSize         = 12;
    const size_t   kMaxBody            = 4096;
    const uint32_t kEventIntervalMs    = 200;
    const uint32_t kCursorRows         = 50;
    const uint32_t kCursorRowsPerFetch = 4;
    const uint32_t kAutoFetchCount     = 10;

    enum Kind : uint8_t
    {
        KIND_REQUEST  = 1,
        KIND_RESPONSE = 2,
        KIND_EVENT    = 3
    };

    enum Command : uint8_t
    {
        COMMAND_START_EVENTS = 1,
        COMMAND_STOP_EVENTS  = 2,
        COMMAND_EVENT        = 3,
        COMMAND_OPEN_CURSOR  = 4,
        COMMAND_FETCH_CURSOR = 5,
        COMMAND_CLOSE_CURSOR = 6,
        COMMAND_SINGLE       = 7
    };

    enum Status : uint16_t
    {
        STATUS_OK    = 0,
        STATUS_ERROR = 1,
        STATUS_END   = 2
    };

    struct Header
    {
        uint8_t  kind;
        uint8_t  command;
        uint16_t status;
        uint32_t request_id;
        uint32_t length;
    };

    inline void putUint16(std::string& out, uint16_t value)
    {
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }

    inline void putUint32(std::string& out, uint32_t value)
    {
        out.push_back(static_cast<char>((value >> 24) & 0xFF));
        out.push_back(static_cast<char>((value >> 16) & 0xFF));
        out.push_back(static_cast<char>((value >> 8) & 0xFF));
        out.push_back(static_cast<char>(value & 0xFF));
    }

    inline uint32_t getByte(const char* data, size_t index)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(data[index]));
    }

    inline uint16_t getUint16(const char* data)
    {
        uint32_t high = getByte(data, 0);
        uint32_t low  = getByte(data, 1);
        return static_cast<uint16_t>((high << 8) | low);
    }

    inline uint32_t getUint32(const char* data)
    {
        uint32_t b0 = getByte(data, 0);
        uint32_t b1 = getByte(data, 1);
        uint32_t b2 = getByte(data, 2);
        uint32_t b3 = getByte(data, 3);
        return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
    }

    inline std::string encodeMessage(Kind kind, Command command, Status status, uint32_t request_id, const std::string& body)
    {
        std::string out;
        out.reserve(kHeaderSize + body.size());
        out.push_back(static_cast<char>(kind));
        out.push_back(static_cast<char>(command));
        putUint16(out, static_cast<uint16_t>(status));
        putUint32(out, request_id);
        putUint32(out, static_cast<uint32_t>(body.size()));
        out += body;
        return out;
    }

    inline Header decodeHeader(const char* data)
    {
        Header header;
        header.kind       = static_cast<uint8_t>(data[0]);
        header.command    = static_cast<uint8_t>(data[1]);
        header.status     = getUint16(data + 2);
        header.request_id = getUint32(data + 4);
        header.length     = getUint32(data + 8);
        return header;
    }

    inline const char* commandName(uint8_t command)
    {
        switch (command)
        {
        case COMMAND_START_EVENTS:
            return "START_EVENTS";
        case COMMAND_STOP_EVENTS:
            return "STOP_EVENTS";
        case COMMAND_EVENT:
            return "EVENT";
        case COMMAND_OPEN_CURSOR:
            return "OPEN_CURSOR";
        case COMMAND_FETCH_CURSOR:
            return "FETCH_CURSOR";
        case COMMAND_CLOSE_CURSOR:
            return "CLOSE_CURSOR";
        case COMMAND_SINGLE:
            return "SINGLE";
        default:
            return "UNKNOWN";
        }
    }

    inline const char* statusName(uint16_t status)
    {
        switch (status)
        {
        case STATUS_OK:
            return "OK";
        case STATUS_ERROR:
            return "ERROR";
        case STATUS_END:
            return "END";
        default:
            return "UNKNOWN";
        }
    }
}

#endif
