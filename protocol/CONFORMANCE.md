# BCP v1 conformance plan

Derived from the approved design in [BCP_V1_DRAFT.md](BCP_V1_DRAFT.md).
These artifacts specify expected behavior; the C# wire tests now implement codec
checks, not physical safety validation. Hardware profiles and sensor outcomes below are fixtures,
not approved actuator calibration.

## Wire vectors

[wire-v1.json](test-vectors/wire-v1.json) contains language-neutral fixtures for
C++ and C#. Hex strings contain bytes in transmission order. Each positive vector
includes header fields, decoded payload fields, payload bytes, CRC input, decoded
packet including little-endian CRC, and COBS bytes including the terminating zero.

Each positive vector is independent. A structurally valid command is not permission
to execute it: session, ownership, generation, ticket and safety checks still apply.
Malformed vectors specify the rejection layer and must cause no control action or
freshness renewal. No production encoder was used to establish expected results.
`groundstation/tests/Bajols.Protocol.Tests` links this JSON file and verifies encoding
from its independently described fields, decoding, CRC, bounds and stream recovery.

The proposed 96-byte maximum payload is now an approved envelope limit. A maximum
size transport-only fixture tests framing, not an assigned message payload.

## Safety transition matrix

Abbreviations: D disarmed; A armed; C command-loss failsafe; F fault failsafe;
E emergency-stop latched. An unchanged state never implies renewal of authorization.
All remote events require a valid session and configured-source authority. ARM,
CLEAR and continuous commands additionally require the relevant owner/ticket checks.

| Event | D | A | C | F | E |
|---|---|---|---|---|---|
| Qualified owner ARM | A; clear old intent, advance generation | Reject wrong state | Reject latch | Reject latch | Reject local reset required |
| Unqualified/non-owner ARM | Reject; unchanged | Reject; unchanged | Reject; unchanged | Reject; unchanged | Reject; unchanged |
| New DISARM | D; neutral outputs | D; stop thrust, neutral outputs | C; preserve recovery | F; preserve recovery | E; preserve evacuation |
| Owner selector changes | D; clear qualification/intent | D; stop thrust, neutral outputs | C; retain latch | F; retain latch | E; retain latch |
| Movement deadline reached | D; qualification invalid | C; stop thrust, seek neutral | C | F; retain command-loss cause | E; preserve evacuation |
| First post-arm command absent for 500 ms | Not applicable | C | Not applicable | Not applicable | Not applicable |
| Fresh movement arrives | Qualification only | Apply permitted intent, bounded deadline | C; no resumption | F; no resumption | E; no resumption |
| Heartbeat/new ticket | No movement renewal | No movement renewal | No clear | No clear | No clear |
| New emergency stop | E; stop/evacuate | E; stop/evacuate | E; stop/evacuate | E; stop/evacuate | E; do not restart procedure |
| Qualified CLEAR, all blockers resolved | Reject wrong state | Reject wrong state | D; cancel recovery, restart qualification | D; cancel recovery, restart qualification | Reject local reset required |
| CLEAR with blockers/non-owner/stale ticket | Reject; unchanged | Reject; unchanged | Reject; unchanged | Reject; unchanged | Reject; unchanged |
| Local emergency reset, procedure ended/input released | No emergency latch to clear | No emergency latch to clear | No emergency latch to clear | No emergency latch to clear | D; restart qualification |
| Local emergency reset conditions unmet | No change | No change | No change | No change | E |
| Depth invalid/stale | D; block arming | F; stop thrust, attempt neutral | F; retain command-loss cause | F | E; preserve evacuation |
| Depth restriction activates | No arming bypass | A; inhibit downward/negative, seek neutral | C; restriction cannot downgrade safety | F; restriction cannot downgrade safety | E; restriction cannot downgrade evacuation |
| Normal buoyancy deadline reached | No normal authorization | A; seek neutral if movement fresh | C | F | E |
| Valid new normal buoyancy target | Reject wrong state | Apply only if supported/permitted | Reject wrong state | Reject wrong state | Reject wrong state |
| Sensor/communications recover | D; qualification still required | No implicit change | C | F | E |
| Reboot, valid persistent emergency latch | E | E | E | E | E |
| Reboot, valid cleared emergency latch | D; never resume intent | D | D | D | Not applicable: latch was active |

