#!/usr/bin/env python3
"""Compile native C++11 conformance tests from the authoritative shared JSON."""

import argparse
import json
import importlib.util
import sys
import os
from pathlib import Path
import re
import subprocess
import tempfile

# Message storage names and field types, in wire order; bytes come only from JSON.
SCHEMAS = {'Hello': ('hello', [['OpaqueId128', 'nonce'], ['uint8_t', 'requestedVersion']]),
 'HelloReply': ('helloReply',
                [['OpaqueId128', 'nonce'],
                 ['uint8_t', 'selectedVersion'],
                 ['OpaqueId128', 'vehicleId']]),
 'SessionConfirm': ('sessionConfirm', [['OpaqueId128', 'nonce']]),
 'SessionReady': ('sessionReady', [['uint32_t', 'controlGeneration']]),
 'ControlTicket': ('controlTicket',
                   [['uint32_t', 'ticketId'],
                    ['uint32_t', 'controlGeneration'],
                    ['uint16_t', 'validityMs']]),
 'MovementCommand': ('movement',
                     [['uint32_t', 'ticketId'],
                      ['uint32_t', 'controlGeneration'],
                      ['int16_t', 'forward'],
                      ['int16_t', 'yaw'],
                      ['int16_t', 'dive']]),
 'Arm': ('arm',
         [['uint32_t', 'requestId'],
          ['uint32_t', 'ticketId'],
          ['uint32_t', 'controlGeneration']]),
 'Disarm': ('disarm', [['uint32_t', 'requestId']]),
 'ClearFailsafe': ('clearFailsafe',
                   [['uint32_t', 'requestId'],
                    ['uint32_t', 'ticketId'],
                    ['uint32_t', 'controlGeneration']]),
 'EmergencyStop': ('emergencyStop', [['uint32_t', 'requestId']]),
 'CommandResult': ('commandResult',
                   [['uint32_t', 'requestId'],
                    ['MessageType', 'originalMessageType'],
                    ['CommandResultCode', 'result'],
                    ['uint32_t', 'controlGeneration']]),
 'BuoyancyCommand': ('buoyancy',
                     [['uint32_t', 'ticketId'],
                      ['uint32_t', 'controlGeneration'],
                      ['BuoyancyTarget', 'target']]),
 'Capabilities': ('capabilities',
                  [['uint16_t', 'capabilityRevision'],
                   ['uint16_t', 'supportedModes'],
                   ['uint8_t', 'supportedAxes'],
                   ['uint16_t', 'supportedSystems'],
                   ['uint32_t', 'supportedTelemetry'],
                   ['uint16_t', 'movementIntervalMs'],
                   ['uint16_t', 'commandTimeoutMs'],
                   ['uint16_t', 'ticketIntervalMs'],
                   ['uint16_t', 'ticketLifetimeMs'],
                   ['uint16_t', 'neutralHoldMs'],
                   ['uint16_t', 'neutralTolerance']]),
 'VehicleStatus': ('status',
                   [['uint32_t', 'statusRevision'],
                    ['uint32_t', 'vehicleUptimeMs'],
                    ['SafetyState', 'safetyState'],
                    ['MovementOwner', 'movementOwner'],
                    ['ControlMode', 'controlMode'],
                    ['uint32_t', 'controlGeneration'],
                    ['uint32_t', 'armingBlockers'],
                    ['uint16_t', 'neutralHoldProgressMs'],
                    ['uint16_t', 'movementAgeMs'],
                    ['uint16_t', 'movementTimeRemainingMs'],
                    ['BuoyancyAction', 'buoyancyAction'],
                    ['ActionStatus', 'buoyancyActionStatus'],
                    ['SafetyCause', 'safetyCause'],
                    ['EstimatedBuoyancy', 'estimatedBuoyancy'],
                    ['AuthorizedBuoyancyTarget', 'authorizedNormalTarget'],
                    ['uint16_t', 'buoyancyCommandTimeRemainingMs'],
                    ['uint16_t', 'activeRestrictions']]),
 'Fault': ('fault',
           [['FaultCode', 'faultCode'],
            ['FaultSeverity', 'severity'],
            ['bool', 'active'],
            ['uint32_t', 'occurrenceCounter']])}
MESSAGE_TYPES = {1: 'Hello',
 2: 'HelloReply',
 3: 'SessionConfirm',
 4: 'SessionReady',
 5: 'ControlTicket',
 6: 'Heartbeat',
 16: 'MovementCommand',
 17: 'Arm',
 18: 'Disarm',
 19: 'ClearFailsafe',
 20: 'EmergencyStop',
 21: 'CommandResult',
 22: 'BuoyancyCommand',
 32: 'Capabilities',
 33: 'GetCapabilities',
 48: 'VehicleStatus',
 49: 'Telemetry',
 50: 'Fault',
 51: 'ObjectReport'}
ENUM_TYPES = {'ActionStatus',
 'AuthorizedBuoyancyTarget',
 'BuoyancyAction',
 'BuoyancyTarget',
 'CommandResultCode',
 'ControlMode',
 'Direction',
 'Error',
 'EstimatedBuoyancy',
 'FaultCode',
 'FaultSeverity',
 'MeasurementId',
 'MessageType',
 'MovementOwner',
 'SafetyCause',
 'SafetyState',
 'StreamResult'}


