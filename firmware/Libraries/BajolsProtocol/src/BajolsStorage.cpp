#include "BajolsStorage.h"

#include <string.h>

namespace bajols {
namespace bcp {
namespace {
// Logical pair locations in the experimental caller-selected window.
constexpr uint16_t kBootOffset = 64;
// Emergency records are isolated from runtime boot updates.
constexpr uint16_t kEmergencyOffset = 128;
// A committed slot has this marker; payload writes never alter it implicitly.
constexpr uint8_t kCommitted = 0xA5;
}  // namespace

IdentityJournal::IdentityJournal(ByteStorage& storage, uint32_t base,
                                 uint16_t busy_timeout_ms)
    : storage_(storage),
      base_(base),
      busy_timeout_ms_(busy_timeout_ms),
      image_(),
      expected_(),
      boot_(),
      active_(),
      replacement_(),
      completion_(),
      cursor_(0),
      pair_offset_(0),
      write_started_(0),
      latest_token_(0),
      emergency_counter_(0),
      write_address_(0),
      write_value_(0),
      copy_(0),
      write_step_(0),
      state_(JournalState::kUninitialized),
      phase_(Phase::kScan),
      write_pending_(false),
      completion_pending_(false) {
  boot_.emergency = StoredEmergency::kAmbiguous;
}

uint32_t IdentityJournal::Counter(const uint8_t* bytes) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    value |= static_cast<uint32_t>(bytes[4 + i]) << (8 * i);
  }
  return value;
}

bool IdentityJournal::ValidRecord(const uint8_t* bytes, uint8_t kind) {
  if (bytes[0] != kCommitted || bytes[1] != 1 || bytes[2] != kind ||
      (kind != 3 && bytes[3] != 0) || bytes[3] > 1 ||
      (kind == 1 && Counter(bytes) != 0) ||
      (kind != 1 && Counter(bytes) == 0xFFFFFFFFUL)) {
    return false;
  }
  for (uint8_t i = 24; i < 30; ++i) {
    if (bytes[i] != 0) {
      return false;
    }
  }
  bool zero = true;
  bool erased = true;
  for (uint8_t i = 8; i < 24; ++i) {
    zero = zero && bytes[i] == 0;
    erased = erased && bytes[i] == 0xFF;
    if (kind != 1 && bytes[i] != 0) {
      return false;
    }
  }
  if (kind == 1 && (zero || erased)) {
    return false;
  }
  uint16_t stored_crc = static_cast<uint16_t>(bytes[30]) |
                        (static_cast<uint16_t>(bytes[31]) << 8);
  return stored_crc == ComputeCrc(bytes + 1, 29);
}

bool IdentityJournal::BeginBoot() {
  if (state_ != JournalState::kUninitialized) {
    return false;
  }
  uint32_t capacity = storage_.Capacity();
  if (busy_timeout_ms_ == 0 || busy_timeout_ms_ >= 32768 || base_ > capacity ||
      capacity - base_ < kWindowSize) {
    Fail();
    return false;
  }
  state_ = JournalState::kBooting;
  phase_ = Phase::kScan;
  cursor_ = 0;
  return true;
}

void IdentityJournal::MakeRecord(uint8_t kind, uint32_t counter, bool latched) {
  memset(expected_, 0, sizeof(expected_));
  expected_[0] = kCommitted;
  expected_[1] = 1;
  expected_[2] = kind;
  expected_[3] = latched ? 1 : 0;
  for (uint8_t i = 0; i < 4; ++i) {
    expected_[4 + i] = static_cast<uint8_t>(counter >> (8 * i));
  }
  uint16_t crc = ComputeCrc(expected_ + 1, 29);
  expected_[30] = static_cast<uint8_t>(crc);
  expected_[31] = static_cast<uint8_t>(crc >> 8);
}

void IdentityJournal::FinishScan() {
  const uint8_t* emergency = image_ + kEmergencyOffset;
  bool emergency_valid =
      ValidRecord(emergency, 3) && ValidRecord(emergency + kRecordSize, 3) &&
      memcmp(emergency, emergency + kRecordSize, kRecordSize) == 0;
  boot_.emergency_storage_healthy = emergency_valid;
  if (emergency_valid) {
    boot_.emergency = emergency[3] == 0 ? StoredEmergency::kCleared
                                        : StoredEmergency::kLatched;
    emergency_counter_ = Counter(emergency);
  }
  const uint8_t* identity = image_;
  const uint8_t* counter = image_ + kBootOffset;
  if (!ValidRecord(identity, 1) || !ValidRecord(identity + kRecordSize, 1) ||
      memcmp(identity, identity + kRecordSize, kRecordSize) != 0 ||
      !ValidRecord(counter, 2) || !ValidRecord(counter + kRecordSize, 2) ||
      memcmp(counter, counter + kRecordSize, kRecordSize) != 0 ||
      Counter(counter) >= 0xFFFFFFFEUL) {
    Fail();
    return;
  }
  memcpy(boot_.vehicle_id.bytes, identity + 8, sizeof(boot_.vehicle_id.bytes));
  MakeRecord(2, Counter(counter) + 1, false);
  BeginPair(kBootOffset);
}

