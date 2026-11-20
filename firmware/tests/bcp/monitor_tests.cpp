#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <deque>
#include <vector>

#include "BajolsMonitor.h"
#include "provisioned_image.h"

namespace {
namespace bcp = bajols::bcp;

// Reports the first violated startup/monitoring invariant.
void Check(bool condition, const char* label) {
  if (!condition) {
    fprintf(stderr, "FAIL monitor: %s\n", label);
    exit(1);
  }
}

// Byte device seeded from the actual offline Python tool's initial records.
class Memory : public bcp::ByteStorage {
 public:
  // Explicit fixture contents, not physical EEPROM.
  uint8_t bytes[192];
  // Read and write attempts for service-budget verification.
  size_t operations = 0;
  // Whether this fixture models deliberate runtime write permission.
  bool writable = true;
  // Creates the same fresh image prepared by the provisioning utility.
  Memory() {
    memcpy(bytes, kProvisionedImage, sizeof(bytes));
  }
  // Returns the fixture's logical capacity.
  uint32_t Capacity() const override {
    return sizeof(bytes);
  }
  // Simulated RAM bytes complete immediately.
  bcp::ByteStorageStatus Status() override {
    return bcp::ByteStorageStatus::kReady;
  }
  // Reads one explicit logical byte and counts it.
  bool Read(uint32_t address, uint8_t& value) override {
    ++operations;
    Check(address < sizeof(bytes), "read in window");
    value = bytes[address];
    return true;
  }
  // Writes one fixture byte only with explicit simulation permission.
  bool StartWrite(uint32_t address, uint8_t value) override {
    ++operations;
    Check(address < sizeof(bytes), "write in window");
    if (!writable) {
      return false;
    }
    bytes[address] = value;
    return true;
  }
};

// Virtual wrapping millisecond clock.
class Clock : public bcp::MillisecondClock {
 public:
  // Onboard time supplied explicitly by the test.
  uint32_t now = 0;
  // Returns the current instant without wall-clock dependencies.
  uint32_t Now() override {
    return now;
  }
};

// Host-only byte queue for encoded PC requests and UART responses.
class Port : public bcp::BytePort {
 public:
  // Newly transmitted PC bytes awaiting bounded consumption.
  std::deque<uint8_t> input;
  // Raw vehicle bytes already delivered to the PC fixture.
  std::vector<uint8_t> output;
  // Returns an available byte or immediately reports no input.
  int Read() override {
    if (input.empty()) {
      return -1;
    }
    uint8_t byte = input.front();
    input.pop_front();
    return byte;
  }
  // Fixed fake UART write capacity.
  size_t Writable() override {
    return 64;
  }
  // Copies exactly the requested bounded chunk.
  size_t Write(const uint8_t* bytes, size_t length) override {
    Check(length <= 64, "bounded serial chunk");
    output.insert(output.end(), bytes, bytes + length);
    return length;
  }
};

// Sends one complete encoded request through the composed runtime.
void Send(bcp::MonitoringRuntime& runtime, Port& port, Clock& clock,
          const bcp::Packet& packet) {
  uint8_t wire[bcp::kMaxWireSize];
  size_t written;
  bcp::Error error;
  Check(bcp::Encode(packet, bcp::Direction::kPcToVehicle, wire, sizeof(wire),
                    written, error),
        "PC encode");
  port.input.insert(port.input.end(), wire, wire + written);
  for (uint8_t i = 0; i < 20; ++i) {
    runtime.Step(port, clock);
  }
}

// Decodes all delivered response bytes and returns the latest requested
// message.
bcp::Packet Last(const Port& port, bcp::MessageType type) {
  bcp::StreamDecoder decoder(bcp::Direction::kVehicleToPc);
  bcp::Packet latest = {};
  bool found = false;
  for (size_t i = 0; i < port.output.size(); ++i) {
    bcp::Packet packet;
    bcp::Error error;
    bcp::StreamResult result = decoder.Push(port.output[i], packet, error);
    Check(result != bcp::StreamResult::kRejected,
          "all composed responses have valid wire syntax");
    if (result == bcp::StreamResult::kPacketReady &&
        packet.header.type == type) {
      latest = packet;
      found = true;
    }
  }
  Check(found, "expected response delivered");
  return latest;
}

// Completes startup while checking no controller/serial response exists early.
void Start(bcp::MonitoringRuntime& runtime, Memory& memory, Port& port,
           Clock& clock) {
  Check(runtime.Begin() && !runtime.Begin(), "one startup per runtime");
  bcp::VehicleStatus status;
  Check(!runtime.GetStatus(clock.now, status),
        "no controller before verified identity");
  for (size_t i = 0; i < 600; ++i) {
    size_t before = memory.operations;
    runtime.Step(port, clock);
    Check(memory.operations - before <= 1,
          "composed startup/runtime one byte storage budget");
    ++clock.now;
  }
}

// Full tool-image -> verified journal -> deferred manager -> encoded BCP
// session.
void TestMonitoring() {
  Memory memory;
  bcp::IdentityJournal journal(memory, 0, 10);
  bcp::MonitoringRuntime runtime(journal);
  Port port;
  Clock clock;
  Start(runtime, memory, port, clock);
  Check(!runtime.Failed() && journal.BootState().boot_counter == 1,
        "provisioned image advances before runtime");
  bcp::Packet hello = {};
  hello.header.type = bcp::MessageType::kHello;
  hello.message.hello.requested_version = 1;
  hello.message.hello.nonce.bytes[0] = 42;
  Send(runtime, port, clock, hello);
  bcp::Packet reply = Last(port, bcp::MessageType::kHelloReply);
  Check(reply.header.session.boot_counter == 1 &&
            reply.message.hello_reply.vehicle_id.bytes[0] == 1 &&
            reply.message.hello_reply.vehicle_id.bytes[15] == 16,
        "provisioned opaque ID preserved on wire");
  bcp::Packet confirm = {};
  confirm.header = {bcp::MessageType::kSessionConfirm, reply.header.session, 1};
  confirm.message.session_confirm.nonce = hello.message.hello.nonce;
  Send(runtime, port, clock, confirm);
  bcp::Capabilities capabilities =
      Last(port, bcp::MessageType::kCapabilities).message.capabilities;
  Check(capabilities.supported_modes == 0 && capabilities.supported_axes == 0 &&
            capabilities.supported_systems == 0 &&
            capabilities.supported_telemetry == 0,
        "monitor advertises no unimplemented movement, ballast or sensors");
  bcp::VehicleStatus status =
      Last(port, bcp::MessageType::kVehicleStatus).message.status;
  Check(status.movement_owner == bcp::MovementOwner::kNone &&
            status.control_mode == bcp::ControlMode::kNone &&
            status.safety_state == bcp::SafetyState::kEmergencyStopLatched,
        "initial provisioning retains emergency latch and no movement owner");
  bcp::ControlTicket ticket =
      Last(port, bcp::MessageType::kControlTicket).message.control_ticket;
  bcp::Packet arm = {};
  arm.header = {bcp::MessageType::kArm, reply.header.session, 2};
  arm.message.arm = {1, ticket.ticket_id, ticket.control_generation};
  Send(runtime, port, clock, arm);
  Check(Last(port, bcp::MessageType::kCommandResult)
                .message.command_result.result !=
            bcp::CommandResultCode::kAccepted,
        "monitor can never arm");
  runtime.Disconnect(clock.now);
  Check(runtime.GetStatus(clock.now, status) &&
            status.safety_state == bcp::SafetyState::kEmergencyStopLatched,
        "disconnect never clears emergency latch");
  Memory reboot = memory;
  bcp::IdentityJournal next_journal(reboot, 0, 10);
  bcp::MonitoringRuntime next(next_journal);
  Port next_port;
  Clock next_clock;
  Start(next, reboot, next_port, next_clock);
  Check(next_journal.BootState().boot_counter == 2,
        "composed reboot never reuses boot identity");
}

// Blank/corrupt/read-only startup never constructs an authorizing manager.
void TestStartupFailures() {
  for (uint8_t failure = 0; failure < 3; ++failure) {
    Memory memory;
    if (failure == 0) {
      memset(memory.bytes, 0xFF, sizeof(memory.bytes));
    }
    if (failure == 1) {
      memory.bytes[64] = 0;
    }
    if (failure == 2) {
      memory.writable = false;
    }
    bcp::IdentityJournal journal(memory, 0, 10);
    bcp::MonitoringRuntime runtime(journal);
    Port port;
    Clock clock;
    Start(runtime, memory, port, clock);
    bcp::VehicleStatus status;
    Check(runtime.Failed() && !runtime.GetStatus(clock.now, status) &&
              port.output.empty(),
          "failed startup exposes no session or unverified status");
  }
}
}  // namespace

// Runs the real provisioning image through boot/transport composition and
// failures.
int main() {
  TestMonitoring();
  TestStartupFailures();
  printf(
      "PASS: offline image -> verified startup -> monitoring session; arming "
      "inhibited\n");
  printf(
      "Host MonitoringRuntime storage: %zu bytes, fixed placement storage, no "
      "heap\n",
      sizeof(bcp::MonitoringRuntime));
  return 0;
}
