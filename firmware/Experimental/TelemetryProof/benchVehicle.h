#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_BENCH_VEHICLE_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_BENCH_VEHICLE_H_

#if defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)

#include <BajolsMonitor.h>

#include "actuatorHardware.h"

namespace bajols {
namespace vehicle {

// Dry-bench Mega IO. The BCP runtime owns session authorization and latching.
class BenchHardware {
 public:
  // Initializes the switches and centers the shield outputs with thrust off.
  void Begin();
  // Samples physical inputs before serial processing and removes thrust
  // immediately on physical stop or loss of ownership.
  void ReadInputs(bcp::MonitoringRuntime& runtime, uint32_t now);
  // Applies only the authorized output intent from the current runtime step.
  void Apply(const bcp::ControlOutputs& outputs);

 private:
  bool ReadReset(bool permitted, uint32_t now);

  ActuatorHardware actuators_;
  bool stop_during_boot_ = false;
  bool reset_level_ = true;
  bool reset_ready_ = false;
  uint32_t reset_changed_at_ = 0;
  uint32_t next_shield_check_at_ = 0;
};

}  // namespace vehicle
}  // namespace bajols

#endif  // BAJOLS_BCP_ENABLE_BENCH_CONTROL
#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_BENCH_VEHICLE_H_
