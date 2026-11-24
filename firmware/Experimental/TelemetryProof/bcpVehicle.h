#ifndef BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_BCP_VEHICLE_H_
#define BAJOLS_FIRMWARE_EXPERIMENTAL_TELEMETRY_PROOF_BCP_VEHICLE_H_

namespace bajols {
namespace vehicle {
// Starts actuator-disabled BCP integration without provisioning or blocking.
void Begin();
// Services bounded transport and persistence, publishing explicit instrument
// health.
void Step();
}  // namespace vehicle
}  // namespace bajols
#endif
