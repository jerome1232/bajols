# BCP v1 — Discussion draft 0.9

Status: approved design baseline, updated 2026-11-19. Numeric registry and bounded
handshake rules approved. The user subsequently authorized the C# codec and its
conformance tests, followed by desktop session handling and a simulated peer.
Firmware integration and powered tests remain outside that implementation scope. Draft 0.8 clarified codec validation without changing wire bytes. Draft 0.9 adds the user-authorized object-reporting extension (section 16).

Sections distinguish agreed behavior from proposals. Agreed timing values are
design targets pending simulation and hardware validation. Existing experimental
firmware does not implement this contract.

## 1. Scope

BCP carries movement intent, control requests, vehicle status and telemetry between
the ground station and onboard firmware.

The onboard firmware owns command arbitration, actuator mapping, safety,
stabilization and buoyancy control. These functions must operate independently of
the ground station.

The first movement mode expresses normalized effort, rather than target speed,
descent rate or depth.

## 2. Agreed control rules

| Item | Agreed behavior |
|---|---|
| Movement owner | Exactly one: PC, FlySky or future autonomy |
| Owner selection | Physical selector on the vehicle for the initial version |
| Arming authority | Only the selected owner |
| Disarm/emergency-stop authority | Either configured control source, regardless of movement ownership |
| Movement cadence | 20 Hz |
| Movement expiry | 500 ms without a valid, fresh command from the active owner while armed |
| Heartbeats | Never renew movement commands |
| Reconnection | Never automatically clears failsafe, arms or resumes movement |
| Emergency-stop reset | Deliberate action at the vehicle |
| Normal buoyancy target cadence | 20 Hz, independently renewed from movement |
| Normal buoyancy target expiry | Earlier of ticket expiry or 500 ms after acceptance |
| Operating depth limit | 5 m below the water surface, enforced onboard |

Changing the owner selector while armed stops thrust, enters `DISARMED` and clears
previous movement intent. The newly selected owner must qualify and explicitly arm.

## 3. Movement intent

Movement commands describe three independent effort axes:

| Axis | Agreed meaning | Agreed positive direction |
|---|---|---|
| Forward | Propulsion effort | Ahead |
| Yaw | Turning effort | Starboard |
| Dive | Diving effort | Downward |

The axis meanings and signs above are agreed.

Agreed encoding: signed 16-bit integers from `-1000` to `+1000`, where zero means
no requested effort. Out-of-range values are rejected.

The firmware maps these values to the vehicle's actuators. Dive-plane behavior at
low speed or in reverse remains a vehicle-controller responsibility.

## 4. Safety states

The states below describe agreed behavior. Numeric encodings are in section 12.

| State | Meaning and output behavior |
|---|---|
| `DISARMED` | Thrust off; rudder, dive planes and ballast controls at configured neutral states |
| `ARMED` | Fresh movement commands from the selected owner may control the vehicle |
| `COMMAND_LOSS_FAILSAFE` | Thrust off; onboard firmware attempts neutral buoyancy; movement remains inhibited |
| `FAULT_FAILSAFE` | Thrust off; inhibit negative seeking; attempt neutral buoyancy; explicit recovery required |
| `ESTOP_LATCHED` | Thrust off; onboard firmware initiates ballast blow; movement remains inhibited until local reset |

Configured neutral ballast controls do not imply neutral buoyancy.

Emergency stop takes priority over all other states. The implementation of
neutral-buoyancy recovery and ballast blow remains deferred to the onboard hardware
and controller design. The current pump/solenoid arrangement is a stand-in, not a
validated final ballast mechanism.

### Agreed normal buoyancy control

Normal targets are neutral, positive (tendency to rise) and negative (tendency to
sink) buoyancy. These are buoyancy targets, not vertical-speed requests, and are
separate from dive effort. Emergency ballast evacuation remains a separate latched
emergency procedure, not an ordinary target.

Normal buoyancy commands require an armed vehicle, the selected movement owner,
a valid session, a fresh ticket and the current control generation. Renew them at
20 Hz; their lifetime ends at the earlier of ticket expiry or 500 ms after
acceptance. Heartbeats do not renew them. Movement and buoyancy commands cannot
renew each other.

On normal-target expiry while movement remains fresh, stop seeking positive or
negative buoyancy and seek neutral. If movement also expires, enter the existing
command-loss failsafe: thrust off and seek neutral. Disarm, command-loss failsafe
and emergency stop override normal buoyancy requests. Routine disarm still applies
configured neutral actuator states, not a promise of neutral buoyancy.

Report requested/effective buoyancy action, action progress and estimated buoyancy
separately. Estimated buoyancy can be unknown, neutral, positive or negative.
Rising or sinking alone does not prove buoyancy because propulsion, control
surfaces and water motion can affect vertical movement. Insufficient feedback must
not be reported as successful recovery.

Expiry changes the controller's target; it cannot instantly undo existing negative
buoyancy. The onboard algorithm, adjustment limits, feedback, tolerances and
completion criteria remain unresolved. Neither fresh commands nor a target request
authorize unbounded ballast adjustment.

### Agreed depth limit

The operating limit is 5 m below the water surface. At the limit, onboard firmware
inhibits negative-buoyancy seeking and further downward dive effort, seeks neutral
buoyancy and reports a depth-limit condition. Thrust-off and emergency-stop commands
remain available. The limit does not cancel or downgrade emergency ballast evacuation.

