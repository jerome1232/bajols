# Bajols.Protocol

Transport-independent BCP v1 codec, with no UI, transport, serial or domain dependencies.
The contract is in [the BCP draft](../../protocol/BCP_V1_DRAFT.md), with shared
[wire vectors](../../protocol/test-vectors/wire-v1.json).

## API layers

- `FrameCodec`: raw envelope serialization, little-endian fields, COBS and
  CRC-16/CCITT-FALSE. Accepts unknown message types as raw frames, never actions.
- `BcpCodec`: typed payloads for all 18 assigned messages; checks direction, exact
  lengths, effort ranges, buoyancy targets, boolean representation and telemetry
  count/duplicates. Unknown message types are rejected here.
- `BcpStreamDecoder`: feed receive bytes through `Push`, consume each result
  immediately, and call `Reset` on disconnect. One 116-byte buffer, no message queue;
  oversized input is discarded through the next delimiter. Empty delimiters are ignored.

```csharp
var packet = new BcpPacket(new SessionId(1, 2), 7,
    new MovementCommand(TicketId: 9, ControlGeneration: 3, Forward: 0, Yaw: 0, Dive: 0));
Span<byte> wire = stackalloc byte[FrameCodec.MaxWireSize];
if (BcpCodec.TryEncode(packet, WireDirection.PcToVehicle, wire, out int length, out var error))
{
    // wire[..length] includes the trailing zero delimiter.
    // This example only encodes bytes; it does not establish a valid session/ticket.
}
```

## Validation and ownership

`TryDecode` accepts exactly one complete frame, including its delimiter. Failure
returns a null packet and a `CodecError`; encoding failure reports zero bytes and
does not modify the destination. Programmer argument errors such as a null packet
throw. Decoded raw payloads own a bounded copy independent of input buffers; decoded
telemetry records are read-only. Encoding callers must not mutate message collections
during a call. Stream decoders are not thread safe.

Opaque 128-bit IDs preserve all 16 bytes without `Guid` field reordering. Received
status enums and masks preserve unknown values; `HasUnknownControlValues` identifies
values the UI must treat as unsupported, never ready-to-arm. Unknown fault codes and
severities likewise remain numeric. Unknown telemetry IDs are skipped after packet
length/duplicate validation. Reserved quality bits are retained and exposed through
`HasKnownQualityFlags`; callers must not use such records for control.

Outgoing typed messages reject unallocated enums/flag bits instead of generating
future protocol extensions. Incoming capability masks retain unknown bits for the
application to ignore. Received unknown command-result values are preserved for
diagnostics, never interpreted as acceptance. The lower raw frame layer deliberately
does not enforce these typed payload rules.

Successful decoding says nothing about session identity, command freshness, ticket
validity, sequence replay, ownership, depth or safety state. These are future stateful
handler responsibilities. Telemetry validity, provenance, age and physical ranges
also need application/controller policy; receiving bytes never refreshes a sample.

## Verification

The xUnit v3 test project links the shared JSON fixtures. It independently constructs
messages from fixture fields for encoding, verifies decoded fields, exercises every
rejection vector and tests malformed frames and stream recovery. See
`../tests/README.md` for commands. No third-party packages are required by this library.
