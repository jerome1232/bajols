# Firmware identity and emergency journal — implementation proposal

Status: implementation direction approved 2026-11-17; portable journal, Mega
EEPROM adapter and runtime coordinator implemented. Physical addresses, wear
leveling, provisioning/service procedures and powered-hardware validation remain
open. The file retains its proposal name for existing links.
BCP draft 0.8's identity and emergency behavior remains authoritative; this adds
no wire fields. The existing vehicle and USB-serial sketches do not write storage.

## Proposed recovery policy

Use a caller-supplied exclusive 192-byte storage window with six 32-byte records:
two provisioned identity copies, two boot-counter copies, two emergency copies.
Both copies of a kind must be valid and identical. Do not select an older valid
boot record when the other is corrupt or newer: that can reuse an identity already
issued by a preceding boot. Torn/conflicting boot records require service rather
than automatic fallback. This favors identity separation over availability.

Runtime never initializes erased storage or provisions an identity. An external,
reviewed service procedure must establish the same stable, nonzero/non-erased
128-bit ID in both identity records, boot counter zero in both boot records,
and emergency state latched with journal counter zero in both emergency records.
Provisioning/recovery tools are outside this prototype. Repair must never decrease
a previously used boot counter or silently change the selected vehicle identity.

Before exposing a verified boot, validate all identity/boot records, advance the
boot counter without wrap, write both boot copies and verify both complete records.
No session can use the new counter until this finishes. A completed write whose
in-memory acknowledgement is lost consumes that counter; the next boot advances
again. Counter gaps are allowed. Counter reuse is not.

Missing/corrupt/conflicting emergency records recover as ambiguous/latched and
storage unhealthy, requiring service. A valid identity may still advance so that
monitoring remains possible. Valid matching clear records mean a completed local
clear; valid matching latch records mean emergency remains latched. The manager
still requires observed procedure end, physical input release and local reset
before requesting a clear. Runtime does not automatically repair ambiguous storage. Identity/boot failure
alone does not invent an emergency latch when the separate emergency pair was
fully recovered as cleared; arming remains blocked by the identity fault. If the
emergency pair could not be verified, its state remains ambiguous/latched.

## Proposed record bytes

Each record is exactly 32 bytes; integers are little-endian. Native structures
are never persisted directly. Offsets are relative to the supplied storage window.

| Offset | Field |
|---|---|
| 0 | Commit marker `0xA5`; invalidated to zero before rewriting |
| 1 | Storage format version `1` |
| 2 | Kind: `1` identity, `2` boot, `3` emergency |
| 3 | Emergency value `0` cleared or `1` latched; zero for other kinds |
| 4–7 | Boot counter or independent emergency journal counter; zero for identity |
| 8–23 | Opaque provisioned vehicle ID for identity; zero for other kinds |
| 24–29 | Reserved, zero |
| 30–31 | CRC-16/CCITT-FALSE over bytes 1–29; excludes the commit marker |

Record pairs begin at offsets 0/32 (identity), 64/96 (boot), and 128/160
(emergency). These are logical offsets for the prototype, not allocated Mega
EEPROM addresses. Unknown versions/kinds, reserved values, invalid CRC, invalid
marker, identical all-zero/all-`0xFF` IDs and mismatched copies are rejected.
Checksums detect accidental corruption with finite coverage; they cannot prevent
malicious modification, coordinated rollback or every possible corruption.

## Byte transactions and emergency priority

A transaction invalidates a slot, writes bytes 1–31, then commits byte 0. Every
byte write completes and is read back before the next write starts. After both
copies are committed, read and compare both complete records before acknowledgement.
The backend exposes nonblocking readiness/read/start-one-byte-write operations.
Each service step performs at most one read or starts one byte write. Busy writes
have a caller-supplied, validated timeout; a stuck/failed backend is a storage fault.