A calibrated depth measurement and intervention margin before 5 m are needed to
account for momentum and control delay. Valid calibrated depth is required for
arming. Invalid or stale depth while armed enters `FAULT_FAILSAFE`: stop thrust,
inhibit negative seeking and attempt neutral buoyancy. Report recovery unavailable
if the failed sensing prevents it; emergency evacuation remains available. The
margin, sensor selection, stale threshold and release/hysteresis remain vehicle-profile
prerequisites requiring validation before powered operation.
This is an operating target, not a guarantee that descent stops immediately or a
verified hull pressure rating.

## 5. Agreed arming qualification

Before every arming:

1. The selected owner must provide fresh movement commands.
2. Every movement axis must remain within ±5% of zero effort for one continuous second.
3. The controls must still be neutral when the explicit arm action is accepted.
4. No emergency-stop latch or blocking fault may be active.

Non-neutral input, command expiry or ownership change resets qualification.
Heartbeats do not count. Completing qualification never automatically arms.

For FlySky, zero effort follows its input mapping; it need not mean every stick is
centered. An arm switch already on at startup or reconnection must be cycled off
and then on.

While disarmed, accepted commands may establish neutral qualification, but their
movement values are never retained for execution after arming. Successful arming
invalidates outstanding tickets by advancing the control generation. Thrust remains
off until a newly sampled command using a new-generation ticket is accepted.
If none arrives within 500 ms after arming, enter command-loss failsafe.

## 6. State transitions

| Current state | Event | Agreed result |
|---|---|---|
| `DISARMED` | Qualified owner explicitly arms | `ARMED` |
| `ARMED` | Either configured source disarms | `DISARMED`; apply routine neutral outputs |
| `ARMED` | Physical owner selector changes | `DISARMED`; clear intent and neutral qualification |
| `ARMED` | Active-owner movement expires | `COMMAND_LOSS_FAILSAFE` |
| `COMMAND_LOSS_FAILSAFE` | Commands or connection return | Remain in failsafe |
| `COMMAND_LOSS_FAILSAFE` | Explicit clear accepted | `DISARMED`; restart neutral qualification |
| Any state | Emergency stop asserted | `ESTOP_LATCHED` |
| `ESTOP_LATCHED` | Remote clear/arm request | Reject |
| `ESTOP_LATCHED` | Deliberate local reset accepted | `DISARMED`; restart neutral qualification |

Agreed safety completion rules:

- Ordinary `DISARM` must not clear any active safety latch or cancel its buoyancy action.
- Failsafe clear comes from the selected owner and requires restored fresh input
  and no blocking fault.
- A local emergency-stop reset is accepted only after any physical stop input is
  released and the onboard emergency procedure is no longer active. Reset returns
  to `DISARMED` and restarts qualification.

Startup enters `DISARMED` unless a persisted emergency-stop latch requires
`ESTOP_LATCHED`. Power cycling cannot clear emergency stop. Ownership changes do
not clear any safety latch. Agreed display/action priority is emergency stop, then
fault failsafe, then command-loss failsafe, then disarmed, then armed. Retain all
active causes; clearing requires every applicable blocking condition resolved.

Depth invalid/stale while armed enters `FAULT_FAILSAFE`. Explicit recovery requires
fresh owner input and cleared blocking faults, followed by neutral qualification
and a separate arm action. `CLEAR_FAILSAFE` handles both non-emergency failsafe
states. Persistent latch behavior is specified in section 13; physical storage
implementation still requires review.

## 7. Agreed session, identity and freshness contract

### Handshake

1. PC sends `HELLO` with a new connection nonce and requested protocol version.
2. Vehicle sends `HELLO_REPLY`, echoing the nonce and assigning a new session ID.
3. PC sends `SESSION_CONFIRM` with the echoed nonce.
4. Vehicle sends `SESSION_READY` and begins issuing tickets.

The PC rejects replies for another connection nonce. Retrying the same pending
handshake returns the existing response rather than creating another session.
Handshake retries must not replace an established session. Session establishment
never grants ownership, clears a safety latch or arms the vehicle.

Reserve the all-zero session ID for `HELLO`. Pending handshakes expire after two
seconds; retries use the same nonce and do not renew that deadline. Only one PC
session may be established at a time.

A new nonce cannot immediately replace an armed PC owner's session. Once movement
authorization expires, command-loss failsafe applies and a replacement session may
be accepted without clearing the latch. When disarmed, a new handshake may replace
the old session, invalidating its tickets and pending requests. Replacing a monitoring
PC session while FlySky owns movement does not interrupt FlySky control.

Handshake retry/sequence rules and vehicle identity are specified in section 13.
Session replacement never clears a failsafe or emergency-stop latch.

### Identities and counters

| Field | Agreed encoding | Rule |
|---|---|---|
| PC connection nonce | 16 opaque bytes | Newly generated for each connection attempt |
| Session ID | Boot counter `u32`, then session counter `u32` | Vehicle-issued; neither counter may wrap |
| Ticket ID | `u32` | Increasing within a session; no wrap |
| Control generation | `u32` | Advances to invalidate earlier control authorization; no wrap |
| Packet sequence | `u32` | Increasing per sender within a session; no wrap |
| Request ID | `u32` | Increasing per PC session for discrete actions |

