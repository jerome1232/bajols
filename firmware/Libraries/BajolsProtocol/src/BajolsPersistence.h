#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_PERSISTENCE_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_PERSISTENCE_H_

#include "BajolsStorage.h"

namespace bajols {
namespace bcp {

// Runtime coordinator, constructed only after the boot journal reaches Ready.
// Board startup must construct SessionManager using that journal's BootState.
// Apply effective actuator safety intent BEFORE calling Step. No output driver,
// boot provisioning or dynamic allocation belongs to this class.
class PersistenceCoordinator {
 public:
  // References a verified journal and its manager; both must outlive this
  // object.
  PersistenceCoordinator(IdentityJournal& journal, SessionManager& manager);
  // Prevents duplicate submissions and acknowledgements through cloned state.
  PersistenceCoordinator(const PersistenceCoordinator&) = delete;
  // Prevents rebinding live persistence ownership to another controller.
  PersistenceCoordinator& operator=(const PersistenceCoordinator&) = delete;
  // Advances safety, submits/supersedes the latest pending token, services at
  // most one byte operation, and delivers verified/failure completions once.
  void Step(uint32_t now);

 private:
  // Reports an unusable journal through the existing manager storage-fault
  // path.
  void ReportFailure(uint32_t now);
  // Exclusive journal, initialized and advanced before manager construction.
  IdentityJournal& journal_;
  // Independent safety owner; responses never clear anything without its
  // checks.
  SessionManager& manager_;
  // Highest submitted or failed manager token, never retried as new work.
  uint32_t submitted_;
  // Whether the coordinator already reported a persistent service fault.
  bool failure_reported_;
};
}  // namespace bcp
}  // namespace bajols
#endif
