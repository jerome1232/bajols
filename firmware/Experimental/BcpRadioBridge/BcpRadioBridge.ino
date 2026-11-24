#include <Arduino.h>
#include <BajolsProtocol.h>
#include <RFM69.h>
#include <RFM69registers.h>
#include <SPI.h>
#include <esp_system.h>
#include <string.h>

#ifndef BAJOLS_RF_BUOY
#define BAJOLS_RF_BUOY 0
#endif
#if BAJOLS_RF_BUOY != 0 && BAJOLS_RF_BUOY != 1
#error "BAJOLS_RF_BUOY must be 0 (shore) or 1 (buoy)"
#endif

namespace {
namespace bcp = bajols::bcp;
constexpr bool kBuoy = BAJOLS_RF_BUOY == 1;
constexpr uint8_t kLocalNode = kBuoy ? 2 : 1;
constexpr uint8_t kPeerNode = kBuoy ? 1 : 2;
constexpr uint8_t kNetwork = 73;
constexpr uint8_t kChipSelect = 27;
constexpr uint8_t kInterrupt = 26;
constexpr uint8_t kReset = 25;
constexpr uint32_t kFrameLifetimeMs = 50;
constexpr uint32_t kReplyWindowMs = 10;
constexpr uint32_t kReplyGuardMs = 2;
constexpr uint32_t kLatestReplyStartMs = 4;
constexpr uint8_t kProtocolTag = 0x10;
constexpr uint8_t kStart = 1;
constexpr uint8_t kEnd = 2;
constexpr size_t kHeaderSize = 5;
constexpr size_t kFragmentCapacity = RF69_MAX_DATA_LEN - kHeaderSize;

HardwareSerial& serialLink = kBuoy ? Serial2 : Serial;

bool replaceable(bcp::MessageType type) {
  using Type = bcp::MessageType;
  return type == Type::kMovementCommand || type == Type::kBuoyancyCommand ||
         type == Type::kTelemetry || type == Type::kObjectReport ||
         type == Type::kVehicleStatus || type == Type::kControlTicket;
}

struct WireFrame {
  uint8_t bytes[bcp::kMaxWireSize];
  size_t size = 0;
  uint32_t receivedAt = 0;
  bool replaceable = false;
};

class FrameQueue {
 public:
  void push(const WireFrame& wire) {
    bcp::Frame decoded;
    bcp::Error error;
    if (!bcp::DecodeFrame(wire.bytes, wire.size, decoded, error)) return;

    const bool canReplace = replaceable(decoded.header.type);
    expire(millis());
    if (decoded.header.type == bcp::MessageType::kDisarm ||
        decoded.header.type == bcp::MessageType::kEmergencyStop) {
      for (size_t i = count_; i > 0; --i) {
        if (frames_[i - 1].replaceable) erase(i - 1);
      }
    }
    if (count_ == kCapacity) {
      if (canReplace) return;
      size_t victim = 0;
      while (victim < count_ && !frames_[victim].replaceable) ++victim;
      if (victim == count_) return;
      erase(victim);
    }
    frames_[count_] = wire;
    frames_[count_++].replaceable = canReplace;
  }

  const WireFrame* front() const { return count_ ? &frames_[0] : nullptr; }
  void pop() { erase(0); }

  void expire(uint32_t now) {
    while (count_ && now - frames_[0].receivedAt >= kFrameLifetimeMs) pop();
  }

 private:
  void erase(size_t index) {
    --count_;
    memmove(frames_ + index, frames_ + index + 1,
            (count_ - index) * sizeof(WireFrame));
  }

  static constexpr size_t kCapacity = 8;
  WireFrame frames_[kCapacity]{};
  size_t count_ = 0;
};

class SerialFramer {
 public:
  void read(FrameQueue& queue, uint32_t now) {
    if (frame_.size && now - frame_.receivedAt >= kFrameLifetimeMs) discard_ = true;
    // Bound the work so continuous serial input cannot starve the RF schedule.
    for (size_t i = 0; i < 128; ++i) {
      const int value = serialLink.read();
      if (value < 0) return;
      if (!frame_.size) frame_.receivedAt = now;

      if (!discard_ && frame_.size < sizeof(frame_.bytes)) {
        frame_.bytes[frame_.size++] = static_cast<uint8_t>(value);
      } else {
        discard_ = true;
      }

      if (value == 0) {
        if (!discard_) queue.push(frame_);
        frame_.size = 0;
        discard_ = false;
      }
    }
  }