Safety priority E > F > C > D > A applies to simultaneous events. All active
causes remain recorded; ordinary stop/source changes never bypass higher priority.
Generation advances once per processed authorization-changing event, not once for
every side effect. D-to-D redundant disarm does not continuously advance it.

## Deterministic scenario fixtures

Use a virtual monotonic onboard clock. At a deadline, process expiry before packets
with that timestamp. Preconditions include a healthy calibrated depth source and
implemented buoyancy functions unless the scenario explicitly faults them.

| Case | Setup/event | Expected result |
|---|---|---|
| T01 neutral qualification | Fresh neutral every 50 ms from t=0; ARM at 999/1000 ms | First rejected incomplete; second eligible only if controls still neutral |
| T02 tolerance boundary | Every effort axis at ±50, then one at ±51 | ±50 qualifies; ±51 resets hold |
| T03 ticket bound | Ticket issued at 1000, movement received at 1400 | Deadline 1500, not 1900 |
| T04 exact expiry | Same ticket presented at 1499/1500 | Eligible at 1499; expired at 1500 |
| T05 heartbeat isolation | Last movement deadline 1500; heartbeats continue | C at 1500; thrust off, neutral attempt |
| T06 one-way loss | Stop delivery of tickets to PC; outbound packets continue | No command outlives last received ticket |
| T07 replay | Duplicate/older movement sequence and previous-session command | No acceptance or deadline extension |
| T08 arm generation | ARM accepted with generation 5 | Generation 6; old tickets invalid; thrust off pending new movement |
| T09 post-arm wait | No new-generation movement by arm time +500 | C; no extra grace from heartbeat/tickets |
| T10 independent buoyancy | Buoyancy expires while movement stays fresh | A; seek neutral, normal target unauthorized |
| T11 reverse independence | Renew buoyancy while movement expires | C; buoyancy cannot keep thrust enabled |
| T12 emergency priority | Emergency, movement expiry and depth fault together | E; retain causes, evacuate, thrust off |
| T13 repeated emergency | New stop while already E | No procedure restart or continual generation increment |
| T14 cached ARM | Retry accepted ARM after later DISARM | Historical result only; stay D |
| T15 cache eviction | More than 16 processed requests, replay evicted old ID | Reject too old, no execution |
| T16 ID conflict | Cached request ID with different type/payload | Reject ID reuse, no action |
| T17 selector change | FlySky owns A; selector becomes PC | D; neutral outputs, restart qualification |
| T18 depth restriction | Active negative target; depth reaches profile intervention point | Invalidate authorization/tickets; inhibit downward; seek neutral |
| T19 held descent | Below release threshold but negative/down control still held | No release and no automatic resumed descent |
| T20 qualified release | Below release; neutral target/no downward effort fresh for 1000 ms | Release, generation advances; require post-release tickets |
| T21 sensor loss | Depth becomes stale while A | F; thrust off; neutral attempt or unavailable status |
| T22 failsafe clear | Blockers resolved; owner sends valid CLEAR | D; recovery target ends; neutral outputs; explicit rearm required |
| T23 emergency reset | Remote CLEAR versus local eligible reset | Remote rejected; local returns D |
| T24 reset power loss | Ambiguous/torn emergency persistence | E; never infer cleared |
| T25 timer wrap | Ticket issue 0xFFFFFF00; receipt 0x00000090 | Elapsed 400 ms; 100 ms remaining |
| T26 handshake timeout | Pending created t=0, retry t=1900, confirm t=2000 | Expired; retry did not extend deadline; assigned session never recycled |
| T27 confirmation replay | Confirm established session again | READY response; no counter/state/ticket reset |
| T28 pending replacement | Healthy established session plus eligible new HELLO | Established session retained until eligible confirmation |
| T29 historical HELLO | Old nonce no longer retained | At most new pending session with new ID; old confirm cannot complete it |
| T30 non-owner monitoring | FlySky owns A; PC reconnects/joystick moves | FlySky qualification/control unaffected; configured PC can still stop |
| T31 telemetry duplicate | Same measurement ID twice | Reject whole packet |
| T32 unknown telemetry | Unknown measurement ID among well-formed records | Skip unknown; never use it for control |
| T33 unknown state | Unknown status enum received by PC | Display unknown, inhibit Arm; keep stop controls |
| T34 status silence | No status for 1000 ms | Mark stale/unconfirmed; never infer disarmed |
| T35 fault recurrence | Inactive→active→active repeat→cleared→active | Counter increments only on the two activations; saturates at max |
| T36 mixed downward movement | Active restriction; fresh valid packet with forward/yaw effort and dive >0 | Reject whole command; no partial forward/yaw update, sequence acceptance or freshness renewal |
| T37 permitted replacement | After T36, a newer valid packet requests dive zero or upward | Accept if all other checks pass; apply permitted intent with its own bounded deadline |
| T38 rejected-command timeout | Only downward packets arrive while inhibited | Existing movement deadline still expires into C; no rejection-based grace |
| T39 inhibited negative buoyancy | Active restriction; valid ticket but target negative | Reject; no queued target, accepted sequence update or buoyancy renewal |
| T40 permitted buoyancy replacement | Restriction active; target neutral or positive | Eligible only if otherwise supported/authorized; cannot renew movement |

