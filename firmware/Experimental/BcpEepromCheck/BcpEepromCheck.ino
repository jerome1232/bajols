#include <BajolsMegaEeprom.h>
#include <BajolsPersistence.h>

namespace {
namespace bcp = bajols::bcp;

// Read-only compile check, not a provisioner or approved operational EEPROM
// map.
bcp::MegaEepromStorage storage(0, bcp::IdentityJournal::kWindowSize, false);
// Example timeout for compilation only; hardware timing remains to be
// validated.
bcp::IdentityJournal journal(storage, 0, 10);

// No actual sensors or ballast controller are implemented in this check.
bcp::ControllerProfile MakeProfile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 0, 0, bcp::kMovementIntervalMs,
                          bcp::kCommandTimeoutMs, bcp::kTicketIntervalMs,
                          bcp::kTicketLifetimeMs, bcp::kNeutralHoldMs,
                          bcp::kNeutralTolerance};
  profile.intervention_depth_mm = 4500;
  profile.release_depth_mm = 4000;
  profile.depth_stale_ms = 2000;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = bcp::FaultSeverity::kBlocking;
  }
  return profile;
}

// This check never claims operational identity, even if existing records
// validate.
bcp::VerifiedBootState UnverifiedBoot() {
  bcp::VerifiedBootState boot = {};
  boot.emergency = bcp::StoredEmergency::kAmbiguous;
  return boot;
}

// The controller remains disabled; production startup must wait for verified
// boot.
bcp::SessionManager manager(MakeProfile(), UnverifiedBoot());
// Exercises linkage of coordinator and hardware backend without enabling
// writes.
bcp::PersistenceCoordinator coordinator(journal, manager);
// Debugger-visible result; no serial or actuator hardware is initialized.
volatile bool identity_inhibited = true;
}  // namespace

// Starts a read-only scan. Any attempted boot advance is refused by the
// backend.
void setup() {
  journal.BeginBoot();
}

// No provisioning and no EEPROM writes, including on previously provisioned
// media.
void loop() {
  if (journal.State() == bcp::JournalState::kBooting) {
    journal.Step(millis());
  } else {
    coordinator.Step(millis());
  }
  identity_inhibited =
      !journal.BootState().identity_verified && !manager.HasSession();
}
