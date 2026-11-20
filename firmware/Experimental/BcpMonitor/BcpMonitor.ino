#include <BajolsArduinoSerial.h>
#include <BajolsBoardStorage.h>
#include <BajolsMonitor.h>

#if defined(BAJOLS_MONITOR_ENABLE_EEPROM_WRITES)
#if !defined(BAJOLS_MONITOR_EEPROM_BASE) || \
    !defined(BAJOLS_MONITOR_WRITE_TIMEOUT_MS)
#error \
    "Write-enabled monitoring requires an explicit EEPROM base and validated byte timeout."
#endif
#else
// Read-only default build; these are compile fixtures, not writable board
// policy.
#if !defined(BAJOLS_MONITOR_EEPROM_BASE)
#define BAJOLS_MONITOR_EEPROM_BASE 0
#endif
#if !defined(BAJOLS_MONITOR_WRITE_TIMEOUT_MS)
#define BAJOLS_MONITOR_WRITE_TIMEOUT_MS 10
#endif
#endif

static_assert(
    BAJOLS_MONITOR_EEPROM_BASE >= 0 &&
        BAJOLS_MONITOR_EEPROM_BASE <= bajols::bcp::kBoardStorageCapacity - 192,
    "Journal window must fit board storage; ESP32 logical base is zero.");
static_assert(BAJOLS_MONITOR_WRITE_TIMEOUT_MS > 0 &&
                  BAJOLS_MONITOR_WRITE_TIMEOUT_MS < 32768,
              "Write timeout must be validated and fit wrap-safe bounds.");

namespace {
namespace bcp = bajols::bcp;

#if defined(BAJOLS_MONITOR_ENABLE_EEPROM_WRITES)
// Explicit build opt-in. If flashed, boot and emergency journal writes are
// real.
constexpr bool kWritesEnabled = true;
#else
// Default sketch cannot write EEPROM, even on already-provisioned media.
constexpr bool kWritesEnabled = false;
#endif

// Exclusive journal window; provisioning remains a separate manual service
// action.
bcp::BoardStorage storage(BAJOLS_MONITOR_EEPROM_BASE,
                          bcp::IdentityJournal::kWindowSize, kWritesEnabled);
// Advances verified identity only after both records are written/read back.
bcp::IdentityJournal journal(storage, BAJOLS_MONITOR_EEPROM_BASE,
                             BAJOLS_MONITOR_WRITE_TIMEOUT_MS);
// Contains no actuator/sensor initialization and cannot assign a movement
// owner.
bcp::MonitoringRuntime runtime(journal);
// USB UART dedicated exclusively to BCP binary frames.
bcp::BasicArduinoSerialPort<decltype(Serial)> port(Serial);
// Fresh wrapping onboard time throughout startup and transport service.
bcp::ArduinoMillisecondClock clock_source;
}  // namespace

// Starts bounded boot recovery; does not provision or wait for a terminal.
void setup() {
  Serial.begin(115200);
  if (bcp::InitializeBoardStorage(storage)) {
    runtime.Begin();
  }
}

// Monitoring/status/stop only; no PWM, motor, servo, receiver or ballast
// drivers.
void loop() { runtime.Step(port, clock_source); }