Emergency write tokens belong to the current runtime manager and are not persisted
as journal counters. A newer latch request can supersede an in-flight clear. The
old in-flight byte must finish/read back; no further old-clear writes are started.
The newer latch rewrites both records with a new journal counter. Only the newest
transaction receives an acknowledgement. No remote request can initialize or clear
the journal. Writes and readback failures require service and never produce a
successful boot or emergency acknowledgement.

Apply emergency intent independently before servicing storage. A power failure
before the first durable latch mutation can still leave valid cleared records.
An already-started old-clear byte also cannot be recalled when a new stop arrives.
The independent hardware latch required by draft 0.8 remains necessary; this
journal cannot guarantee emergency persistence through those windows. Neither a
CRC nor redundant records fixes that physical limitation.

## Review and validation still required

The fixed pairs are intentionally conservative and are not wear leveling. Commit
cells are written on every transaction. EEPROM endurance, brownout behavior,
write atomicity, timeout bounds, redundant physical placement, service recovery,
provisioning ownership and independent emergency hardware require review before
enabling operational EEPROM use. The nonblocking Mega adapter exists but all
check sketches keep write permission disabled. No default timeout or writable EEPROM
address is selected by the library. Actual storage contents must not be modified
until the board integration and provisioning action are explicitly authorized.

## Implemented Mega adapter and coordinator

`BajolsMegaEeprom.h` provides an ATmega2560-only `ByteStorage` backend. Its caller
supplies an explicit access window and a write-enable permission; construction
never touches EEPROM. Reads and starts check readiness under a short interrupt
lock; busy operations return immediately. Writes use erase-and-write mode and
consecutive EEMPE/EEPE enable/start instructions. There is no polling wait loop,
EEPROM-ready ISR, flash programming, or Arduino `EEPROM.write/put` call. Cells
already containing the requested value are skipped, with journal readback intact.
The adapter must exclusively own EEPROM access. This reduces unnecessary writes
but does not wear-level the fixed commit cells or change the 192-byte format.

The sequence was checked against the [Microchip ATmega2560 datasheet](https://www.microchip.com/bin/mchp/product-ds.ATmega2560.pdf)
and the cached AVR device headers. A non-LTO object-code check confirmed consecutive
SBI instructions for EEMPE then EEPE. Hardware electrical behavior, fuse/brownout
settings, timing bounds and endurance are not validated by that inspection.

`PersistenceCoordinator` is constructed after journal startup using the controller
created from the same verified boot evidence. Each step advances safety, submits
only new tokens (including stop supersession), performs at most one storage byte
operation, and passes the journal's verified/failure completion to the controller.
A failed/unusable journal reports emergency-latch storage failure and stays failed;
it cannot restart a clear. Apply effective emergency outputs before storage service.
Boot-manager construction and actual output drivers remain board integration work.

`BcpEepromCheck` compiles the real backend with write permission explicitly false.
It may read the example window if uploaded but cannot advance boot counters,
provision records or commit emergency data, including on already-provisioned media.
The check uses unverified controller evidence throughout and is not an operational
serial vehicle. No hardware was flashed during implementation.

## Offline initial provisioning and monitoring composition

The initial-only tool and readback checks in [firmware/tools](../firmware/tools/README.md)
now generate reviewable sparse records; they never access hardware. Runtime still
cannot initialize erased storage or repair an already-used identity. The generated
initial emergency pair is latched. Stop the runtime during provisioning/readback
so a legitimate boot advance cannot race verification of the initial zero counter.

`MonitoringRuntime` now provides deferred fixed-storage controller construction
from verified journal evidence and binds endpoint/persistence runtime service.
`BcpMonitor` is an actuator-free target with no advertised control/sensor support.
Its default build is read-only; deliberate write opt-in additionally requires an
explicit base matching the image and a validated byte timeout. After a separately
authorized upload of such a build, boot and emergency writes would be real. No
programming or upload was performed during this implementation. Endurance/wear
leveling, independent emergency hardware and operational control remain pending.
