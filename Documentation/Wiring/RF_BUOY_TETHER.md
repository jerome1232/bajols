# Shore radio, buoy bridge and submarine tether

Status: architecture discussion and component recommendations, 2026-11-20.
The user selected the **915 MHz RFM69HCW** for initial RF trials and a buoy radio
with a hard line down to the submarine. ESP32 Dev Module is the selected ESP32
platform. Specific bridge boards, tether transceivers, pin assignments, electrical
protection and RF settings below remain recommendations, not a finalized BOM.

## Arrangement

```mermaid
flowchart LR
    PC["Ground station"] <-->|"USB serial / BCP"| SHORE["Shore ESP32"]
    SHORE <-->|"SPI + interrupt"| SRF["915 MHz RFM69HCW"]
    SRF <-->|"Bidirectional RF; one direction at a time"| BRF["915 MHz RFM69HCW on buoy"]
    BRF <-->|"SPI + interrupt"| BUOY["Buoy ESP32"]
    BUOY <-->|"UART"| BLINE["Buoy differential transceiver"]
    BLINE <-->|"Two CAT6 twisted pairs; full duplex"| SLINE["Submarine differential transceiver"]
    SLINE <-->|"UART / BCP"| SUB["Submarine ESP32 or Mega"]
```

There are **two RF modules** and **two bridge microcontrollers**: one radio/MCU at
the shore computer and another radio/MCU on the buoy. The submarine's existing
controller is a third MCU; it does not need an RF module for this arrangement.
The buoy antenna stays above the water. The CAT6 tether carries differential serial
between line transceivers at both ends. The radio interface is **SPI**, not a UART.

Four-wire full-duplex RS-422/RS-485 is recommended for the tether: one pair in each
direction, so commands and telemetry do not require cable-direction switching.
The RF link still needs transmit/receive scheduling; a full-duplex tether does not
make a single RFM69HCW transmit and receive simultaneously.

## Recommended modules

Manufacturer specifications were checked on 2026-11-20. Do not treat stock, prices
or advertised maximum ranges as measured BAJOLS performance.

