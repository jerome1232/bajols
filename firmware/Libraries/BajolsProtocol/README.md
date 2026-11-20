# Portable BCP v1 codec and session policy

This library implements the wire contract in [BCP draft 0.9](../../../protocol/BCP_V1_DRAFT.md),
using the draft 0.9 [shared wire vectors](../../../protocol/test-vectors/wire-v1.json).
The stateless codec handles syntax only. The separate `BajolsSession.h` manager
implements session and authorization policy; neither component drives actuators.
Successful decoding never authorizes a command.
The existing `TelemetryProof` sketch does not use this library yet.

The `bajols::bcp` namespace provides raw `Frame` and typed `Packet` encode/decode
functions, CRC-16/CCITT-FALSE, and a byte-at-a-time `StreamDecoder`. Include
`BajolsProtocol.h`. Select the packet union member using `header.type`; empty
messages do not access the union. Initialize outgoing packets with `{}` and set
the matching member before encoding. Packet values own their storage and may be
copied; there are no borrowed input buffers.

`Encode` validates outgoing values and direction. `Decode` validates syntax and
direction, preserves unknown reporting enums/bits, rejects unknown message IDs,
and skips unknown telemetry measurements after checking all measurement IDs for
duplicates. Call `HasUnknownControlValues` and `HasKnownQualityFlags` before using
reporting values; those helpers do not assess freshness or authorization.
`DecodeFrame` permits unknown message IDs for envelope-level inspection.

All buffers and work are bounded: payload 96 bytes, decoded packet 114 bytes,
delimited wire frame 116 bytes, telemetry at most 11 records. The core has no
heap allocation, Arduino dependencies, exceptions, or RTTI. Stream parsing has no
queue: process each `kPacketReady` result before consuming the next byte. Empty
delimiters are ignored. Oversized input is discarded through its delimiter and
reports one rejection. Call `Reset` when a transport disconnects. Failed decode
leaves output unchanged; failed encode leaves destination unchanged and sets
`written` to zero. Caller-provided pointers must address their stated lengths.
The stateless codec does not provide a clock or command timer.

## Stateful session and authorization

`SessionManager` owns one pending and one established PC connection, separate
16-entry ticket and discrete-result caches, independent movement/buoyancy leases,
source selection, neutral qualification, depth inhibition and safety latches.
The native encoded `Peer` fixture exercises it with a virtual clock; it is separate
from the C# simulated vehicle and from actual firmware actuator integration.

Supply an immutable `ControllerProfile` with validated sensor thresholds,
implemented capabilities, configured sources and explicit fault severities.
There is no default vehicle calibration. Monitoring profiles may report zero or
partial control capabilities. Arming requires effort mode, all three movement axes,
emergency and neutral-seeking support, plus the existing ownership/safety checks.
The test profile's 4.5 m intervention, 4 m release and 2000 ms sensor expiry are
simulation fixtures only. Do not use them as production settings without review.

All public calls belong on one control-loop thread. Supply wrapping `uint32_t`
monotonic onboard milliseconds and call `Advance` frequently, including during
silence, malformed traffic and disconnected operation. Calls must not be separated
by an entire clock wrap. Expiry runs before input at the same timestamp. `Poll`
emits bounded reports/tickets without catch-up loops; `PacketSink::Send` must
consume each packet synchronously without blocking or reentering the manager.
The sink owns bounded transmission and transport policy; failed delivery never
extends any authorization. `Receive` takes complete delimited frames and returns
syntax success, not permission to execute. Its outputs must come only from
`Outputs`, after current safety checks; disarmed input is qualification only.

Movement and buoyancy sequences are tracked separately. Discrete requests use
request identity, independent of movement sequence ordering. Only accepted
continuous packets advance their stream sequence or renew its lease. Cached
results retain their original generation and never execute again. Counter
exhaustion stops control before wrap. Ticket/sequence/request exhaustion requires
a new session and explicit failsafe recovery; exhausted identity/generation space
requires a verified fresh boot or service. Boot counter zero and maximum are
refused. This implementation reserves the final counter value rather than
attempting a last authorization with no remaining sequence space.