All integers use little-endian encoding. The session ID is eight bytes, with the
boot counter serialized first. Before counter exhaustion, require a new session
while movement remains disabled; boot-counter exhaustion requires maintenance.
Session, ticket, packet-sequence and request counters start at one. Counter
exhaustion requires a new session before wrap; boot-counter exhaustion requires
maintenance. Initial control generation is one (section 12).

The persistent boot counter must be advanced and verified before sessions are
issued. Torn writes and corruption must not permit identity reuse. If valid new
identity cannot be established, BCP arming remains unavailable. Persistent layout,
wear management and recovery behavior require separate review.

### Tickets and movement deadlines

- Vehicle issues a ticket every 50 ms, valid for 500 ms from onboard issuance time.
- Store outstanding tickets in a fixed 16-entry ring buffer.
- Tickets contain session identity (in the header), ticket ID, control generation
  and advertised validity duration. The vehicle retains their issue times.
- An expired, evicted, unknown or wrong-generation ticket is rejected.
- Tickets may be reused within their lifetime for newly sampled commands.
- The PC must not attach a new ticket to old buffered movement intent.
- Both directions must work: loss of incoming tickets eventually prevents valid
  PC movement commands even if the outgoing connection still works.
- Heartbeats, new tickets and duplicate commands never extend an existing command.

For each accepted movement command:

```text
movement deadline = earlier of:
  ticket issue time + 500 ms
  command acceptance time + 500 ms
```

A command received with a 400 ms-old ticket has at most 100 ms remaining.
Deadlines use the onboard clock; PC clock synchronization is not required.

The receiver validates the complete command, current session, ticket, generation,
owner and sequence before accepting any part. Invalid or non-owner commands do
not refresh movement expiry. Movement accepts only a sequence newer than the last
accepted movement command and uses latest-value semantics; never retry old intent.

FlySky needs equivalent input freshness through its adapter. Reading cached channel
values is not evidence of new operator input; exact receiver-loss detection remains
an implementation requirement.

### Control generation

Advance generation and invalidate outstanding tickets when:

- Arming succeeds.
- Routine disarming occurs.
- Ownership changes.
- Command-loss failsafe begins or is cleared.
- Fault failsafe begins or is cleared.
- Emergency stop first latches or is locally reset.

Repeated stop requests in an already identical stopped state do not continually
advance generation. Earlier `ARM` and `CLEAR_FAILSAFE` requests cannot enable
control after a later safety transition. One processed event advances generation
once even when it changes both ownership and state.

### Discrete requests and duplicates

Retries use a new packet sequence but retain the original request ID and payload.
Maintain a fixed 16-entry result cache and highest processed request ID:

- A cached duplicate with identical payload returns its recorded result without
  executing again.
- Reusing an ID with different content is rejected.
- Older requests whose results were evicted are rejected without execution.
- Rejected requests are recorded too.
- A cached successful `ARM` result describes the original action, not the current
  state. Current vehicle status remains authoritative.

`ARM` and `CLEAR_FAILSAFE` require a fresh ticket and current control generation.
`DISARM` and `EMERGENCY_STOP` require a valid session and configured-source authority,
but no unexpired movement ticket. Stop processing is independent of movement
sequence ordering. Process in this order:

1. Validate frame, CRC, version, direction, exact length and session.
2. Validate configured-source authority.
3. For discrete requests, check request identity and cached result.
4. For enabling requests, check generation, ticket, ownership and safety conditions.
5. Execute once and record the result.

Old stop duplicates must not execute again after later rearming. Cached duplicates
return their original result; evicted old IDs are rejected. Newly asserted stops
use new request IDs. Invalid traffic must not delay onboard safety deadline checks.

## 8. Message IDs and payloads

Fields appear in serialization order. Session ID and packet sequence are in the
frame header. Payload layouts below are agreed unless explicitly noted otherwise.
Envelope parameters were accepted in the completion package (section 9).

| ID | Message | Direction | Payload |
|---|---|---|---|
| `0x01` | `HELLO` | PC → vehicle | nonce `[16]`, requested version `u8` |
| `0x02` | `HELLO_REPLY` | Vehicle → PC | echoed nonce `[16]`, selected version `u8`, vehicle ID `[16]`; assigned session in header |
| `0x03` | `SESSION_CONFIRM` | PC → vehicle | echoed nonce `[16]` |
| `0x04` | `SESSION_READY` | Vehicle → PC | control generation `u32` |
| `0x05` | `CONTROL_TICKET` | Vehicle → PC | ticket ID `u32`, control generation `u32`, validity ms `u16` |
| `0x06` | `HEARTBEAT` | Both | Empty |
| `0x10` | `MOVEMENT_COMMAND` | PC → vehicle | ticket ID `u32`, control generation `u32`, forward `i16`, yaw `i16`, dive `i16` |
| `0x11` | `ARM` | PC → vehicle | request ID `u32`, ticket ID `u32`, control generation `u32` |
| `0x12` | `DISARM` | PC → vehicle | request ID `u32` |
| `0x13` | `CLEAR_FAILSAFE` | PC → vehicle | request ID `u32`, ticket ID `u32`, control generation `u32` |
| `0x14` | `EMERGENCY_STOP` | PC → vehicle | request ID `u32` |
| `0x15` | `COMMAND_RESULT` | Vehicle → PC | request ID `u32`, original message type `u8`, result `u8`, current control generation `u32` |
| `0x16` | `BUOYANCY_COMMAND` | PC → vehicle | ticket ID `u32`, control generation `u32`, target `u8` |
| `0x20` | `CAPABILITIES` | Vehicle → PC | See section 8.2 |
| `0x21` | `GET_CAPABILITIES` | PC → vehicle | Empty |
| `0x30` | `VEHICLE_STATUS` | Vehicle → PC | See section 8.3 |
| `0x31` | `TELEMETRY` | Vehicle → PC | See section 8.4 |
| `0x32` | `FAULT` | Vehicle → PC | See section 8.5 |
| `0x33` | `OBJECT_REPORT` | Vehicle → PC | Bounded source snapshot; see section 16 |

