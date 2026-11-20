#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_STORAGE_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_STORAGE_H_

#include "BajolsSession.h"

namespace bajols {
namespace bcp {

// Backend state sampled without waiting for a physical byte transaction.
enum class ByteStorageStatus : uint8_t {
  // One immediate read or new byte write may be attempted.
  kReady,
  // A previously started byte is still programming.
  kBusy,
  // The backend detected a hardware/service failure.
  kFailed
};

// Exclusive asynchronous byte storage; physical backends implement this
// boundary.
class ByteStorage {
 public:
  // Releases a backend through its interface.
  virtual ~ByteStorage() {}
  // Returns available byte count, independent of any selected logical window.
  virtual uint32_t Capacity() const = 0;
  // Reports current readiness without blocking or completing a new write.
  virtual ByteStorageStatus Status() = 0;
  // Immediately reads one byte; false is a permanent transaction failure.
  virtual bool Read(uint32_t address, uint8_t& value) = 0;
  // Starts exactly one byte write and returns without waiting for completion.
  virtual bool StartWrite(uint32_t address, uint8_t value) = 0;
};

// Observable portable prototype lifecycle; failed journals require service.
enum class JournalState : uint8_t {
  // No boot scan has been requested.
  kUninitialized,
  // Reading or advancing verified boot identity.
  kBooting,
  // Verified boot is available; emergency health may independently require
  // service.
  kReady,
  // Writing the latest manager emergency transaction.
  kEmergencyWriting,
  // Read/write/identity/counter verification failed; no more writes are
  // allowed.
  kFailed
};

// Verified outcome delivered once for the latest runtime emergency token.
struct StorageCompletion {
  // Manager token; never confused with the independent durable journal counter.
  uint32_t token;
  // True only after both complete records were read back and matched.
  bool verified;
};

// Experimental storage layout; review protocol/FIRMWARE_STORAGE_PROPOSAL.md.
// This is not a provisioning tool or validation of physical EEPROM behavior.
class IdentityJournal {
 public:
  // Fixed record bytes, serialized explicitly rather than using native structs.
  static constexpr uint16_t kRecordSize = 32;
  // Exclusive logical window size; no absolute hardware address is chosen.
  static constexpr uint16_t kWindowSize = 192;

  // References exclusive storage and a caller-selected window/validated
  // timeout. Busy timeout must be positive and below 32768 ms; no default is
  // supplied.
  IdentityJournal(ByteStorage& storage, uint32_t base,
                  uint16_t busy_timeout_ms);
  // Live identity/persistence state must not be cloned.
  IdentityJournal(const IdentityJournal&) = delete;
  // Prevents replacement of an in-flight durable transaction.
  IdentityJournal& operator=(const IdentityJournal&) = delete;
  // Starts exactly one boot scan/advance per instance; never provisions blanks.
  bool BeginBoot();
  // Performs at most one immediate read or starts one byte write per call.
  // Must be serviced frequently with wrapping onboard milliseconds.
  void Step(uint32_t now);
  // Starts a manager transaction after boot verification; newer latch tokens
  // may supersede an in-flight clear. Replayed/clear-while-busy tokens are
  // refused.
  bool RequestEmergency(const EmergencyWrite& request);
  // Returns evidence; identity_verified stays false until boot readback
  // completes.
  VerifiedBootState BootState() const;
  // Reports current lifecycle without advancing storage operations.
  JournalState State() const;
  // Consumes the latest acknowledgement; false means no completion is
  // available.
  bool TakeCompletion(StorageCompletion& completion);

 private:
  // Byte-level service phase, separate from the externally observable
  // lifecycle.
  enum class Phase : uint8_t {
    // Startup scans all six records one byte at a time.
    kScan,
    // Starts one invalidation/payload/commit write.
    kWrite,
    // Reads back the most recently completed byte before proceeding.
    kReadBack,
    // Reads both full records after their final commit bytes completed.
    kVerify
  };
  // Checks typed shape, reserved bytes, commit marker and checksum.
  static bool ValidRecord(const uint8_t* bytes, uint8_t kind);
  // Parses explicit little-endian counters without native alignment
  // assumptions.
  static uint32_t Counter(const uint8_t* bytes);
  // Constructs a fixed boot/emergency record with zero reserved bytes.
  void MakeRecord(uint8_t kind, uint32_t counter, bool latched);
  // Validates scanned pairs and starts the durable boot-counter advance.
  void FinishScan();
  // Sets up both-copy writing/readback for the current expected record.
  void BeginPair(uint16_t offset);
  // Builds and starts a checked emergency journal update.
  bool BeginEmergency(const EmergencyWrite& request);
  // Ends the current verified transaction without exposing partial state.
  void FinishPair();
  // Fails closed and returns failure only for the newest emergency token.
  void Fail();
  // Maps transaction byte order: invalidate, payload/CRC, commit marker.
  uint8_t WriteIndex() const;

  // Exclusive nonblocking backend, owned by its board/service adapter.
  ByteStorage& storage_;
  // Logical base chosen by the future board layout, not the portable library.
  uint32_t base_;
  // Validated maximum age of an accepted asynchronous write.
  uint16_t busy_timeout_ms_;
  // All six scanned records; reused for final both-copy verification.
  uint8_t image_[kWindowSize];
  // Immutable expected record for the current two-copy transaction.
  uint8_t expected_[kRecordSize];
  // Evidence is returned by value and becomes verified only after final
  // readback.
  VerifiedBootState boot_;
  // Runtime manager request currently being serviced.
  EmergencyWrite active_;
  // Newer latch to replace an in-flight transaction after the current byte
  // ends.
  EmergencyWrite replacement_;
  // One completion slot; the manager must consume it before submitting another.
  StorageCompletion completion_;
  // Startup/final verification byte cursor.
  uint16_t cursor_;
  // Relative first-slot offset of the active pair.
  uint16_t pair_offset_;
  // Byte-write start time for wrap-safe busy timeout.
  uint32_t write_started_;
  // Last accepted runtime token, independent of durable journal numbering.
  uint32_t latest_token_;
  // Durable emergency sequence, including interrupted/superseded transactions.
  uint32_t emergency_counter_;
  // Last started byte address, relative to the exclusive window.
  uint16_t write_address_;
  // Last started byte value, retained until readback verifies it.
  uint8_t write_value_;
  // Current pair copy, zero or one.
  uint8_t copy_;
  // Transaction byte step, zero invalidates and 32 commits.
  uint8_t write_step_;
  // Public lifecycle projection.
  JournalState state_;
  // Internal bounded byte service phase.
  Phase phase_;
  // Whether a physical write is outstanding and its timeout must be checked.
  bool write_pending_;
  // Whether a completion is ready for the manager to consume.
  bool completion_pending_;
};
}  // namespace bcp
}  // namespace bajols
#endif