void IdentityJournal::BeginPair(uint16_t offset) {
  pair_offset_ = offset;
  copy_ = 0;
  write_step_ = 0;
  cursor_ = 0;
  write_pending_ = false;
  phase_ = Phase::kWrite;
}

bool IdentityJournal::BeginEmergency(const EmergencyWrite& request) {
  active_ = request;
  replacement_.pending = false;
  if (emergency_counter_ >= 0xFFFFFFFEUL) {
    Fail();
    return false;
  }
  ++emergency_counter_;
  MakeRecord(3, emergency_counter_, request.latched);
  BeginPair(kEmergencyOffset);
  state_ = JournalState::kEmergencyWriting;
  return true;
}

bool IdentityJournal::RequestEmergency(const EmergencyWrite& request) {
  if (!request.pending || request.token == 0 ||
      request.token <= latest_token_ || !boot_.identity_verified ||
      !boot_.emergency_storage_healthy ||
      (state_ != JournalState::kReady &&
       state_ != JournalState::kEmergencyWriting)) {
    return false;
  }
  if (state_ == JournalState::kEmergencyWriting) {
    if (!request.latched) {
      return false;
    }
    replacement_ = request;
    latest_token_ = request.token;
    return true;
  }
  if (completion_pending_ && !request.latched) {
    return false;
  }
  completion_pending_ = false;
  latest_token_ = request.token;
  // Set lifecycle before counter checks so any failure acknowledges this token.
  state_ = JournalState::kEmergencyWriting;
  return BeginEmergency(request);
}

uint8_t IdentityJournal::WriteIndex() const {
  return write_step_ == 32 ? 0 : write_step_;
}

void IdentityJournal::FinishPair() {
  if (state_ == JournalState::kBooting) {
    boot_.boot_counter = Counter(expected_);
    boot_.identity_verified = true;
  } else {
    boot_.emergency = expected_[3] == 0 ? StoredEmergency::kCleared
                                        : StoredEmergency::kLatched;
    completion_ = {active_.token, true};
    completion_pending_ = true;
  }
  state_ = JournalState::kReady;
}

void IdentityJournal::Fail() {
  if (state_ == JournalState::kEmergencyWriting) {
    completion_ = {latest_token_, false};
    completion_pending_ = true;
    boot_.emergency = StoredEmergency::kAmbiguous;
    boot_.emergency_storage_healthy = false;
  } else {
    // Identity failure must not invent an emergency procedure when the separate
    // emergency pair was successfully recovered. Unknown startup state remains
    // ambiguous because boot_ starts ambiguous until FinishScan verifies it.
    boot_.identity_verified = false;
  }
  state_ = JournalState::kFailed;
}

void IdentityJournal::Step(uint32_t now) {
  if (state_ != JournalState::kBooting &&
      state_ != JournalState::kEmergencyWriting) {
    return;
  }
  ByteStorageStatus status = storage_.Status();
  if ((status != ByteStorageStatus::kReady &&
       status != ByteStorageStatus::kBusy) ||
      (write_pending_ &&
       static_cast<uint32_t>(now - write_started_) >= busy_timeout_ms_)) {
    Fail();
    return;
  }
  if (status == ByteStorageStatus::kBusy) {
    if (!write_pending_) {
      Fail();
    }
    return;
  }
  if (replacement_.pending && !write_pending_) {
    EmergencyWrite replacement = replacement_;
    BeginEmergency(replacement);
    return;
  }
  switch (phase_) {
    case Phase::kScan:
      if (!storage_.Read(base_ + cursor_, image_[cursor_])) {
        Fail();
        return;
      }
      if (++cursor_ == kWindowSize) {
        FinishScan();
      }
      break;
    case Phase::kWrite: {
      uint8_t index = WriteIndex();
      write_address_ = pair_offset_ + copy_ * kRecordSize + index;
      write_value_ = write_step_ == 0 ? 0 : expected_[index];
      if (!storage_.StartWrite(base_ + write_address_, write_value_)) {
        Fail();
        return;
      }
      write_pending_ = true;
      write_started_ = now;
      phase_ = Phase::kReadBack;
      break;
    }
    case Phase::kReadBack: {
      uint8_t value;
      if (!storage_.Read(base_ + write_address_, value) ||
          value != write_value_) {
        Fail();
        return;
      }
      write_pending_ = false;
      if (++write_step_ > 32) {
        write_step_ = 0;
        if (++copy_ == 2) {
          cursor_ = 0;
          phase_ = Phase::kVerify;
          break;
        }
      }
      phase_ = Phase::kWrite;
      break;
    }
    case Phase::kVerify:
      if (!storage_.Read(base_ + pair_offset_ + cursor_, image_[cursor_])) {
        Fail();
        return;
      }
      if (image_[cursor_] != expected_[cursor_ % kRecordSize]) {
        Fail();
        return;
      }
      if (++cursor_ == 2 * kRecordSize) {
        FinishPair();
      }
      break;
  }
}

VerifiedBootState IdentityJournal::BootState() const {
  return boot_;
}
JournalState IdentityJournal::State() const {
  return state_;
}

bool IdentityJournal::TakeCompletion(StorageCompletion& completion) {
  if (!completion_pending_) {
    return false;
  }
  completion = completion_;
  completion_pending_ = false;
  return true;
}
}  // namespace bcp
}  // namespace bajols
