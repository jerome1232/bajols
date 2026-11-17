# Ground-station verification

From `groundstation/`, run `dotnet test Bajols.GroundStation.slnx --configuration Release`.

- **Bajols.Protocol.Tests** links the 37 shared JSON vectors and verifies wire bytes,
  typed fields, malformed frames, directions, unknown values and bounded stream recovery.
- **Bajols.Integration.Tests** uses deterministic onboard times for the transition
  scenarios, plus end-to-end encoded simulated sessions, wrong identity, command loss,
  stale status, input expiry, YAML validation, and serial bounds/cancellation. Its Linux
  pseudo-terminal test transfers actual driver bytes in both directions; it is skipped
  on Windows, where a hardware loopback test remains required.
- **Bajols.Desktop.Tests** loads the actual XAML headlessly, confirms initial unknown
  state, connects the simulator, qualifies/arms, observes telemetry, releases input,
  clears the presentation on disconnect and verifies emergency state survives reconnect.

These tests do not validate ballast physics, durable emergency/identity storage,
actual FlySky inputs or powered actuator behavior. See `protocol/CONFORMANCE.md` and
`groundstation/README.md` for scope and hardware prerequisites.
