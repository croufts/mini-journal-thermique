"""Optional PC diagnostic: short M02 Pro job paced by FF03 buffer credits.

Requires `pip install bleak`. This is a diagnostic, not the autonomous sender.
Power-cycle the printer after any incomplete raster before running again.
"""
import argparse
import asyncio
import json
import re
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

WRITE = "0000ff02-0000-1000-8000-00805f9b34fb"
NOTIFY = "0000ff03-0000-1000-8000-00805f9b34fb"


async def ensure_windows_bond(address):
    """The tested unit accepts encryption but rejects Bleak's MITM-first pairing."""
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import (
        DeviceInformation, DevicePairingKinds, DevicePairingProtectionLevel,
        DevicePairingResultStatus,
    )
    device = await BluetoothLEDevice.from_bluetooth_address_async(int(address.replace(":", ""), 16))
    if device is None:
        raise RuntimeError("Périphérique BLE indisponible ; aucun envoi")
    try:
        info = await DeviceInformation.create_from_id_async(device.device_information.id)
        if info.pairing.is_paired:
            return
        pairing = info.pairing.custom

        def requested(_, args):
            if args.pairing_kind == DevicePairingKinds.CONFIRM_ONLY:
                args.accept()

        token = pairing.add_pairing_requested(requested)
        try:
            result = await pairing.pair_with_protection_level_async(
                DevicePairingKinds.CONFIRM_ONLY, DevicePairingProtectionLevel.ENCRYPTION)
            if result.status not in (DevicePairingResultStatus.PAIRED, DevicePairingResultStatus.ALREADY_PAIRED):
                raise RuntimeError(f"Association BLE refusée : {result.status.name} ; aucun envoi")
        finally:
            pairing.remove_pairing_requested(token)
    finally:
        device.close()


async def run(address, series=None):
    if series:
        manifest = Path(series).resolve()
        jobs = [(item["label"], (manifest.parent / item["file"]).read_bytes())
                for item in json.loads(manifest.read_text(encoding="utf-8"))]
    else:
        header = (Path(__file__).resolve().parents[1] / "firmware/include/calibration_ticket.h").read_text()
        data_text = header.split("calibrationTicket[] PROGMEM = {", 1)[1].split("};", 1)[0]
        jobs = [("JKL", bytes(int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", data_text)))]
    device = await BleakScanner.find_device_by_address(address, timeout=8)
    if device is None:
        raise RuntimeError("M02 Pro non détectée ; aucun envoi")
    if sys.platform == "win32":
        await ensure_windows_bond(address)
    credits = asyncio.Semaphore(4)
    received = 0
    notifications = {}
    complete = asyncio.Event()

    def notified(_, packet):
        nonlocal received
        key = bytes(packet).hex()
        notifications[key] = notifications.get(key, 0) + 1
        if bytes(packet) == b"\x01\x01":
            received += 1
            credits.release()
        if bytes(packet) == b"\x1a\x0f\x0c":
            complete.set()

    # This unit requires an encrypted bond before FF03 subscriptions.
    async with BleakClient(device, timeout=30, pair=sys.platform != "win32") as client:
        characteristic = client.services.get_characteristic(WRITE)
        if characteristic is None or "write-without-response" not in characteristic.properties:
            raise RuntimeError("Entrée BLE attendue absente ; aucun envoi")
        await client.start_notify(NOTIFY, notified)
        size = min(182, characteristic.max_write_without_response_size)
        for label, data in jobs:
            sent = 0
            started = time.monotonic()
            complete.clear()
            print(f"[BLE] {label}: blocs={size}, total={len(data)}", flush=True)
            try:
                for offset in range(0, len(data), size):
                    # No blind fallback to timed bursts or automatic job retries.
                    await asyncio.wait_for(credits.acquire(), timeout=10)
                    block = data[offset:offset+size]
                    await client.write_gatt_char(characteristic, block, response=False)
                    sent += len(block)
                print(f"[BLE] {label}: envoi terminé en {time.monotonic()-started:.2f}s", flush=True)
                await asyncio.wait_for(complete.wait(), timeout=30)
                print(f"[BLE] {label}: notification de fin reçue", flush=True)
                # Keep the connection alive between separately initialised swatches.
                # Credits confirm buffer slots, not physical paper output.
                await asyncio.sleep(15)
            finally:
                print(f"[BLE] {label}: écrit={sent}/{len(data)}, durée={time.monotonic()-started:.2f}s, crédits={received}", flush=True)
                # Device-info responses can contain the printer's serial number.
                print(f"[BLE] Notifications: crédits={notifications.get('0101', 0)}, fins={notifications.get('1a0f0c', 0)}, autres={sum(n for key, n in notifications.items() if key not in ('0101', '1a0f0c'))}", flush=True)
        await client.stop_notify(NOTIFY)
    print("[BLE] Liaison fermée ; contrôler le ticket physique", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--address", required=True)
    parser.add_argument("--series", help="JSON list of labelled independent binary jobs")
    args = parser.parse_args()
    asyncio.run(run(args.address, args.series))