 private:
  WireFrame frame_{};
  bool discard_ = false;
};

class Radio : public RFM69 {
 public:
  Radio() : RFM69(kChipSelect, kInterrupt, true) {}

  bool begin() {
    pinMode(kReset, OUTPUT);
    digitalWrite(kReset, HIGH);
    delay(1);
    digitalWrite(kReset, LOW);
    delay(10);
    SPI.begin(18, 19, 23, kChipSelect);
    if (!initialize(RF69_915MHZ, kLocalNode, kNetwork)) return false;

    setHighPower(true);
    setPowerDBm(0);
    // 32 MHz / 128 = 250 kbit/s. Deviation is about 125 kHz.
    writeReg(REG_BITRATEMSB, 0);
    writeReg(REG_BITRATELSB, 128);
    writeReg(REG_FDEVMSB, 0x08);
    writeReg(REG_FDEVLSB, 0);
    writeReg(REG_RXBW, RF_RXBW_DCCFREQ_010 | RF_RXBW_MANT_16 | RF_RXBW_EXP_0);
    writeReg(REG_PAYLOADLENGTH, RF69_MAX_DATA_LEN + 3);
    receiveDone();
    return true;
  }

  void transmit(const uint8_t* data, size_t size) {
    // The poll/reply schedule owns airtime. Use the driver's FIFO envelope
    // with bounded waits so a radio fault cannot stall the serial service.
    setMode(RF69_MODE_STANDBY);
    if (waitFor(REG_IRQFLAGS1, RF_IRQFLAGS1_MODEREADY, 1000)) {
      writeReg(REG_IRQFLAGS2, RF_IRQFLAGS2_FIFOOVERRUN);
      select();
      SPI.transfer(REG_FIFO | 0x80);
      SPI.transfer(static_cast<uint8_t>(size + 3));
      SPI.transfer(kPeerNode);
      SPI.transfer(kLocalNode);
      SPI.transfer(0);  // Driver control byte. No RF ACK request.
      for (size_t i = 0; i < size; ++i) SPI.transfer(data[i]);
      unselect();
      setMode(RF69_MODE_TX);
      waitFor(REG_IRQFLAGS2, RF_IRQFLAGS2_PACKETSENT, 4000);
      setMode(RF69_MODE_STANDBY);
    }
    receiveDone();
  }

 private:
  bool waitFor(uint8_t reg, uint8_t mask, uint32_t timeoutUs) {
    const uint32_t start = micros();
    do {
      if (readReg(reg) & mask) return true;
    } while (micros() - start < timeoutUs);
    return false;
  }
};

class Bridge {
 public:
  bool begin() {
    serialLink.setRxBufferSize(512);
    if (kBuoy) {
      serialLink.begin(115200, SERIAL_8N1, 16, 17);
    } else {
      serialLink.begin(115200, SERIAL_8N1);
    }
    slot_ = esp_random();
    return radio_.begin();
  }

  void service() {
    receiveRadio();
    uint32_t now = millis();
    serialInput_.read(toRadio_, now);
    toSerial_.expire(now);
    const WireFrame* output = toSerial_.front();
    if (output && serialLink.availableForWrite() >= static_cast<int>(output->size)) {
      serialLink.write(output->bytes, output->size);
      toSerial_.pop();
    }

    now = millis();
    if (kBuoy) {
      const uint32_t age = now - slotAt_;
      if (slotPending_ && age >= kReplyGuardMs) {
        slotPending_ = false;
        if (age <= kLatestReplyStartMs) transmit(now);
        else sendOffset_ = 0;
      }
    } else if (!sentPoll_ || now - slotAt_ >= kReplyWindowMs) {
      if (slotPending_) incoming_.size = 0;  // Reply lost.
      ++slot_;
      transmit(now);
      slotAt_ = millis();
      slotPending_ = true;
      sentPoll_ = true;
    }
  }

