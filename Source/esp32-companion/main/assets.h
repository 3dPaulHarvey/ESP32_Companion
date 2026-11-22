#ifndef ASSETS_H
#define ASSETS_H

#include <cstddef>
#include <cstdint>
#include <esp_partition.h>
#include <spi_flash_mmap.h>
#include <model_path.h>

// Read-only local assets. The partition supplies the English wake model only.
class Assets {
public:
    static Assets& GetInstance() {
        static Assets instance;
        return instance;
    }
    ~Assets();
    bool Apply();

private:
    Assets() = default;
    Assets(const Assets&) = delete;
    Assets& operator=(const Assets&) = delete;
    bool InitializePartition();
    bool GetAssetData(const char* name, const uint8_t*& data, size_t& size) const;

    const uint8_t* mapped_ = nullptr;
    esp_partition_mmap_handle_t mmap_handle_ = 0;
    uint32_t file_count_ = 0;
    size_t stored_size_ = 0;
    srmodel_list_t* models_list_ = nullptr;
};

#endif
