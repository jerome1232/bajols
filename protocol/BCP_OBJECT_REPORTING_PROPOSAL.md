# BCP v1 object-observation extension

Status: authorized for implementation, 2026-11-19; part of draft 0.9.
The v1 envelope and existing message layouts remain unchanged. C++/C# codecs and
shared vectors now cover this extension. Simulation observations travel through
encoded BCP frames and remain explicitly synthetic; no physical sensor is claimed.
The historical filename is retained to preserve links.

## Verified checkout and user requirements

The current `TelemetryProof/output.cpp` registers iBus speed, RPM, pressure,
voltage and heading. Its header calls several values fake sensor data. No object,
sonar, range or bearing acquisition/reporting implementation was found in that
sketch. The debug column headed `Object` in `input.cpp` labels channel values;
it is not an obstacle report. Distance-sensor choice/driver remains deferred.

The user requested distance **and bearing**, with a six-metre display range,
flashing current detections and greater visual prominence at shorter range.
Firmware should know each sensor's location and approximate body-relative object
position. These requirements do not establish sensor model, actual field of view,
physical clearance guarantees, tracking, fusion, braking or automatic avoidance.

## Wire contract

Report bounded **per-sensor observation snapshots**, not persistent world objects.
Firmware owns sensor mounting transforms and estimation. Ground-station code
consumes already-normalized observations; it does not guess sensor placement from
an index or commanded joystick values. A distance-only beam can produce an
estimated bearing along its calibrated axis, with beam uncertainty marked.
It must not be reported as an exact measured angle.

Use a new vehicle-to-PC `OBJECT_REPORT` message rather than overloading scalar
`TELEMETRY`. Assigned ID **0x33**, and `SupportedSystems` bit **4** for
implemented object reporting. The
capability bit is independent of movement, arming and buoyancy support; reporting
must not advertise planned hardware as implemented. Older typed codecs reject the
unknown message; existing receivers already ignore unknown capability bits.
The sender mask and field documentation include this capability.

Keep the envelope at version 1 and maximum payload 96 bytes. Packet:

| Header payload field | Type | Meaning |
|---|---|---|
| format_revision | u8 | 1 for this record layout |
| sensor_id | u8 | Configured source 1–254; 0/255 reserved |
| scan_sequence | u32 | Monotonic new acquisition/status-event counter per source per boot |
| sample_age_ms | u16 | Age of oldest constituent acquisition at send; 65535 unknown, 65534 saturated |
| sensor_state | u8 | 0 unavailable; 1 acquisition ready; 2 fault; 3 uncalibrated |
| report_flags | u8 | Bit 0 truncated; bit 1 simulated; others reserved |
| observation_count | u8 | 0–4; complete replacement snapshot for this source |

Each observation is **21 bytes**, in this order:

| Record field | Type | Meaning |
|---|---|---|
| observation_id | u16 | Nonzero identifier unique within this snapshot only |
| sensor_range_mm | u32 | Actual radial range from the reporting sensor's reference face |
| vehicle_range_mm | u32 | Estimated range from the fixed vehicle datum after mounting transform |
| bearing_cdeg | i16 | Vehicle-relative azimuth: 0 ahead, +9000 starboard, -9000 port |
| elevation_cdeg | i16 | Positive up; -9000…+9000; 32767 explicitly unknown |
| range_half_width_mm | u16 | Estimated vehicle-range uncertainty; 65535 unknown |
| bearing_half_width_cdeg | u16 | Azimuth uncertainty, 0…18000; 65535 unknown |
| elevation_half_width_cdeg | u16 | Elevation uncertainty, 0…9000; 65535 unknown |
| quality_flags | u8 | Valid/simulated/estimated/sensor-fault bits as in telemetry |

Header is 11 bytes; four records give **95 bytes**. Exact payload length must be
`11 + 21 * observation_count`. No fragmentation or unbounded point cloud is needed.
`u32` ranges reserve 0xFFFFFFFF for unknown; valid records require real ranges and
bearing within -18000…17999. Zero distance is a valid near/contact reading, never
an unavailable sentinel. Invalid-quality records are not rendered as objects.
Unknown quality/report bits, state or revision reject the entire typed report;
conforming senders emit only allocated values. Rejected frames cannot establish
fresh coverage or valid objects. Duplicated observation IDs reject the whole report.

