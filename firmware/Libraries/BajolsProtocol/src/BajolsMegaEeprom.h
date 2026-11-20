#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MEGA_EEPROM_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MEGA_EEPROM_H_

#if !defined(__AVR_ATmega2560__)
#error \
    "BajolsMegaEeprom requires the ATmega2560; use ByteStorage on other targets."
#endif

#include "BajolsStorage.h"

namespace bajols {
namespace bcp {

// Exclusive ATmega2560 EEPROM adapter. No constructor or read starts a write.
// Do not mix with EEPROM library calls, EEPROM ISRs, or flash self-programming.
class MegaEepromStorage : public ByteStorage {
 public:
  // Restricts access to an explicit physical window. Writes need explicit
  // opt-in; read-only mode cannot advance a journal even on already-provisioned
  // hardware.
  MegaEepromStorage(uint16_t base, uint16_t length, bool writes_enabled);
  // The EEPROM controller has one owner and cannot be cloned.
  MegaEepromStorage(const MegaEepromStorage&) = delete;
  // Prevents accidental reassignment of controller ownership/window
  // permissions.
  MegaEepromStorage& operator=(const MegaEepromStorage&) = delete;
  // Returns physical device bytes; Read/StartWrite also enforce the chosen
  // window.
  uint32_t Capacity() const override;
  // Returns immediately; conflicting EEPROM ISR/flash programming is a failure.
  ByteStorageStatus Status() override;
  // Reads one byte only when ready, with a bounded interrupt-protected strobe.
  bool Read(uint32_t address, uint8_t& value) override;
  // Starts one erase/write operation and returns without waiting for EEPE.
  // Unchanged cells are skipped to reduce wear; journal readback still
  // verifies.
  bool StartWrite(uint32_t address, uint8_t value) override;

 private:
  // Checks overflow-free bounds against both device and explicit access window.
  bool InWindow(uint32_t address) const;
  // First accessible EEPROM cell, selected by the board/service caller.
  uint16_t base_;
  // Exclusive window length; invalid windows permanently refuse all access.
  uint16_t length_;
  // Explicit permission, never enabled by boot detection or blank-media
  // recovery.
  bool writes_enabled_;
  // Whether the requested window fits the physical ATmega2560 device.
  bool valid_;
};
}  // namespace bcp
}  // namespace bajols
#endif
