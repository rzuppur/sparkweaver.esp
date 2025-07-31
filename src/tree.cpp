#include "tree.h"

#include <Arduino.h>

#ifdef DISABLED
#undef DISABLED
#endif

#include <SparkWeaverCore.h>
#include <LittleFS.h>
#include <bluetooth.h>

namespace {
    SparkWeaverCore::Engine engine;

    std::vector<uint8_t> tree;
    SemaphoreHandle_t    tree_mutex;
}

namespace Tree {
    constexpr auto TREE_FILE_PATH = "/tree.bin";

    std::vector<uint8_t> loadTreeFromStorage()
    {
        std::vector<uint8_t> tree_file{};
        if (LittleFS.exists(TREE_FILE_PATH)) {
            if (File file = LittleFS.open(TREE_FILE_PATH, "r")) {
                if (const size_t size = file.size(); size > 0) {
                    tree_file.resize(size);
                    if (const auto read = file.read(tree_file.data(), size); read != size) {
                        tree_file.clear();
                    }
                }
                file.close();
            }
        }
        return tree_file;
    }

    void storeTreeToStorage(const std::vector<uint8_t>& tree)
    {
        if (tree.size() <= 1) {
            LittleFS.remove(TREE_FILE_PATH);
        } else if (File file = LittleFS.open(TREE_FILE_PATH, "w")) {
            file.write(tree.data(), tree.size());
            file.close();
        }
    }

    void init()
    {
        tree_mutex = xSemaphoreCreateMutex();
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS mount failed");
        }

        setTree(loadTreeFromStorage());
        if (tree.empty()) {
            setTree({SparkWeaverCore::TREE_VERSION});
        }
    }

    std::vector<uint8_t> getTree()
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        std::vector<uint8_t> result = tree;
        xSemaphoreGive(tree_mutex);
        return result;
    }

    void setTree(const std::vector<uint8_t>& new_tree)
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        try {
            engine.build(new_tree);
            tree = new_tree;
            storeTreeToStorage(tree);
            Serial.println("Builder: success");
        } catch (const std::exception& e) {
            Serial.println(e.what());
            tree.clear();
        }
        Bluetooth::setTree(tree);
        xSemaphoreGive(tree_mutex);
    }

    uint8_t* tick() noexcept
    {
        xSemaphoreTake(tree_mutex, portMAX_DELAY);
        uint8_t* p_result = engine.tick();
        xSemaphoreGive(tree_mutex);
        return p_result;
    }

    std::vector<uint8_t> listExternalTriggers() noexcept
    {
        return engine.listExternalTriggers();
    }

    void triggerExternalTrigger(const uint8_t id) noexcept
    {
        engine.triggerExternalTrigger(id);
    }
}