`SetOwner`, `SetDepth`, `SetFault` and local-input APIs are trusted board-adapter
boundaries, not wire commands. Depth samples must represent actual acquisition;
repeated cached sensor/receiver values cannot renew freshness. Depth health is
computed from `SetDepth`; it is the authority for the depth-unavailable fault.
Local FlySky/autonomy adapters must supply increasing sample counters and enforce
an arm switch's off-then-on edge after startup/reconnect. Those adapters do not
exist here yet. A monitoring PC session cannot disturb the selected local owner's
qualification or movement. Configured PC/local sources can still stop it.

`ControlOutputs` expresses permitted intent and effective buoyancy action. Hardware
mapping, stabilization, adjustment limits and ballast control remain unimplemented.
`SetBuoyancyFeedback` requires observed controller feedback; a request never means
successful recovery. Completion does not clear a latch. Independent emergency,
fault and command-loss causes remain visible when a higher-priority latch dominates.

### Persistence boundary

`VerifiedBootState` is evidence supplied by a future board adapter, which must
advance and verify the boot counter and validate provisioned identity before use.
This class cannot prove those claims or recover EEPROM itself. Missing identity
blocks PC sessions/arming; ambiguous emergency state starts latched. Storage
failure blocks arming and requires service. Monitoring and stop commands remain
available when identity is still valid.

A new stop immediately inhibits movement and exposes an asynchronous
`EmergencyWrite` request. Hardware must apply effective emergency intent before
performing storage work. Only acknowledge after a redundant/checksummed write and
readback succeed. Local reset additionally requires physical input release and
observed procedure end; it remains latched until the matching durable-clear token
is verified. New stop assertion supersedes an in-flight clear. Old acknowledgements
are ignored; failed writes retain the latch and require service. This models the
transaction boundary, not a validated durable-storage implementation. EEPROM alone
cannot guarantee latching when power vanishes before any write; the independent
hardware latch remains a prerequisite for powered operation.

## Bounded serial endpoint

`BajolsEndpoint.h` adds `SerialEndpoint` around the independent session manager.
`BajolsArduinoSerial.h` supplies `HardwareSerial` and `millis()` adapters for the
Mega. The portable endpoint itself has no Arduino dependency or heap allocation.
It holds a reference to a longer-lived manager; do not attach another endpoint
or independently poll that manager through a competing output sink.

`Step(port, clock)` services at most 64 receive bytes, runs safety/report
scheduling, and writes at most 64 transmit bytes per call. It reads a fresh
onboard timestamp at frame boundaries and after transmission work. All adapter
operations must return immediately. Call `Step` continuously; byte budgets bound
work but are not measured real-time latency guarantees. Hardware timing and UART
overrun behavior still require validation before connecting actuators.

The receive buffer holds one delimited frame (116 bytes maximum). Oversized input
is discarded through its delimiter; malformed/empty input cannot renew control.
There is no queue of stale incoming movement packets. The manager still checks
session, owner, ticket, generation, sequence, restrictions and safety state.

Eight fixed outgoing frame slots preserve assigned sequence order. Unstarted
status, ticket, heartbeat and per-code fault reports coalesce by session: remove
the superseded frame and append the new one. A partial head frame cannot be
replaced. Admission favors handshake/discrete replies and safety reports over
tickets/heartbeats; safety reports may also evict a cached discrete result.
Retained frames remain FIFO because the desktop rejects out-of-order vehicle
sequences. Full critical queues may drop replies: their discrete results remain
cached for an identical request retry. Drops, coalescing, malformed input and
adapter errors are exposed as saturating diagnostics. A dropped reply never
rolls back or grants an action. Safety deadlines and stop processing continue
even when transmit capacity stays zero indefinitely.

