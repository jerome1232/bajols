#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_BOARD_STORAGE_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_BOARD_STORAGE_H_

#if defined(__AVR_ATmega2560__)
#include "BajolsMegaEeprom.h"
#elif defined(ARDUINO_ARCH_ESP32)
#include "BajolsEsp32Nvs.h"
#else
#error \
    "Board storage supports ATmega2560 and Arduino-ESP32; supply ByteStorage elsewhere."
#endif

namespace bajols {
namespace bcp {
#if defined(__AVR_ATmega2560__)
// Mega physical EEPROM addresses; its native controller owns each byte write.
using BoardStorage = MegaEepromStorage;
// Physical capacity available to a caller-selected Mega journal window.
constexpr int32_t kBoardStorageCapacity = 4096;
// AVR needs no worker startup and performs no writes during initialization.
inline bool InitializeBoardStorage(BoardStorage& storage) {
  return storage.Status() != ByteStorageStatus::kFailed;
}
#else
// ESP32 logical journal bytes backed by a dedicated NVS partition/worker.
using BoardStorage = Esp32NvsStorage;
// Logical byte capacity; the flash partition offset is not an EEPROM base.
constexpr int32_t kBoardStorageCapacity = IdentityJournal::kWindowSize;
// Default read-only ESP32 builds refuse NVS recovery/initialization entirely.
inline bool InitializeBoardStorage(BoardStorage& storage) {
  return storage.Begin();
}
#endif
}  // namespace bcp
}  // namespace bajols
#endif
