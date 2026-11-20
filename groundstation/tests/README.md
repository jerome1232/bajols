# Ground-station verification

From `groundstation/`, run `dotnet test Bajols.GroundStation.slnx --configuration Release`.

- **Bajols.Protocol.Tests** links the 63 shared JSON vectors and verifies wire bytes,
  typed fields, malformed frames, directions, unknown values and bounded stream recovery.
- **Bajols.Integration.Tests** uses deterministic onboard times for the transition
  scenarios, plus end-to-end encoded simulated sessions, wrong identity, command loss,
  stale status, input expiry, YAML validation, and serial bounds/cancellation. Its Linux
  pseudo-terminal test transfers actual driver bytes in both directions; it is skipped
  on Windows, where a hardware loopback test remains required.
- **Bajols.Desktop.Tests** loads the actual XAML headlessly, confirms initial unknown
  state, connects the simulator, qualifies/arms, observes telemetry, releases input,
  clears the presentation on disconnect and verifies emergency state survives reconnect.
  It also checks the live 16-point heading labels, neutral-check progress binding,
  depth marker/limit colour and invalid-depth handling, simulated object placement,
  expanded object projection, and visible control layout at default/minimum sizes.
  Integration tests exercise exact object expiry/fading, source snapshot replacement,
  bounded placement and world points moving relative to the vehicle under thrust.

These tests do not validate ballast physics, durable emergency/identity storage,
actual FlySky inputs or powered actuator behavior. See [protocol conformance](../../protocol/CONFORMANCE.md) and
[ground-station operation](../README.md) for scope and hardware prerequisites.
