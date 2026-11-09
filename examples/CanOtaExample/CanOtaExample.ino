#include <Arduino.h>
#include <CanFwUpdate.h>

// Match these GPIOs to your board/transceiver wiring.
constexpr gpio_num_t CAN_TX = GPIO_NUM_2;
constexpr gpio_num_t CAN_RX = GPIO_NUM_3;
constexpr uint8_t NODE_ID = 1;
constexpr char VERSION[] = "v3";

CanFwUpdate updater(CAN_TX, CAN_RX, NODE_ID);

uint16_t counter = 0;

void setup() {

    // Pulling GPIO 21 low to disable transceiver standby (active)
    // The board used for this test has GPIO 21 tied to the CAN transceiver's standby pin; it
    // defaults to standby unless pulled low by the MCU for ultra-low-power sleep.
    pinMode(21, OUTPUT);
    digitalWrite(21, LOW);

    Serial.begin(115200);
    delay(300);
    Serial.printf("CAN OTA example: %s\r\n", VERSION);
    Serial.println(updater.begin() ? "CAN ready (500 kbps)" : "CAN init failed");
}

void loop() {
    updater.poll();
    // Existing application logic runs here.
    if (counter++ >= 1000) {
        Serial.printf("CAN OTA example: %s\r\n", VERSION);
        counter = 0;
    }
    counter++;
    delay(1);
}