A ready zero-count, non-truncated report means **no reported returns from that
source in that acquisition**, not a clear path. Unavailable/fault/uncalibrated
reports contain zero observations and cannot set truncated. Simulated provenance
is carried on empty/status reports too; record simulation flags must agree.

When more than four returns exist, send the nearest four by sensor range, break
ties deterministically, and set truncated. This remains a replacement snapshot,
not a partial list to accumulate. Report that additional returns were omitted;
absence of an omitted marker is not evidence that an object disappeared.

## Body coordinates and onboard mounting knowledge

Select a fixed mechanical datum in the vehicle profile, independent of changing
ballast/centre of gravity. Body axes are **forward, starboard, down**. Reported
azimuth is clockwise toward starboard looking down from above; elevation is
positive up. These are relative to the body, not magnetic/world bearings.
Ground-station heading/pitch rotates observations into the fixed display compass.
No world-fixed object map or motion-compensated tracking is implied.

Each configured sensor needs a stable ID, position `[forward, starboard, down]`
in mm, yaw/pitch/roll mounting orientation, calibrated valid range, acquisition
stale threshold, and angular/range uncertainty or an explicit unknown value.
Yaw is starboard-positive; mounting pitch is nose-up-positive; roll is
starboard-down-positive. The mounting transform is
`body_point = mount_translation + Rz(yaw) * Ry(pitch) * Rx(roll) * sensor_point`
using the same forward/starboard/down axes in each sensor frame. Raw sensor
azimuth/elevation must first be converted from that device's own convention.

Calculate vehicle range, azimuth and elevation from the transformed point:

- `range = norm(body_point)`.
- `bearing = atan2(starboard, forward)`.
- `elevation = atan2(-down, hypot(forward, starboard))`.

For example, a sensor at `[100, 50, 0]` mm, aligned ahead, measuring a return
300 mm ahead gives body point `[400, 50, 0]`, approximately 403 mm vehicle range
and +7.13 degrees bearing. The raw sensor range remains 300 mm. These example
values are not mounting calibration for the actual vehicle.

Keeping **both distances** is necessary: a body-datum range is not the same as
sensor clearance, especially for a sensor mounted near the bow. The initial
six-metre display threshold applies to sensor range, not range from the vehicle centre.
No claim of minimum hull clearance is possible without an appropriate calibrated
hull/sensor geometry model. The current rendered submarine has no physical scale.

Mounting/calibration and source IDs must remain fixed during a session; changing
them requires disarmed reconfiguration and a new session. Human-editable YAML
can author the profile, but wire packets are binary and onboard storage/parsing
must remain bounded. Recommend at most 16 configured ranging sources initially,
so a desktop cache has at most 64 current records. Actual hardware choice,
underwater behavior, temperature/medium corrections, FoV and blind zones remain
engineering prerequisites. Missing mounting calibration reports uncalibrated;
it must not silently default all sensors to the vehicle origin or a zero-error ray.

## Snapshot ordering, freshness and transport

Envelope session/sequence checks still apply. A new source acquisition or explicit
source-state transition advances `scan_sequence`; retransmission does not. A
repeated sequence must not renew freshness, restore old observations or replace
newer data. Repeated sequences are ignored even if their advertised age increases; changed
observation content under the same sequence is inconsistent and also ignored.
Source counters never wrap; reserve the final sequence for an unavailable state,
then require source/service recovery with a nonreused boot identity.

For a newer snapshot, replace only that sensor's cached observations. State loss
invalidates its prior returns immediately. Other source snapshots remain independent.
Clear every observation on disconnect/session replacement; reconnect cannot revive
an earlier snapshot. No cross-sensor fusion or stable track IDs are claimed.

Displayed age includes advertised onboard age plus monotonic elapsed time since
receipt. Unknown age is not fresh. Actual stale limits are validated per-source
profile values; the existing UI preview uses its telemetry freshness setting.
Repeated snapshots, heartbeats and unrelated telemetry cannot keep a return alive.
When data expires show unavailable/stale, not zero objects or a clear-path message.

