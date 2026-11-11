#include "CanFwUpdate.h"
#include <Update.h>
#include <esp_system.h>
#include <string.h>
#include "esp_ota_ops.h"

using CanFwUpdateProtocol::CMD_ID_BASE;
using CanFwUpdateProtocol::DATA_ID_BASE;
using CanFwUpdateProtocol::STATUS_ID_BASE;


// Override Arduino's auto-validate of update
extern "C" bool verifyRollbackLater() {
    return true;
}



////////////////
// INITIALIZE //
////////////////

CanFwUpdate::CanFwUpdate(uint8_t node_id)
    : node_id_(node_id) {}



////////////
// PUBLIC //
////////////

void CanFwUpdate::poll() {
    initializeState();

    switch (status_.state) {
        case State::RECEIVING:
            // Abort an interrupted firmware transfer.
            if (uint32_t(millis() - status_.lastActivityMs) >= kSessionTimeoutMs)
                abortUpdate();
            break;

        case State::REBOOTING:
            if (int32_t(millis() - status_.rebootAtMs) >= 0)
                ESP.restart();
            break;

        case State::READY:
        case State::PENDING:
        case State::ROLLBACK:
            // Confirmation and rollback timeout to be added separately.
            break;
    }
}


bool CanFwUpdate::handleMessage(const twai_message_t& m) {

    initializeState();

    if (m.extd || m.rtr) return false;

    if (m.identifier == uint32_t(CMD_ID_BASE + node_id_)) {
        if (m.data_length_code > 0) process_command(m);
        return true;
    }

    if (m.identifier == uint32_t(DATA_ID_BASE + node_id_)) {
        if (m.data_length_code > 0) process_data(m);
        return true;
    }

    return false;
}


bool CanFwUpdate::confirmFirmware() {
    initializeState();

    if (status_.state != State::READY && status_.state != State::PENDING)
        return false;

    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(
            esp_ota_get_running_partition(), &otaState) != ESP_OK)
        return false;

    if (otaState == ESP_OTA_IMG_PENDING_VERIFY &&
        esp_ota_mark_app_valid_cancel_rollback() != ESP_OK)
        return false;

    status_.state = State::READY;
    return true;
}



/////////////
// PRIVATE //
/////////////

void CanFwUpdate::initializeState() {
    if (status_.initialized) return;
    status_.initialized = true;

    esp_ota_img_states_t otaState;

    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &otaState) == ESP_OK &&
                                    otaState == ESP_OTA_IMG_PENDING_VERIFY) {

        status_.state = State::PENDING;
        status_.pendingSinceMs = millis();
    }
}


// Command Handler //

void CanFwUpdate::process_command(const twai_message_t& m) {
    const uint8_t* d = m.data;
    const Command cmd = static_cast<Command>(d[0]);
    const State curr_state = status_.state;
    State next_state = curr_state;

    switch (curr_state) {
        case State::READY:
            switch (cmd) {
                case Command::BEGIN:
                    if (m.data_length_code != 5) break;
                    if (beginUpdate(read32(d + 1))) {
                        next_state = State::RECEIVING;
                        publish_status(cmd, Status::OK);
                    } else {
                        abortUpdate();
                        publish_status(cmd, Status::FLASH_ERROR);
                    }
                    break;

                case Command::ABORT:
                    // Idempotent: a host can abort even without an active transfer.
                    if (m.data_length_code == 1)
                        publish_status(cmd, Status::OK);
                    break;

                case Command::BLOCK:
                case Command::END:
                    publish_status(cmd, Status::BAD_STATE);
                    break;

                default:
                    break;
            }
            break;

        case State::RECEIVING:
            switch (cmd) {
                case Command::BEGIN:
                    if (m.data_length_code != 5) break;
                    // Re-start an interrupted session, preserving host retry behavior.
                    abortUpdate();
                    next_state = State::READY;
                    if (beginUpdate(read32(d + 1))) {
                        next_state = State::RECEIVING;
                        publish_status(cmd, Status::OK);
                    } else {
                        abortUpdate();
                        publish_status(cmd, Status::FLASH_ERROR);
                    }
                    break;

                case Command::BLOCK: {
                    if (m.data_length_code != 7) break;
                    const uint32_t offset = read32(d + 1);
                    const uint16_t len = read16(d + 5);

                    if (offset != status_.committed) {
                        publish_status(cmd, Status::BAD_OFFSET);
                        break;
                    }
                    if (!len || len > kBlockBytes || len > status_.expected - status_.committed) {
                        publish_status(cmd, Status::BAD_STATE);
                        break;
                    }

                    status_.blockLen = len;
                    status_.blockUsed = 0;
                    status_.nextSeq = 0;
                    status_.blockReceiving = true;
                    status_.lastActivityMs = millis();
                    publish_status(cmd, Status::OK);
                    break;
                }

                case Command::END:
                    if (m.data_length_code != 5) break;
                    if (status_.blockReceiving || status_.committed != status_.expected) {
                        publish_status(cmd, Status::BAD_STATE);
                        break;
                    }
                    if ((status_.crc ^ 0xFFFFFFFFU) != read32(d + 1)) {
                        publish_status(cmd, Status::CRC_ERROR);
                        abortUpdate();
                        next_state = State::READY;
                        break;
                    }
                    if (!Update.end()) {
                        publish_status(cmd, Status::FLASH_ERROR);
                        abortUpdate();
                        next_state = State::READY;
                        break;
                    }

                    publish_status(cmd, Status::OK);
                    status_.rebootAtMs = millis() + 1000;
                    next_state = State::REBOOTING;
                    break;

                case Command::ABORT:
                    if (m.data_length_code != 1) break;
                    abortUpdate();
                    next_state = State::READY;
                    publish_status(cmd, Status::OK);
                    break;

                default:
                    break;
            }
            break;

        case State::PENDING:
            // The new image must be confirmed before accepting another update.
            // PING and CONFIRM commands will be handled here next.
            switch (cmd) {
                case Command::BEGIN:
                case Command::BLOCK:
                case Command::END:
                case Command::ABORT:
                    publish_status(cmd, Status::BAD_STATE);
                    break;
                default:
                    break;
            }
            break;

        case State::REBOOTING:
        case State::ROLLBACK:
            // Don't start another operation while transitioning to another image.
            switch (cmd) {
                case Command::BEGIN:
                case Command::BLOCK:
                case Command::END:
                case Command::ABORT:
                    publish_status(cmd, Status::BAD_STATE);
                    break;
                default:
                    break;
            }
            break;
    }

    status_.state = next_state;
}