These are design-derived expectations. `Bajols.Integration.Tests` now executes
representative deterministic and encoded-link scenarios, including expiry, tickets,
replay, neutral qualification, depth restriction, source changes, fault recovery and
emergency reset. Durable persistence and actual hardware behavior remain unvalidated.

## Conformance clarification disposition

The matrix exposed these details; draft 0.8 section 15 records their disposition:

- Resolved in draft 0.7: downward movement packets are rejected as a whole, without
  applying forward/yaw or renewing freshness. Negative-buoyancy packets are also
  rejected while inhibited. T36–T40 capture these decisions; no clamping is allowed.
- The depth-release hold requires both input streams fresh and continuous eligible
  sensor/input conditions; expiry or an excursion resets it. This is a future stateful
  test, not part of the wire decoder.
- Handshake nonce is an opaque 16-byte value, not a particular UUID representation.
  Vehicle ID byte representation is likewise opaque; textual display must preserve
  bytes consistently. No UUID field byte-swapping should be inferred.
- Fault severity is carried explicitly; the codec supplies no inferred default.
  Controller profiles must assign it before fault transmission. Wire fixtures specify
  a sample severity without allocating a default for a code.
- Generation/sequence exhaustion and persistence failure belong to the future session
  manager: stop before authorization counters exhaust, never wrap or reuse identity.
  Exact event integration and storage timing remain simulator/hardware prerequisites.

The C# codec has no authority to execute state transitions. The desktop session
manager and simulated peer are implemented separately from it. The 40 scenarios
remain the conformance checklist; representative cases are automated in
`Bajols.Integration.Tests`, while physical/persistent hardware requirements remain
outside the simulator. Simulator profile values are fixtures, not hardware settings.

## Portable firmware policy implementation

`firmware/Libraries/BajolsProtocol/src/BajolsSession.h` now implements the portable
session and authorization layer, independently of the stateless C++ codec.
`firmware/tests/bcp/session_tests.cpp` drives encoded PC packets through it with
an explicit virtual onboard clock. Run both suites using
`python3 firmware/tests/bcp/run_native_tests.py` (also `--sanitize`).

| Native coverage | Cases |
|---|---|
| Neutral timing, tolerance, ticket lifetime, replay, generation, first-command deadline and timer wrap | T01–T09, T25 |
| Independent buoyancy/movement expiry | T10–T11 |
| Safety priority, repeated stops, cached historical actions, eviction and ID conflict | T12–T16 |
| Local source changes and monitoring-session replacement | T17, T30 |
| Depth restriction, held descent, qualified release, sensor loss and explicit recovery | T18–T22, T36–T40 |
| Local reset, pending durable transactions, failed writes and ambiguous startup records | T23–T24, modeled storage only |
| Pending expiry, retry retention, confirmation replay and historical nonce/session separation | T26–T29 |
| Saturating fault recurrence | T35 |