Only call `Write` with a size fitting `Writable()`; actual short/zero writes are
handled. The Arduino adapter rechecks available driver-ring capacity before
writing and never calls `flush`, waits for bytes or uses timeout-based reads.
Use its UART exclusively for binary BCP: no debug text or other writers. The
existing sketch's iBus `Serial2` and optional debug `Serial` remain untouched.

On a known disconnect/cancel, call `endpoint.Reset(now)`. It discards endpoint
input/output, revokes the current/pending PC session and immediately enters
command-loss failsafe for active PC control. A local owner's control remains
independent. A canceled partial TX frame is terminated with a delimiter before
new output; bytes already accepted by the UART cannot be recalled. A new
handshake never clears a latch or resumes motion. Mega USB-UART cable state is
not reliably exposed by `HardwareSerial`, so silence is handled by onboard
command expiry; this adapter does not claim automatic cable detection.

`BcpSerialCheck` initializes only USB serial at 115200 8N1, then repeatedly
services this endpoint. It advertises no implemented buoyancy/sensor systems and
supplies unverified, ambiguous boot evidence. Therefore it remains stopped and
cannot complete a live ground-station handshake. This is intentional until a
verified boot/identity storage adapter exists; no hard-coded boot identity is
substituted. It does not initialize receivers, EEPROM, sensors or actuators.
The native encoded peer supplies explicit in-memory identity/sensor fixtures to
exercise complete serial handshakes, authorization and simulated control.

## Experimental identity/emergency journal

`BajolsStorage.h` implements a portable prototype of the
[storage proposal](../../../protocol/FIRMWARE_STORAGE_PROPOSAL.md), not an approved
EEPROM layout. It uses an exclusive caller-selected 192-byte logical window with
matching identity, boot and emergency record pairs. There is no default writable address, provisioning command or automatic initialization
of erased storage. The existing sketches remain disconnected from this prototype.

`IdentityJournal::BeginBoot` starts an incremental scan and boot-counter advance.
Each `Step(now)` performs at most one byte read or starts one byte write. The
`ByteStorage` backend must return immediately, report busy/failed writes and use
exclusive ownership of its window. The caller supplies a validated positive busy
timeout below 32768 ms; no hardware timing value is selected here. An unexpectedly
busy device without a journal-owned outstanding byte fails closed.

Only construct a controller with verified identity after the journal is ready and
`BootState().identity_verified` is true. The new boot counter is exposed only
after both copies have been written and fully read back. Corrupt, mismatched or
torn boot records require service; there is no fallback to an older identity.
Identity and emergency recovery are separate: a failed identity check blocks
sessions/arming without inventing emergency evacuation if the independent
emergency records were fully recovered as cleared. Unverified emergency records
remain ambiguous/latched and unhealthy.

After applying effective emergency intent, submit a new manager
`PendingEmergencyWrite()` token to `RequestEmergency` once. Keep calling journal
`Step` alongside safety/serial work. Consume `TakeCompletion` and pass its token
and verified result to `SessionManager::CompleteEmergencyWrite`. A newer latch
request may supersede an in-flight clear; the current physical byte must finish
before it can be read back and further old-clear work abandoned. Only the newest
token is acknowledged. Busy clear replacement, replayed tokens and attempts to
repair ambiguous startup records are refused. False submission is not successful
persistence. The implemented `PersistenceCoordinator` owns token submission and
failure acknowledgement; use it rather than independently resubmitting tokens.
Actual board startup must first complete the journal scan/advance, construct a
manager from that same `BootState`, then construct the runtime coordinator.
No existing serial/actuator sketch has this operational startup binding yet.

The native tests model power loss after each of 67 boot-write boundaries and
134 latch/clear boundaries, all 1536 individual-bit corruptions, 67 in-flight
clear supersessions, backend failures, readback corruption, exact timeout across
clock wrap, exhaustion and manager acknowledgement handling. These bounded memory
models do not validate physical EEPROM behavior. Before the first durable latch
mutation, or while an older physical clear byte is already programming, a new
stop can still be lost on power failure. The independent hardware emergency latch
remains required. Fixed pairs also concentrate wear; endurance/wear leveling,
provisioning, service recovery, physical placement and brownout behavior need
review before any Mega EEPROM binding.

