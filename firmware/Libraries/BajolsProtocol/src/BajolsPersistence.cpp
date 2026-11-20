#include "BajolsPersistence.h"

namespace bajols {
namespace bcp {

PersistenceCoordinator::PersistenceCoordinator(IdentityJournal& journal,
                                               SessionManager& manager)
    : journal_(journal),
      manager_(manager),
      submitted_(0),
      failure_reported_(false) {}

void PersistenceCoordinator::ReportFailure(uint32_t now) {
  if (!failure_reported_) {
    manager_.SetFault(FaultCode::kEmergencyLatchStorageFailed, true, now);
    failure_reported_ = true;
  }
  EmergencyWrite request = manager_.PendingEmergencyWrite();
  if (request.pending && request.token > submitted_) {
    submitted_ = request.token;
    manager_.CompleteEmergencyWrite(request.token, false, now);
  }
}

void PersistenceCoordinator::Step(uint32_t now) {
  manager_.Advance(now);
  if (failure_reported_) {
    ReportFailure(now);
    return;
  }
  if (journal_.State() != JournalState::kReady &&
      journal_.State() != JournalState::kEmergencyWriting) {
    ReportFailure(now);
    return;
  }
  EmergencyWrite request = manager_.PendingEmergencyWrite();
  if (request.pending && request.token > submitted_) {
    if (!journal_.RequestEmergency(request)) {
      ReportFailure(now);
      return;
    }
    submitted_ = request.token;
  }
  journal_.Step(now);
  StorageCompletion completion;
  if (journal_.TakeCompletion(completion)) {
    manager_.CompleteEmergencyWrite(completion.token, completion.verified, now);
  }
  if (journal_.State() == JournalState::kFailed) {
    ReportFailure(now);
  }
  manager_.Advance(now);
}
}  // namespace bcp
}  // namespace bajols
