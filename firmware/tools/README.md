# Initial identity provisioning — offline tooling

`provision_identity.py` prepares and checks a review bundle. It has no serial,
programmer or device-writing capability. It does not run `avrdude`, flash firmware,
change the ground-station configuration or obtain a dump from the board.

This tool is for a **new, never-used vehicle identity**. It refuses a target window
containing any non-erased byte, has no force option and never overwrites an output
directory. It is not a service-recovery tool: erasing a used journal and starting
its boot counter over can reuse prior session identities. Maintain the provisioned
ID in the vehicle registry; supplied IDs must be unique and never recycled.

## Prepare for review

Obtain a fresh, complete 4096-byte raw EEPROM dump using a separately authorized
service procedure. Stop the monitoring runtime throughout provisioning/readback;
otherwise its deliberate boot-counter updates will invalidate initial verification.
Select an exclusive 192-byte window and use the same base in the firmware build.
No physical address is selected automatically by this utility.

```sh
python3 firmware/tools/provision_identity.py prepare \
  --initial-provision \
  --input /tmp/mega-eeprom-before.bin \
  --base 0 \
  --generate-id \
  --output /tmp/bajols-initial-provision
```

Alternatively use `--vehicle-id` with exactly 32 wire-order hex characters for an
explicitly assigned new ID. Zero, erased and bundled simulator IDs are refused.
The generated bundle contains:

- `identity.eep.hex`: sparse Intel HEX for only the selected 192-byte window.
- `manifest.json`: source dump/image/window hashes, ID, base and initial state.
- `REVIEW.txt`: service checks and the ground-station identity setting.

Review the actual vehicle, unique ID, source dump and window before any separate
hardware-writing action. The image contains matching identity records, boot
counter zero and **emergency latched** records; it never initially clears the stop.
Bytes outside the window do not appear in the sparse image. Check programmer
behavior separately; the tool does not assume an external programmer preserves
unmentioned cells. No programmer command or automatic hardware step is included.

## Verify the initial readback

After a separately approved programming action, obtain another full raw dump
before starting/flashing the monitoring runtime. Verify with:

```sh
python3 firmware/tools/provision_identity.py verify \
  --original /tmp/mega-eeprom-before.bin \
  --readback /tmp/mega-eeprom-after.bin \
  --manifest /tmp/bajols-initial-provision/manifest.json
```

This requires exact initial records, the reviewed source/hash/base and unchanged
bytes everywhere outside the selected window. It rejects partial writes, modified
IDs, reset state/counters and collateral changes. A successful offline comparison
is not a claim about brownout performance, EEPROM endurance or emergency hardware.

## Actuator-free monitoring firmware

`firmware/Experimental/BcpMonitor` waits for the asynchronous journal to advance
and verify its boot counter before constructing a manager, endpoint and persistence
coordinator in fixed storage. It advertises no movement modes, axes, ballast
systems or telemetry sensors, assigns no movement owner and cannot arm. It can
report BCP status and process explicit stop requests after a verified handshake.
It starts emergency latched after initial provisioning. No remote reset exists.

The default build is read-only and therefore **cannot complete a live handshake**.
A deliberate write-enabled build requires all of:

- `BAJOLS_MONITOR_ENABLE_EEPROM_WRITES`
- `BAJOLS_MONITOR_EEPROM_BASE` matching the reviewed/provisioned window
- `BAJOLS_MONITOR_WRITE_TIMEOUT_MS`, positive and below 32768, validated for the board

Pass these as `compiler.cpp.extra_flags` through Arduino CLI/build configuration.
If such a build is later flashed, boot-counter advances and emergency journal
writes are real. No upload or hardware writes are performed by this repository's
native tests or compile CI. Use the independent emergency latch and validate the
board's endurance/brownout policy before any operational control implementation.
The current fixed pairs are not wear leveling.

For the existing ground station, select `transport: serial`, the correct `port`,
`baud_rate: 115200` and the bundle's `expected_vehicle_id` in a separate vehicle
configuration. The simulator configuration is unchanged. Monitoring can connect
but its unsupported control capabilities keep Arm unavailable. Physical USB reset,
port permission and programmer behavior remain to be checked on actual hardware.
