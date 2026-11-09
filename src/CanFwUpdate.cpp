#include "CanFwUpdate.h"
#include <Update.h>
#include <esp_system.h>
#include <string.h>

using CanFwUpdateProtocol::CMD_ID_BASE;
using CanFwUpdateProtocol::DATA_ID_BASE;
using CanFwUpdateProtocol::STATUS_ID_BASE;

////////////////
// INITIALIZE //
////////////////

CanFwUpdate::CanFwUpdate(gpio_num_t tx, gpio_num_t rx, uint8_t node)
    : tx_(tx), rx_(rx), node_(node) {}



////////////
// PUBLIC //
////////////

bool CanFwUpdate::begin() {
    if (node_ == 0 || node_ > 31) return false;
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(tx_, rx_, TWAI_MODE_NORMAL);
    g.rx_queue_len = 64;
    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    if (twai_start() == ESP_OK) return true;
    twai_driver_uninstall();
    return false;

}


void CanFwUpdate::poll() {
    twai_message_t m;

    // Drain pending traffic; BLOCK ACK naturally provides back-pressure.
    while (twai_receive(&m, 0) == ESP_OK) {
        if (m.extd || m.rtr || m.data_length_code == 0) continue;

        if (     m.identifier == uint32_t(CMD_ID_BASE  + node_)) process_command(m);
        else if (m.identifier == uint32_t(DATA_ID_BASE + node_)) process_data(m);
    }
    if (rebootPending_ && int32_t(millis() - rebootAt_) >= 0) ESP.restart();
}



/////////////
// PRIVATE //
/////////////

// Command Handler //

void CanFwUpdate::process_command(const twai_message_t& m) {
    const uint8_t *d = m.data;

    const Command cmd = static_cast<Command>(d[0]);

    switch (cmd) {

        case Command::BEGIN: {
            if (m.data_length_code != 5) return;

            if (running_) abortUpdate();

            rebootPending_ = false;
            committed_     = 0;

            expected_ = read32(d + 1);
            crc_ = 0xFFFFFFFF;
            if (!expected_ || !Update.begin(expected_, U_FLASH)) {
                publish_status(cmd, Status::FLASH_ERROR);
                return;
            }

            running_ = true;
            publish_status(cmd, Status::OK);
            return;
        }

        case Command::BLOCK: {
            if (m.data_length_code != 7) return;
            if (!running_) { publish_status(cmd, Status::BAD_STATE); return; }

            const uint32_t offset = read32(d + 1);
            const uint16_t len    = read16(d + 5);

            if (offset != committed_) { publish_status(cmd, Status::BAD_OFFSET); return; }

            if (!len || len > kBlockBytes || len > expected_ - committed_) {
                publish_status(cmd, Status::BAD_STATE); return;
            }
            blockLen_ = len;
            blockUsed_ = 0;
            nextSeq_ = 0;
            receiving_ = true;
            publish_status(cmd, Status::OK);
            return;
        }

        case Command::END: {
            if (m.data_length_code != 5) return;

            if (!running_ || receiving_ || committed_ != expected_) {
                publish_status(cmd, Status::BAD_STATE); return;
            }

            if ((crc_ ^ 0xFFFFFFFFU) != read32(d + 1)) {
                publish_status(cmd, Status::CRC_ERROR);
                abortUpdate();
                return;
            }

            if (!Update.end()) {
                publish_status(cmd, Status::FLASH_ERROR);
                abortUpdate();
                return;
            }

            running_ = false;
            publish_status(cmd, Status::OK);
            rebootPending_ = true;
            rebootAt_ = millis() + 1000;
            return;
        }

        case Command::ABORT:
            if (m.data_length_code != 1) return;
            abortUpdate();
            publish_status(cmd, Status::OK);
            return;

        default: return;
    }
}


// DATA HANDLER //

void CanFwUpdate::process_data(const twai_message_t& m) {

    const Command cmd = Command::DATA;

    if (!running_ || !receiving_ || m.data_length_code < 2) return;

    if (m.data[0] != nextSeq_) {
        receiving_ = false;
        publish_status(cmd, Status::BAD_SEQUENCE);
        return;
    }

    const size_t remaining = blockLen_ - blockUsed_;
    const size_t len = (remaining < 7) ? remaining : 7;

    // Exactly 7 bytes per non-final frame, exactly remaining bytes on final frame.
    if (m.data_length_code != 1 + len) {
        receiving_ = false;
        publish_status(cmd, Status::BAD_SEQUENCE);
        return;
    }
    memcpy(block_ + blockUsed_, m.data + 1, len);
    blockUsed_ += len;
    ++nextSeq_;
    if (blockUsed_ != blockLen_) return;
    receiving_ = false;
    if (Update.write(block_, blockLen_) != blockLen_) {
        publish_status(cmd, Status::FLASH_ERROR);
        abortUpdate();
        return;
    }

    // Compute CRC-32/ISO-HDLC
    for (size_t i = 0; i < blockLen_; ++i) {
        crc_ ^= block_[i];
        for (int j = 0; j < 8; ++j)
            crc_ = (crc_ >> 1) ^ ((crc_ & 1U) ? 0xEDB88320U : 0U);
    }
    committed_ += blockLen_;
    publish_status(cmd, Status::OK);
}


// HELPERS //

void CanFwUpdate::abortUpdate() {
    if (running_) Update.abort();
    running_ = receiving_ = false;
}


void CanFwUpdate::publish_status(Command cmd, Status status) {
    twai_message_t m = {};
    m.identifier = STATUS_ID_BASE + node_;
    m.data_length_code = 6;
    m.data[0] = static_cast<uint8_t>(cmd);
    m.data[1] = static_cast<uint8_t>(status);
    for (int i = 0; i < 4; ++i) {
        m.data[2 + i] = uint8_t(committed_ >> (8 * i));
    }
    twai_transmit(&m, pdMS_TO_TICKS(100));
}


uint16_t CanFwUpdate::read16(const uint8_t *p) {
    return uint16_t(p[0]) |
          (uint16_t(p[1]) << 8);
}


uint32_t CanFwUpdate::read32(const uint8_t *p) {
    return uint32_t(p[0])        |
          (uint32_t(p[1]) << 8)  |
          (uint32_t(p[2]) << 16) |
          (uint32_t(p[3]) << 24);
}