The native suite additionally exercises arm, clear, disarm, emergency and owner
selector behavior across all five states, malformed traffic at deadlines,
counter exhaustion, failed durable clear and a stop superseding an in-flight
clear. T31–T32 belong to the shared-vector codec suite; T33–T34 remain desktop
interpretation/status-silence responsibilities, not onboard packet authorization.
This is representative scenario coverage, not exhaustive physical validation.

The boot evidence, storage acknowledgements, sensor samples and controller
feedback in tests are explicit fixtures. No hardware-verified persistent
boot transaction, physical emergency latch, receiver adapter, real sensor profile,
actuator mapping or ballast controller has been implemented. The separate
`BcpSessionCheck` Mega sketch verifies toolchain compatibility only and starts with
unverified boot evidence. The existing `TelemetryProof` control loop is unchanged.

### Serial integration coverage

`firmware/tests/bcp/endpoint_tests.cpp` adds an encoded peer driving the portable
`SerialEndpoint` through a fake nonblocking byte port and virtual clock. It tests
HELLO fragmentation, short/zero writes, bounded 64-byte RX/TX service, oversized
frame recovery, eight-slot queue saturation, coalescing without sequence
reordering, authoritative emergency status admission, stop application while
replies are dropped, deadline progression during RX flood/TX backpressure, and
partial-frame cancellation/reconnect. A known PC disconnect immediately revokes
its authorization; cancellation preserves selected local-owner control.

`BcpSerialCheck` compiles the Mega `HardwareSerial`/`millis()` adapters at 115200
8N1 in a separate sketch. No actuators or persistent storage are initialized;
its unverified boot evidence deliberately prevents an operational handshake.
Native fixtures, not this sketch, supply simulated verified identity and sensors.
Byte-work bounds are verified, but actual parser/output latency, serial driver
overruns, durable identity recovery and powered operation remain unvalidated.

### Experimental storage coverage

`firmware/tests/bcp/storage_tests.cpp` tests the portable `IdentityJournal` against
an in-memory byte device using the proposed format in
[FIRMWARE_STORAGE_PROPOSAL.md](FIRMWARE_STORAGE_PROPOSAL.md). It cuts power after
all 67 two-record boot-write boundaries and 134 emergency latch/clear boundaries,
flips all 1536 individual record bits, and supersedes a local clear with a new
stop at 67 write boundaries. It additionally covers erased media, CRC-valid
semantic errors, no older-boot fallback, final paired readback, counter exhaustion,
read/write/readback failures, exact busy timeout across clock wrap, logical-window
overflow, token replay and the manager's asynchronous acknowledgement boundary.
Identity failure cannot invent an emergency latch when the independent emergency
pair is fully verified cleared. T24's ambiguous storage behavior is modeled;
physical torn-write behavior remains unvalidated.

The tests explicitly demonstrate that loss before the first durable latch mutation
can retain old cleared records. An in-flight physical clear byte cannot be recalled.
Independent emergency hardware remains required. The fixed pairs are a reviewable
prototype without wear leveling, provisioning or service-recovery tooling.
`BcpStorageCheck` compiles only a RAM backend and never accesses Mega EEPROM.
Actual writable board mapping and operational startup integration are not enabled.

### Mega EEPROM adapter and coordinator

The ATmega2560 nonblocking byte backend now implements readiness, address-window
checks, explicit write permission, unchanged-cell skipping, bounded atomic register
access and erase/write start. The Mega compile-only check uses write permission
false. Its instructions were also inspected in a non-LTO AVR object to verify the
consecutive EEMPE/EEPE start sequence; no physical writes or flash/upload occurred.

Native storage tests additionally execute `PersistenceCoordinator` token ownership,
one-byte service bounds, no repeated writes while idle, stop superseding clear,
and failed clear acknowledgement retaining emergency latch/service blockers.
The coordinator owns runtime submissions; verified boot startup must still create
the controller from the same journal evidence before runtime service. Real-device
interrupt latency, electrical/brownout behavior, endurance/wear leveling, approved
physical mapping and provisioning remain separate prerequisites. Supplied sketches
cannot write EEPROM or provide an operational provisioned serial vehicle.

