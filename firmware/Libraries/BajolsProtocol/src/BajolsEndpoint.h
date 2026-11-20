#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ENDPOINT_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_ENDPOINT_H_

#include "BajolsSession.h"

namespace bajols {
namespace bcp {

// Nonblocking byte transport, independent of Arduino and USB connection policy.
class BytePort {
 public:
  // Releases an adapter through its interface.
  virtual ~BytePort() {}
  // Returns one available byte, or -1 immediately when no byte is available.
  virtual int Read() = 0;
  // Returns how many bytes Write can accept immediately without waiting.
  virtual size_t Writable() = 0;
  // Accepts up to length bytes without waiting and returns the actual count.
  // The caller never requests more than the latest Writable result.
  virtual size_t Write(const uint8_t* bytes, size_t length) = 0;
};

// Onboard monotonic milliseconds; a fresh reading is taken at frame boundaries.
class MillisecondClock {
 public:
  // Releases a clock adapter through its interface.
  virtual ~MillisecondClock() {}
  // Returns wrapping uint32_t milliseconds, never a desktop wall clock.
  virtual uint32_t Now() = 0;
};

// Saturating boot-local diagnostics; none of these values authorize commands.
struct EndpointDiagnostics {
  // Complete frames rejected by the codec, independent of policy rejection.
  uint32_t malformed_frames;
  // Oversized frames discarded through the following delimiter.
  uint32_t oversized_frames;
  // Outgoing unsent replaceable reports superseded by fresher reports.
  uint32_t coalesced_frames;
  // Outgoing frames evicted or refused because fixed storage was full.
  uint32_t dropped_frames;
  // Adapter contract violations or internal outgoing encoding failures.
  uint32_t adapter_errors;
};

// Bounded receive framing and ordered outgoing queue around one session
// manager. This class never initializes or drives actuators, and never owns
// identity storage.
class SerialEndpoint : private PacketSink {
 public:
  // Maximum byte reads per Step, including noise and empty delimiters.
  static constexpr size_t kReceiveBudget = 64;
  // Maximum bytes accepted by the transport per Step.
  static constexpr size_t kTransmitBudget = 64;
  // Fixed frame slots shared by reports and discrete/handshake responses.
  static constexpr uint8_t kQueueCapacity = 8;

  // References a longer-lived manager; each manager has exactly one endpoint.
  explicit SerialEndpoint(SessionManager& manager);
  // A live byte stream and its authorization manager cannot be duplicated.
  SerialEndpoint(const SerialEndpoint&) = delete;
  // Prevents accidental reassignment of a partial frame or live connection.
  SerialEndpoint& operator=(const SerialEndpoint&) = delete;
  // Runs bounded RX, safety/report scheduling and nonblocking TX in that order.
  // Must be called frequently even during silence or transmit backpressure.
  void Step(BytePort& port, MillisecondClock& clock);
  // Offers a trusted source snapshot through session validation and the bounded
  // queue. True means publication accepted, not delivery acknowledgement.
  bool PublishObjects(const ObjectReport& report, uint32_t now);
  // Cancels all buffered input/output and revokes PC authorization. If output
  // was partial, the next write first emits a delimiter to restore framing.
  // Call for a known disconnect/cancel; it never resets local-owner control.
  void Reset(uint32_t now);
  // Returns a snapshot of saturating local counters without clearing them.
  EndpointDiagnostics Diagnostics() const;
  // Returns fixed outgoing occupancy, including an in-flight partial frame.
  uint8_t QueuedFrames() const;

 private:
  // One encoded frame with metadata used only for bounded queue admission.
  struct QueuedFrame {
    // Complete immutable encoded bytes, including the delimiter.
    uint8_t wire[kMaxWireSize];
    // Number of encoded bytes in this slot.
    uint8_t length;
    // Bytes already accepted by the serial driver; a started slot is immutable.
    uint8_t sent;
    // Message type for report coalescing and admission priority.
    MessageType type;
    // Fault code when type is Fault; zero for all other reports.
    uint16_t key;
    // Identity prevents coalescing across pending/current/replaced sessions.
    SessionId session;
  };

  // Saturates a diagnostic counter rather than wrapping it.
  static void Increment(uint32_t& counter);
  // Identifies responses whose request identity must not be coalesced.
  static bool Critical(MessageType type);
  // Identifies safety/status reports whose visibility outranks cached replies.
  static bool SafetyReport(MessageType type);
  // Removes an unstarted slot, preserving the order of all retained sequences.
  void Remove(uint8_t slot);
  // Receives one byte without allocating or buffering more than one frame.
  void Consume(uint8_t value, uint32_t now);
  // Encodes/adopts a synchronous manager response without blocking the loop.
  void Send(const Packet& packet) override;
  // Writes bounded chunks, preserving a partial head frame and byte ordering.
  void Drain(BytePort& port, MillisecondClock& clock);

  // The independent safety/authorization owner; not owned by this endpoint.
  SessionManager& manager_;
  // Fixed outgoing storage, never a heap queue.
  QueuedFrame queue_[kQueueCapacity];
  // Fixed incoming frame, with no queue of stale received commands.
  uint8_t receive_[kMaxWireSize];
  // Saturating diagnostic projection.
  EndpointDiagnostics diagnostics_;
  // Number of occupied outgoing slots.
  uint8_t queued_;
  // Number of non-delimiter incoming bytes, bounded to 115.
  uint8_t received_;
  // Whether input is being discarded until its next delimiter.
  bool discarding_;
  // Whether a canceled partial transmission still needs a resync delimiter.
  bool resync_pending_;
};
}  // namespace bcp
}  // namespace bajols
#endif