A provisional publication target is up to 5 Hz per sensor, only when meaningful
new samples exist and within a validated aggregate link budget. Actual source
count/cadence may require lower rates. Coalesce unsent snapshots by **session and
sensor ID**, never just message type. Round-robin scheduling prevents starvation;
stop/safety/control traffic keeps priority. Do not queue old scans to replay later.
The Mega parser, source cache, execution cost and 115200-baud capacity need measured
validation before enabling a hardware driver. This extension does not alter any
arming rule, command lease, emergency response or actuator arbitration.

## Display and simulation behavior

The desktop consumes session-checked BCP snapshots, retaining source counters
without refreshing repeated scans. The display remains a schematic forward view,
not a calibrated map; it currently uses sensor range and bearing and does not draw
elevation/uncertainty volumes or persistent tracks. Invalid/fault-quality records
are omitted. The renderer projects markers relative to the nose and vehicle
attitude and keeps warnings on screen at steep pitch.

Simulator controls can inject a body-relative test return or place stationary
world points at the selected current range/bearing. At most 32 points are retained.
The vehicle's illustrative motion changes their relative range/bearing/elevation.
The synthetic sensor is at the vehicle datum and reports a 6000 mm forward
hemisphere at 5 Hz, nearest four returns with truncation. This is a test fixture,
not a chosen physical field of view. Clearing removes placed points and the test
return. Source reports still pass through both codecs and session handling.

A return within the six-metre sensor-range window flashes bright/dim at 1.5 Hz.
Its base marker stays visible while size, opacity, stroke and amber-to-red color
increase toward zero range. No audible alarm or automatic control action is added.
Physical sensor driver integration and calibration remain deferred.

## Conformance coverage and remaining hardware work

- All four cardinal bearings, angular wrap, pitch/elevation conventions and offset mounts.
- Distance-only broad beams produce estimated bearings with uncertainty, not invented precision.
- Missing calibration, faults, no-return and unknown-age reports remain distinguishable.
- Per-source complete replacement, independent sources, truncation and nearest-four selection.
- Duplicate IDs, invalid coordinates/count/length, unknown revision and reserved bits.
- Old/repeated scan sequences, changed content at the same sequence, previous-session traffic.
- Exact freshness expiry and age accumulation; no renewal from heartbeat or retransmission.
- Sensor range versus datum range, inclusive 0/6000 mm display boundaries and farther returns.
- Mixed real/simulated provenance and unknown uncertainty/elevation handling.
- Queue coalescing/fairness, overload priority, counter exhaustion and AVR memory/timing bounds.

Shared wire vectors cover empty/status, one/four returns, angular/range boundaries,
unknown sentinels, truncation, provenance, invalid fields/count/length/direction and
duplicate IDs. Desktop tests cover source replacement, replay, exact age expiry,
source bounds, disconnect clearing, and encoded simulated placement/motion.
Firmware endpoint tests cover coalescing by session and sensor ID.

The scenario list above also specifies future hardware/driver checks; passing
codec tests does not validate physical bearing estimates, mounting transforms,
acquisition timing, underwater behavior or aggregate publication budgets.
Sensor-specific calibration, cadence and stale limits remain hardware/profile work.

The object display retains the latest source snapshot within six metres when its
acquisition expires. Historical markers stop flashing and fade to grey/white over
one second after expiry, with a stale label. Their positions are last reported
body-relative positions, not motion-compensated tracks. New source snapshots
replace them (including empty/fault snapshots); disconnect clears them. This is a
presentation change and does not renew freshness or change the BCP wire layout.

The six-metre display/simulator limit provisionally covers the anticipated 3–6 m
sensor class. It is not a validated physical detection range or selected hardware
specification. BCP ranges remain unsigned millimetres; no wire change is needed.

The schematic forward guide ends at one metre. Projection uses 3.4 model units per
metre and clamps distant annotations to the scene edge, without changing reported
millimetres or warning thresholds. This visual scaling is not a calibrated sensor
mounting transform, field of view or physical vehicle scale. The dashboard depth
gauge and status-progress presentation are described in the
[ground-station guide](../groundstation/README.md); none changes this wire contract.