### Offline provisioning and composed monitoring startup

`firmware/tools/provision_identity.py` prepares initial paired records only from
an erased target window in an exact full-device dump, produces a sparse image
and review manifest, and checks complete readback including unchanged bytes outside
the window. Tests reject non-erased/partial sources, invalid/reserved identities,
existing output, manifest tampering, malformed readback and collateral changes.
There is no device/programmer invocation or force/recovery mode.

The native runner passes the actual offline tool's generated records into
`monitor_tests.cpp`. `MonitoringRuntime` advances/verifies boot identity before
placement-constructing its fixed-storage session manager, endpoint and coordinator.
The test completes encoded HELLO/CONFIRM/READY, checks the provisioned opaque ID,
zero control/sensor capabilities, arming rejection, reboot counter separation,
and absence of a manager/session after blank, corrupt or read-only boot failure.
Startup and runtime preserve the one-storage-byte-per-step bound. Zero/partial
capability profiles may monitor, but incomplete control support blocks arming even
for a selected owner with otherwise qualified input.

The new `BcpMonitor` Mega target initializes USB serial only. It defaults read-only;
write-enabled compilation requires explicit permission, physical base and timeout.
Both variants compile. No initial image was programmed and no firmware uploaded.
Native tests are RAM models, not proof of physical endurance, power-loss response,
USB operation or hardware safety. The fixed-pair development format still needs
an endurance/wear policy before operational control.

### Object-reporting extension (draft 0.9)

The user authorized protocol implementation on 2026-11-19. See
[the object-reporting extension](BCP_OBJECT_REPORTING_PROPOSAL.md) for the assigned
`0x33` message, SupportedSystems bit 4, bounded 95-byte maximum snapshot, body
coordinates, provenance and acquisition age. Both codecs now cover these fields.
The shared suite contains 63 vectors (the original 37 plus 26 object cases).
Existing message payloads and the v1 envelope are unchanged.

The client retains at most sixteen sources, replaces complete snapshots only for
newer scan counters, combines onboard/local age, and clears them on disconnect.
Fault/unavailable states replace prior returns. Repeated scans and heartbeats do
not renew acquisition freshness. Firmware TX coalesces reports by session/source.
The simulated peer publishes the nearest four returns from at most 32 stationary
world points plus an optional body-relative test return. All reports are synthetic
and traverse BCP; the desktop no longer fabricates received observations locally.

Hardware sensor choice, mounting transforms/calibration, source-specific stale
profiles, measured bandwidth/fairness across physical sources and physical safety
remain deferred. The schematic display currently uses sensor range and bearing;
it does not render reported elevation/uncertainty or establish hull clearance.

The object display retains the latest source snapshot within six metres when its
acquisition expires. Historical markers stop flashing and fade to grey/white over
one second after expiry, with a stale label. Their positions are last reported
body-relative positions, not motion-compensated tracks. New source snapshots
replace them (including empty/fault snapshots); disconnect clears them. This is a
presentation change and does not renew freshness or change the BCP wire layout.

The six-metre display/simulator limit provisionally covers the anticipated 3–6 m
sensor class. It is not a validated physical detection range or selected hardware
specification. BCP ranges remain unsigned millimetres; no wire change is needed.

### Dashboard presentation checks

The desktop suite renders actual XAML with Skia at default/minimum window sizes,
checking visible controls without scrollbars. Readouts and plain-language status
are overlaid on the 3D scene; the movement controls, buoyancy selector and bottom
boxed actions remain separate from protocol/session policy.

The neutral-check bar reflects onboard progress up to 1000 ms, resets on unavailable
status and is hidden while armed. It never establishes arming authority. The depth
gauge spans 0–4.5 m, warms toward red at its limit, clamps only its marker for deeper
values and hides the marker when depth is invalid/unconfirmed. Tests also check
compass/degrees labels and the enlarged object projection at extreme attitudes.
The guide ray ends at one metre; it is independent of the provisional six-metre
object display limit. These are presentation checks, not sensor calibration,
physical clearance guarantees or changes to onboard safety thresholds.
