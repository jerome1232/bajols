# BAJOLS ground station

A .NET 10/Avalonia desktop ground station for Ubuntu/GNOME and Windows. It includes
BCP encoding, explicit session establishment, validated YAML configuration, a serial
adapter, a simulated peer, keyboard/slider input and a live status dashboard.
It starts disconnected and never automatically connects, clears failsafe or arms.

## Build, test and run

Install a .NET 10 SDK. From this directory:

```sh
dotnet restore Bajols.GroundStation.slnx --locked-mode
dotnet build Bajols.GroundStation.slnx --configuration Release --no-restore
dotnet test Bajols.GroundStation.slnx --configuration Release --no-build --no-restore
dotnet run --project Bajols.GroundStation
```

Run the desktop application in a graphical session. CI builds/tests on Linux and
Windows; headless desktop tests do not need a display server. The Linux serial test
uses a pseudo-terminal, not vehicle hardware; Windows needs a real loopback device
for the equivalent driver test and explicitly skips that Linux-only test.

`global.json` accepts stable .NET 10 feature bands. Pinned package lock files include
Avalonia 12.1.6, System.IO.Ports 10.0.0 and YamlDotNet 18.1.0. Update lock files
intentionally when changing dependencies.

## Operate the simulator

1. Press **Connect**. The bundled configuration selects the simulator and checks its
   vehicle ID. Status changes from unconfirmed to confirmed; telemetry is marked SIM.
2. Hold **Space** with forward/yaw/dive centered. After one second of fresh neutral
   input, press **Arm** while continuing to hold Space.
3. Use sliders or W/S (forward/reverse), A/D (port/starboard), R/F (down/up). Keyboard
   effort is 50%; sliders cover the full normalized range. Select a buoyancy target
   independently. These are high-level effort/target commands, never actuator outputs.
4. Releasing Space or the input-enable button, losing window focus or disconnecting
   clears local input. Without fresh movement the peer enters command-loss failsafe.
5. Restore held neutral input, press **Clear failsafe**, complete qualification again,
   then explicitly arm. Resumed communication alone never resumes control.

The mouse input-enable button is another hold control; Space allows clicking actions
while keeping enable held. The input snapshot expires after 200 ms if the UI stops
refreshing it, independently of the background communication loop.

Simulation controls inject movement loss, the 5 m depth condition and depth-sensor
loss. **Local emergency reset** represents an action at the simulated vehicle and
is only available in simulation. No remote BCP reset message is sent. The model
preserves emergency state across link reconnects and model reboots during the app
session. It has no durable emergency-latch storage across application restarts.

## Visual attitude

The side silhouette pitches nose-up/down and the top silhouette turns against a
compass rose. They use received pitch/heading telemetry, including for real peers;
requested joystick values alone never masquerade as measured attitude. Missing,
invalid or stale data hides the silhouettes and marks attitude unconfirmed.
Heading animation follows the shortest path across north instead of spinning backward.

Simulation now includes deliberately simple kinematics: yaw changes heading only
while thrust is applied, with turn rate proportional to thrust magnitude. Dive
effort approaches a pitch angle, forward effort and pitch affect depth, and buoyancy
actions add illustrative ascent/descent. Hold Space, arm, then use W/S, A/D and R/F
to see the response. These rates are visual fixtures, not physical calibration.

## Status and command behavior

- Connection, session and status freshness are distinct. Missing or stale status is
  UNKNOWN/unconfirmed, never inferred disarmed. Status expires after one second.
- Telemetry checks validity, quality flags, sample age and local elapsed time. Missing,
  invalid or old measurements display a dash. Simulated/estimated values are labeled.
- Arm requires confirmed compatible capabilities, ownership, a fresh ticket, fresh
  enabled operator input and zero onboard arming blockers.
- Disarm and emergency stop remain available to an established configured session
  even if status/tickets are stale. A lost physical connection cannot deliver them.
- Request results show onboard acceptance/rejection; sending a request is not proof
  that a physical action completed. There are no automatic command retries.
- A wrong vehicle ID, unsupported status or mismatched capabilities cannot authorize
  movement. Changed capabilities within a session terminate the connection.

## YAML configuration

The bundled `Bajols.GroundStation/station.yaml` is copied beside the executable.
Override it explicitly:

```sh
dotnet run --project Bajols.GroundStation -- --config /path/to/station.yaml
```

Schema 1 selects `transport: simulator` or `transport: serial`. Serial also requires
an explicit port (such as `/dev/ttyACM0` or `COM3`) and the actual provisioned
32-hex-character vehicle ID; the simulator ID cannot be used for serial. Baud is
fixed at the BCP v1 115200/8N1 profile. This adapter requires BCP firmware/another BCP
peer; the existing experimental Arduino sketch does not implement BCP.

Unknown/duplicate YAML keys, oversized files, unsupported schemas, missing/invalid
identity, nonfinite input settings and out-of-range timeouts/buffers are rejected.
Configuration errors disable connection and appear in the dashboard. There is no
silent fallback to simulation after a malformed configuration. Restart to reload.
Desktop configuration does not override onboard safety thresholds or the depth limit.

## Project boundaries

| Project | Responsibility |
|---|---|
| Bajols.Protocol | Transport-independent messages, CRC/COBS codec and bounded decoder; no other project/package dependencies |
| Bajols.Core | Domain connection state and normalized movement intent |
| Bajols.Communication | Client session/authorization, bounded serial and simulator wire transports |
| Bajols.Input | Fresh input snapshots, hold gating, normalization and expiry |
| Bajols.Configuration | Strict human-editable YAML schema and validation |
| Bajols.Simulator | Deterministic reference peer/safety state model, independent of Avalonia and serial APIs |
| Bajols.GroundStation | Avalonia views, observable view model and explicit operator commands |

The simulator and ground station communicate through encoded BCP frames; neither
gets a codec bypass for command handling. Simulator queues hold at most 16 whole
frames in each direction. Its outbound congestion policy drops frames rather than
building a backlog. Serial reads use a caller buffer capped to 256-byte chunks;
writes allow at most one 116-byte frame. Driver operations use finite timeouts off
the UI thread; cancellation closes the serial connection. Actual driver open/close
latency remains OS/device-dependent and requires hardware testing.

## Simulator limitations and next hardware work

The simulated vehicle models authorization, tickets, replay/idempotency, safety
latches and illustrative motion, not hydrodynamics. Depth can also be overridden by
fault injection, buoyancy estimate stays unknown, and evacuation completion is simulated. Its 4.5 m
intervention / 4 m release profile is a test fixture, not approved hardware calibration.
FlySky ownership can be selected in model tests, but no physical iBus adapter or
FlySky arming switch is implemented in this desktop simulator. Gamepads are deferred;
keyboard and on-screen inputs are implemented.

Onboard firmware still owns real actuator control and safety. Persistent boot/latch
storage, ballast hardware, sensors, profile validation and powered testing remain
separate work. Simulated behavior and driver-loopback tests do not validate them.
