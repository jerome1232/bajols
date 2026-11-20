#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ARDUINO_SERIAL_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ARDUINO_SERIAL_H_

#include <Arduino.h>

#include "BajolsEndpoint.h"

namespace bajols {
namespace bcp {

// AVR HardwareSerial adapter. Use this port exclusively for BCP binary frames;
// debug text and other writers invalidate both framing and capacity guarantees.
class ArduinoSerialPort : public BytePort {
 public:
  // References an existing UART; begin() remains an explicit setup
  // responsibility.
  explicit ArduinoSerialPort(HardwareSerial& serial) : serial_(serial) {}
  // Reads immediately, returning -1 when the hardware receive buffer is empty.
  int Read() override {
    return serial_.read();
  }
  // Returns currently free UART ring capacity without waiting for transmission.
  size_t Writable() override {
    int available = serial_.availableForWrite();
    return available > 0 ? static_cast<size_t>(available) : 0;
  }
  // Copies only bytes fitting the current driver ring; never flushes or waits.
  size_t Write(const uint8_t* bytes, size_t length) override {
    size_t available = Writable();
    if (length > available) {
      length = available;
    }
    return length == 0 ? 0 : serial_.write(bytes, length);
  }

 private:
  // Exclusive protocol UART, shared only with its own transmit interrupt.
  HardwareSerial& serial_;
};

// Explicit onboard clock; no synchronization with the ground station is needed.
class ArduinoMillisecondClock : public MillisecondClock {
 public:
  // Returns the Arduino's wrapping monotonic millisecond count.
  uint32_t Now() override {
    return millis();
  }
};
}  // namespace bcp
}  // namespace bajols
#endif