No remote emergency-stop-reset operation exists. A successful result means acceptance, not
physical completion of ballast evacuation or neutral-buoyancy recovery.

### Agreed renewable buoyancy message

`BUOYANCY_COMMAND` targets: `0` neutral, `1` positive, `2` negative. Emergency
evacuation is not an ordinary target. Reject invalid targets. Use latest-value
semantics without request IDs, automatic retries or per-command acknowledgements.
Accept only a sequence newer than the last accepted buoyancy command; track ordering
separately from movement. Report accepted target separately from effective action.

When a depth restriction activates, invalidate a blocked negative target's
authorization. It must not automatically resume after release. The approved neutral
qualification and ticket-generation rules are specified in section 13.

### 8.1 Command results

| Code | Meaning |
|---|---|
| `0x00` | Accepted |
| `0x01` | Unsupported message |
| `0x02` | Invalid payload |
| `0x03` | Not movement owner |
| `0x04` | Unknown or expired ticket |
| `0x05` | Wrong control generation |
| `0x06` | Invalid safety state |
| `0x07` | Neutral hold incomplete |
| `0x08` | Blocking fault |
| `0x09` | Local reset required |
| `0x0A` | Request too old |
| `0x0B` | Request ID reused with different content |

Invalid-CRC and invalid-session frames are discarded without executing commands;
diagnostic counters may report failures separately. Unknown messages never execute
actions. Reporting for unsupported messages without request IDs remains open.

### 8.2 Capabilities

Send after session establishment and in response to `GET_CAPABILITIES`. Capabilities
remain fixed throughout a session; changing them requires a new session while
disarmed. They report implemented support, not verified physical effectiveness or
current health. Planned buoyancy functions cannot be advertised as implemented.
Reserved unsupported bits are zero.

| Field, in order | Encoding | Meaning |
|---|---|---|
| Capability revision | `u16` | Capability-set identifier for this session |
| Supported modes | `u16` bitmask | Bit 0: effort control |
| Supported axes | `u8` bitmask | Bits 0–2: forward, yaw, dive |
| Supported systems | `u16` bitmask | Bits 0–4: emergency evacuation, neutral seeking, positive seeking, negative seeking, object reporting |
| Supported telemetry | `u32` bitmask | Measurements in section 8.4; bit mapping in section 12 |
| Movement interval | `u16`, ms | 50 |
| Command timeout | `u16`, ms | 500 |
| Ticket interval | `u16`, ms | 50 |
| Ticket lifetime | `u16`, ms | 500 |
| Neutral hold | `u16`, ms | 1000 |
| Neutral tolerance | `u16` | 50 in the -1000…1000 effort scale |

### 8.3 Vehicle status

Send at 5 Hz and queue an immediate update after safety-state, ownership or blocking
condition changes. Coalesce pending updates to bound queue size.

| Field, in order | Encoding | Meaning |
|---|---|---|
| Status revision | `u32` | Increments when reported state changes |
| Vehicle uptime | `u32`, ms | Onboard snapshot time; wraps |
| Safety state | `u8` | Disarmed, armed, command-loss failsafe, emergency-stop latched, fault failsafe |
| Movement owner | `u8` | None, PC, FlySky, autonomy |
| Control mode | `u8` | Initially effort control |
| Control generation | `u32` | Current authorization generation |
| Arming blockers | `u32` bitmask | All currently applicable blockers |
| Neutral hold progress | `u16`, ms | Saturates at 1000 |
| Movement age | `u16`, ms | Last accepted owner movement age; 0xFFFF unavailable |
| Movement time remaining | `u16`, ms | Remaining authorization lifetime; zero when none |
| Buoyancy action | `u8` | None, seek neutral/positive/negative buoyancy, emergency ballast evacuation |
| Buoyancy action status | `u8` | Inactive, running, completed, failed, unavailable |
| Safety cause | `u8` | Why the current safety state was entered |
| Estimated buoyancy | `u8` | Unknown, neutral, positive, negative |
| Authorized normal target | `u8` | None, neutral, positive, negative |
| Buoyancy command time remaining | `u16`, ms | Zero when no normal target is authorized |
| Active restrictions | `u16` bitmask | Depth limit active, depth unavailable, downward control inhibited |

Enum values, causes, blockers, revision and age rules are defined in section 12.
Completion must reflect onboard physical completion criteria, not merely issuing
a valve command.

The appended fields above were accepted in the completion package. Exact numeric
enums and restriction bits are defined in section 12. Requested target,
currently authorized target and effective action must not be conflated: an expired
or inhibited negative target has no continuing authorization.

Arming blockers report all applicable reasons:

- No selected owner.
- Selected source unavailable or input stale.
- Movement input outside neutral tolerance.
- Neutral hold incomplete.
- Command-loss failsafe requires clearing.
- Emergency stop requires local reset.
- Blocking onboard fault.
- Required subsystem unavailable.
- Explicit arm action has not been reset, such as a FlySky switch still on after reconnecting.

An empty blocker mask never arms automatically. Neutral qualification applies to
the selected owner's input; a non-owner PC joystick cannot affect FlySky qualification.
A connected non-owner PC still receives tickets and status and may assert disarm
or emergency stop.

The ground station marks status stale after one second without an update and shows
the last known state as unconfirmed. Missing status never means disarmed. This
UI deadline does not replace onboard command or ticket expiry.

### 8.4 Telemetry

Agreed layout, transmitted initially at 5 Hz subject to actual sensor sampling:

```text
record_count:u8
records[record_count]:
  measurement_id:u8
  quality_flags:u8
  sample_age_ms:u16
  value:i32
```

Each record is eight bytes; at most 11 records fit within the 96-byte payload cap.
Payload length must equal `1 + 8 * record_count`. Transmission never refreshes a
sample. Age saturates at 65534 ms; 65535 means unknown. Each measurement requires a
validated stale threshold in the onboard vehicle profile. Invalid values must not
be displayed as current measurements. Flags independently identify valid, simulated,
estimated and sensor-fault conditions; bit positions are defined in section 12.

| ID | Measurement | Unit |
|---|---|---|
| `0x01` | Depth below water surface | mm |
| `0x02` | Absolute pressure | Pa |
| `0x03` | Supply voltage | mV |
| `0x04` | Propulsion RPM magnitude | RPM |
| `0x05` | Heading | centidegrees |
| `0x06` | Pitch | centidegrees |
| `0x07` | Roll | centidegrees |
| `0x08` | Water temperature | millidegrees Celsius |

Heading reference, attitude coordinate convention and physical validity ranges
are defined in section 12. Existing simulated/uncalibrated data cannot be
advertised as validated depth or voltage.

### 8.5 Faults

Agreed payload: `fault_code:u16`, `severity:u8`, `active:u8`,
`occurrence_counter:u32`. Report fault changes; vehicle status retains current
blocking conditions, so a missed event cannot hide an interlock. Fault response
is assigned explicitly in section 13, not inferred solely from severity.

Initial faults cover depth unavailable, buoyancy controller unavailable/failed,
emergency evacuation failed, persistent identity-storage failure and control-loop
timing failure. Exact codes and severity values are defined in section 12.
Occurrence semantics, active-fault repetition and responses are agreed in section 13.

## 9. Agreed wire envelope

```text
COBS(
  version:u8
  message_type:u8
  flags:u8
  session_id:u64
  sequence:u32
  payload_length:u8
  payload:0..96 bytes
  crc:u16
) + 0x00
```

- Little-endian integers; explicit serialization.
- CRC-16/CCITT-FALSE over decoded header and payload: polynomial `0x1021`, initial
  value `0xFFFF`, no reflection, final XOR zero. ASCII `123456789` check: `0x29B1`.
- Maximum decoded packet: 114 bytes.
- Maximum encoded frame including delimiter: 116 bytes.
- Fixed buffers and bounded parser work.
- Reject malformed frames, unsupported versions, nonzero reserved flags and
  incorrect payload lengths.
- Discard oversized input through the next delimiter.

Initial USB serial profile: 115200 baud, 8N1. Signed integers use two's complement;
serialize individual fields rather than native structs. Version is 1, flags zero.
Reject unexpected lengths, reserved flag bits and invalid enum values.

Expiry occurs at elapsed time greater than or equal to the lifetime. Process safety
deadlines before commands at the same boundary. Use wrap-safe onboard elapsed-time
arithmetic. Fixed bounded queues prioritize stop handling and safety/status; coalesce
continuous commands and telemetry. Numerical parser/control-loop execution budgets
must be measured and documented before hardware enablement.

CRC detects accidental corruption, not malicious messages. Authentication and trust boundaries must be resolved before
exposing control links to untrusted networks.

## 10. Required verification

The simulator and shared C++/C# test vectors must verify:

- Every permitted and rejected safety transition.
- Exact 500 ms expiry behavior and timer wraparound.
- Continued heartbeats after movement updates stop.
- Delayed, duplicate, reordered and previous-session commands.
- Source changes with displaced controls.
- Neutral qualification interrupted before arming.
- Replayed arm and clear requests after a later stop.
- Emergency-stop priority and rejection of remote reset.
- Malformed frames and bounded parser resource use.

Additional verification must cover session handshake retries and replacement,
boot-counter torn writes, counter exhaustion, ticket eviction and exact expiry,
one-way link failure, generation changes, the first post-arm command deadline,
result-cache eviction/conflicting request IDs, capability stability and stale status.

Buoyancy/depth verification must cover independent stream expiry, heartbeats not
renewing buoyancy targets, fallback to neutral with fresh movement, safety overrides,
unknown estimates and failed/unavailable recovery. Test delayed/duplicate buoyancy
commands without extending authorization. Exercise negative requests and downward
dive effort at the 5 m limit, intervention-margin behavior, sensor loss, and continued
availability of disarm/emergency stop. Unresolved sensor and recovery policies must
be decided before their expected test outcomes can be fixed.

