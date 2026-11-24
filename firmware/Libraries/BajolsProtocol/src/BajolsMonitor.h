#ifndef BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MONITOR_H_
#define BAJOLS_FIRMWARE_LIBRARIES_BAJOLS_PROTOCOL_SRC_BAJOLS_MONITOR_H_

#include "BajolsEndpoint.h"
#include "BajolsPersistence.h"

namespace bajols {
namespace bcp {

// Verified startup, bounded serial, telemetry and persistence. The default
// profile is monitor-only. The explicit bench profile also exposes movement
// intent to a trusted dry-bench hardware adapter.
class MonitoringRuntime {
 public:
  // References the exclusive journal; constructs no manager before verified
  // boot.
  explicit MonitoringRuntime(IdentityJournal& journal,
                             uint32_t telemetry_mask = 0,
                             bool bench_control = false);
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
  void Step(BytePort& port, MillisecondClock& clock);
  // Applies physical inputs before servicing serial. Returns false until a
  // verified bench session manager exists.
  bool SetBenchInputs(bool pc_selected, bool stop_asserted, bool reset_pressed,
                      uint32_t now);
  // Reads authorized, fresh movement or a zero-initialized safe output.
  ControlOutputs BenchOutputs(uint32_t now);
  // Replaces the latest acquired instrument snapshot; sample age is relative to
  // now, not the next transmission. Invalid records never replace retained
  // data.
  bool SetTelemetry(const Telemetry& telemetry, uint32_t now);
  // Cancels a known PC transport connection without clearing any safety latch.
  void Disconnect(uint32_t now);
  // Returns authoritative status only after a verified boot constructed the
  // manager.
  bool GetStatus(uint32_t now, VehicleStatus& status);
  // Reports whether startup failed before any PC session could be established.
  bool Failed() const;

 private:
  static ControllerProfile Profile(uint32_t telemetry_mask, bool bench_control);
  // Stable references are initialized together only after journal verification.
  struct Context {
    // Independent authoritative safety/session state using the verified boot.
    SessionManager manager;
    // Fixed serial framing/queue storage referencing manager.
    SerialEndpoint endpoint;
    // Runtime journal submission/acknowledgement owner referencing manager.
    PersistenceCoordinator persistence;
    // Constructs all runtime references after successful boot evidence exists.
    Context(IdentityJournal& journal, const VerifiedBootState& boot,
            uint32_t telemetry_mask, bool bench_control);
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
  // Immutable implemented instrument-reporting mask; unknown bits fail startup.
  uint32_t telemetry_mask_;
  const bool bench_control_;
  // Latest instrument snapshot, never refreshed merely by a transmission.
  Telemetry telemetry_;
  // Whether a validated snapshot has been submitted by the instrument adapter.
  bool has_telemetry_;
  // Acquisition timestamp associated with telemetry_.
  uint32_t telemetry_at_;
  // Last publication attempt, bounded to one per 200 milliseconds.
  uint32_t telemetry_sent_at_;
};
}  // namespace bcp
}  // namespace bajols
#endif
