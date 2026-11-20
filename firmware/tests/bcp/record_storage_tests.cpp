#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "BajolsRecordStorage.h"
#include "provisioned_image.h"

namespace {
namespace bcp = bajols::bcp;
// Reports the violated journal/flash transaction invariant.
void Check(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "FAIL record storage: %s\n", message);
    exit(1);
  }
}
// Host model of atomic record commit; pending data is lost on simulated power
// cut.
class Backend : public bcp::RecordBackend {
 public:
  // Last durable record set, independent of the facade's staged image.
  uint8_t durable[192];
  // One copied asynchronous request.
  uint8_t request[32];
  // Request slot in the fixed record set.
  uint8_t slot = 0;
  // Number of accepted physical record transactions.
  size_t commits = 0;
  // Current simulated worker lifecycle.
  bcp::ByteStorageStatus state = bcp::ByteStorageStatus::kReady;
  // Injects an incorrect durable readback after a nominal commit.
  bool corrupt = false;
  // Starts with the actual provisioning tool's six records.
  Backend() { memcpy(durable, kProvisionedImage, sizeof(durable)); }
  // Observes worker state without completing it.
  bcp::ByteStorageStatus Status() override { return state; }
  // Reads only durable data, not a pending request.
  bool ReadRecord(uint8_t index, uint8_t* bytes) override {
    if (state != bcp::ByteStorageStatus::kReady || index >= 6) {
      return false;
    }
    memcpy(bytes, durable + index * 32, 32);
    return true;
  }
  // Accepts exactly one asynchronous record and retains independent request
  // bytes.
  bool StartRecordWrite(uint8_t index, const uint8_t* bytes) override {
    if (state != bcp::ByteStorageStatus::kReady || index >= 6) {
      return false;
    }
    slot = index;
    memcpy(request, bytes, sizeof(request));
    state = bcp::ByteStorageStatus::kBusy;
    ++commits;
    return true;
  }
  // Models successful commit/readback or an injected incorrect result.
  void Complete() {
    if (state == bcp::ByteStorageStatus::kBusy) {
      memcpy(durable + slot * 32, request, 32);
      if (corrupt) {
        durable[slot * 32 + 2] ^= 1;
      }
      state = bcp::ByteStorageStatus::kReady;
    }
  }
};
// Advances the byte journal while physical completion remains a separate
// action.
void Step(bcp::IdentityJournal& journal, Backend& backend, uint32_t now) {
  journal.Step(now);
  backend.Complete();
}
// Completes a fresh boot without any synthetic identity evidence.
void Boot(bcp::IdentityJournal& journal, Backend& backend) {
  Check(journal.BeginBoot(), "begin boot");
  for (uint32_t now = 0;
       now < 1000 && journal.State() == bcp::JournalState::kBooting; ++now) {
    Step(journal, backend, now);
  }
  Check(journal.State() == bcp::JournalState::kReady,
        "verified record-backed boot");
}
// Payload staging never commits an unverified valid marker; read-only is
// strict.
void TestTransactions() {
  Backend backend;
  bcp::RecordJournalStorage storage(backend, true);
  Check(storage.Load() && !storage.Load(), "load once");
  Check(!storage.StartWrite(65, 1), "payload requires invalid marker");
  Check(storage.StartWrite(64, 0), "queue invalidation");
  Check(storage.Status() == bcp::ByteStorageStatus::kBusy,
        "busy until worker completion");
  uint8_t value;
  Check(!storage.Read(64, value), "no reads during commit");
  backend.Complete();
  Check(storage.Status() == bcp::ByteStorageStatus::kReady,
        "verified invalidation");
  Check(storage.StartWrite(65, 7), "stage payload");
  Check(backend.commits == 1 && backend.durable[65] == 1,
        "staging is not physical persistence");
  Check(storage.StartWrite(64, 0xA5), "queue final marker");
  backend.Complete();
  Check(storage.Status() == bcp::ByteStorageStatus::kReady &&
            backend.durable[65] == 7,
        "final marker persists full staged record");
  Check(!storage.StartWrite(192, 0) && !storage.StartWrite(0, 3),
        "bounds and marker encoding");
  Backend read_backend;
  bcp::RecordJournalStorage readonly(read_backend, false);
  Check(readonly.Load() && !readonly.StartWrite(0, 0xA5) &&
            read_backend.commits == 0,
        "read-only never writes unchanged data");
  Backend bad;
  bcp::RecordJournalStorage corrupt(bad, true);
  Check(corrupt.Load() && corrupt.StartWrite(64, 0), "start corrupt test");
  bad.corrupt = true;
  bad.Complete();
  Check(corrupt.Status() == bcp::ByteStorageStatus::kFailed,
        "mismatching readback latches failure");
}
// At every byte-state boundary, reboot sees only durable records, never RAM
// staging.
void TestPowerCuts() {
  for (uint32_t cut = 0; cut < 450; ++cut) {
    Backend backend;
    bcp::RecordJournalStorage storage(backend, true);
    Check(storage.Load(), "power-cut load");
    bcp::IdentityJournal journal(storage, 0, 100);
    Check(journal.BeginBoot(), "power-cut begin");
    for (uint32_t now = 0; now < cut; ++now) {
      Step(journal, backend, now);
    }
    bool was_verified = journal.BootState().identity_verified;
    uint32_t old_counter = journal.BootState().boot_counter;
    Backend reboot;
    memcpy(reboot.durable, backend.durable, sizeof(reboot.durable));
    bcp::RecordJournalStorage restored(reboot, true);
    Check(restored.Load(), "load durable reboot records");
    bcp::IdentityJournal next(restored, 0, 100);
    Check(next.BeginBoot(), "reboot begin");
    for (uint32_t now = 0;
         now < 1000 && next.State() == bcp::JournalState::kBooting; ++now) {
      Step(next, reboot, now);
    }
    if (next.State() == bcp::JournalState::kReady) {
      Check(next.BootState().identity_verified &&
                (!was_verified || next.BootState().boot_counter > old_counter),
            "verified boot identity never reused after power cut");
    } else {
      Check(!next.BootState().identity_verified,
            "partial durable pair cannot authorize");
    }
  }
}
// The paired journal semantics and conservative emergency recovery are
// unchanged.
void TestEmergencyCuts() {
  for (uint32_t cut = 0; cut < 220; ++cut) {
    Backend backend;
    bcp::RecordJournalStorage storage(backend, true);
    Check(storage.Load(), "emergency load");
    bcp::IdentityJournal journal(storage, 0, 100);
    Boot(journal, backend);
    Check(backend.commits == 4, "paired boot uses four record commits");
    bcp::EmergencyWrite clear = {1, false, true};
    Check(journal.RequestEmergency(clear), "local clear request");
    for (uint32_t now = 0; now < cut; ++now) {
      Step(journal, backend, now);
    }
    bool durable_clear =
        backend.durable[128] == 0xA5 && backend.durable[160] == 0xA5 &&
        backend.durable[131] == 0 && backend.durable[163] == 0 &&
        memcmp(backend.durable + 128, backend.durable + 160, 32) == 0;
    Backend reboot;
    memcpy(reboot.durable, backend.durable, sizeof(reboot.durable));
    bcp::RecordJournalStorage restored(reboot, true);
    Check(restored.Load(), "emergency reboot load");
    bcp::IdentityJournal next(restored, 0, 100);
    Boot(next, reboot);
    Check(durable_clear ||
              next.BootState().emergency != bcp::StoredEmergency::kCleared,
          "RAM-staged or mixed clear never restores cleared emergency");
  }
}
}  // namespace
// Runs portable flash-backed transaction tests without NVS/Arduino
// dependencies.
int main() {
  TestTransactions();
  TestPowerCuts();
  TestEmergencyCuts();
  printf(
      "PASS: record-backed journal; 450 boot cuts, 220 clear cuts, four "
      "commits per paired update\n");
  return 0;
}
