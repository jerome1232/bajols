# Firmware builds and persistent identity provisioning

This guide covers the actuator-disabled BCP firmware on **Arduino Mega 2560** and
**classic ESP32 Dev Module with 4 MB flash**. The wire protocol and 192-byte journal
record format are the same. The storage adapters and programming commands differ.
Other ESP32 boards need their own FQBN, flash-size/partition and USB settings checked;
the legacy FlySky/actuator experiment remains Mega-only.

All commands below are manual examples, run from the repository root in Bash.
Replace serial ports with your actual board. Close the ground station and Serial
Monitor before service operations. Compile CI and offline tests never upload,
provision, erase a board or change ground-station configuration.

## Storage and safety boundary

| Item | Mega 2560 | ESP32 Dev Module |
|---|---|---|
| Physical storage | 4096-byte hardware EEPROM | Dedicated 24 KB flash NVS partition |
| Adapter | `MegaEepromStorage` | `Esp32NvsStorage` with flash worker |
| Journal address | Explicit physical EEPROM base, usually 0 | Logical base **0**, never a flash address |
| Physical partition | Not applicable | `bajols`, offset `0x310000`, size `0x6000` |
| Durable representation | Six 32-byte records in EEPROM | Namespace `journal`, six 32-byte blobs `r0`…`r5` |
| Default build | No EEPROM writes; boot advance refused | No dedicated-NVS initialization or writes; startup refused |
| Writable opt-in | Same BCP build definitions shown below | Same definitions; timeout covers asynchronous record commit |
| Initial image | Sparse `identity.eep.hex` | `bajols-nvs.bin` from Espressif's NVS encoder |

A live BCP session needs provisioned identity **and** a writable build, because
verified startup durably advances the boot counter. Default read-only firmware
cannot complete the handshake. Blank/corrupt/missing records fail closed; there is
no automatic initial identity creation, erase, format-on-error or reset-to-zero.

Initial provisioning is only for a **new, never-used identity**. Both tools refuse
an occupied target and existing output directory. Never erase/reprovision a used
journal to recover a connection: that can reuse session identities. Preserve the
bundle and unique ID in your vehicle records. Recovery of used media is a separate
service procedure, not a flag in these tools.

Initial emergency records are **latched**. Connected status may therefore show an
emergency stop; it is not a provisioning failure. Neither platform advertises
movement modes, axes or ballast support, and neither can arm. There is no remote
emergency reset. Physical reset procedure, real sensor drivers and actuator control
remain a later integration stage.

## Toolchains

