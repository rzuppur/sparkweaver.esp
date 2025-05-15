#include <Arduino.h>
#include <esp_dmx.h>

#include "bluetooth.h"
#include "tree.h"

constexpr static dmx_port_t DMX_UART_PORT = 1;
constexpr static uint8_t    ENABLE_PIN    = 4;
constexpr static uint8_t    RX_PIN        = 16;
constexpr static uint8_t    TX_PIN        = 17;

void setup()
{
    Serial.begin(SERIAL_MONITOR_SPEED);

    Bluetooth::init();
    Tree::init();

    constexpr auto config = DMX_CONFIG_DEFAULT;
    dmx_driver_install(DMX_UART_PORT, &config, {}, 0);
    dmx_set_pin(DMX_UART_PORT, TX_PIN, RX_PIN, ENABLE_PIN);
}

void loop()
{
    if (const auto p_tick_data = Tree::tick(); p_tick_data != nullptr) {
        dmx_write(DMX_UART_PORT, p_tick_data, DMX_PACKET_SIZE);
        dmx_send(DMX_UART_PORT);
        dmx_wait_sent(DMX_UART_PORT, DMX_TIMEOUT_TICK);
    }
}