`BcpStorageCheck` compiles the prototype against erased **RAM only**. It confirms
blank media yields no verified identity and no write attempts; it does not provide
operational identity to `BcpSerialCheck`. No EEPROM headers/drivers are included,
and no hardware storage or actuator action is performed. The Mega check compiled
to 3,674 bytes flash and 529 bytes global RAM; stack and other vehicle firmware
are excluded.

### Mega EEPROM backend and runtime coordinator

Include `BajolsMegaEeprom.h` only on ATmega2560; the rest of the library remains
portable. Construct `MegaEepromStorage(base, length, writes_enabled)` with an
explicit exclusive physical window. Its capacity is 4096 bytes; access still
requires both a valid chosen window and the requested address inside it. No write
occurs at construction. False write permission refuses every write, even when a
journal scans valid already-provisioned records.

The adapter returns immediately when EEPROM is busy, skips unchanged cells and
starts changed byte programming using the AVR control registers. A short atomic
section preserves the caller's interrupt state and the required EEMPE/EEPE timing.
Generated non-LTO AVR object code was checked for consecutive enable/start SBI
instructions. Do not mix this adapter with Arduino EEPROM routines, EEPROM-ready
interrupts, self-programming, or another writer. There are no wait loops or blocking
Arduino EEPROM calls. Readback/timeouts belong to the independent journal.

`PersistenceCoordinator::Step(now)` advances safety, submits a new manager token
once, supersedes an in-flight clear with a newer stop, services at most one byte
operation and acknowledges verified/failure results. Apply effective emergency
outputs before storage service. A failed/misinitialized journal reports a service
fault and remains failed. Construct the coordinator after successful journal boot
startup, with the controller using the same boot evidence. Static board lifetime
and startup construction are not implemented by the coordinator itself.

`BcpEepromCheck` compiles both components with an explicitly read-only EEPROM
adapter and an unverified controller. If uploaded it can scan the example window
at base zero, but cannot write or establish operational identity. No flash/upload
was performed. It compiles to 7,420 bytes flash and 1,253 bytes global RAM, excluding
stack and the actual vehicle firmware. Actual hardware writes, provisioning and
operational startup remain disabled in supplied sketches. Skipping unchanged
bytes reduces wear but is not wear leveling; the approved fixed-pair development
format still needs an endurance policy before powered operation.

## Provisioning and verified monitoring startup

The [offline provisioning tool](../../tools/README.md) prepares a sparse initial
image and review manifest from a fresh raw EEPROM dump and explicit window.
It refuses non-erased target bytes, overwrites, reserved IDs and implicit reset or
repair. Initial records retain emergency stop; boot counters start at zero only
for a new, never-used identity. Its readback command requires the exact reviewed
records and unchanged bytes outside the window. It cannot access hardware, invoke
a programmer, flash code or modify ground-station settings.

`BajolsMonitor.h` provides `MonitoringRuntime`, an actuator-free composition. It
starts the incremental journal, waits for verified boot evidence, then constructs
`SessionManager`, `SerialEndpoint` and `PersistenceCoordinator` together in a
fixed aligned union. Placement construction does not allocate heap memory. A
failed startup never constructs a controller or sends an unverified handshake.
This completes the startup ordering missing from the individual check sketches.

The monitoring profile advertises no implemented modes, axes, ballast or telemetry,
assigns no movement owner, and has no actuator initialization/output API. Sessions
can report identity/status and process stop requests; they cannot arm. The desktop
can display the connection but will report unsupported control capabilities. No
actual sensor data or simulated healthy depth is injected. An initially provisioned
vehicle remains emergency latched. Local reset hardware is not implemented here.