## 11. Remaining implementation prerequisites

- Validate ballast mechanism, neutral states, buoyancy adjustment limits and completion criteria.
- Select/calibrate sensors and stale thresholds; validate depth intervention margin and release hysteresis.
- Design persistent boot-counter and emergency-latch storage, torn-write recovery and wear handling.
- Verify iBus input-loss detection, reversal behavior and source-transition safety.
- Measure parser, safety-loop and output latency budgets; verify serial throughput and AVR RAM use.
- Finalize outstanding wire semantics below before producing authoritative golden vectors.

The user separately authorized the C# codec, desktop session handling, simulated
peer, serial transport, UI integration, input and YAML configuration. Actual actuator
implementation and powered tests are outside that scope.

## 12. Approved numeric registry

These numeric assignments, coordinate conventions and field rules are approved.
Changes require explicit revision; shared conformance vectors must use these values.

| Field | Agreed values |
|---|---|
| Safety state | 0 disarmed; 1 armed; 2 command-loss failsafe; 3 emergency-stop latched; 4 fault failsafe |
| Movement owner | 0 none; 1 PC; 2 FlySky; 3 autonomy |
| Control mode | 0 none; 1 effort |
| Buoyancy action | 0 none; 1 seek neutral; 2 seek positive; 3 seek negative; 4 emergency evacuation |
| Action status | 0 inactive; 1 running; 2 completed; 3 failed; 4 unavailable |
| Estimated buoyancy | 0 unknown; 1 neutral; 2 positive; 3 negative |
| Authorized normal target | 0 none; 1 neutral; 2 positive; 3 negative |
| Fault severity | 0 information; 1 warning; 2 blocking; 3 critical |
| Fault active | 0 cleared; 1 active |

The target byte in `BUOYANCY_COMMAND` uses its agreed 0/1/2 values; the status field
adds a none value and therefore uses a different enum. Keep their types distinct.

Agreed bit assignments (unlisted bits reserved):

| Field | Bits |
|---|---|
| Arming blockers | 0 no owner; 1 stale/unavailable owner; 2 non-neutral input; 3 neutral hold incomplete; 4 command-loss latch; 5 emergency latch; 6 blocking fault; 7 required subsystem unavailable; 8 arm action must reset; 9 fault-failsafe latch |
| Supported systems | 0 emergency evacuation; 1 neutral seeking; 2 positive seeking; 3 negative seeking; 4 object reporting |
| Supported telemetry | Bit `measurement_id - 1` for IDs 1 through 8 |
| Active restrictions | 0 depth limit active; 1 depth unavailable; 2 downward control inhibited |
| Telemetry quality | 0 valid; 1 simulated; 2 estimated; 3 sensor fault |

Senders set reserved bits zero. Agreed receiver behavior: unknown blocker or
restriction bits prevent presenting ready-to-arm; unknown capability bits are
ignored; reserved telemetry-quality bits make the record unusable for control.

| Safety cause | Agreed value |
|---|---|
| Startup | 0 |
| Explicit arm | 1 |
| Explicit disarm | 2 |
| Owner selector changed | 3 |
| Movement expired | 4 |
| Active PC session revoked | 5 |
| Emergency stop asserted | 6 |
| Command-loss failsafe cleared | 7 |
| Local emergency-stop reset | 8 |
| Depth unavailable/stale | 9 |
| Other blocking fault | 10 |
| Fault failsafe cleared | 11 |

Repeated events leaving the state unchanged do not overwrite its entry cause.
Agreed fault codes: 1 depth unavailable; 2 buoyancy controller unavailable;
3 buoyancy controller failed; 4 emergency evacuation failed; 5 identity storage
failed; 6 control-loop timing failed; 7 emergency-latch storage failed.

Agreed field rules:

- Movement age saturates at 65534 ms; 65535 means no accepted command in the current
  ownership/control generation.
- Status revision wraps as a change indicator. Increment on discrete state, owner,
  mode, generation, blocker, buoyancy action/status/estimate/authorized-target,
  restriction or cause changes, not ticking ages or neutral-hold progress.
- Control generation starts at one. Fault-failsafe entry/clear advances generation
  and invalidates tickets. `CLEAR_FAILSAFE` handles either non-emergency failsafe.
- Agreed superseding rule: fault failsafe has reporting/action priority over
  command-loss failsafe; retain both causes and require resolved blockers before clear.
- Heading is 0..35999 centidegrees clockwise from magnetic north; pitch is positive
  nose-up, roll positive starboard-down, each -18000..18000. Conversion/calibration
  must occur onboard; invalid/unknown reference is not a valid measurement.

## 13. Approved gap resolutions

These resolutions supersede the review gaps in draft 0.4. Their behavioral rules
and the numeric registry in section 12 are approved.

### 13.1 Depth restriction and renewed descent

On restriction activation, cancel negative-buoyancy authorization, inhibit downward
dive effort and seek neutral. Advance control generation once and invalidate
outstanding tickets. Permitted movement intent may remain only until its existing
deadline; do not grant an extra grace interval.

After depth falls below the validated release threshold, require fresh owner input
requesting neutral buoyancy and no downward dive effort for one continuous second.
Only then may downward control become eligible. Advance generation and issue new
tickets on release. Negative requests during inhibition cannot become queued actions.
Only newly sampled requests using post-release tickets may authorize negative
seeking. A held-down control cannot automatically resume descent.

