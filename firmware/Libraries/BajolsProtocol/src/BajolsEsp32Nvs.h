#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ESP32_NVS_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ESP32_NVS_H_

#if !defined(ARDUINO_ARCH_ESP32)
#error "BajolsEsp32Nvs requires Arduino-ESP32."
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>

#include "BajolsRecordStorage.h"

namespace bajols {
namespace bcp {
// Exclusive NVS service for partition bajols, namespace journal, keys r0..r5.
// Construct for firmware lifetime; no other task may access this namespace.
class Esp32NvsBackend : public RecordBackend {
 public:
  // Constructs fixed worker/cache storage, performing no flash access.
  Esp32NvsBackend();
  // Worker, NVS namespace and durable mirrors have one owner.
  Esp32NvsBackend(const Esp32NvsBackend&) = delete;
  // Prevents replacement of a live task context.
  Esp32NvsBackend& operator=(const Esp32NvsBackend&) = delete;
  // Stops the idle worker at teardown; firmware normally retains it until
  // reset.
  ~Esp32NvsBackend() override;
  // Loads all provisioned records and creates a fixed-storage worker. Read-only
  // builds refuse NVS initialization because recovery itself may write flash.
  bool Begin(bool writes_enabled);
  // Observes a release/acquire publication flag without blocking.
  ByteStorageStatus Status() override;
  // Reads only the worker's verified mirror while no transaction is active.
  bool ReadRecord(uint8_t slot, uint8_t* bytes) override;
  // Publishes one copied request to the flash worker, never doing flash I/O
  // here.
  bool StartRecordWrite(uint8_t slot, const uint8_t* bytes) override;

 private:
  // FreeRTOS entry with firmware-lifetime backend ownership.
  static void WorkerEntry(void* context);
  // Processes record transactions and verifies each commit before reporting
  // Ready.
  void Run();
  // Coordinates worker state and visibility of fixed request/mirror buffers.
  std::atomic<ByteStorageStatus> state_;
  // Verified persistent record mirrors; no full NVS partition is held in RAM.
  uint8_t records_[6][IdentityJournal::kRecordSize];
  // Copied immutable request while Busy.
  uint8_t request_[IdentityJournal::kRecordSize];
  // Slot selected for the current copied request.
  uint8_t slot_;
  // Fixed worker stack; ESP-IDF task stack depth is expressed in bytes.
  StackType_t stack_[2048];
  // Fixed task-control storage; task creation needs no heap allocation.
  StaticTask_t task_storage_;
  // Worker handle, null until deliberate writable initialization succeeds.
  TaskHandle_t task_;
};

// Board-compatible facade with the same constructor as MegaEepromStorage.
// ESP32 uses logical base zero and exactly 192 bytes, independent of flash
// offset.
class Esp32NvsStorage : public ByteStorage {
 public:
  // Validates the logical window; no constructor writes or initializes NVS.
  Esp32NvsStorage(uint16_t base, uint16_t length, bool writes_enabled);
  // Initializes only a deliberately writable, already-provisioned partition.
  bool Begin();
  // Returns the logical journal capacity, not physical flash size.
  uint32_t Capacity() const override;
  // Reports asynchronous record-worker state without blocking.
  ByteStorageStatus Status() override;
  // Reads a bounded byte from the verified/staged journal facade.
  bool Read(uint32_t address, uint8_t& value) override;
  // Stages one byte or starts a durable marker commit without waiting.
  bool StartWrite(uint32_t address, uint8_t value) override;

 private:
  // Exclusive fixed worker, initialized before the byte facade loads its
  // mirror.
  Esp32NvsBackend backend_;
  // Portable record-transaction implementation shared with native power-cut
  // tests.
  RecordJournalStorage records_;
  // Caller-selected write permission, never inferred from existing media.
  bool writes_enabled_;
  // Only the fixed logical zero-based journal window is supported.
  bool valid_;
};
}  // namespace bcp
}  // namespace bajols
#endif
