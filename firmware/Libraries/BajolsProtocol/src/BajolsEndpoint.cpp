#include "BajolsEndpoint.h"

namespace bajols {
namespace bcp {

SerialEndpoint::SerialEndpoint(SessionManager& manager)
    : manager_(manager),
      queue_(),
      receive_(),
      diagnostics_(),
      queued_(0),
      received_(0),
      discarding_(false),
      resync_pending_(false) {}

bool SerialEndpoint::PublishObjects(const ObjectReport& report, uint32_t now) {
  return manager_.PublishObjects(report, now, *this);
}

bool SerialEndpoint::PublishTelemetry(const Telemetry& telemetry,
                                      uint32_t now) {
  return manager_.PublishTelemetry(telemetry, now, *this);
}

void SerialEndpoint::Increment(uint32_t& counter) {
  if (counter != 0xFFFFFFFFUL) {
    ++counter;
  }
}

bool SerialEndpoint::Critical(MessageType type) {
  return type == MessageType::kHelloReply ||
         type == MessageType::kSessionReady ||
         type == MessageType::kCapabilities ||
         type == MessageType::kCommandResult;
}

bool SerialEndpoint::SafetyReport(MessageType type) {
  return type == MessageType::kVehicleStatus || type == MessageType::kFault;
}

void SerialEndpoint::Remove(uint8_t slot) {
  for (uint8_t i = slot; i + 1 < queued_; ++i) {
    queue_[i] = queue_[i + 1];
  }
  --queued_;
}

void SerialEndpoint::Send(const Packet& packet) {
  const MessageType type = packet.header.type;
  const uint16_t key =
      type == MessageType::kFault
          ? static_cast<uint16_t>(packet.message.fault.fault_code)
      : type == MessageType::kObjectReport
          ? packet.message.object_report.sensor_id
          : 0;
  if (!Critical(type)) {
    for (uint8_t i = 0; i < queued_; ++i) {
      const QueuedFrame& old = queue_[i];
      if (old.sent == 0 && old.type == type && old.key == key &&
          old.session.boot_counter == packet.header.session.boot_counter &&
          old.session.session_counter ==
              packet.header.session.session_counter) {
        Remove(i);
        Increment(diagnostics_.coalesced_frames);
        break;
      }
    }
  }
  if (queued_ == kQueueCapacity) {
    bool removed = false;
    // Admission favors safety and discrete/handshake replies; retained frames
    // stay FIFO. Reordering serialized frames would make the desktop reject
    // older sequences.
    if (Critical(type) || SafetyReport(type)) {
      for (uint8_t i = 0; i < queued_; ++i) {
        if (queue_[i].sent == 0 && !Critical(queue_[i].type) &&
            !SafetyReport(queue_[i].type)) {
          Remove(i);
          removed = true;
          Increment(diagnostics_.dropped_frames);
          break;
        }
      }
    }
    if (!removed && SafetyReport(type)) {
      // Discrete results are cached for retry; preserve authoritative safety
      // state.
      for (uint8_t i = 0; i < queued_; ++i) {
        if (queue_[i].sent == 0 &&
            queue_[i].type == MessageType::kCommandResult) {
          Remove(i);
          removed = true;
          Increment(diagnostics_.dropped_frames);
          break;
        }
      }
    }
    if (!removed) {
      Increment(diagnostics_.dropped_frames);
      return;
    }
  }
  QueuedFrame& frame = queue_[queued_];
  size_t written;
  Error error;
  if (!Encode(packet, Direction::kVehicleToPc, frame.wire, sizeof(frame.wire),
              written, error)) {
    Increment(diagnostics_.adapter_errors);
    return;
  }
  frame.length = static_cast<uint8_t>(written);
  frame.sent = 0;
  frame.type = type;
  frame.key = key;
  frame.session = packet.header.session;
  ++queued_;
}

void SerialEndpoint::Consume(uint8_t value, uint32_t now) {
  if (value == 0) {
    if (discarding_) {
      discarding_ = false;
      received_ = 0;
      Increment(diagnostics_.oversized_frames);
    } else if (received_ != 0) {
      receive_[received_++] = 0;
      size_t length = received_;
      received_ = 0;
      if (!manager_.Receive(receive_, length, now, *this)) {
        Increment(diagnostics_.malformed_frames);
      }
    }
  } else if (!discarding_) {
    if (received_ == kMaxWireSize - 1) {
      received_ = 0;
      discarding_ = true;
    } else {
      receive_[received_++] = value;
    }
  }
}

void SerialEndpoint::Drain(BytePort& port, MillisecondClock& clock) {
  size_t budget = kTransmitBudget;
  // At most eight frame fragments plus a possible cancellation delimiter.
  for (uint8_t iteration = 0; iteration <= kQueueCapacity && budget != 0;
       ++iteration) {
    if (!resync_pending_ && queued_ == 0) {
      break;
    }
    size_t available = port.Writable();
    if (available == 0) {
      break;
    }
    uint8_t delimiter = 0;
    const uint8_t* bytes = &delimiter;
    size_t length = 1;
    if (!resync_pending_) {
      const QueuedFrame& frame = queue_[0];
      bytes = frame.wire + frame.sent;
      length = frame.length - frame.sent;
    }
    if (length > available) {
      length = available;
    }
    if (length > budget) {
      length = budget;
    }
    size_t written = port.Write(bytes, length);
    if (written > length) {
      Increment(diagnostics_.adapter_errors);
      resync_pending_ = true;
      Reset(clock.Now());
      break;
    }
    if (written == 0) {
      break;
    }
    budget -= written;
    if (resync_pending_) {
      resync_pending_ = false;
    } else {
      queue_[0].sent += static_cast<uint8_t>(written);
      if (queue_[0].sent == queue_[0].length) {
        Remove(0);
      }
    }
    manager_.Advance(clock.Now());
  }
}

void SerialEndpoint::Step(BytePort& port, MillisecondClock& clock) {
  manager_.Advance(clock.Now());
  for (size_t i = 0; i < kReceiveBudget; ++i) {
    int value = port.Read();
    if (value == -1) {
      break;
    }
    if (value < 0 || value > 255) {
      Increment(diagnostics_.adapter_errors);
      Reset(clock.Now());
      break;
    }
    Consume(static_cast<uint8_t>(value), clock.Now());
  }
  manager_.Poll(clock.Now(), *this);
  Drain(port, clock);
  manager_.Advance(clock.Now());
}

void SerialEndpoint::Reset(uint32_t now) {
  resync_pending_ = resync_pending_ || (queued_ != 0 && queue_[0].sent != 0);
  queued_ = 0;
  received_ = 0;
  discarding_ = false;
  manager_.RevokePcSession(now);
}

EndpointDiagnostics SerialEndpoint::Diagnostics() const { return diagnostics_; }
uint8_t SerialEndpoint::QueuedFrames() const { return queued_; }

}  // namespace bcp
}  // namespace bajols