 private:
  void transmit(uint32_t now) {
    uint8_t packet[RF69_MAX_DATA_LEN] = {kProtocolTag};
    for (size_t i = 0; i < 4; ++i) {
      packet[1 + i] = static_cast<uint8_t>(slot_ >> (8 * i));
    }

    if (sendOffset_ && (now - outgoing_.receivedAt >= kFrameLifetimeMs ||
                        slot_ != lastFragmentSlot_ + 1u)) {
      sendOffset_ = 0;
    }
    if (!sendOffset_) {
      toRadio_.expire(now);
      outgoing_.size = 0;
      if (const WireFrame* next = toRadio_.front()) {
        outgoing_ = *next;
        toRadio_.pop();
      }
    }

    size_t length = 0;
    if (outgoing_.size) {
      const size_t remaining = outgoing_.size - sendOffset_;
      length = remaining < kFragmentCapacity ? remaining : kFragmentCapacity;
      if (!sendOffset_) packet[0] |= kStart;
      memcpy(packet + kHeaderSize, outgoing_.bytes + sendOffset_, length);
      sendOffset_ += length;
      lastFragmentSlot_ = slot_;
      if (sendOffset_ == outgoing_.size) {
        packet[0] |= kEnd;
        sendOffset_ = 0;
      }
    }
    radio_.transmit(packet, kHeaderSize + length);
  }

  void receiveRadio() {
    if (!radio_.receiveDone() || radio_.SENDERID != kPeerNode ||
        radio_.TARGETID != kLocalNode || radio_.DATALEN < kHeaderSize ||
        radio_.DATALEN > RF69_MAX_DATA_LEN) return;

    const uint8_t* packet = radio_.DATA;
    const uint8_t flags = packet[0] & (kStart | kEnd);
    if ((packet[0] & ~(kStart | kEnd)) != kProtocolTag) return;

    uint32_t slot = 0;
    for (size_t i = 0; i < 4; ++i) {
      slot |= static_cast<uint32_t>(packet[1 + i]) << (8 * i);
    }
    const uint32_t now = millis();
    if (kBuoy) {
      if (heardPoll_ && slot == slot_) return;
      slot_ = slot;
      slotAt_ = now;
      slotPending_ = true;
      heardPoll_ = true;
    } else {
      if (!slotPending_ || slot != slot_ || now - slotAt_ >= kReplyWindowMs) return;
      slotPending_ = false;
    }
    acceptFragment(packet + kHeaderSize, radio_.DATALEN - kHeaderSize, flags, now);
  }

  void acceptFragment(const uint8_t* bytes, size_t length, uint8_t flags,
                      uint32_t now) {
    if (flags & kStart) {
      incoming_.size = 0;
      incoming_.receivedAt = now;
    } else if (!incoming_.size || slot_ != lastReceivedSlot_ + 1u ||
               now - incoming_.receivedAt >= kFrameLifetimeMs) {
      incoming_.size = 0;
      return;
    }

    if (!length || (!(flags & kEnd) && length != kFragmentCapacity) ||
        length > sizeof(incoming_.bytes) - incoming_.size) {
      incoming_.size = 0;
      return;
    }

    memcpy(incoming_.bytes + incoming_.size, bytes, length);
    incoming_.size += length;
    lastReceivedSlot_ = slot_;
    if (flags & kEnd) {
      toSerial_.push(incoming_);
      incoming_.size = 0;
    }
  }

  Radio radio_;
  SerialFramer serialInput_;
  FrameQueue toRadio_;
  FrameQueue toSerial_;
  WireFrame outgoing_{};
  WireFrame incoming_{};
  size_t sendOffset_ = 0;
  uint32_t slot_ = 0;
  uint32_t slotAt_ = 0;
  uint32_t lastFragmentSlot_ = 0;
  uint32_t lastReceivedSlot_ = 0;
  bool slotPending_ = false;
  bool heardPoll_ = false;
  bool sentPoll_ = false;
};

Bridge bridge;
bool radioReady = false;
}  // namespace

void setup() {
  radioReady = bridge.begin();
}

void loop() {
  if (radioReady) bridge.service();
  delay(1);
}