The verified versions are Arduino CLI **1.6.0**, Arduino AVR core **1.8.8**,
Arduino-ESP32 **3.3.9**, and Espressif NVS generator package **0.3.0**.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install arduino:avr@1.8.8
arduino-cli core install esp32:esp32@3.3.9 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli board list
```

ESP32 core installation includes compilers for several chips and needs several
GB of disk space. The default BCP sketch uses the local `BajolsProtocol` library
and board core, not the legacy receiver/servo libraries.

In Arduino IDE, add the Espressif board-manager URL above, install the same board
core, and select Mega 2560 or ESP32 Dev Module. The latter requires 4 MB flash and
the supplied sketch `partitions.csv`; do not replace that layout with an unrelated
partition scheme. Use Arduino CLI for the explicit build definitions below so the
exact persistent-storage settings are reproducible. On Windows, serial ports look
like `COM3`, tool paths are under the Arduino15 directory in Local AppData, and
Python venv activation uses `Scripts` instead of `bin`.

## Compile without hardware writes

```sh
arduino-cli compile --fqbn arduino:avr:mega --libraries firmware/Libraries --output-dir /tmp/bajols-build/mega-readonly firmware/Experimental/TelemetryProof
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries firmware/Libraries --output-dir /tmp/bajols-build/esp32-readonly firmware/Experimental/TelemetryProof
```

Both are actuator-disabled. `BAJOLS_BCP_DEMO_TELEMETRY` optionally generates clearly
simulated depth/supply/heading/pitch instead of unavailable sensor reports; it does
not enable control or bypass verified identity. A demo-only build is still read-only.

The Mega legacy experiment can be selected with
`--build-property 'compiler.cpp.extra_flags=-DBAJOLS_ENABLE_LEGACY_CONTROL'` plus
its separately installed libraries. This directly drives hardware, bypasses BCP
arbitration, and is deliberately rejected on ESP32.

### Monitoring-only target

`firmware/Experimental/BcpMonitor` also builds for both boards and uses the same
provisioned records and partition map. To build it, substitute that sketch path
in the commands above. Its writable definitions use `BAJOLS_MONITOR_ENABLE_EEPROM_WRITES`,
`BAJOLS_MONITOR_EEPROM_BASE` and `BAJOLS_MONITOR_WRITE_TIMEOUT_MS`, with the same
board-specific base/timeout rules. It advertises no instrument types and has no
demo-telemetry option. Switching these two firmware targets does not justify
resetting or reprovisioning the existing board identity.

## Mega: initial provisioning

### 1. Load read-only firmware and obtain a stable backup

For a new board on the bench, manually upload the read-only build:

```sh
MEGA_PORT=/dev/ttyACM0
arduino-cli upload --fqbn arduino:avr:mega --port "$MEGA_PORT" --input-dir /tmp/bajols-build/mega-readonly firmware/Experimental/TelemetryProof
```

Do not use a running EEPROM-write-enabled application while preparing/verifying an
initial dump; resets can advance the journal. A read-only avrdude command does not
prevent the application itself from writing after avrdude releases the board.

The following Linux path matches the tested AVR installation. Check the tool path
shown by a verbose Arduino upload if your installation differs.

```sh
AVRDUDE_DIR="$HOME/.arduino15/packages/arduino/tools/avrdude/8.0.0-arduino1"
"$AVRDUDE_DIR/bin/avrdude" -C "$AVRDUDE_DIR/etc/avrdude.conf" -p m2560 -c wiring -P "$MEGA_PORT" -b 115200 -n -A -U eeprom:r:/tmp/mega-before.bin:r
wc -c /tmp/mega-before.bin
```

The dump must be exactly **4096 bytes**. `-n` prevents avrdude writes; `-A` retains
trailing `0xFF` bytes. The selected 192-byte window must be entirely erased.

### 2. Prepare and review the offline image

```sh
python3 firmware/tools/provision_identity.py prepare --initial-provision --input /tmp/mega-before.bin --base 0 --generate-id --output /tmp/mega-provision
```

Review `manifest.json` and `REVIEW.txt`, including vehicle ID, original dump hash
and physical EEPROM base. Use `--vehicle-id` instead of `--generate-id` only for an
explicitly assigned, unique new 32-hex-character ID. No device is accessed here.

### 3. Manually program only the selected EEPROM window

This command writes hardware. It is a separate service action after bundle review.
Do not add a chip erase, force signature override, or an EEPROM-wide erase.

```sh
"$AVRDUDE_DIR/bin/avrdude" -C "$AVRDUDE_DIR/etc/avrdude.conf" -p m2560 -c wiring -P "$MEGA_PORT" -b 115200 -D -A -U eeprom:w:/tmp/mega-provision/identity.eep.hex:i
"$AVRDUDE_DIR/bin/avrdude" -C "$AVRDUDE_DIR/etc/avrdude.conf" -p m2560 -c wiring -P "$MEGA_PORT" -b 115200 -n -A -U eeprom:r:/tmp/mega-after.bin:r
python3 firmware/tools/provision_identity.py verify --original /tmp/mega-before.bin --readback /tmp/mega-after.bin --manifest /tmp/mega-provision/manifest.json
```

Verification checks the exact initial records and every unchanged byte outside
the selected window. Stop if verification fails; do not repeatedly reprovision.

### 4. Compile and manually upload the writable runtime

The following 10 ms byte timeout is a development example requiring validation for
the actual board. The base must match the reviewed provisioned window.

```sh
arduino-cli compile --fqbn arduino:avr:mega --libraries firmware/Libraries --output-dir /tmp/bajols-build/mega-live --build-property 'compiler.cpp.extra_flags=-DBAJOLS_BCP_ENABLE_EEPROM_WRITES -DBAJOLS_BCP_EEPROM_BASE=0 -DBAJOLS_BCP_WRITE_TIMEOUT_MS=10' firmware/Experimental/TelemetryProof
arduino-cli upload --fqbn arduino:avr:mega --port "$MEGA_PORT" --input-dir /tmp/bajols-build/mega-live firmware/Experimental/TelemetryProof
```

Actual boot-counter/latch writes now occur. Do not run initial-image verification
after the runtime has advanced the counter; it is no longer the initial image.

## ESP32: initial provisioning

This procedure assumes an **unencrypted classic ESP32 with 4 MB flash** and the
supplied partition map. Encrypted/secure provisioning, other flash sizes and other
partition layouts need a separate reviewed process; do not adapt offsets by guess.

### 1. Load the read-only firmware and confirm the partition map

```sh
ESP32_PORT=/dev/ttyUSB0
arduino-cli upload --fqbn esp32:esp32:esp32 --port "$ESP32_PORT" --input-dir /tmp/bajols-build/esp32-readonly firmware/Experimental/TelemetryProof
```

The upload uses the sketch's custom partition table. The BCP partition is at
`0x310000` for `0x6000` bytes; default Arduino/Wi-Fi NVS remains separately at
`0x9000`. Never upload a merged image containing a fresh BCP partition over a used
identity, select an erase-all upload option, or issue `erase-flash` as routine setup.
The default firmware does not initialize the dedicated NVS partition, because
NVS recovery itself can mutate flash. Initialization requires writable opt-in.

### 2. Back up the full flash while keeping the application stopped

Use the esptool supplied with the pinned ESP32 core:

```sh
ESPTOOL="$HOME/.arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool"
"$ESPTOOL" --chip esp32 --port "$ESP32_PORT" flash-id
"$ESPTOOL" --chip esp32 --port "$ESP32_PORT" --baud 460800 --after no-reset read-flash 0x0 0x400000 /tmp/esp32-before.bin
wc -c /tmp/esp32-before.bin
```

Confirm physical flash is 4 MB; the backup must contain **4194304 bytes**. Lower
service baud to 115200 if the USB bridge is unreliable. This service baud is
independent of BCP's 115200 baud. `--after no-reset` keeps the application stopped
after the operation. An existing non-erased BCP partition is not an initial target.

### 3. Install the offline NVS encoder and prepare a bundle

```sh
python3 -m venv /tmp/bajols-nvs-tools
. /tmp/bajols-nvs-tools/bin/activate
python3 -m pip install esp-idf-nvs-partition-gen==0.3.0
NVS_GENERATOR="$(python3 -c 'import esp_idf_nvs_partition_gen.nvs_partition_gen as module; print(module.__file__)')"
python3 firmware/tools/provision_esp32.py prepare --initial-provision --input /tmp/esp32-before.bin --generate-id --nvs-generator "$NVS_GENERATOR" --output /tmp/esp32-provision
```

The offline tool requires a complete backup and an entirely erased BCP partition.
It emits:

- `bajols-nvs.bin`: the **only** flashable image, exactly 24 KB.
- `journal.csv`: six 32-byte blobs in namespace `journal`, keys `r0` through `r5`.
- `journal-records.bin`: the shared 192-byte logical journal, **not** a flash image.
- `manifest.json` and `REVIEW.txt`: identity, initial latched state, partition map,
  source/encoder/content hashes and the ground-station ID setting.

The encoder is Espressif's official generator, not a home-grown NVS implementation.
Keep its version/environment and exact source file for verification. A failed
preparation leaves its directory for inspection; use a new output path after
resolving the error. Never reuse an ID from a simulator or another physical board.

### 4. Review, manually program the partition, then verify full readback

These are deliberate flash-write commands after reviewing the board and bundle.
They program **only** the dedicated BCP partition, not default NVS or the full chip.

```sh
"$ESPTOOL" --chip esp32 --port "$ESP32_PORT" --baud 460800 --after no-reset write-flash 0x310000 /tmp/esp32-provision/bajols-nvs.bin
"$ESPTOOL" --chip esp32 --port "$ESP32_PORT" --baud 460800 --after no-reset read-flash 0x0 0x400000 /tmp/esp32-after.bin
python3 firmware/tools/provision_esp32.py verify --original /tmp/esp32-before.bin --readback /tmp/esp32-after.bin --manifest /tmp/esp32-provision/manifest.json --nvs-generator "$NVS_GENERATOR"
```

Verification regenerates the expected NVS image, checks identity/layout/hashes,
compares the full BCP partition and rejects changes anywhere else in flash. Keep
runtime writes disabled until this passes. The tool has no hardware-writing API.

### 5. Compile and manually upload the writable runtime

**EEPROM base is logical zero on ESP32**, not `0x310000`. The provisional 3000 ms
storage timeout includes worker scheduling, NVS commit and readback. Validate
worst-case timing, including flash garbage collection, for the actual board.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 --libraries firmware/Libraries --output-dir /tmp/bajols-build/esp32-live --build-property 'compiler.cpp.extra_flags=-DBAJOLS_BCP_ENABLE_EEPROM_WRITES -DBAJOLS_BCP_EEPROM_BASE=0 -DBAJOLS_BCP_WRITE_TIMEOUT_MS=3000' firmware/Experimental/TelemetryProof
arduino-cli upload --fqbn esp32:esp32:esp32 --port "$ESP32_PORT" --input-dir /tmp/bajols-build/esp32-live firmware/Experimental/TelemetryProof
```

