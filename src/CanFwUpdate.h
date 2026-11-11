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

    private:

        using Command = CanFwUpdateProtocol::Command;
        using Status  = CanFwUpdateProtocol::Status;

        static constexpr uint16_t kBlockBytes = CanFwUpdateProtocol::BLOCK_SIZE;
        static constexpr uint32_t kSessionTimeoutMs = 10000;

        uint8_t node_id_;
        bool running_ = false, receiving_ = false, rebootPending_ = false;
        uint32_t expected_ = 0, committed_ = 0, crc_ = 0xFFFFFFFF;
        uint16_t blockLen_ = 0, blockUsed_ = 0;
        uint8_t  nextSeq_  = 0, block_[kBlockBytes] = {};
        uint32_t rebootAt_ = 0;

        uint32_t lastActivityMs_ = 0;

        void process_command(const twai_message_t& m);
        void process_data(   const twai_message_t& m);
        void publish_status(Command cmd, Status status);
        void abortUpdate();
        static uint32_t read32(const uint8_t *p);
        static uint16_t read16(const uint8_t *p);
};
