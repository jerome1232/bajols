#include "BajolsRecordStorage.h"

#include <string.h>

namespace bajols {
namespace bcp {
RecordJournalStorage::RecordJournalStorage(RecordBackend& backend,
                                           bool writes_enabled)
    : backend_(backend),
      writes_enabled_(writes_enabled),
      loaded_(false),
      failed_(false),
      pending_(false),
      slot_(0),
      image_(),
      expected_() {}

bool RecordJournalStorage::Load() {
  if (loaded_ || failed_ || backend_.Status() != ByteStorageStatus::kReady) {
    return false;
  }
  for (uint8_t slot = 0; slot < 6; ++slot) {
    if (!backend_.ReadRecord(slot,
                             image_ + slot * IdentityJournal::kRecordSize)) {
      failed_ = true;
      return false;
    }
  }
  loaded_ = true;
  return true;
}

uint32_t RecordJournalStorage::Capacity() const {
  return IdentityJournal::kWindowSize;
}

ByteStorageStatus RecordJournalStorage::Status() {
  if (!loaded_ || failed_) {
    return ByteStorageStatus::kFailed;
  }
  ByteStorageStatus state = backend_.Status();
  if (state == ByteStorageStatus::kFailed ||
      (!pending_ && state != ByteStorageStatus::kReady)) {
    failed_ = true;
    return ByteStorageStatus::kFailed;
  }
  if (pending_ && state == ByteStorageStatus::kReady) {
    uint8_t durable[IdentityJournal::kRecordSize];
    if (!backend_.ReadRecord(slot_, durable) ||
        memcmp(durable, expected_, sizeof(durable)) != 0) {
      failed_ = true;
      return ByteStorageStatus::kFailed;
    }
    memcpy(image_ + slot_ * sizeof(durable), durable, sizeof(durable));
    pending_ = false;
  }
  return state;
}

bool RecordJournalStorage::Read(uint32_t address, uint8_t& value) {
  if (address >= Capacity() || Status() != ByteStorageStatus::kReady) {
    return false;
  }
  value = image_[address];
  return true;
}

bool RecordJournalStorage::StartWrite(uint32_t address, uint8_t value) {
  if (!writes_enabled_ || address >= Capacity() ||
      Status() != ByteStorageStatus::kReady) {
    return false;
  }
  uint8_t slot = address / IdentityJournal::kRecordSize;
  uint8_t index = address % IdentityJournal::kRecordSize;
  uint8_t* record = image_ + slot * IdentityJournal::kRecordSize;
  if (index != 0) {
    if (record[0] != 0) {
      return false;  // A durable invalidation must precede payload staging.
    }
    record[index] = value;
    return true;
  }
  if (value != 0 && value != 0xA5) {
    return false;
  }
  if (record[0] == value) {
    return true;
  }
  memcpy(expected_, record, sizeof(expected_));
  expected_[0] = value;
  if (!backend_.StartRecordWrite(slot, expected_)) {
    failed_ = true;
    return false;
  }
  slot_ = slot;
  pending_ = true;
  return true;
}
}  // namespace bcp
}  // namespace bajols