def snake(name):
    """Convert public C# spelling to C++ struct field spelling."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).lower()


def fixture_header(document):
    """Generate independent field construction and assertions for every vector."""
    lines = ["// Generated in a temporary directory from shared JSON.",
             "void RunSharedVectors() {"]
    for vector in document["vectors"]:
        lines += ["  {", "    Packet expected = {};" ]
        fields = vector.get("fields", {})
        header = vector.get("header", {})
        positive = vector["expected"] == "wire_valid"
        assignments = []
        if positive:
            message_type = MESSAGE_TYPES[header["message_type"]]
            assignments += [("header.type", "MessageType::k" + message_type),
                            ("header.session.boot_counter", str(header["boot_counter"]) + "UL"),
                            ("header.session.session_counter", str(header["session_counter"]) + "UL"),
                            ("header.sequence", str(header["sequence"]) + "UL")]
            if message_type in SCHEMAS:
                member, schema = SCHEMAS[message_type]
                for field_type, field in schema:
                    path = "message." + snake(member) + "." + snake(field)
                    if field_type == "OpaqueId128":
                        for i, value in enumerate(bytes.fromhex(fields[snake(field) + "_hex"])):
                            assignments.append((path + ".bytes[" + str(i) + "]", str(value)))
                    else:
                        value = fields[snake(field)]
                        expression = str(value)
                        if field_type in ENUM_TYPES:
                            expression = "static_cast<" + field_type + ">(" + expression + ")"
                        elif field_type == "bool":
                            expression = "true" if value else "false"
                        assignments.append((path, expression))
            elif message_type == "ObjectReport":
                for field in ["format_revision", "sensor_id", "scan_sequence", "sample_age_ms", "sensor_state", "report_flags"]:
                    assignments.append(("message.object_report." + field, str(fields[field])))
                assignments.append(("message.object_report.count", str(len(fields["observations"]))))
                for i, record in enumerate(fields["observations"]):
                    for field, value in record.items():
                        assignments.append(("message.object_report.observations[" + str(i) + "]." + field, str(value) + ("UL" if value > 2147483647 else "")))
            elif message_type == "Telemetry":
                assignments.append(("message.telemetry.count", str(len(fields["records"]))))
                for i, record in enumerate(fields["records"]):
                    for field, value in record.items():
                        expression = str(value)
                        if field == "measurement_id":
                            expression = "static_cast<MeasurementId>(" + expression + ")"
                        assignments.append(("message.telemetry.records[" + str(i) + "]." + field,
                                            expression))
        for field, value in assignments:
            lines.append("    expected." + field + " = " + value + ";")
        direction = "kVehicleToPc" if vector.get("direction") == "vehicle_to_pc" else "kPcToVehicle"
        lines.append("    Packet actual = TestVector(" + json.dumps(vector["name"]) + ", " +
                     json.dumps(vector["wire_hex"]) + ", Direction::" + direction + ", " +
                     json.dumps(vector["expected"]) + ", expected, " +
                     json.dumps(vector.get("payload_hex", "")) + ", " +
                     json.dumps(vector.get("crc_input_hex", "")) + ", " +
                     str(vector.get("crc_value", 0)) + ");")
        for field, value in assignments:
            lines.append("    Check(actual." + field + " == expected." + field +
                         ", " + json.dumps(vector["name"] + ": " + field) + ");")
        lines.append("    (void)actual;")
        lines.append("  }")
    lines.append("}")
    return "\n".join(lines)


def main():
    """Build in temporary storage and execute the native test binary."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="Enable ASan and UBSan")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    source = root / "firmware/Libraries/BajolsProtocol/src"
    document = json.loads((root / "protocol/test-vectors/wire-v1.json").read_text())
    with tempfile.TemporaryDirectory(prefix="bajols-bcp-tests-") as temporary:
        build = Path(temporary)
        (build / "shared_vectors.h").write_text(fixture_header(document))
        tool_path = root / "firmware/tools/provision_identity.py"
        spec = importlib.util.spec_from_file_location("bajols_provision", tool_path)
        provision = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(provision)
        initial = provision.initial_window(bytes(range(1, 17)))
        (build / "provisioned_image.h").write_text(
            "// Generated by the actual offline provisioning implementation.\n"
            "const uint8_t kProvisionedImage[] = {" +
            ",".join(str(value) for value in initial) + "};\n")
        subprocess.run([sys.executable, str(Path(__file__).with_name("provisioning_tests.py"))], check=True)
        common = [os.environ.get("CXX", "g++"), "-std=c++11", "-Wall", "-Wextra",
                  "-Werror", "-pedantic", "-fno-exceptions", "-fno-rtti", "-g",
                  "-I" + str(source), "-I" + str(build)]
        if args.sanitize:
            common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       "-fno-pie", "-no-pie"]
        suites = [
            ("codec_tests", ["BajolsProtocol.cpp"]),
            ("session_tests", ["BajolsProtocol.cpp", "BajolsSession.cpp"]),
            ("endpoint_tests", ["BajolsProtocol.cpp", "BajolsSession.cpp", "BajolsEndpoint.cpp"]),
            ("storage_tests", ["BajolsProtocol.cpp", "BajolsSession.cpp", "BajolsStorage.cpp", "BajolsPersistence.cpp"]),
            ("monitor_tests", ["BajolsProtocol.cpp", "BajolsSession.cpp", "BajolsStorage.cpp",
                               "BajolsPersistence.cpp", "BajolsEndpoint.cpp", "BajolsMonitor.cpp"]),
        ]
        for name, library_sources in suites:
            command = common + [str(source / item) for item in library_sources]
            command += [str(Path(__file__).with_name(name + ".cpp")),
                        "-o", str(build / name)]
            subprocess.run(command, check=True)
            subprocess.run([str(build / name)], check=True)


if __name__ == "__main__":
    main()
