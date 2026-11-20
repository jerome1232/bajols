#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BajolsPersistence.h"
#include "BajolsStorage.h"

namespace {
namespace bcp = bajols::bcp;

// Stops immediately with the durable invariant which failed.
void Check(bool condition, const char* label) {
  if (!condition) {
    fprintf(stderr, "FAIL storage: %s\n", label);
    exit(1);
  }
}

// Explicit in-memory device; no OS file or physical EEPROM is written.
class Memory : public bcp::ByteStorage {
 public:
  // Window plus sentinels for logical address-bound tests.
  uint8_t bytes[256];
  // Completed or accepted physical byte-write count.
  size_t writes = 0;
  // Immediate byte-read count.
  size_t reads = 0;
  // Fault-injection switches for independent backend failure paths.
  bool fail_read = false;
  // Refuses starting writes before any mutation.
  bool fail_write = false;
  // Claims success while leaving the requested cell unchanged.
  bool ignore_write = false;
  // Holds an accepted byte pending until Complete is called by the test.
  bool asynchronous = false;
  // Whether a deferred byte is in progress.
  bool pending = false;
  // Optional permanent hardware failure.
  bool failed = false;
  // Deferred cell address, owned by this simulated device.
  uint32_t address = 0;
  // Deferred cell value, owned by this simulated device.
  uint8_t value = 0;