The boot counter advances before a session can be established. NVS recovery may
write within the dedicated partition only after this explicit opt-in. Neither the
application nor the storage adapter erases/recreates a failed journal. Initial
image verification is no longer applicable after the first runtime advance.

## Connect the ground station

These commands describe the current direct USB connection. The planned
[RFM69HCW buoy and tether arrangement](../Documentation/Wiring/RF_BUOY_TETHER.md)
adds separate shore/buoy bridge firmware and tether UART selection; those are not
yet implemented. Submarine identity and BCP authorization remain end to end.

Copy the appropriate bundle's vehicle ID into a separate YAML configuration:

```yaml
schema_version: 1
transport: serial
port: "/dev/ttyUSB0"
expected_vehicle_id: "COPY_THE_32_HEX_CHARACTERS_FROM_THE_MANIFEST"
baud_rate: 115200
```

Use the actual port: a Mega commonly appears as `/dev/ttyACM0`, a USB-UART ESP32 as
`/dev/ttyUSB0`, and Windows boards as `COM3` or similar. Do not use the bundled
simulator ID. Run:

```sh
dotnet run --project groundstation/Bajols.GroundStation -- --config /absolute/path/to/vehicle.yaml
```

Close other serial applications and press Connect. Supported telemetry types can
be reported unavailable until drivers exist; add `-DBAJOLS_BCP_DEMO_TELEMETRY` to
the writable build flags only for explicitly simulated instrument testing. No
movement/ballast capability is advertised, Arm stays unavailable, and an initially
latched emergency is retained. Board reset logs are not BCP telemetry.