// DATA HANDLER //

void CanFwUpdate::process_data(const twai_message_t& m) {

    const Command cmd = Command::DATA;

    if (status_.state != State::RECEIVING || !status_.blockReceiving ||
        m.data_length_code < 2) return;

    if (m.data[0] != status_.nextSeq) {
        status_.blockReceiving = false;
        publish_status(cmd, Status::BAD_SEQUENCE);
        return;
    }

    const size_t remaining = status_.blockLen - status_.blockUsed;
    const size_t len = (remaining < 7) ? remaining : 7;

    // Exactly 7 bytes per non-final frame, exactly remaining bytes on final frame.
    if (m.data_length_code != 1 + len) {
        status_.blockReceiving = false;
        publish_status(cmd, Status::BAD_SEQUENCE);
        return;
    }
    memcpy(block_ + status_.blockUsed, m.data + 1, len);
    status_.blockUsed += len;
    ++status_.nextSeq;
    status_.lastActivityMs = millis();
    if (status_.blockUsed != status_.blockLen) return;
    status_.blockReceiving = false;
    uint32_t start = millis();
    size_t written = Update.write(block_, status_.blockLen);
    Serial.printf("Flash write: %lu ms\r\n", millis() - start);
    if (written != status_.blockLen) {
        publish_status(cmd, Status::FLASH_ERROR);
        abortUpdate();
        return;
    }

    // Compute CRC-32/ISO-HDLC
    for (size_t i = 0; i < status_.blockLen; ++i) {
        status_.crc ^= block_[i];
        for (int j = 0; j < 8; ++j)
            status_.crc = (status_.crc >> 1) ^ ((status_.crc & 1U) ? 0xEDB88320U : 0U);
    }
    status_.committed += status_.blockLen;
    publish_status(cmd, Status::OK);
}


// HELPERS //

bool CanFwUpdate::beginUpdate(uint32_t imageSize) {
    status_.expected = imageSize;
    status_.committed = 0;
    status_.crc = 0xFFFFFFFFU;
    status_.blockLen = 0;
    status_.blockUsed = 0;
    status_.nextSeq = 0;
    status_.blockReceiving = false;

    if (!status_.expected || !Update.begin(status_.expected, U_FLASH))
        return false;

    status_.lastActivityMs = millis();
    return true;
}


void CanFwUpdate::abortUpdate() {
    if (status_.state == State::RECEIVING)
        Update.abort();

    status_.state = State::READY;
    status_.blockReceiving = false;
    status_.expected = 0;
    status_.committed = 0;
    status_.blockLen = 0;
    status_.blockUsed = 0;
    status_.nextSeq = 0;
    status_.lastActivityMs = 0;
    status_.crc = 0xFFFFFFFFU;
}

void CanFwUpdate::publish_status(Command cmd, Status status) {
    twai_message_t m = {};
    m.identifier = STATUS_ID_BASE + node_id_;
    m.data_length_code = 6;
    m.data[0] = static_cast<uint8_t>(cmd);
    m.data[1] = static_cast<uint8_t>(status);
    for (int i = 0; i < 4; ++i) {
        m.data[2 + i] = uint8_t(status_.committed >> (8 * i));
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