  // Models unprovisioned erased storage.
  Memory() {
    memset(bytes, 0xFF, sizeof(bytes));
  }
  // Returns fixed device capacity independently of the selected logical window.
  uint32_t Capacity() const override {
    return sizeof(bytes);
  }
  // Reports busy/ready immediately without completing a byte implicitly.
  bcp::ByteStorageStatus Status() override {
    return failed    ? bcp::ByteStorageStatus::kFailed
           : pending ? bcp::ByteStorageStatus::kBusy
                     : bcp::ByteStorageStatus::kReady;
  }
  // Reads one memory cell and counts every attempted device operation.
  bool Read(uint32_t cell, uint8_t& result) override {
    ++reads;
    Check(cell < sizeof(bytes) && !pending,
          "read stays in device bounds and waits for readiness");
    if (fail_read) {
      return false;
    }
    result = bytes[cell];
    return true;
  }
  // Accepts exactly one byte, optionally deferring its durable completion.
  bool StartWrite(uint32_t cell, uint8_t next) override {
    ++writes;
    Check(cell < sizeof(bytes) && !pending,
          "writes stay in bounds and never overlap");
    if (fail_write) {
      return false;
    }
    if (asynchronous) {
      pending = true;
      address = cell;
      value = next;
    } else if (!ignore_write) {
      bytes[cell] = next;
    }
    return true;
  }
  // Finishes a queued byte, modeling the device completing between control
  // ticks.
  void Complete() {
    if (pending) {
      if (!ignore_write) {
        bytes[address] = value;
      }
      pending = false;
    }
  }
};

// Independent fixture builder for the proposed serialized record layout.
void Record(uint8_t* bytes, uint8_t kind, uint32_t counter, bool latched) {
  memset(bytes, 0, 32);
  bytes[0] = 0xA5;
  bytes[1] = 1;
  bytes[2] = kind;
  bytes[3] = latched ? 1 : 0;
  for (uint8_t i = 0; i < 4; ++i) {
    bytes[4 + i] = static_cast<uint8_t>(counter >> (8 * i));
  }
  if (kind == 1) {
    for (uint8_t i = 0; i < 16; ++i) {
      bytes[8 + i] = i + 1;
    }
  }
  uint16_t crc = bcp::ComputeCrc(bytes + 1, 29);
  bytes[30] = static_cast<uint8_t>(crc);
  bytes[31] = static_cast<uint8_t>(crc >> 8);
}

// Creates explicit factory/service fixtures, outside runtime provisioning APIs.
void Provision(Memory& memory, bool latched = true, uint32_t counter = 0,
               uint16_t base = 0, uint32_t emergency_counter = 0) {
  Record(memory.bytes + base, 1, 0, false);
  memcpy(memory.bytes + base + 32, memory.bytes + base, 32);
  Record(memory.bytes + base + 64, 2, counter, false);
  memcpy(memory.bytes + base + 96, memory.bytes + base + 64, 32);
  Record(memory.bytes + base + 128, 3, emergency_counter, latched);
  memcpy(memory.bytes + base + 160, memory.bytes + base + 128, 32);
}

// Checks the one-device-operation budget on every step, including verification.
void Step(bcp::IdentityJournal& journal, Memory& memory, uint32_t& now) {
  size_t before = memory.reads + memory.writes;
  journal.Step(now++);
  Check(memory.reads + memory.writes - before <= 1,
        "one byte operation per service step");
}

// Drives a healthy journal to a stable public outcome with an explicit loop
// bound.
void Drive(bcp::IdentityJournal& journal, Memory& memory, uint32_t& now) {
  for (size_t i = 0; i < 2000; ++i) {
    memory.Complete();
    if (journal.State() == bcp::JournalState::kReady ||
        journal.State() == bcp::JournalState::kFailed) {
      return;
    }
    Step(journal, memory, now);
  }
  Check(false, "bounded transaction completes");
}

// Starts and verifies one simulated boot without assuming any durable evidence.
void Boot(bcp::IdentityJournal& journal, Memory& memory, uint32_t& now) {
  Check(journal.BeginBoot(), "start boot once");
  Check(!journal.BootState().identity_verified,
        "startup never preclaims verified identity");
  Drive(journal, memory, now);
}

// Verifies erased media, provisioning separation, stable identity and monotonic
// boots.
void TestBootIdentity() {
  Memory blank;
  bcp::IdentityJournal erased(blank, 0, 10);
  uint32_t now = 0;
  Boot(erased, blank, now);
  Check(erased.State() == bcp::JournalState::kFailed &&
            !erased.BootState().identity_verified &&
            erased.BootState().emergency == bcp::StoredEmergency::kAmbiguous &&
            blank.writes == 0,
        "blank storage is never initialized or issued an identity");
  Memory memory;
  Provision(memory, true, 0, 16);
  uint8_t identity[64];
  memcpy(identity, memory.bytes + 16, sizeof(identity));
  bcp::IdentityJournal first(memory, 16, 10);
  Boot(first, memory, now);
  Check(first.State() == bcp::JournalState::kReady &&
            first.BootState().identity_verified &&
            first.BootState().boot_counter == 1 &&
            first.BootState().vehicle_id.bytes[0] == 1 &&
            first.BootState().emergency == bcp::StoredEmergency::kLatched,
        "verified first boot and stable ID");
  Check(memcmp(identity, memory.bytes + 16, sizeof(identity)) == 0 &&
            memory.bytes[15] == 0xFF && memory.bytes[208] == 0xFF,
        "boot touches neither provisioned identity nor window sentinels");
  Check(!first.BeginBoot(), "same journal cannot issue another boot");
  bcp::IdentityJournal second(memory, 16, 10);
  Boot(second, memory, now);
  Check(second.BootState().boot_counter == 2,
        "reboot consumes the next counter");
}

// Cuts power after every byte-write boundary of a two-copy boot transaction.
void TestBootPowerCuts() {
  for (size_t cut = 0; cut <= 66; ++cut) {
    Memory memory;
    Provision(memory, true, 41);
    bcp::IdentityJournal interrupted(memory, 0, 10);
    uint32_t now = 0;
    Check(interrupted.BeginBoot(), "power-cut boot begins");
    while (memory.writes < cut) {
      Step(interrupted, memory, now);
      Check(now < 2000, "reach selected boot mutation");
    }
    Check(!interrupted.BootState().identity_verified,
          "no boot identity before full paired readback");
    // Only durable bytes survive. Runtime journal state is discarded on reboot.
    bcp::IdentityJournal reboot(memory, 0, 10);
    Boot(reboot, memory, now);
    if (cut == 0 || cut == 66) {
      Check(reboot.State() == bcp::JournalState::kReady &&
                reboot.BootState().boot_counter == (cut == 0 ? 42U : 43U),
            "consume complete counters and allow gaps");
    } else {
      Check(reboot.State() == bcp::JournalState::kFailed &&
                !reboot.BootState().identity_verified,
            "torn boot never falls back to older identity");
    }
  }
}

// Flips every individual bit across every record and refuses older-copy
// fallback.
void TestCorruption() {
  for (size_t cell = 0; cell < 192; ++cell) {
    for (uint8_t bit = 0; bit < 8; ++bit) {
      Memory memory;
      Provision(memory, false, 7);
      memory.bytes[cell] ^= static_cast<uint8_t>(1U << bit);
      bcp::IdentityJournal journal(memory, 0, 10);
      uint32_t now = 0;
      Boot(journal, memory, now);
      if (cell < 128) {
        Check(!journal.BootState().identity_verified &&
                  journal.State() == bcp::JournalState::kFailed &&
                  memory.writes == 0,
              "identity/boot corruption cannot select surviving older copy");
        Check(journal.BootState().emergency == bcp::StoredEmergency::kCleared &&
                  journal.BootState().emergency_storage_healthy,
              "identity failure does not manufacture an emergency latch");
      } else {
        Check(journal.BootState().identity_verified &&
                  journal.BootState().boot_counter == 8 &&
                  journal.BootState().emergency ==
                      bcp::StoredEmergency::kAmbiguous &&
                  !journal.BootState().emergency_storage_healthy,
              "emergency corruption latches and blocks arming but permits "
              "valid identity");
        Check(!journal.RequestEmergency({1, false, true}),
              "ambiguous storage cannot be repaired by remote clear");
      }
    }
  }
  Memory mismatch;
  Provision(mismatch);
  Record(mismatch.bytes + 96, 2, 9, false);
  bcp::IdentityJournal journal(mismatch, 0, 10);
  uint32_t now = 0;
  Boot(journal, mismatch, now);
  Check(!journal.BootState().identity_verified,
        "CRC-valid divergent counters still require service");
}

// Cuts power after each durable byte of latch and locally authorized clear
// writes.
void TestEmergencyPowerCuts() {
  for (uint8_t clear = 0; clear < 2; ++clear) {
    for (size_t cut = 0; cut <= 66; ++cut) {
      Memory memory;
      Provision(memory, clear != 0);
      bcp::IdentityJournal journal(memory, 0, 10);
      uint32_t now = 0;
      Boot(journal, memory, now);
      size_t origin = memory.writes;
      Check(journal.RequestEmergency({1, clear == 0, true}),
            "start explicit emergency transaction");
      while (memory.writes - origin < cut) {
        Step(journal, memory, now);
      }
      bcp::StorageCompletion completion;
      Check(!journal.TakeCompletion(completion),
            "byte commits alone never acknowledge before paired readback");
      bcp::IdentityJournal reboot(memory, 0, 10);
      Boot(reboot, memory, now);
      Check(reboot.BootState().identity_verified,
            "emergency writes do not damage boot identity");
      if (cut == 0) {
        Check(reboot.BootState().emergency ==
                  (clear ? bcp::StoredEmergency::kLatched
                         : bcp::StoredEmergency::kCleared),
              "before first durable mutation old state survives: independent "
              "hardware latch required");
      } else if (cut == 66) {
        Check(reboot.BootState().emergency ==
                  (clear ? bcp::StoredEmergency::kCleared
                         : bcp::StoredEmergency::kLatched),
              "only fully committed matching records recover the new state");
      } else {
        Check(
            reboot.BootState().emergency == bcp::StoredEmergency::kAmbiguous &&
                !reboot.BootState().emergency_storage_healthy,
            "partial emergency transaction is never inferred cleared");
      }
    }
  }
}

// Supersedes a clear at every possible byte boundary and never acknowledges it.
void TestSupersededClear() {
  for (size_t cut = 0; cut <= 66; ++cut) {
    Memory memory;
    Provision(memory);
    bcp::IdentityJournal journal(memory, 0, 10);
    uint32_t now = 0;
    Boot(journal, memory, now);
    Check(journal.RequestEmergency({1, false, true}), "local clear starts");
    size_t origin = memory.writes;
    while (memory.writes - origin < cut) {
      Step(journal, memory, now);
    }
    Check(!journal.RequestEmergency({2, false, true}),
          "clear cannot replace in-flight work");
    Check(journal.RequestEmergency({2, true, true}),
          "new stop supersedes clear");
    Check(!journal.RequestEmergency({1, false, true}),
          "older token cannot supersede stop");
    Drive(journal, memory, now);
    bcp::StorageCompletion completion;
    Check(journal.TakeCompletion(completion) && completion.token == 2 &&
              completion.verified && !journal.TakeCompletion(completion),
          "only newest token completes exactly once");
    Check(journal.BootState().emergency == bcp::StoredEmergency::kLatched,
          "superseding stop commits both latched copies");
    bcp::IdentityJournal reboot(memory, 0, 10);
    Boot(reboot, memory, now);
    Check(reboot.BootState().emergency == bcp::StoredEmergency::kLatched,
          "superseding stop survives modeled reboot");
  }
}

// Exercises actual asynchronous readiness, bounded failure and wrap-safe
// timeout.
void TestBackendFailures() {
  Memory async;
  Provision(async);
  async.asynchronous = true;
  bcp::IdentityJournal delayed(async, 0, 10);
  uint32_t now = 0;
  Boot(delayed, async, now);
  Check(delayed.BootState().identity_verified,
        "asynchronous completion is read back");
  Memory stuck;
  Provision(stuck);
  stuck.asynchronous = true;
  bcp::IdentityJournal timeout(stuck, 0, 10);
  now = 0;
  Check(timeout.BeginBoot(), "timeout boot begins");
  while (stuck.writes == 0) {
    Step(timeout, stuck, now);
  }
  // Restart the transaction's time near wrap using a separate instance.
  Memory wrap;
  Provision(wrap);
  wrap.asynchronous = true;
  bcp::IdentityJournal wrapping(wrap, 0, 10);
  now = 0xFFFFFF3BUL;
  Check(wrapping.BeginBoot(), "wrap boot begins");
  while (wrap.writes == 0) {
    Step(wrapping, wrap, now);
  }
  uint32_t started = now - 1;
  now = started + 9;
  Step(wrapping, wrap, now);
  Check(wrapping.State() == bcp::JournalState::kBooting,
        "busy write before deadline");
  now = started + 10;
  Step(wrapping, wrap, now);
  Check(wrapping.State() == bcp::JournalState::kFailed &&
            !wrapping.BootState().identity_verified,
        "stuck backend fails at exact wrap-safe timeout");
  for (uint8_t failure = 0; failure < 4; ++failure) {
    Memory memory;
    Provision(memory);
    memory.fail_read = failure == 0;
    memory.fail_write = failure == 1;
    memory.ignore_write = failure == 2;
    memory.failed = failure == 3;
    bcp::IdentityJournal journal(memory, 0, 10);
    now = 0;
    Boot(journal, memory, now);
    Check(journal.State() == bcp::JournalState::kFailed &&
              !journal.BootState().identity_verified,
          "read/write/readback/hardware failures never issue boot evidence");
    size_t writes = memory.writes;
    for (uint8_t i = 0; i < 10; ++i) {
      Step(journal, memory, now);
    }
    Check(memory.writes == writes, "failed journal never continues writes");
  }
  Memory max;
  Provision(max, true, 0xFFFFFFFEUL);
  bcp::IdentityJournal exhausted(max, 0, 10);
  now = 0;
  Boot(exhausted, max, now);
  Check(!exhausted.BootState().identity_verified && max.writes == 0,
        "boot counter never wraps");
  Memory emergency;
  Provision(emergency, true, 0, 0, 0xFFFFFFFEUL);
  bcp::IdentityJournal full(emergency, 0, 10);
  Boot(full, emergency, now);
  Check(!full.RequestEmergency({1, true, true}),
        "emergency journal counter never wraps");
  bcp::StorageCompletion completion;
  Check(full.TakeCompletion(completion) && completion.token == 1 &&
            !completion.verified,
        "counter exhaustion fails the requesting manager token");
  Memory bounds;
  bcp::IdentityJournal outside(bounds, 0xFFFFFFF0UL, 10);
  Check(!outside.BeginBoot() && bounds.reads == 0 && bounds.writes == 0,
        "overflowing window is refused before I/O");
  bcp::IdentityJournal zero_timeout(bounds, 0, 0);
  Check(!zero_timeout.BeginBoot(), "no unsafe default or zero timeout");
}

// Recomputes fixture checksums after intentional semantic violations.
void FixtureChecksum(uint8_t* record) {
  uint16_t crc = bcp::ComputeCrc(record + 1, 29);
  record[30] = static_cast<uint8_t>(crc);
  record[31] = static_cast<uint8_t>(crc >> 8);
}

// CRC validity alone cannot authorize an unknown format or unprovisioned
// identity.
void TestSemanticAndFinalReadbackFailures() {
  for (uint8_t violation = 0; violation < 5; ++violation) {
    Memory memory;
    Provision(memory);
    uint8_t* record = memory.bytes;
    if (violation == 0) {
      record[1] = 2;
    }
    if (violation == 1) {
      record[24] = 1;
    }
    if (violation == 2) {
      memset(record + 8, 0, 16);
    }
    if (violation == 3) {
      memset(record + 8, 0xFF, 16);
    }
    if (violation == 4) {
      record[4] = 1;
    }
    FixtureChecksum(record);
    memcpy(record + 32, record, 32);
    bcp::IdentityJournal journal(memory, 0, 10);
    uint32_t now = 0;
    Boot(journal, memory, now);
    Check(!journal.BootState().identity_verified && memory.writes == 0,
          "matching CRC-valid but semantically invalid identity is refused");
  }
  Memory final_readback;
  Provision(final_readback);
  bcp::IdentityJournal boot(final_readback, 0, 10);
  uint32_t now = 0;
  Check(boot.BeginBoot(), "full readback verification starts");
  while (final_readback.writes < 66) {
    Step(boot, final_readback, now);
  }
  final_readback.bytes[68] ^= 1;
  Drive(boot, final_readback, now);
  Check(!boot.BootState().identity_verified &&
            boot.State() == bcp::JournalState::kFailed,
        "final paired readback detects corruption of an earlier verified byte");
  Memory failed_write;
  Provision(failed_write);
  bcp::IdentityJournal emergency(failed_write, 0, 10);
  Boot(emergency, failed_write, now);
  failed_write.fail_write = true;
  Check(emergency.RequestEmergency({1, true, true}),
        "emergency fault starts only after verified boot");
  Drive(emergency, failed_write, now);
  bcp::StorageCompletion completion;
  Check(emergency.TakeCompletion(completion) && !completion.verified &&
            completion.token == 1 && emergency.BootState().identity_verified &&
            !emergency.BootState().emergency_storage_healthy,
        "emergency write failure preserves separate boot evidence but never "
        "acknowledges success");
}

// Simulation-only controller profile used to exercise the existing manager
// boundary.
bcp::ControllerProfile Profile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 15, 255, 50, 500, 50, 500, 1000, 50};
  profile.intervention_depth_mm = 4500;
  profile.release_depth_mm = 4000;
  profile.depth_stale_ms = 2000;
  profile.pc_configured = true;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = bcp::FaultSeverity::kBlocking;
  }
  return profile;
}