| Role | Quantity | Selected or recommended hardware | Reason and qualification |
|---|---:|---|---|
| RF | 2 | **Selected:** [SparkFun RFM69HCW 915 MHz, COM-13909](https://www.sparkfun.com/rfm69hcw-wireless-transceiver-915mhz.html) | SPI radio with up to 300 kbit/s FSK; this listing is the bare module and needs a carrier/appropriate wiring. |
| RF carrier alternative | 2, replacing the bare modules | **Recommended for bench assembly:** [SparkFun RFM69 Breakout 915 MHz, WRL-12775](https://www.sparkfun.com/sparkfun-rfm69-breakout-915mhz.html) | Same radio family with accessible breakout connections. Choose bare modules or populated breakouts, not both sets. |
| Shore and buoy bridge MCUs | 2 | **Recommended:** classic ESP32 Dev Module with 4 MB flash; [Espressif ESP32-DevKitC](https://www.espressif.com/en/products/devkits/esp32-devkitc/overview) is a carrier candidate | Reuses the project's ESP32 toolchain; provides SPI, hardware UART and a USB programming/serial connection. Confirm the purchased module's flash size. |
| Compact tether interface | 2 | **Recommended non-isolated prototype:** [MIKROE RS485 3 Click, MIKROE-2821](https://www.mikroe.com/rs485-3-click) | 3.3 V UART interface using SN65HVD31DR, full-duplex four-wire connection. Requires mikroBUS/header wiring or an adapter; do not assume direct ESP32 pin compatibility. |
| Isolated tether alternative | 2, replacing the compact interfaces | **Bench candidate:** [Waveshare TTL TO RS422 (B)](https://www.waveshare.com/ttl-to-rs422-b.htm) | Manufacturer specifies full duplex, 3.3–5 V TTL compatibility and power/signal isolation. Its DIN-rail enclosure is bulky; verify enclosure fit and the exact revision's wiring before selection. |
| Vehicle controller | Existing board | Selected ESP32 Dev Module, or the supported Mega 2560 | Owns BCP authorization, safety and future actuator drivers. It is separate from the buoy bridge. |
| Antennas | 2 | Antennas matched to 915 MHz and the chosen module/carrier connector | Keep the buoy antenna clear of the water and enclosure metal; determine antenna arrangement from the actual radio/carrier. |

The [MIKROE specifications](https://www.mikroe.com/rs485-3-click) identify a 3.3 V,
42.9 × 25.4 mm UART board with separate TX/RX differential terminals. This fits a
compact point-to-point prototype; it is not a galvanically isolated interface.
Its always-enabled driver is suitable for one transmitter per pair, not multiple
active transmitters sharing the same pair. Use the board's actual schematic and
terminal labels when wiring.

The [Waveshare specifications](https://www.waveshare.com/ttl-to-rs422-b.htm) list an
approximately 106 × 58.6 × 36 mm enclosure and selectable 120-ohm termination.
It is an alternative for evaluating isolation, not a finalized submarine module.
Its case should not be assumed waterproof. Follow its bus-side reference/isolation
instructions; do not defeat isolation by indiscriminately joining cable and local
grounds.

For a later custom carrier, [TI THVD1451](https://www.ti.com/product/THVD1451) is a
full-duplex 3.3–5 V transceiver IC candidate, not a ready-to-connect module. Carrier
layout, protection and termination would still need design and review.

Generic MAX485/SP3485 two-wire boards are commonly **half-duplex**. They would
require a different direction-control/scheduling design and are not drop-in
substitutes for the two-pair arrangement recommended here.

## Connections and logic levels

### Radio to its local bridge MCU

Both radio/MCU nodes need local SPI clock, MOSI, MISO, chip select, an interrupt
connection and the reset connection required by the selected driver. Use a
regulated 3.3 V radio supply with decoupling appropriate to transmit-current pulses;
check the complete bridge/radio power budget before choosing a regulator.

The [RFM69HCW datasheet](https://cdn.sparkfun.com/datasheets/Wireless/General/RFM69HCW-V1.1.pdf)
is the electrical reference. A breakout does not automatically imply a 5 V-tolerant
SPI interface. ESP32 logic is the preferred starting point for the two bridges;
any 5 V MCU connection needs compatible level handling.

Exact SPI/UART GPIO assignments remain open. Select them after checking the actual
ESP32 board, boot-strapping pins, flash connections and any other peripherals.

### Tether endpoint wiring

Conceptual point-to-point wiring, before assigning physical connector pins:

| Logical circuit | Buoy connection | Submarine connection |
|---|---|---|
| MCU transmit | UART TX → line-driver input | UART TX → line-driver input |
| MCU receive | UART RX ← line-receiver output | UART RX ← line-receiver output |
| CAT6 pair A | Differential TX+ / TX− | Differential RX+ / RX− |
| CAT6 pair B | Differential RX+ / RX− | Differential TX+ / TX− |
| Reference circuit | Designed reference connection | Corresponding reference connection |
| Remaining conductors | Unassigned | Unassigned |

Use both conductors of one twisted pair for each differential signal; do not
split a signal across pairs. Check positive/negative terminal definitions against
the exact modules: A/B naming is not consistent between all suppliers. The table
is not an RJ45 pinout or an approved cable-colour assignment.

For a non-isolated interface, provide a deliberate signal-reference path and keep
both receivers within their common-mode limits. For isolated interfaces, follow
the selected modules' bus-side reference and power arrangement instead. Reserve
remaining conductors until the power-distribution decision is made. This guide
does not assign tether power or assume that a signal-ground conductor is an
adequate motor/pump power return.

CAT6 typically has nominal 100-ohm differential impedance. For this fixed
point-to-point arrangement, evaluate termination at the receiving end of each
unidirectional pair against the actual cable and driver specifications. Inspect
onboard resistors before adding external ones; a module's default 120-ohm network
is not automatically the final design. Confirm waveforms with the real tether
length and connectors. [TI wiring and termination guidance](https://www.ti.com/lit/an/slla272d/slla272d.pdf)

CAT6 here is a cable carrying serial signals; Ethernet hardware is not required.
Label connectors with their actual function and finalize the connector/pinout
before making a tether. Cable jacket, waterproof feedthroughs, enclosure sealing
and strain relief remain mechanical/electrical choices; ordinary patch cable is
not automatically suitable for repeated underwater use.

## Bandwidth and initial settings

Planning workload: 20 Hz movement, 20 Hz buoyancy while armed, 20 Hz tickets,
5 Hz status, five instrument measurements at 5 Hz, one object source with four
returns at 5 Hz, and a 1 Hz heartbeat in each direction. BCP's encoded overhead
is 20 bytes per packet for the current bounded payload sizes.

| Traffic | Encoded bytes/second |
|---|---:|
| Ground station → submarine | 1,280 |
| Submarine → ground station | 1,770 |
| Combined | **3,050**, or **24.4 kbit/s** |

These are a design workload estimate, not measurements of the current
actuator-disabled firmware. Each additional four-return object source at 5 Hz
adds 575 bytes/s, or 4.6 kbit/s, before RF overhead. Current USB/UART framing is
115200 baud, 8N1; the workload above consumes approximately 12.8 and 17.7 kbit/s
on the two UART directions after start/stop bits. RF preambles, link headers,
acknowledgements, turnarounds and retransmissions need additional airtime.

Recommend at least **50 kbit/s usable combined throughput**, preferably 100 kbit/s
or more for additional sources and margin. An initial bench target is approximately
250 kbit/s nominal RF if the selected driver/profile supports it; the module's
specified ceiling is 300 kbit/s. These are provisional settings. Configure both
radios consistently and measure delivered throughput, latency, packet loss and
range. Antenna placement above water and buoy motion matter more than a vendor's
best-case range figure. Operating region and permitted RF configuration remain
to be confirmed before deployment.

## Radio packet size and bridge firmware

The module has a **66-byte FIFO**. The common
[LowPowerLab RFM69 driver](https://github.com/LowPowerLab/RFM69/blob/master/RFM69.h)
sets `RF69_MAX_DATA_LEN` to **61 application bytes**. BCP frames can reach
**116 bytes**, including COBS and delimiter. Do not assume a complete BCP frame
fits a single ordinary driver packet. The FIFO size alone is not a universal
hardware payload limit; a more complex streaming driver is a separate option.

Recommended first implementation: bounded RF fragmentation/reassembly underneath
BCP, with explicit limits and expiry. The fragment format, packet identifiers,
RF acknowledgement policy, half-duplex schedule, retry limits and reassembly
expiry still need a transport design. No RF packet layout is assigned here.
Most small control frames fit one driver packet; larger telemetry/object frames
need the chosen transport's larger-frame handling.

Bridge requirements:

- Preserve each complete BCP frame exactly, including vehicle session, sequence,
  payload, CRC and delimiter. Keep per-direction BCP ordering; discard obsolete
  queued frames rather than replaying them after newer traffic.
- Bound storage to explicit queues/reassembly slots. Incomplete RF data must not
  turn into a fabricated BCP frame or an unbounded stream backlog.
- Give control/stop traffic priority over replaceable telemetry when admitting and
  scheduling traffic, without later forwarding superseded packets out of order.
- Bound retries and queue age. The 500 ms movement/ticket freshness budget spans
  the full ticket-out/command-back path, not 500 ms per bridge or per RF fragment.
  Target under 50 ms one-way latency in normal conditions; validate tails as well.
- A radio acknowledgement means hop delivery, not onboard command acceptance.
  Ground-station actions still depend on BCP results and fresh vehicle status.
- Never issue replacement vehicle tickets, renew old movement commands using
  heartbeats, select ownership, arm or automatically resume movement at a bridge.
  Recovery/reconnection still follows the onboard safety state machine.
- Keep debug output off the binary USB/UART BCP stream.

The submarine remains the sole BCP vehicle endpoint. Ground-station
`expected_vehicle_id` identifies the submarine, not a shore or buoy bridge.
RF node addresses and network IDs, if used, are transport configuration and do
not replace that identity or provide command authorization.

## Implementation status and next decisions

Implemented in this checkout: C++/C# BCP, USB serial communication, bounded vehicle
runtime, simulated peer, object reports and Mega/ESP32 persistent-startup support.
The vehicle firmware still advertises no actuator support and cannot arm.

**Not implemented yet:** RFM69 driver integration, shore/buoy bridge targets, RF
fragmentation/scheduling, tether UART selection and a validated physical tether.
The current vehicle sketch binds BCP to `Serial`; connecting a transceiver to a
different UART will require firmware transport selection. The current desktop
serial transport can serve a future USB shore bridge, but it does not currently
configure or drive an RFM69 itself.

Before wiring/implementation, settle the intended shore–buoy distance, tether
length, exact carrier/module revisions, SPI/interrupt/UART pins, 3.3 V power budget,
isolation/reference arrangement, cable/connector/termination and radio transport
contract. The full-duplex tether and bridge MCU/module list are recommendations
for review; only the initial RFM69HCW choice and buoy/hard-line topology are user
hardware decisions so far.

Related documentation:

- [Firmware build and persistent-identity provisioning](../../firmware/BUILD_AND_PROVISION.md)
- [Ground-station operation](../../groundstation/README.md)
- [BCP v1 contract](../../protocol/BCP_V1_DRAFT.md)
- [Object-reporting extension](../../protocol/BCP_OBJECT_REPORTING_PROPOSAL.md)
