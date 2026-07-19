#!/usr/bin/env python3
"""Receive-only Meshtastic adapter. It has no transmit or config-write path."""

import argparse
import json
import threading
import time
from datetime import datetime, timezone

from google.protobuf.json_format import MessageToDict
from pubsub import pub
from serial.tools import list_ports


def envelope(kind, message_id, payload):
    return {
        "type": kind,
        "message_id": message_id,
        "protocol_version": {"major": 1, "minor": 0},
        "payload": payload,
    }


def node_id(node):
    return node.get("user", {}).get("id") or f"!{node['num']:08x}"


def topology_node(node, include_names=False):
    result = {
        "node_id": node_id(node),
        "hops_away": node.get("hopsAway"),
        "last_heard_unix": node.get("lastHeard"),
        "snr": node.get("snr"),
    }
    if include_names:
        user = node.get("user", {})
        result.update(long_name=user.get("longName"), short_name=user.get("shortName"))
    return {key: value for key, value in result.items() if value is not None}


def packet_metadata(packet):
    allowed = ("fromId", "toId", "channel", "rxSnr", "rxRssi", "hopLimit", "hopStart", "wantAck", "viaMqtt")
    result = {key: packet[key] for key in allowed if key in packet}
    port = packet.get("decoded", {}).get("portnum")
    if port is not None:
        result["portnum"] = str(port)
    return result


def safe_health(node):
    metrics = node.get("deviceMetrics", {})
    allowed = ("batteryLevel", "voltage", "channelUtilization", "airUtilTx", "uptimeSeconds")
    return {key: metrics[key] for key in allowed if key in metrics}


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def find_port(name):
    return next((port for port in list_ports.comports() if port.device.casefold() == name.casefold()), None)


def emit(record, lock):
    with lock:
        print(json.dumps(record, separators=(",", ":")), flush=True)


def run(args):
    port = find_port(args.port)
    if not port:
        raise RuntimeError(f"port not present: {args.port}")
    if (port.serial_number or "").casefold() != args.expect_usb_serial.casefold():
        raise RuntimeError("USB serial does not match paired identity")

    from meshtastic.protobuf import config_pb2
    from meshtastic.serial_interface import SerialInterface

    interface = SerialInterface(devPath=args.port)
    lock = threading.Lock()
    try:
        local_num = interface.myInfo.my_node_num
        local = interface.nodesByNum.get(local_num, {"num": local_num})
        local_id = node_id(local)
        if local_id.casefold() != args.expect_node_id.casefold():
            raise RuntimeError("Meshtastic node ID does not match paired identity")

        metadata = MessageToDict(interface.metadata, preserving_proto_field_name=True)
        lora = interface.localNode.localConfig.lora
        topology = sorted(
            (topology_node(node, args.include_names) for node in interface.nodesByNum.values()),
            key=lambda node: node["node_id"],
        )
        emit(envelope("hello", "meshtastic_hello_01", {
            "adapter": {"id": "meshtastic.readonly", "version": "0.1.0"},
            "features": ["discovery", "telemetry", "events"],
            "capability_namespaces": ["meshtastic"],
            "config_schema_version": 1,
        }), lock)
        emit(envelope("node_snapshot", "meshtastic_snapshot_01", {
            "node_id": local_id,
            "connection_state": "online",
            "paired_identity": {
                "port": port.device,
                "usb_serial": port.serial_number,
                "usb_vid": f"{port.vid:04X}",
                "usb_pid": f"{port.pid:04X}",
                "node_num": local_num,
            },
            "hardware_model": local.get("user", {}).get("hwModel") or metadata.get("hw_model"),
            "firmware_version": metadata.get("firmware_version"),
            "region": config_pb2.Config.LoRaConfig.RegionCode.Name(lora.region),
            "tx_enabled_observed": lora.tx_enabled,
            "health": safe_health(local),
            "topology": topology,
            "privacy": {
                "names": "included" if args.include_names else "omitted",
                "positions": "omitted",
                "packet_payloads": "omitted",
                "channel_keys": "omitted",
            },
            "capabilities": ["meshtastic.status.read", "meshtastic.topology.read", "meshtastic.packet_metadata.read"],
        }), lock)

        sequence = 0

        def on_receive(packet, _interface):
            nonlocal sequence
            sequence += 1
            emit(envelope("event", f"meshtastic_event_{sequence:06d}", {
                "node_id": local_id,
                "source_sequence": sequence,
                "received_at": utc_now(),
                "kind": "meshtastic.packet_metadata.received",
                "packet": packet_metadata(packet),
            }), lock)

        if args.listen_seconds:
            pub.subscribe(on_receive, "meshtastic.receive")
            time.sleep(args.listen_seconds)
            pub.unsubscribe(on_receive, "meshtastic.receive")
        emit(envelope("heartbeat", "meshtastic_heartbeat_01", {
            "node_id": local_id,
            "connection_state": "online",
            "received_packet_count": sequence,
        }), lock)
    finally:
        interface.close()


def self_test():
    packet = {
        "fromId": "!a", "toId": "!b", "channel": 0, "rxSnr": 4.5,
        "decoded": {"portnum": "TEXT_MESSAGE_APP", "text": "secret", "payload": b"secret"},
        "position": {"latitude": 1}, "psk": "secret",
    }
    sanitized = json.dumps(packet_metadata(packet))
    assert "TEXT_MESSAGE_APP" in sanitized
    assert "secret" not in sanitized and "latitude" not in sanitized and "psk" not in sanitized
    node = {"num": 1, "user": {"id": "!00000001", "longName": "Private"}, "position": {"latitude": 1}}
    redacted = json.dumps(topology_node(node))
    assert "Private" not in redacted and "latitude" not in redacted
    print("meshtastic readonly sanitizer: ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM11")
    parser.add_argument("--expect-usb-serial", required=False)
    parser.add_argument("--expect-node-id", required=False)
    parser.add_argument("--listen-seconds", type=int, default=0)
    parser.add_argument("--include-names", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.expect_usb_serial or not args.expect_node_id:
        parser.error("--expect-usb-serial and --expect-node-id are required")
    if not 0 <= args.listen_seconds <= 300:
        parser.error("--listen-seconds must be in 0..300")
    run(args)


if __name__ == "__main__":
    main()
