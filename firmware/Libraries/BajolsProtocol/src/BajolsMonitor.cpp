#include "BajolsMonitor.h"

#if defined(ARDUINO_ARCH_AVR)
#include <new.h>
#else
#include <new>
#endif

namespace bajols {
namespace bcp {

MonitoringRuntime::ContextStorage::ContextStorage() : inactive(0) {}
MonitoringRuntime::ContextStorage::~ContextStorage() {}

MonitoringRuntime::Context::Context(IdentityJournal& journal,
                                    const VerifiedBootState& boot,
                                    uint32_t telemetry_mask)
    : manager(Profile(telemetry_mask), boot),
      endpoint(manager),
      persistence(journal, manager) {}

MonitoringRuntime::MonitoringRuntime(IdentityJournal& journal,
                                     uint32_t telemetry_mask)
    : journal_(journal),
      storage_(),
      begun_(false),
      ready_(false),
      failed_(false),
      telemetry_mask_(telemetry_mask),
      telemetry_(),
      has_telemetry_(false),
      telemetry_at_(0),
      telemetry_sent_at_(0) {}

MonitoringRuntime::~MonitoringRuntime() {
  if (ready_) {
    storage_.active.~Context();
  }
}

ControllerProfile MonitoringRuntime::Profile(uint32_t telemetry_mask) {
  ControllerProfile profile = {};
  profile.capabilities = {1,  0,   0,    0, telemetry_mask, 50, 500,
                          50, 500, 1000, 50};
  // Required structural fields, never used as real sensor calibration here.
  profile.intervention_depth_mm = 5000;
  profile.release_depth_mm = 0;
  profile.depth_stale_ms = 1;
  profile.pc_configured = true;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = FaultSeverity::kBlocking;
  }
  return profile;
}

bool MonitoringRuntime::Begin() {
  if (begun_) {
    return false;
  }
  begun_ = true;
  if ((telemetry_mask_ & ~255UL) != 0 || !journal_.BeginBoot()) {
    failed_ = true;
  }
  return !failed_;
}

void MonitoringRuntime::Step(BytePort& port, MillisecondClock& clock) {
  if (!begun_ || failed_) {
    return;
  }
  if (!ready_) {
    journal_.Step(clock.Now());
    if (journal_.State() == JournalState::kFailed) {
      failed_ = true;
      return;
    }
    if (journal_.State() != JournalState::kReady) {
      return;
    }
    VerifiedBootState boot = journal_.BootState();
    if (!boot.identity_verified) {
      failed_ = true;
      return;
    }
    new (static_cast<void*>(&storage_.active))
        Context(journal_, boot, telemetry_mask_);
    ready_ = true;
    return;  // Keep the final startup read separate from runtime storage work.
  }
  Context& context = storage_.active;
  context.endpoint.Step(port, clock);
  context.persistence.Step(clock.Now());
  uint32_t now = clock.Now();
  if (has_telemetry_ &&
      static_cast<uint32_t>(now - telemetry_sent_at_) >= 200) {
    telemetry_sent_at_ = now;
    Telemetry aged = telemetry_;
    uint32_t elapsed = now - telemetry_at_;
    for (uint8_t i = 0; i < aged.count; ++i) {
      uint16_t age = aged.records[i].sample_age_ms;
      if (age != 65535) {
        aged.records[i].sample_age_ms =
            elapsed >= 65534UL - age ? 65534
                                     : static_cast<uint16_t>(age + elapsed);
      }
    }
    context.endpoint.PublishTelemetry(aged, now);
    if (elapsed >= 65534) {
      // Retain an explicitly unknown-age tombstone across future clock wraps.
      for (uint8_t i = 0; i < telemetry_.count; ++i) {
        telemetry_.records[i].sample_age_ms = 65535;
      }
    }
  }
}

bool MonitoringRuntime::SetTelemetry(const Telemetry& telemetry, uint32_t now) {
  if (failed_ || telemetry.count > kMaxTelemetryRecords) {
    return false;
  }
  for (uint8_t i = 0; i < telemetry.count; ++i) {
    uint8_t id = static_cast<uint8_t>(telemetry.records[i].measurement_id);
    if (id < 1 || id > 8 || (telemetry_mask_ & (1UL << (id - 1))) == 0) {
      return false;
    }
  }
  Packet packet = {};
  packet.header.type = MessageType::kTelemetry;
  packet.message.telemetry = telemetry;
  uint8_t wire[kMaxWireSize];
  size_t written;
  Error error;
  if (!Encode(packet, Direction::kVehicleToPc, wire, sizeof(wire), written,
              error)) {
    return false;
  }
  telemetry_ = telemetry;
  telemetry_at_ = now;
  has_telemetry_ = true;
  return true;
}

void MonitoringRuntime::Disconnect(uint32_t now) {
  if (ready_) {
    storage_.active.endpoint.Reset(now);
  }
}

bool MonitoringRuntime::GetStatus(uint32_t now, VehicleStatus& status) {
  if (!ready_) {
    return false;
  }
  status = storage_.active.manager.Status(now);
  return true;
}

bool MonitoringRuntime::Failed() const { return failed_; }
}  // namespace bcp
}  // namespace bajols
