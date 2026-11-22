#include "assets.h"
#include "audio_service.h"

#include <cJSON.h>
#include <cstring>
#include <esp_log.h>

namespace {
constexpr char kTag[] = "Assets";
constexpr size_t kHeaderSize = 12;
struct AssetEntry {
    char name[32];
    uint32_t size;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
};
static_assert(sizeof(AssetEntry) == 44);

uint32_t ReadU32(const uint8_t* data) {
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool ValidWakeModel(const uint8_t* data, size_t size) {
    // ESP-SR's loader accepts no length, so validate its fixed inner format first.
    constexpr size_t header_size = 4 + 36 + 3 * 40;
    if (size < header_size || ReadU32(data) != 1 ||
        !std::memchr(data + 4, '\0', 32) ||
        std::strcmp(reinterpret_cast<const char*>(data + 4), "wn9_hiesp") ||
        ReadU32(data + 36) != 3) return false;
    const char* names[] = {"_MODEL_INFO_", "wn9_data", "wn9_index"};
    unsigned seen = 0;
    for (size_t i = 0; i < 3; ++i) {
        const auto entry = data + 40 + i * 40;
        if (!std::memchr(entry, '\0', 32)) return false;
        unsigned file = 0;
        while (file < 3 && std::strcmp(reinterpret_cast<const char*>(entry), names[file])) ++file;
        if (file == 3 || (seen & (1u << file))) return false;
        seen |= 1u << file;
        const uint32_t offset = ReadU32(entry + 32);
        const uint32_t length = ReadU32(entry + 36);
        if (offset < header_size || offset > size || !length || length > size - offset) return false;
    }
    return seen == 7;
}
}

Assets::~Assets() {
    if (mmap_handle_) esp_partition_munmap(mmap_handle_);
}

bool Assets::InitializePartition() {
    if (mapped_) return true;
    auto partition = esp_partition_find_first(ESP_PARTITION_TYPE_ANY,
        ESP_PARTITION_SUBTYPE_ANY, "assets");
    if (!partition || partition->size < kHeaderSize) {
        ESP_LOGE(kTag, "Missing local wake-model assets partition");
        return false;
    }
    const uint8_t* data = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    const auto err = esp_partition_mmap(partition, 0, partition->size,
        ESP_PARTITION_MMAP_DATA, reinterpret_cast<const void**>(&data), &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Cannot map local assets: %s", esp_err_to_name(err));
        return false;
    }
    uint32_t header[3];
    std::memcpy(header, data, sizeof(header));
    const uint32_t count = header[0];
    const uint32_t length = header[2];
    if (length > partition->size - kHeaderSize || count > length / sizeof(AssetEntry)) {
        ESP_LOGE(kTag, "Invalid local assets header");
        esp_partition_munmap(handle);
        return false;
    }
    uint32_t checksum = 0;
    for (size_t i = 0; i < length; ++i) checksum += data[kHeaderSize + i];
    if ((checksum & 0xffff) != header[1]) {
        ESP_LOGE(kTag, "Invalid local assets checksum");
        esp_partition_munmap(handle);
        return false;
    }
    mapped_ = data;
    mmap_handle_ = handle;
    file_count_ = count;
    stored_size_ = kHeaderSize + length;
    return true;
}

bool Assets::GetAssetData(const char* name, const uint8_t*& data, size_t& size) const {
    if (!mapped_) return false;
    const size_t payload = kHeaderSize + file_count_ * sizeof(AssetEntry);
    for (uint32_t i = 0; i < file_count_; ++i) {
        AssetEntry entry;
        std::memcpy(&entry, mapped_ + kHeaderSize + i * sizeof(entry), sizeof(entry));
        if (!std::memchr(entry.name, '\0', sizeof(entry.name)) || std::strcmp(entry.name, name)) continue;
        if (entry.offset > stored_size_ - payload) return false;
        const size_t start = payload + entry.offset;
        if (stored_size_ - start < 2 || entry.size > stored_size_ - start - 2 ||
            mapped_[start] != 'Z' || mapped_[start + 1] != 'Z') return false;
        data = mapped_ + start + 2;
        size = entry.size;
        return true;
    }
    return false;
}

bool Assets::Apply() {
    if (models_list_) return true;
    if (!InitializePartition()) return false;
    const uint8_t* data = nullptr;
    size_t size = 0;
    if (!GetAssetData("index.json", data, size)) {
        ESP_LOGE(kTag, "Missing local assets index");
        return false;
    }
    auto root = cJSON_ParseWithLength(reinterpret_cast<const char*>(data), size);
    if (!root) return false;
    auto version = cJSON_GetObjectItemCaseSensitive(root, "version");
    auto model = cJSON_GetObjectItemCaseSensitive(root, "srmodels");
    const bool valid = cJSON_IsNumber(version) && version->valueint == 1 &&
        cJSON_IsString(model) && GetAssetData(model->valuestring, data, size) && size > 0;
    cJSON_Delete(root);
    if (!valid) {
        ESP_LOGE(kTag, "Missing or unsupported local wake-model assets");
        return false;
    }
    if (!ValidWakeModel(data, size)) {
        ESP_LOGE(kTag, "Invalid Hi ESP wake-model data");
        return false;
    }
    models_list_ = srmodel_load(data);
    if (!models_list_) {
        ESP_LOGE(kTag, "Failed to load local wake model");
        return false;
    }
    AudioService::GetInstance().SetModelsList(models_list_);
    return true;
}
