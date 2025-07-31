#include "bluetooth.h"

#include <Arduino.h>
#include <NimBLEClient.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <WiFi.h>
#include <tree.h>

namespace {
    constexpr uint32_t PW_DEFAULT             = 654321;
    constexpr uint8_t  PW_IS_SET              = 0x00;
    constexpr uint8_t  PW_NEEDS_TO_BE_CHANGED = 0x01;

    constexpr uint8_t CHUNK_COUNT_MAX          = 0xFF - 2;
    constexpr uint8_t CHUNK_FIRST_INDEX        = 0x01;
    constexpr uint8_t CHUNK_TRANSMISSION_END   = 0x00;
    constexpr uint8_t CHUNK_TRANSMISSION_ERROR = 0xFF;

    constexpr auto SVC_SW      = "00001000-d8b4-4b1a-b585-d4931d8dc888";
    constexpr auto CHR_TREE    = "00001001-d8b4-4b1a-b585-d4931d8dc888";
    constexpr auto CHR_PW      = "00001002-d8b4-4b1a-b585-d4931d8dc888";
    constexpr auto CHR_TRIGGER = "00001003-d8b4-4b1a-b585-d4931d8dc888";

    Preferences preferences;
    uint32_t    ble_pw = PW_DEFAULT;

    SemaphoreHandle_t tree_chr_mutex;

    NimBLEServer*         p_server;
    NimBLEService*        p_svc_sw;
    NimBLECharacteristic* p_chr_tree;
    NimBLECharacteristic* p_chr_pw;
    NimBLECharacteristic* p_chr_trigger;
    NimBLEAdvertising*    p_advertising;

    void startAdvertising();
    void stopAdvertising();
    void unbindClients();

    void setPw(const uint32_t new_pw)
    {
        if (new_pw < 100000 || new_pw > 999999 || new_pw == 123456) {
            return;
        }
        ble_pw = new_pw;
        preferences.putUInt("pw", ble_pw);
        stopAdvertising();
        unbindClients();
        startAdvertising();
        Serial.print("Password changed: ");
        Serial.println(ble_pw);
    }

    class ChrTreeCallbacks final : public NimBLECharacteristicCallbacks {
        std::ptrdiff_t data_offset  = 0;
        uint8_t        chunk_number = CHUNK_FIRST_INDEX;

        uint8_t              write_chunk = CHUNK_FIRST_INDEX;
        std::vector<uint8_t> write_data;

        void resetRead()
        {
            data_offset  = 0;
            chunk_number = CHUNK_FIRST_INDEX;
        }

        void resetWrite()
        {
            write_chunk = CHUNK_FIRST_INDEX;
            write_data.clear();
        }

    public:
        std::vector<uint8_t> full_data;

        void reset()
        {
            resetRead();
            resetWrite();
        }

        void onRead(NimBLECharacteristic* p_chr) override
        {
            xSemaphoreTake(tree_chr_mutex, portMAX_DELAY);
            if (chunk_number > CHUNK_COUNT_MAX) {
                Serial.println("Bluetooth read tree: Too many chunks!");
                p_chr->setValue(CHUNK_TRANSMISSION_ERROR);
                resetRead();
            } else if (data_offset < full_data.size()) {
                Serial.print("Bluetooth read tree: chunk ");
                Serial.println(chunk_number);
                const std::ptrdiff_t max_chunk  = NimBLEDevice::getMTU() - 16;
                const std::ptrdiff_t chunk_size = std::min(
                    max_chunk,
                    static_cast<std::ptrdiff_t>(full_data.size()) - data_offset);
                std::vector chunk_data(full_data.begin() + data_offset, full_data.begin() + data_offset + chunk_size);
                chunk_data.insert(chunk_data.begin(), chunk_number++);
                p_chr->setValue(chunk_data);
                data_offset += chunk_size;
            } else {
                p_chr->setValue(CHUNK_TRANSMISSION_END);
                resetRead();
                Serial.println("Bluetooth read tree: complete");
            }
            xSemaphoreGive(tree_chr_mutex);
        }

        void onWrite(NimBLECharacteristic* p_chr) override
        {
            if (const std::vector<uint8_t>& packet = p_chr->getValue();
                packet.size() == 1 && packet.at(0) == CHUNK_TRANSMISSION_END) {
                Tree::setTree(write_data);
                resetWrite();
                Serial.println("Bluetooth write tree: complete");
            } else if (packet.size() <= 1) {
                resetWrite();
                Serial.println("Bluetooth write tree: invalid");
            } else {
                if (packet.at(0) == write_chunk) {
                    Serial.print("Bluetooth write tree: chunk ");
                    Serial.println(write_chunk);
                    write_data.insert(write_data.end(), packet.begin() + 1, packet.end());
                    write_chunk += 1;
                } else {
                    resetWrite();
                    Serial.println("Bluetooth write tree: chunk mismatch");
                }
            }
        }
    };

