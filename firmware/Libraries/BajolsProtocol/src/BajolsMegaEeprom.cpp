// The portable library still builds on native hosts and other Arduino targets.
#if defined(__AVR_ATmega2560__)
#include "BajolsMegaEeprom.h"

#include <avr/io.h>
#include <util/atomic.h>

namespace bajols {
namespace bcp {

MegaEepromStorage::MegaEepromStorage(uint16_t base, uint16_t length,
                                     bool writes_enabled)
    : base_(base),
      length_(length),
      writes_enabled_(writes_enabled),
      valid_(length != 0 && static_cast<uint32_t>(base) + length <=
                                static_cast<uint32_t>(E2END) + 1) {}

uint32_t MegaEepromStorage::Capacity() const {
  return static_cast<uint32_t>(E2END) + 1;
}

bool MegaEepromStorage::InWindow(uint32_t address) const {
  return valid_ && address >= base_ && address - base_ < length_;
}

ByteStorageStatus MegaEepromStorage::Status() {
  if (!valid_ || (EECR & _BV(EERIE)) != 0 || (SPMCSR & _BV(SPMEN)) != 0) {
    return ByteStorageStatus::kFailed;
  }
  return (EECR & _BV(EEPE)) != 0 ? ByteStorageStatus::kBusy
                                 : ByteStorageStatus::kReady;
}

bool MegaEepromStorage::Read(uint32_t address, uint8_t& value) {
  if (!InWindow(address)) {
    return false;
  }
  bool read = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (Status() == ByteStorageStatus::kReady) {
      EEAR = static_cast<uint16_t>(address);
      EECR |= _BV(EERE);
      value = EEDR;
      read = true;
    }
  }
  return read;
}

bool MegaEepromStorage::StartWrite(uint32_t address, uint8_t value) {
  if (!writes_enabled_ || !InWindow(address)) {
    return false;
  }
  bool accepted = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (Status() == ByteStorageStatus::kReady) {
      EEAR = static_cast<uint16_t>(address);
      EECR |= _BV(EERE);
      if (EEDR != value) {
        EEDR = value;
        // Select erase-and-write mode. Do not spin waiting for hardware.
        EECR &= static_cast<uint8_t>(~(_BV(EEPM1) | _BV(EEPM0)));
        // Consecutive SBI instructions guarantee the four-cycle enable window.
        asm volatile("sbi %0, %1\n\tsbi %0, %2"
                     :
                     : "I"(_SFR_IO_ADDR(EECR)), "I"(EEMPE), "I"(EEPE)
                     : "memory");
      }
      accepted = true;
    }
  }
  return accepted;
}
}  // namespace bcp
}  // namespace bajols
#endif
