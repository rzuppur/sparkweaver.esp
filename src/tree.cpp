#include "tree.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SparkWeaverCore.h>
#include <bluetooth.h>

namespace {
    SparkWeaverCore::Builder builder;
    Preferences              preferences;

    std::string       tree_string;
    SemaphoreHandle_t tree_mutex;
}

namespace Tree {
    void init()
    {
        tree_mutex = xSemaphoreCreateMutex();
        if (tree_mutex == nullptr) {
            Serial.println("Failed to create mutex");
            return;
        }

        preferences.begin("tree", false);
        if (preferences.isKey("tree")) {
            setTree(preferences.getString("tree").c_str());
        }
        if (tree_string.empty()) {
            setTree("");
            preferences.remove("tree");
        }
    }

    std::string getTree()
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        std::string result = tree_string;
        xSemaphoreGive(tree_mutex);
        return result;
    }

    void setTree(const std::string& tree)
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        try {
            builder.build(tree);
            tree_string = tree;
            preferences.putString("tree", tree.c_str());
            Serial.println("Builder: success");
        } catch (const std::exception& e) {
            Serial.println(e.what());
            tree_string = "";
        }
        Bluetooth::setTree(tree_string);
        xSemaphoreGive(tree_mutex);
    }

    uint8_t* tick() noexcept
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        uint8_t* p_result = builder.tick();
        xSemaphoreGive(tree_mutex);
        return p_result;
    }
}