While downward control is inhibited, reject any `MOVEMENT_COMMAND` with `dive > 0`
as a whole. Do not clamp the dive axis or apply its forward/yaw components. The
rejected packet does not update accepted intent, accepted movement sequence or
movement freshness. Existing permitted intent retains only its existing deadline.
The ground station must send a new command with zero or upward dive effort to
continue permitted movement. If valid movement updates cease, the normal deadline
still enters command-loss failsafe; rejection grants no extra grace interval.

Reject `BUOYANCY_COMMAND` with target negative while the restriction inhibits
negative seeking. Do not apply or queue the target, update accepted buoyancy
sequence, or renew buoyancy freshness. These continuous commands retain their
no-per-command-acknowledgement policy; current restriction status explains inhibition.
Zero/upward movement and neutral/positive buoyancy requests remain subject to all
other state, ownership, ticket, generation and capability checks.

The release threshold/hysteresis remains a validated vehicle-profile prerequisite.
No normal buoyancy target survives disarm, source change, session replacement or
safety recovery. Before the first normal target after arming, seek neutral.

### 13.2 Emergency persistence

Stop thrust and begin the emergency procedure immediately; do not wait for EEPROM.
Use redundant, checksummed persistent records. Interrupted or ambiguous state means
emergency stop remains latched. Storage failure blocks arming and requires service.

Software cannot guarantee persistence when power disappears before any write reaches
storage. An independently latched emergency-stop mechanism is the approved hardware
direction for powered operation. Until available and validated, document that
limitation; EEPROM alone does not guarantee persistence through all interruptions.
Physical circuit, storage transaction design and local reset sequence remain
implementation prerequisites.

### 13.3 Stable vehicle identity

Provision a stable 128-bit vehicle ID separately from routinely edited configuration.
`HELLO_REPLY` includes it after selected version (33-byte payload). The PC compares
it with the explicitly selected vehicle profile before confirming. Changing vehicles
requires explicit operator selection. Boot/session counters still distinguish
reboots/connections. Vehicle identity is identification, not authentication.
Missing or corrupt identity blocks BCP arming until repaired.

### 13.4 Handshake sequence and retries

- `HELLO` uses the zero session ID and sequence zero.
- `HELLO_REPLY` uses the assigned session and starts the vehicle sequence at one.
- `SESSION_CONFIRM` starts the PC sequence at one in that session.
- Later transmissions increase their sender sequence, including retries.
- Retries retain nonce and assigned session; the initial `HELLO` exception remains
  sequence zero until a session ID has been assigned to the PC.
- Repeated valid confirmations return `SESSION_READY` without resetting counters,
  tickets, qualification or state.
- Retain only one pending handshake. Unrelated requests cannot replace it repeatedly
  or extend its two-second deadline.

Approved bounded-retention rule, replacing the earlier unbounded nonce-history rule:

1. PC generates a random 128-bit nonce for each new connection attempt; retries
   retain that nonce.
2. Vehicle retains only the pending and current established session nonces.
3. Matching retries return the existing handshake response without extending
   deadlines or resetting session state.
4. A delayed historical `HELLO` may create a pending handshake only when replacement
   is otherwise permitted. It receives a new, never-reused session ID. Historical
   confirmations and commands cannot complete or control that new session.
5. Expired/abandoned pending handshakes consume their session IDs; never recycle them.
6. Creating a pending replacement does not revoke a valid established session.
   Replace it only after confirmation and only if replacement is permitted then.

Session IDs provide replay separation; nonces correlate connection attempts.
Neither authenticates a peer. Historical nonces need not be stored indefinitely.

### 13.5 Fault responses

| Fault | Approved response |
|---|---|
| Depth unavailable/stale | Block arming; while armed enter fault failsafe, stop thrust and attempt neutral |
| Required buoyancy controller unavailable/failed | Block arming; while armed enter fault failsafe; report recovery unavailable when necessary |
| Emergency evacuation failed | Keep emergency stop latched and thrust off; report failure; never automatically rearm |
| Identity storage failed | Reject new sessions lacking valid identity; active PC control enters failsafe |
| Emergency-latch storage failed | Block arming; preserve active emergency response; service required |
| Control-loop timing failure | Stop thrust; enter fault failsafe; attempt buoyancy recovery only if dependable execution remains possible |

Retain all active fault causes even when a single state has display priority.
Emergency stop outranks fault failsafe, which outranks command-loss failsafe.
Watchdog/reset and independent output behavior require implementation review: a
stalled processor cannot reliably execute software recovery.

### 13.6 Heartbeat, fault events and unknown data

- Send heartbeats once per second each direction. Diagnostic only; no control
  deadline depends on them.
- Send faults on change and repeat all active faults once per second.
- Occurrence counters start at zero each boot, increment on inactive-to-active
  transitions and saturate at `UINT32_MAX`. Session identity distinguishes boots.
- Reject an entire telemetry packet with inconsistent length or duplicate IDs.
- Skip unknown measurement IDs; never use them in control calculations.
- Unknown status enums display as unknown/unsupported and inhibit the PC Arm action;
  disarm and emergency stop remain accessible.
- Display unknown fault codes numerically with severity; do not invent recovery behavior.

The onboard controller remains authoritative independently of desktop interpretation.

### 13.7 Ending safety buoyancy actions

