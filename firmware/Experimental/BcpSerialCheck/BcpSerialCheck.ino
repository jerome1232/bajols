#include <BajolsArduinoSerial.h>

namespace {
namespace bcp = bajols::bcp;

// Compile-only sensor limits; no sensors or buoyancy functions are implemented.
bcp::ControllerProfile MakeProfile() {
  bcp::ControllerProfile profile = {};
  profile.capabilities = {1, 1, 7, 0, 0, 50, 500, 50, 500, 1000, 50};
  profile.intervention_depth_mm = 4500;
  profile.release_depth_mm = 4000;
  profile.depth_stale_ms = 2000;
  profile.pc_configured = true;
  for (uint8_t i = 0; i < 7; ++i) {
    profile.fault_severity[i] = bcp::FaultSeverity::kBlocking;
  }
  return profile;
}

// No persistent identity adapter exists yet; never claim a verified boot.
bcp::VerifiedBootState UnverifiedBoot() {
  bcp::VerifiedBootState boot = {};
  boot.emergency = bcp::StoredEmergency::kAmbiguous;
  return boot;
}

// Safety remains independent of incoming bytes and serial transmit progress.
bcp::SessionManager manager(MakeProfile(), UnverifiedBoot());
// Fixed receive/transmit storage and per-iteration work limits.
bcp::SerialEndpoint endpoint(manager);
// Dedicated USB UART; this sketch never writes debug text on it.
bcp::ArduinoSerialPort port(Serial);
// Fresh onboard time at frame and service boundaries.
bcp::ArduinoMillisecondClock clock_source;
// Debugger-visible state; never used to drive physical hardware.
volatile bool thrust_inhibited = true;
}  // namespace

// Initializes the protocol UART only; no receiver, EEPROM or actuator setup.
void setup() {
  Serial.begin(115200, SERIAL_8N1);
  manager.SetOwner(bcp::MovementOwner::kPc, millis());
}

// Services bounded serial work continuously, including during link silence.
void loop() {
  endpoint.Step(port, clock_source);
  bcp::ControlOutputs output = manager.Outputs(millis());
  thrust_inhibited = output.forward == 0 && output.yaw == 0 && output.dive == 0;
}
