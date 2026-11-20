#include <BajolsStorage.h>

namespace {
namespace bcp = bajols::bcp;

// RAM-only test device; this sketch never includes or writes physical EEPROM.
class RamStorage : public bcp::ByteStorage {
 public:
  // Models blank erased bytes without pretending they are provisioned identity.
  RamStorage() : bytes_(), writes_(0) {
    for (uint16_t i = 0; i < sizeof(bytes_); ++i) {
      bytes_[i] = 0xFF;
    }
  }
  // Returns this RAM fixture's exclusive capacity.
  uint32_t Capacity() const override {
    return sizeof(bytes_);
  }
  // RAM fixture accesses complete immediately.
  bcp::ByteStorageStatus Status() override {
    return bcp::ByteStorageStatus::kReady;
  }
  // Reads one fixture byte, never a physical register or EEPROM address.
  bool Read(uint32_t address, uint8_t& value) override {
    if (address >= sizeof(bytes_)) {
      return false;
    }
    value = bytes_[address];
    return true;
  }
  // Mutates only RAM and counts attempts; blank media should never reach here.
  bool StartWrite(uint32_t address, uint8_t value) override {
    if (address >= sizeof(bytes_)) {
      return false;
    }
    bytes_[address] = value;
    ++writes_;
    return true;
  }
  // Exposes attempts for debugger confirmation that blank storage was not
  // written.
  uint16_t Writes() const {
    return writes_;
  }

 private:
  // Explicitly volatile-by-lifecycle RAM fixture, erased anew on every startup.
  uint8_t bytes_[bcp::IdentityJournal::kWindowSize];
  // Number of RAM writes attempted during the check.
  uint16_t writes_;
};

// This object is a simulation device, not an EEPROM backend.
RamStorage storage;
// Logical offset zero and 10 ms timeout are RAM fixture parameters only.
bcp::IdentityJournal journal(storage, 0, 10);
// Debugger-visible result; no claim about execution is made by compile CI.
volatile bool self_test_passed = false;
}  // namespace

// Starts the incremental boot scan without provisioning or initializing
// hardware.
void setup() {
  journal.BeginBoot();
}

// Services bounded storage work and confirms unprovisioned RAM fails closed.
void loop() {
  journal.Step(millis());
  self_test_passed = journal.State() == bcp::JournalState::kFailed &&
                     !journal.BootState().identity_verified &&
                     storage.Writes() == 0;
}