## Troubleshooting

- **No handshake:** the default build is deliberately read-only. Check writable
  definitions, provisioned records, selected port and board-specific storage base.
  ESP32 also needs the supplied partition table and all six NVS blobs.
- **Vehicle ID mismatch:** copy the physical board's manifest ID, not the simulator
  ID or its USB serial number. Do not solve this by reprovisioning a used journal.
- **Connected but Arm unavailable:** expected for this actuator-disabled stage.
  Initial emergency state is latched and movement capabilities are not advertised.
- **No instrument values:** default sources report unavailable. Explicit demo
  telemetry is simulated; actual sensors still need drivers and calibration.
- **Dump/image rejected:** verify exact sizes and use the matching platform tool.
  An occupied target requires service review, not erase-all or a force option.
- **NVS encoder import fails:** run preparation and verification in the same pinned
  venv, with `--nvs-generator` pointing to the package source shown above.

## Persistence implementation and verification limits

Mega uses asynchronous native EEPROM byte writes with paired CRC-protected
records. ESP32 uses a fixed-storage FreeRTOS worker and NVS blob commits. The
portable byte facade stages payload only under a durably invalidated record
marker. It commits a full record when invalidating or setting the final marker,
then verifies the durable mirror. A paired boot update needs four record commits,
not 67 NVS commits. The six record blobs and the journal checks remain separate:
one NVS key write is not treated as an atomic update of both paired records.

NVS/flash can stall instruction fetch/cache even with I/O on another task. Worker
separation is not a hard real-time or brownout guarantee. Missing blobs, worker
failure, readback mismatch and timeouts inhibit startup/control; no automatic erase
or fallback provisioning occurs. Wear/garbage collection, power loss, media faults,
interrupt latency and independent emergency hardware still require actual-board
validation before powered control. The native tests model journal boundaries and
record completion, not NVS's internal flash implementation or anti-rollback.

```sh
python3 firmware/tests/bcp/run_native_tests.py
python3 firmware/tests/bcp/run_native_tests.py --sanitize
python3 firmware/tests/bcp/nvs_generator_smoke.py
```

Run the last command in the NVS-generator venv. Native tests include byte-journal
power cuts, 450 record-backed boot cuts, 220 clear cuts, record readback failures,
read-only refusal and offline provisioning/collateral-write rejection. The smoke
test uses the real pinned encoder on synthetic flash backups, without hardware.
The Compile Sketches workflow retains Mega coverage; Compile ESP32 Firmware checks
read-only/demo-writable vehicle builds and portable fixtures. Compile/test success
is not permission to flash or a claim of physical safety validation.

References: [Espressif NVS](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/storage/nvs_flash.html),
[Espressif NVS generator](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/storage/nvs_partition_gen.html),
[esptool commands](https://docs.espressif.com/projects/esptool/en/latest/esp32/esptool/basic-commands.html).
