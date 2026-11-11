#pragma once

#include <Arduino.h>
#include "driver/twai.h"
#include "CanFwUpdateProtocol.h"

// Prototype only: no authentication, use on a trusted CAN bus.
class CanFwUpdate {
    public:
        CanFwUpdate(uint8_t node_id = 1);
        void poll();   // Call frequently from loop()
        bool handleMessage(const twai_message_t& msg);
        bool confirmFirmware();

    private:

        using Command = CanFwUpdateProtocol::Command;
        using Status  = CanFwUpdateProtocol::Status;

        static constexpr uint16_t kBlockBytes = CanFwUpdateProtocol::BLOCK_SIZE;
        static constexpr uint32_t kSessionTimeoutMs = 10000;  // Time allowed between messages from the update client

        enum class State : uint8_t {
            READY,
            RECEIVING,
            REBOOTING,
            PENDING,
            ROLLBACK
        };

        struct StatusData {
            State state = State::READY;   // Current firmware update state
            bool initialized = false;     // Whether boot-time OTA state has been checked
            bool blockReceiving = false;  // Whether a data block is being received
            uint32_t expected = 0;        // Total firmware image size, in bytes
            uint32_t committed = 0;       // Firmware bytes successfully written to flash
            uint32_t crc = 0xFFFFFFFFU;   // Running firmware CRC-32 accumulator (needs to match CRC sent by update client)
            uint16_t blockLen = 0;        // Expected length of the current data block, in bytes (max is 224 bytes)
            uint16_t blockUsed = 0;       // Bytes collected in the current data block
            uint8_t  nextSeq = 0;         // Expected sequence number of the next CAN data frame
            uint32_t lastActivityMs = 0;  // Time of the last accepted transfer activity
            uint32_t rebootAtMs = 0;      // Scheduled reboot time after a successful update
            uint32_t pendingSinceMs = 0;  // Time the new firmware entered pending validation
        };

        StatusData status_;                    // Runtime status for this updater instance
        uint8_t node_id_;                  // CAN node identifier
        uint8_t block_[kBlockBytes] = {};  // Temporary storage for one firmware data block

        void initializeState();

        void process_command(const twai_message_t& m);
        void process_data(   const twai_message_t& m);
        void publish_status(Command cmd, Status status);
        bool beginUpdate(uint32_t imageSize);
        void abortUpdate();
        static uint32_t read32(const uint8_t *p);
        static uint16_t read16(const uint8_t *p);
};
