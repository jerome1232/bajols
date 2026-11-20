#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MONITOR_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MONITOR_H_

#include "BajolsEndpoint.h"
#include "BajolsPersistence.h"

namespace bajols {
namespace bcp {

// Actuator-free startup/runtime composition, not an operational vehicle
// controller. No movement owner, sensors, modes, axes or ballast systems are
// advertised.
class MonitoringRuntime {
 public:
  // References the exclusive journal; constructs no manager before verified
  // boot.
  explicit MonitoringRuntime(IdentityJournal& journal);
  // Destroys the explicitly constructed fixed-storage context if startup
  // finished.
  ~MonitoringRuntime();
  // Live boot identity, streams and durable tokens cannot be cloned.
  MonitoringRuntime(const MonitoringRuntime&) = delete;
  // Prevents accidental replacement of a live authorization domain.
  MonitoringRuntime& operator=(const MonitoringRuntime&) = delete;
  // Starts the asynchronous boot scan exactly once; never provisions blank
  // media.
  bool Begin();
  // Advances one startup byte or bounded serial/persistence work after startup.
  // No actuator output API exists in this monitoring-only composition.
  void Step(BytePort& port, MillisecondClock& clock);
  // Cancels a known PC transport connection without clearing any safety latch.
  void Disconnect(uint32_t now);
  // Returns authoritative status only after a verified boot constructed the
  // manager.
  bool GetStatus(uint32_t now, VehicleStatus& status);
  // Reports whether startup failed before any PC session could be established.
  bool Failed() const;

 private:
  // Builds an immutable profile with no implemented actuation/sensor
  // capabilities.
  static ControllerProfile Profile();
  // Stable references are initialized together only after journal verification.
  struct Context {
    // Independent authoritative safety/session state using the verified boot.
    SessionManager manager;
    // Fixed serial framing/queue storage referencing manager.
    SerialEndpoint endpoint;
    // Runtime journal submission/acknowledgement owner referencing manager.
    PersistenceCoordinator persistence;
    // Constructs all runtime references after successful boot evidence exists.
    Context(IdentityJournal& journal, const VerifiedBootState& boot);
  };
  // Union alignment is determined by Context; placement construction uses no
  // heap.
  union ContextStorage {
    // Active before verified startup; never interpreted as manager state.
    uint8_t inactive;
    // Fixed aligned storage activated only once after boot verification.
    Context active;
    // Starts without constructing an unverified manager.
    ContextStorage();
    // Context destruction is handled explicitly by MonitoringRuntime.
    ~ContextStorage();
  };

  // Startup/persistence owner, outliving this runtime.
  IdentityJournal& journal_;
  // Fixed backing storage for the deferred runtime context.
  ContextStorage storage_;
  // Whether Begin was called, preventing repeated boot-counter consumption.
  bool begun_;
  // Whether the union contains a constructed Context.
  bool ready_;
  // Latched startup failure; no automatic service recovery or retry.
  bool failed_;
};
}  // namespace bcp
}  // namespace bajols
#endif
