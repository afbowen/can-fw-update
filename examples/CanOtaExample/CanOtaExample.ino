#include <Arduino.h>
#include <CanFwUpdate.h>

// Match these GPIOs to your board/transceiver wiring.
constexpr gpio_num_t CAN_TX = GPIO_NUM_2;
constexpr gpio_num_t CAN_RX = GPIO_NUM_3;
constexpr uint8_t NODE_ID = 1;
constexpr char VERSION[] = "v4";
constexpr uint32_t HEARTBEAT_ID = 0x123;


CanFwUpdate updater(NODE_ID);

uint16_t counter = 0;
uint32_t heartbeatCounter = 0;


/////////////
// HEPLERS //
/////////////

void sendHeartbeat() {
    twai_message_t msg = {};
    msg.identifier = HEARTBEAT_ID;
    msg.data_length_code = 4;
    memcpy(msg.data, &heartbeatCounter, sizeof(heartbeatCounter));

    esp_err_t err = twai_transmit(&msg, 0);

    Serial.printf("Heartbeat %lu: %s\r\n",
                  heartbeatCounter, esp_err_to_name(err));

    if (err == ESP_OK) heartbeatCounter++;
}


bool initCAN() {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX,
                                                          CAN_RX,
                                                          TWAI_MODE_NORMAL);
    twai_timing_config_t t = TWAI_TIMING_CONFIG_1MBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    g.rx_queue_len = 64;

    if (twai_driver_install(&g, &t, &f) != ESP_OK)
        return false;

    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }

    return true;
}



//////////
// MAIN //
//////////

void setup() {

    // Pulling GPIO 21 low to disable transceiver standby (active)
    // The board used for this test has GPIO 21 tied to the CAN transceiver's standby pin; it
    // defaults to standby unless pulled low by the MCU for ultra-low-power sleep.
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);

    Serial.begin(115200);
    delay(300);
    Serial.printf("CAN OTA example: %s\r\n", VERSION);
    Serial.printf("CAN: %s\r\n", initCAN() ? "ready" : "init failed");
}

void loop() {
    twai_message_t msg;

    while (twai_receive(&msg, 0) == ESP_OK) {
        if (!updater.handleMessage(msg)) {
            Serial.printf("App CAN RX: 0x%03lX [%d]\r\n",
                          (unsigned long)msg.identifier,
                          msg.data_length_code);
        }
    }

    updater.poll();


    // ATTENTION - Existing application logic runs here.


    // Printing purely for this demo
    static uint32_t lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 1000) {
        lastHeartbeat = millis();
        sendHeartbeat();
        Serial.printf("CAN OTA example: %s\r\n", VERSION);
    }
    delay(1);
}