Achieving a target may stop actuator adjustment but never clears a safety latch.
Command-loss or fault-failsafe clear cancels the recovery target and enters routine
`DISARMED` behavior. Emergency evacuation cannot be cancelled by remote disarm or
failsafe clear. Local emergency reset requires the procedure ended and reset
conditions satisfied. Failed or unavailable neutral recovery is never completed.

## 14. Conformance review and hardware prerequisites

The numeric registry, coordinate conventions and bounded nonce-retention rules are
approved. The [conformance plan](CONFORMANCE.md) supplies a five-state transition
matrix, deterministic scenarios and [shared wire vectors](test-vectors/wire-v1.json)
for all assigned message types, signed boundaries and malformed input.
The vectors were independently decoded and CRC-checked during preparation. C# codec
tests now execute the shared vectors. Stateful simulator and end-to-end session
scenarios are also tested separately; hardware conformance remains pending.
The shared file now references draft 0.9 and contains 63 vectors: the original 37
plus 26 object-reporting cases. Both C# and portable C++ codecs execute them.
Existing message layouts remain unchanged by the additive object extension.
Review findings in the plan identify remaining behavioral clarifications rather
than silently selecting new acceptance policies.
Any inconsistency found during this review must be recorded and resolved explicitly,
not silently changed by codec implementations.

Hardware prerequisites in section 11 remain mandatory before enabling affected
functions. Design approval is not a claim of validated neutral-buoyancy recovery,
emergency evacuation or depth-limit effectiveness. No commit, push, firmware
implementation or powered test is authorized by this document.

## 15. Codec implementation clarifications

These clarify interpretation of the existing contract; they introduce no new wire
fields or message IDs. The C# codec implements syntax and serialization, not the
session manager or safety controller.

- Header offsets are version 0, type 1, flags 2, boot counter 3, session counter 7,
  sequence 11, payload length 15 and payload 16. CRC follows the payload, low byte
  first. `TryDecode` consumes exactly one complete delimited frame.
- Nonces and vehicle IDs are opaque 16-byte sequences. Preserve order as transmitted;
  do not apply mixed-endian UUID/`Guid` conversion or infer a UUID subtype.
- The raw frame layer can return an unknown message type for diagnostics. The typed
  message layer rejects it and never invents a request ID or an acknowledgement.
- Validate exact payload length before reading fields, and check telemetry duplicates
  before skipping unknown IDs. Zero-record telemetry is valid. Duplicate unknown IDs
  still reject the packet. At most 11 records are parsed.
- Strictly reject invalid outgoing values, invalid command target enums, out-of-range
  effort and non-boolean FAULT active bytes. Received status enums/masks, fault codes,
  severity and command-result values preserve unknown numeric values for unsupported
  display/diagnostics. Unknown results are never acceptance. Reserved telemetry quality
  bits make a record unusable for control, without discarding other records.
- Received capability masks preserve unknown bits for application-level ignoring;
  conforming senders emit only allocated bits. No received masks bypass onboard policy.
- Ignoring an empty stream delimiter has no state effect. Oversized input is discarded
  through its delimiter; the next complete frame can decode. Buffering is fixed and
  results are consumed by the caller without an unbounded internal message queue.
- Wire decoding does not check current session, tickets, generation, replay, ownership,
  depth inhibition or fault state. Those checks must precede execution in stateful
  handlers. Invalid syntax cannot produce a decoded command. Valid syntax is never
  evidence of fresh input or authority.

The depth-release hold uses both movement and buoyancy freshness independently.
An expired stream, renewed non-neutral buoyancy target, downward dive request,
invalid/stale depth or depth excursion back above the release threshold breaks the
continuous hold. A new eligible hold starts from zero; heartbeats cannot sustain it.
This is a stateful implementation requirement, not a codec decision.

Fault messages carry an explicit severity. The codec does not infer a default from
fault code; fault-specific response remains the table in section 13.5. A controller
profile must assign the reporting severity before emitting a fault.

Counter exhaustion/persistence failure cannot be solved by the stateless codec.
A future manager must stop movement before exhausting the counters needed for
authorization and must not wrap, reuse identity or renew old intent. Persistent
storage failure follows section 13.5. Counter-boundary and failed-storage transitions
remain mandatory simulator/hardware verification before enabling vehicle control.

## 16. Object-observation extension

Authorized for protocol implementation on 2026-11-19. The normative payload and
source semantics are in [the object-reporting extension](BCP_OBJECT_REPORTING_PROPOSAL.md).
`OBJECT_REPORT` is vehicle-to-PC message `0x33`; SupportedSystems bit 4 indicates
implemented reporting. The v1 envelope and 96-byte maximum payload are unchanged.
This is additive: older typed codecs reject the new message and ignore the unknown
capability bit. Existing message payload layouts remain unchanged.

Reports carry up to four observations per source with sensor range, vehicle-datum
range, body-relative bearing/elevation, uncertainty, provenance and acquisition age.
They grant no control authority. Source snapshots replace rather than accumulate;
replayed scans and heartbeats cannot renew their freshness. Clear the cache when
the session ends. The initial desktop limit is sixteen sources.

C++ and C# codecs and shared vectors implement this extension. The simulated peer
publishes synthetic acquisitions over BCP. Sensor hardware, driver integration,
mounting calibration, measured link budgets and powered validation remain deferred.