`BcpMonitor` uses the real Mega adapters. The default build disables EEPROM writes
and cannot complete boot identity verification. Enabling writes requires explicit
build definitions for permission, EEPROM base and validated byte timeout, described
in the tooling README. A later upload of that opt-in build will perform actual boot
and emergency writes; no upload or provisioning occurred in this work. The base
must match the reviewed image. The native integration test uses the actual Python
tool's bytes through the C++ journal/runtime, verifies a complete encoded session,
checks identity separation after reboot and rejects arming/failed startup.

Both default and explicit-opt-in Mega builds compile to 25,084 bytes flash and
2,637 bytes global RAM for this isolated monitoring target, excluding stack and
the existing vehicle firmware. Fixed-pair wear, brownout behavior, manual service
programming and physical runtime remain unvalidated. Operational control still
requires approved sensor, selector, ballast and independent emergency hardware.

## Verification

From the repository root:

```sh
python3 firmware/tests/bcp/run_native_tests.py
python3 firmware/tests/bcp/run_native_tests.py --sanitize
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpCodecCheck
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpSessionCheck
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpSerialCheck
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpStorageCheck
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpEepromCheck
arduino-cli compile --fqbn arduino:avr:mega \
  --library firmware/Libraries/BajolsProtocol \
  firmware/Experimental/BcpMonitor
```

The Python runner requires Python 3 and a C++11 compiler (`g++` by default,
override with `CXX`). It generates temporary C++ field assertions directly from
the same JSON used by C#, including independent outgoing packet construction,
incoming field checks, raw framing, CRC, malformed inputs and stream recovery.
No generated fixtures or binaries are stored in the repository. Sanitizer checks
require compiler sanitizer support. In environments traced with ptrace, run with
`ASAN_OPTIONS=detect_leaks=0` to disable unsupported LeakSanitizer only.

`BcpCodecCheck` performs an isolated memory-only codec check in `setup()` and
retains a `self_test_passed` value for debugger inspection. It does not initialize
serial, receivers, sensors or actuators. Compiling it establishes toolchain
compatibility, not on-device execution or vehicle readiness. Mega resource reports
measure this sketch only; integrating the complete control loop requires a new
RAM, stack and timing review.

`BcpSessionCheck` separately compiles the manager. Its startup evidence is
intentionally unverified and ambiguous, so it remains latched and cannot establish
a session. The Mega compile measured 18,190 bytes flash and 934 bytes global RAM
for this isolated sketch. These figures exclude transient stack usage and the
existing vehicle firmware; no on-device or powered tests were performed.

`BcpSerialCheck` compiled to 21,030 bytes flash and 2,302 bytes global RAM on the
Mega, including the manager, endpoint queues and UART driver. Transient stack and
the existing vehicle firmware are excluded. Native tests cover all HELLO byte
splits, short/zero writes, oversize recovery, fixed byte/queue bounds, safety under
RX flood/TX backpressure, ordered coalescing, full-queue emergency handling and
partial-frame cancellation/reconnect.

## C++ conventions

Follow the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html),
with C++11 and C integer headers for the Arduino AVR toolchain. The Arduino
library include name and sketch naming follow Arduino discovery conventions;
`setup()` and `loop()` retain their required names. Use fixed-width integers for
wire values, explicit little-endian reads/writes, braced control bodies, documented
public fields/methods, and one statement per line. The existing experimental
sketch is outside this library's style scope.

Object reporting uses vehicle-to-PC `0x33` and SupportedSystems bit 4. Each complete
per-sensor snapshot contains at most four observations (95 payload bytes), with
sensor/datum range, body-relative bearing/elevation, uncertainty and sample age.
See [the extension](../../../protocol/BCP_OBJECT_REPORTING_PROPOSAL.md).

Trusted drivers may call `SerialEndpoint::PublishObjects(report, now)` on the
control-loop thread. It applies safety deadlines, requires an established session
and advertised capability, validates the report and assigns session/sequence.
The bounded queue coalesces unsent snapshots by session and sensor ID; a successful
publication is not a delivery acknowledgement. Drivers own acquisition counters,
calibration, sample age, bounded source scheduling and link-budget enforcement.
No experimental hardware sketch advertises object support until a driver exists.
