#include "bcpVehicle.h"

#if !defined(BAJOLS_ENABLE_LEGACY_CONTROL)
#include <BajolsArduinoSerial.h>
#include <BajolsBoardStorage.h>
#include <BajolsMonitor.h>

#if defined(BAJOLS_BCP_ENABLE_EEPROM_WRITES)
#if !defined(BAJOLS_BCP_EEPROM_BASE) || !defined(BAJOLS_BCP_WRITE_TIMEOUT_MS)
#error "Writable BCP builds require an explicit EEPROM base and byte timeout."
#endif
#else
#ifndef BAJOLS_BCP_EEPROM_BASE
#define BAJOLS_BCP_EEPROM_BASE 0
#endif
#ifndef BAJOLS_BCP_WRITE_TIMEOUT_MS
#define BAJOLS_BCP_WRITE_TIMEOUT_MS 10
#endif
#endif

static_assert(
    BAJOLS_BCP_EEPROM_BASE >= 0 &&
        BAJOLS_BCP_EEPROM_BASE <= bajols::bcp::kBoardStorageCapacity - 192,
    "The journal window must fit board storage; ESP32 uses logical base zero.");
static_assert(BAJOLS_BCP_WRITE_TIMEOUT_MS > 0 &&
                  BAJOLS_BCP_WRITE_TIMEOUT_MS < 32768,
              "The EEPROM byte timeout must fit wrap-safe bounds.");

namespace bajols {
namespace vehicle {
namespace {
namespace bcp = bajols::bcp;

#if defined(BAJOLS_BCP_ENABLE_EEPROM_WRITES)
// Deliberate build opt-in; no automatic provisioning is performed.
constexpr bool kWritesEnabled = true;
#else
// Default firmware never starts an EEPROM write, including boot-counter
// updates.
constexpr bool kWritesEnabled = false;
#endif
// Depth, supply, heading and pitch report paths; health may be unavailable.
constexpr uint32_t kTelemetryMask = 0x35;
// BCP SensorFault bit; never accompanied by Valid for unavailable instruments.
constexpr uint8_t kUnavailableQuality = 8;
// BCP Valid and Simulated bits, used only by the explicit demo build.
constexpr uint8_t kDemoQuality = 3;
// Exclusive, explicitly bounded journal ownership.
bcp::BoardStorage storage(BAJOLS_BCP_EEPROM_BASE,
                          bcp::IdentityJournal::kWindowSize, kWritesEnabled);
// Identity verification and durable boot/latch state precede session creation.
bcp::IdentityJournal journal(storage, BAJOLS_BCP_EEPROM_BASE,
                             BAJOLS_BCP_WRITE_TIMEOUT_MS);
// No movement/ballast capabilities, owner selection or actuator output
// interface.
bcp::MonitoringRuntime runtime(journal, kTelemetryMask);
// Serial is exclusively binary BCP; debug printing is not enabled on this path.
bcp::BasicArduinoSerialPort<decltype(Serial)> port(Serial);
// Wrapping monotonic onboard time.
bcp::ArduinoMillisecondClock clock_source;
// Last new instrument acquisition, independent of serial transmission times.
uint32_t last_sample_at = 0;

// Explicit driver stand-in. No uncalibrated ADC/pressure value becomes valid
// depth or voltage. The optional demo build labels all generated values
// simulated.
bcp::Telemetry Acquire(uint32_t now) {
  bcp::Telemetry telemetry = {};
  telemetry.count = 4;
  telemetry.records[0] = {bcp::MeasurementId::kDepth, kUnavailableQuality,
                          65535, 0};
  telemetry.records[1] = {bcp::MeasurementId::kSupplyVoltage,
                          kUnavailableQuality, 65535, 0};
  telemetry.records[2] = {bcp::MeasurementId::kHeading, kUnavailableQuality,
                          65535, 0};
  telemetry.records[3] = {bcp::MeasurementId::kPitch, kUnavailableQuality,
                          65535, 0};
#if defined(BAJOLS_BCP_DEMO_TELEMETRY)
  telemetry.records[0] = {bcp::MeasurementId::kDepth, kDemoQuality, 0, 2000};
  telemetry.records[1] = {bcp::MeasurementId::kSupplyVoltage, kDemoQuality, 0,
                          12000};
  telemetry.records[2] = {bcp::MeasurementId::kHeading, kDemoQuality, 0,
                          static_cast<int32_t>((now / 100) % 36000)};
  telemetry.records[3] = {bcp::MeasurementId::kPitch, kDemoQuality, 0, 0};
#else
  (void)now;
#endif
  return telemetry;
}
}  // namespace

void Begin() {
  Serial.begin(115200);
  if (bcp::InitializeBoardStorage(storage)) {
    runtime.Begin();
  }
}

void Step() {
  uint32_t now = clock_source.Now();
  if (static_cast<uint32_t>(now - last_sample_at) >= 200) {
    last_sample_at = now;
    runtime.SetTelemetry(Acquire(now), now);
  }
  runtime.Step(port, clock_source);
}
}  // namespace vehicle
}  // namespace bajols
#endif  // !BAJOLS_ENABLE_LEGACY_CONTROL
