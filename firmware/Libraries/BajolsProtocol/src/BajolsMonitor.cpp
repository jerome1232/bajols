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
                                    const VerifiedBootState& boot)
    : manager(Profile(), boot),
      endpoint(manager),
      persistence(journal, manager) {}

MonitoringRuntime::MonitoringRuntime(IdentityJournal& journal)
    : journal_(journal),
      storage_(),
      begun_(false),
      ready_(false),
      failed_(false) {}

MonitoringRuntime::~MonitoringRuntime() {
  if (ready_) {
    storage_.active.~Context();
  }
}

ControllerProfile MonitoringRuntime::Profile() {
  ControllerProfile profile = {};
  profile.capabilities = {1, 0, 0, 0, 0, 50, 500, 50, 500, 1000, 50};
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
  if (!journal_.BeginBoot()) {
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
    new (static_cast<void*>(&storage_.active)) Context(journal_, boot);
    ready_ = true;
    return;  // Keep the final startup read separate from runtime storage work.
  }
  Context& context = storage_.active;
  context.endpoint.Step(port, clock);
  context.persistence.Step(clock.Now());
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

bool MonitoringRuntime::Failed() const {
  return failed_;
}
}  // namespace bcp
}  // namespace bajols
