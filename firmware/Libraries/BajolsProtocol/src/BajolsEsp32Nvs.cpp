#if defined(ARDUINO_ARCH_ESP32)
#include "BajolsEsp32Nvs.h"

#include <esp_partition.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <stdlib.h>
#include <string.h>

namespace bajols {
namespace bcp {
namespace {
// Reserved by the project's ESP32 partition table, separate from Wi-Fi NVS.
constexpr char kPartition[] = "bajols";
// This namespace contains only the six BCP journal records.
constexpr char kNamespace[] = "journal";
// Persistent key names are part of the ESP32 provisioning format.
const char* const kKeys[] = {"r0", "r1", "r2", "r3", "r4", "r5"};
}  // namespace

Esp32NvsBackend::Esp32NvsBackend()
    : state_(ByteStorageStatus::kFailed),
      records_(),
      request_(),
      slot_(0),
      stack_(),
      task_storage_(),
      task_(nullptr) {}

Esp32NvsBackend::~Esp32NvsBackend() {
  // Killing an active NVS commit could corrupt service state. Firmware-lifetime
  // ownership is required; fail-stop rather than returning with a live worker.
  if (task_ != nullptr) {
    if (state_.load(std::memory_order_acquire) == ByteStorageStatus::kBusy) {
      abort();
    }
    vTaskDelete(task_);
  }
}

bool Esp32NvsBackend::Begin(bool writes_enabled) {
  if (!writes_enabled || task_ != nullptr) {
    return false;
  }
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, kPartition);
  uint8_t header[32];
  if (partition == nullptr || partition->address != 0x310000 ||
      partition->size != 0x6000 ||
      esp_partition_read(partition, 0, header, sizeof(header)) != ESP_OK) {
    return false;
  }
  bool erased = true;
  for (uint8_t value : header) {
    erased = erased && value == 0xFF;
  }
  if (erased || nvs_flash_init_partition(kPartition) != ESP_OK) {
    return false;  // Never erase/reformat or generate an identity on failure.
  }
  nvs_handle_t handle;
  if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) !=
      ESP_OK) {
    return false;
  }
  bool valid = true;
  for (uint8_t slot = 0; slot < 6; ++slot) {
    size_t length = sizeof(records_[slot]);
    if (nvs_get_blob(handle, kKeys[slot], records_[slot], &length) != ESP_OK ||
        length != sizeof(records_[slot])) {
      valid = false;
      break;
    }
  }
  nvs_close(handle);
  if (!valid) {
    return false;
  }
  state_.store(ByteStorageStatus::kReady, std::memory_order_release);
  task_ = xTaskCreateStatic(WorkerEntry, "bcp-nvs", sizeof(stack_), this, 1,
                            stack_, &task_storage_);
  if (task_ == nullptr) {
    state_.store(ByteStorageStatus::kFailed, std::memory_order_release);
    return false;
  }
  return true;
}

ByteStorageStatus Esp32NvsBackend::Status() {
  return state_.load(std::memory_order_acquire);
}

bool Esp32NvsBackend::ReadRecord(uint8_t slot, uint8_t* bytes) {
  if (slot >= 6 || bytes == nullptr || Status() != ByteStorageStatus::kReady) {
    return false;
  }
  memcpy(bytes, records_[slot], sizeof(records_[slot]));
  return true;
}

bool Esp32NvsBackend::StartRecordWrite(uint8_t slot, const uint8_t* bytes) {
  if (slot >= 6 || bytes == nullptr || task_ == nullptr ||
      Status() != ByteStorageStatus::kReady) {
    return false;
  }
  memcpy(request_, bytes, sizeof(request_));
  slot_ = slot;
  state_.store(ByteStorageStatus::kBusy, std::memory_order_release);
  xTaskNotifyGive(task_);
  return true;
}

void Esp32NvsBackend::WorkerEntry(void* context) {
  static_cast<Esp32NvsBackend*>(context)->Run();
}

void Esp32NvsBackend::Run() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (state_.load(std::memory_order_acquire) != ByteStorageStatus::kBusy) {
      continue;
    }
    nvs_handle_t handle;
    bool success = nvs_open_from_partition(kPartition, kNamespace,
                                           NVS_READWRITE, &handle) == ESP_OK;
    uint8_t readback[IdentityJournal::kRecordSize];
    if (success) {
      size_t length = sizeof(readback);
      success =
          nvs_set_blob(handle, kKeys[slot_], request_, sizeof(request_)) ==
              ESP_OK &&
          nvs_commit(handle) == ESP_OK &&
          nvs_get_blob(handle, kKeys[slot_], readback, &length) == ESP_OK &&
          length == sizeof(readback) && memcmp(readback, request_, length) == 0;
      nvs_close(handle);
    }
    if (success) {
      memcpy(records_[slot_], readback, sizeof(readback));
    }
    state_.store(
        success ? ByteStorageStatus::kReady : ByteStorageStatus::kFailed,
        std::memory_order_release);
  }
}

Esp32NvsStorage::Esp32NvsStorage(uint16_t base, uint16_t length,
                                 bool writes_enabled)
    : backend_(),
      records_(backend_, writes_enabled),
      writes_enabled_(writes_enabled),
      valid_(base == 0 && length == IdentityJournal::kWindowSize) {}

bool Esp32NvsStorage::Begin() {
  return valid_ && backend_.Begin(writes_enabled_) && records_.Load();
}

uint32_t Esp32NvsStorage::Capacity() const { return records_.Capacity(); }
ByteStorageStatus Esp32NvsStorage::Status() { return records_.Status(); }
bool Esp32NvsStorage::Read(uint32_t address, uint8_t& value) {
  return valid_ && records_.Read(address, value);
}
bool Esp32NvsStorage::StartWrite(uint32_t address, uint8_t value) {
  return valid_ && records_.StartWrite(address, value);
}
}  // namespace bcp
}  // namespace bajols
#endif
