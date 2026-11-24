#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_ACTUATOR_HARDWARE_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_ACTUATOR_HARDWARE_H_

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL) || defined(BAJOLS_BCP_ENABLE_BENCH_CONTROL)

#include "data_utils.h"
#include "motor.h"

namespace bajols {
namespace vehicle {

class ActuatorHardware {
 public:
  ActuatorHardware();
  void Begin();
  bool Failed() const { return failed_; }
  void CheckShield();
  void DisableMotor();
  // Shield levels are 0–4096, with 4096 selecting full on.
  void SetMotor(legacy::Direction direction, uint16_t level);
  void SetSurfaces(uint16_t rudder_us, uint16_t dive_plane_us);

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
  void SetThrottle(legacy::Direction direction, uint8_t throttle);
  void SetPump(bool fill);
  int32_t ReadRpm();
#endif

 private:
  void Fail();

#if defined(BAJOLS_ENABLE_LEGACY_CONTROL)
  legacy::HBridgePwmEncoder engine_;
#else
  legacy::HBridgePwm engine_;
#endif
  legacy::HBridge pump_;
  bool failed_ = false;
  uint16_t rudder_us_ = legacy::kServoCenterUs;
  uint16_t dive_plane_us_ = legacy::kServoCenterUs;
};

}  // namespace vehicle
}  // namespace bajols

#endif  // Actuator-enabled build
#endif  // BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_ACTUATOR_HARDWARE_H_
