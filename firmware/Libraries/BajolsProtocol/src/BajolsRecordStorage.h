#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_RECORD_STORAGE_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_RECORD_STORAGE_H_

#include "BajolsStorage.h"

namespace bajols {
namespace bcp {
// Fixed-record durable service. ReadRecord returns the last verified durable
// mirror; physical commit and readback run outside the journal/control thread.
class RecordBackend {
 public:
  // Releases a backend through its interface after its worker has stopped.
  virtual ~RecordBackend() {}
  // Observes readiness without waiting for a flash operation.
  virtual ByteStorageStatus Status() = 0;
  // Copies one of six verified 32-byte records without physical I/O.
  virtual bool ReadRecord(uint8_t slot, uint8_t* bytes) = 0;
  // Copies a bounded request; Ready is reported only after durable readback.
  virtual bool StartRecordWrite(uint8_t slot, const uint8_t* bytes) = 0;
};

// Journal-specific byte facade, not general EEPROM emulation. Payload bytes
// stage only after a durable marker invalidation; the final A5 marker commits
// the whole record. Thus a 67-byte paired update needs four record commits,
// not one flash commit per payload byte. Final journal readback follows actual
// durable record verification, never an uncommitted valid marker.
class RecordJournalStorage : public ByteStorage {
 public:
  // References an exclusive, longer-lived asynchronous record backend.
  RecordJournalStorage(RecordBackend& backend, bool writes_enabled);
  // In-flight journal mirrors and marker transactions must not be cloned.
  RecordJournalStorage(const RecordJournalStorage&) = delete;
  // Prevents reassignment of exclusive backend ownership.
  RecordJournalStorage& operator=(const RecordJournalStorage&) = delete;
  // Loads six durable mirrors before journal startup; no write is performed.
  bool Load();
  // Returns the fixed 192-byte logical window; physical offsets are separate.
  uint32_t Capacity() const override;
  // Completes pending marker verification without physical I/O or waiting.
  ByteStorageStatus Status() override;
  // Reads the staged journal image only while the backend is idle.
  bool Read(uint32_t address, uint8_t& value) override;
  // Stages a payload byte or queues an invalidation/final-marker transaction.
  bool StartWrite(uint32_t address, uint8_t value) override;

 private:
  // Exclusive durable record service.
  RecordBackend& backend_;
  // Explicit write permission; unchanged writes are also refused when false.
  bool writes_enabled_;
  // Whether the complete initial mirror was successfully loaded.
  bool loaded_;
  // Latched backend/readback error; recovery requires restart/service.
  bool failed_;
  // Whether a marker commit is awaiting verified completion.
  bool pending_;
  // Slot associated with the pending marker operation.
  uint8_t slot_;
  // Complete logical image, including payload staging under invalid markers.
  uint8_t image_[IdentityJournal::kWindowSize];
  // Expected durable image for the pending record.
  uint8_t expected_[IdentityJournal::kRecordSize];
};
}  // namespace bcp
}  // namespace bajols
#endif