// Applies a completion through the manager's existing verified-token interface.
void Acknowledge(bcp::IdentityJournal& journal, bcp::SessionManager& manager,
                 uint32_t now) {
  bcp::StorageCompletion completion;
  Check(journal.TakeCompletion(completion),
        "manager acknowledgement available");
  manager.CompleteEmergencyWrite(completion.token, completion.verified, now);
}

// Tests verified boot evidence and local reset without any storage in command
// handlers.
void TestManagerBoundary() {
  Memory memory;
  Provision(memory, false);
  bcp::IdentityJournal journal(memory, 0, 10);
  uint32_t now = 0;
  Boot(journal, memory, now);
  bcp::SessionManager manager(Profile(), journal.BootState());
  manager.SetEmergencyInput(true, now);
  Check(manager.Outputs(now).forward == 0 &&
            manager.Status(now).safety_state ==
                bcp::SafetyState::kEmergencyStopLatched,
        "emergency applies before storage service");
  Check(journal.RequestEmergency(manager.PendingEmergencyWrite()),
        "manager token submitted once");
  Drive(journal, memory, now);
  Acknowledge(journal, manager, now);
  manager.SetEmergencyInput(false, now);
  manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                              bcp::ActionStatus::kCompleted,
                              bcp::EstimatedBuoyancy::kUnknown, now);
  Check(manager.RequestLocalReset(now),
        "local procedure conditions authorize durable clear");
  Check(journal.RequestEmergency(manager.PendingEmergencyWrite()),
        "local clear reaches journal");
  for (uint8_t i = 0; i < 10; ++i) {
    Step(journal, memory, now);
  }
  manager.SetEmergencyInput(true, now);
  Check(journal.RequestEmergency(manager.PendingEmergencyWrite()),
        "new manager stop supersedes clear token");
  Drive(journal, memory, now);
  Acknowledge(journal, manager, now);
  Check(manager.Status(now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "superseded clear never resets manager");
  manager.SetEmergencyInput(false, now);
  manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                              bcp::ActionStatus::kCompleted,
                              bcp::EstimatedBuoyancy::kUnknown, now);
  Check(manager.RequestLocalReset(now), "new deliberate reset required");
  Check(journal.RequestEmergency(manager.PendingEmergencyWrite()),
        "new clear accepted after previous completion");
  Drive(journal, memory, now);
  Check(manager.Status(now).safety_state ==
            bcp::SafetyState::kEmergencyStopLatched,
        "local reset stays latched through storage verification");
  Acknowledge(journal, manager, now);
  Check(manager.Status(now).safety_state == bcp::SafetyState::kDisarmed,
        "verified local clear returns D only");
  bcp::IdentityJournal reboot(memory, 0, 10);
  Boot(reboot, memory, now);
  bcp::SessionManager restarted(Profile(), reboot.BootState());
  Check(restarted.Status(now).safety_state == bcp::SafetyState::kDisarmed &&
            !restarted.HasSession(),
        "verified cleared reboot never resumes control or session");
}
// Verifies coordinator token ownership, nonblocking service and permanent
// faults.
void TestCoordinator() {
  Memory memory;
  Provision(memory, false);
  bcp::IdentityJournal journal(memory, 0, 10);
  uint32_t now = 0;
  Boot(journal, memory, now);
  bcp::SessionManager manager(Profile(), journal.BootState());
  bcp::PersistenceCoordinator coordinator(journal, manager);
  manager.SetEmergencyInput(true, now);
  uint32_t token = manager.PendingEmergencyWrite().token;
  for (size_t i = 0; i < 300; ++i) {
    size_t before = memory.reads + memory.writes;
    coordinator.Step(now++);
    Check(memory.reads + memory.writes - before <= 1,
          "coordinator preserves byte budget");
  }
  Check(!manager.PendingEmergencyWrite().pending &&
            manager.PendingEmergencyWrite().token == token &&
            journal.BootState().emergency == bcp::StoredEmergency::kLatched,
        "coordinator submits and acknowledges a token once");
  size_t writes = memory.writes;
  for (uint8_t i = 0; i < 10; ++i) {
    coordinator.Step(now++);
  }
  Check(memory.writes == writes, "idle coordinator never rewrites the latch");
  manager.SetEmergencyInput(false, now);
  manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                              bcp::ActionStatus::kCompleted,
                              bcp::EstimatedBuoyancy::kUnknown, now);
  Check(manager.RequestLocalReset(now), "coordinator clear precondition");
  for (uint8_t i = 0; i < 10; ++i) {
    coordinator.Step(now++);
  }
  manager.SetEmergencyInput(true, now);
  uint32_t newest = manager.PendingEmergencyWrite().token;
  for (size_t i = 0; i < 300; ++i) {
    coordinator.Step(now++);
  }
  Check(!manager.PendingEmergencyWrite().pending &&
            manager.PendingEmergencyWrite().token == newest &&
            manager.Status(now).safety_state ==
                bcp::SafetyState::kEmergencyStopLatched &&
            journal.BootState().emergency == bcp::StoredEmergency::kLatched,
        "coordinator supersedes clear before continuing storage work");
  manager.SetEmergencyInput(false, now);
  manager.SetBuoyancyFeedback(bcp::BuoyancyAction::kEmergencyEvacuation,
                              bcp::ActionStatus::kCompleted,
                              bcp::EstimatedBuoyancy::kUnknown, now);
  Check(manager.RequestLocalReset(now), "second local reset eligible");
  memory.fail_write = true;
  coordinator.Step(now++);
  Check(!manager.PendingEmergencyWrite().pending &&
            manager.Status(now).safety_state ==
                bcp::SafetyState::kEmergencyStopLatched &&
            (manager.Status(now).arming_blockers & 64U) != 0,
        "coordinator storage failure never acknowledges reset success");
}

}  // namespace

// Runs byte-boundary power-loss, corruption and stateful acknowledgement
// scenarios.
int main() {
  TestBootIdentity();
  TestBootPowerCuts();
  TestCorruption();
  TestEmergencyPowerCuts();
  TestSupersededClear();
  TestBackendFailures();
  TestManagerBoundary();
  TestSemanticAndFinalReadbackFailures();
  TestCoordinator();
  printf(
      "PASS: storage prototype; 67 boot cuts, 134 emergency cuts, 1536 "
      "single-bit corruptions, 67 clear supersessions\n");
  printf(
      "Host IdentityJournal storage: %zu bytes; RAM backend, physical EEPROM not "
      "exercised\n",
      sizeof(bcp::IdentityJournal));
  return 0;
}
