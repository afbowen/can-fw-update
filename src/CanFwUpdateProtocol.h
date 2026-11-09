// Auto-generated. Do not edit.
#pragma once
#include <stdint.h>

namespace CanFwUpdateProtocol {
constexpr uint16_t CMD_ID_BASE = 0x600;
constexpr uint16_t DATA_ID_BASE = 0x640;
constexpr uint16_t STATUS_ID_BASE = 0x680;
constexpr uint16_t BLOCK_SIZE = 224;

enum class Command : uint8_t {
    BEGIN = 0x81,
    BLOCK = 0x82,
    DATA = 0x83,
    END = 0x84,
    ABORT = 0x85,
};

enum class Status : uint8_t {
    OK = 0,
    BAD_STATE = 1,
    BAD_OFFSET = 2,
    FLASH_ERROR = 3,
    CRC_ERROR = 4,
    BAD_SEQUENCE = 5,
};
} // namespace CanFwUpdateProtocol