    class ChrPwCallbacks final : public NimBLECharacteristicCallbacks {
    public:
        void onRead(NimBLECharacteristic* p_chr) override
        {
            p_chr->setValue(ble_pw == PW_DEFAULT ? PW_NEEDS_TO_BE_CHANGED : PW_IS_SET);
        }

        void onWrite(NimBLECharacteristic* p_chr) override
        {
            const auto value = p_chr->getValue();
            if (value.size() != 4) {
                return;
            }
            const auto data = value.data();
            setPw(data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24);
            p_chr_pw->setValue(ble_pw == PW_DEFAULT ? PW_NEEDS_TO_BE_CHANGED : PW_IS_SET);
        }
    };

    class ChrTriggerCallbacks final : public NimBLECharacteristicCallbacks {
    public:
        void onRead(NimBLECharacteristic* p_chr) override
        {
            p_chr->setValue(Tree::listExternalTriggers());
        }

        void onWrite(NimBLECharacteristic* p_chr) override
        {
            const auto value = p_chr->getValue();
            if (value.size() != 1) {
                return;
            }
            const auto data = value.data();
            Tree::triggerExternalTrigger(data[0]);
        }
    };

    ChrTreeCallbacks    chr_tree_callbacks;
    ChrPwCallbacks      chr_pw_callbacks;
    ChrTriggerCallbacks chr_trigger_callbacks;

    class ServerCallbacks final : public NimBLEServerCallbacks {
        void onConnect(NimBLEServer* p_server) override
        {
            chr_tree_callbacks.reset();
            Serial.println("Bluetooth: client connected");
        }

        void onDisconnect(NimBLEServer* pServer) override
        {
            Serial.println("Bluetooth: client disconnected");
        }
    };

    ServerCallbacks server_callbacks;

    void startAdvertising()
    {
        NimBLEDevice::setSecurityPasskey(ble_pw);

        p_chr_tree->setCallbacks(&chr_tree_callbacks);
        p_chr_pw->setCallbacks(&chr_pw_callbacks);
        p_chr_trigger->setCallbacks(&chr_trigger_callbacks);
        p_svc_sw->start();

        p_advertising = NimBLEDevice::getAdvertising();
        p_advertising->setAppearance(0x008D);
        p_advertising->addServiceUUID(SVC_SW);
        p_advertising->start();

        Serial.println("Bluetooth: advertising");
    }

    void stopAdvertising()
    {
        if (p_advertising != nullptr && p_advertising->isAdvertising()) {
            p_advertising->stop();
            Serial.println("Bluetooth: stopped advertising");
        } else {
            Serial.println("Bluetooth: advertising already stopped");
        }
    }

    void unbindClients()
    {
        const auto n_clients = NimBLEDevice::getNumBonds();
        for (uint8_t i = 0; i < n_clients; i++) {
            NimBLEDevice::deleteBond(NimBLEDevice::getBondedAddress(i));
        }
        Serial.println(("Bluetooth: unbound clients (" + std::to_string(n_clients) + ")").c_str());
    }

    [[noreturn]] void task(void*)
    {
        NimBLEDevice::init("SW_" + std::string(WiFi.macAddress().c_str()).erase(5, 12).erase(2, 1));
        NimBLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
        NimBLEDevice::setMTU(BLE_ATT_MTU_MAX);
        NimBLEDevice::setSecurityAuth(true, true, true);
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

        p_server = NimBLEDevice::createServer();
        p_server->setCallbacks(&server_callbacks);
        p_svc_sw   = p_server->createService(SVC_SW);
        p_chr_tree =
            p_svc_sw->createCharacteristic(CHR_TREE, READ | READ_ENC | READ_AUTHEN | WRITE | WRITE_ENC | WRITE_AUTHEN);
        p_chr_pw =
            p_svc_sw->createCharacteristic(CHR_PW, READ | READ_ENC | READ_AUTHEN | WRITE | WRITE_ENC | WRITE_AUTHEN);
        p_chr_trigger =
            p_svc_sw->createCharacteristic(
                CHR_TRIGGER,
                READ | READ_ENC | READ_AUTHEN | WRITE | WRITE_ENC | WRITE_AUTHEN);

        startAdvertising();

        for (;;) {
            delay(1000);
        }
    }
}

namespace Bluetooth {
    void init()
    {
        tree_chr_mutex = xSemaphoreCreateMutex();

        preferences.begin("bluetooth", false);
        if (preferences.isKey("pw")) {
            ble_pw = preferences.getUInt("pw");
            if (ble_pw < 100000 || ble_pw > 999999) {
                ble_pw = PW_DEFAULT;
                preferences.remove("pw");
            }
        }

        TaskHandle_t handle;
        xTaskCreatePinnedToCore(task, "bluetooth", 4096, nullptr, 1, &handle, CONFIG_BT_NIMBLE_PINNED_TO_CORE);
    }

    void setTree(const std::vector<uint8_t>& tree)
    {
        xSemaphoreTake(tree_chr_mutex, portMAX_DELAY);
        chr_tree_callbacks.full_data = tree;
        xSemaphoreGive(tree_chr_mutex);
    }
}
