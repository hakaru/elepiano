#!/usr/bin/env python3
"""elepiano BLE Bridge - iOS から BLE 経由で CC/PC を制御する"""

import asyncio
import signal
import sys
import json

from alsa_midi_sender import AlsaMidiSender
from status_monitor import StatusMonitor
from gatt_service import GattApplication

from dbus_next.aio import MessageBus
from dbus_next import BusType

STATUS_SOCKET = "/tmp/elepiano-status.sock"


class BleBridge:
    def __init__(self):
        self.midi = AlsaMidiSender("ble-bridge")
        self.status = StatusMonitor(STATUS_SOCKET)
        self.gatt: GattApplication | None = None
        self._running = True

    def on_cc(self, cc: int, value: int) -> None:
        """BLE CC 書き込み → ALSA MIDI CC 送信"""
        self.midi.send_cc(0, cc, value)

    def on_pc(self, program: int) -> None:
        """BLE Program Change → ALSA MIDI PC 送信"""
        self.midi.send_program_change(0, program)

    def on_status(self, status: dict) -> None:
        """elepiano ステータス更新 → BLE Status Notify"""
        if self.gatt:
            self.gatt.status_char.update_value(json.dumps(status))
            if "program" in status:
                self.gatt.pc_char.update_value(status["program"] - 1)

    async def run(self) -> None:
        print("elepiano BLE Bridge starting...")

        # D-Bus 接続
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()

        # GATT アプリケーション
        self.gatt = GattApplication(
            on_cc=self.on_cc,
            on_pc=self.on_pc,
        )
        await self.gatt.register(bus)

        # ステータス監視
        self.status.set_callback(self.on_status)
        status_task = asyncio.create_task(self.status.run())

        print("BLE Bridge running. Ctrl+C to stop.")

        # メインループ
        try:
            while self._running:
                await asyncio.sleep(1.0)
        except asyncio.CancelledError:
            pass
        finally:
            self.status.stop()
            status_task.cancel()
            self.midi.close()
            bus.disconnect()
            print("BLE Bridge stopped.")

    def stop(self) -> None:
        self._running = False


def main() -> None:
    bridge = BleBridge()

    loop = asyncio.new_event_loop()

    def signal_handler():
        bridge.stop()

    loop.add_signal_handler(signal.SIGINT, signal_handler)
    loop.add_signal_handler(signal.SIGTERM, signal_handler)

    try:
        loop.run_until_complete(bridge.run())
    finally:
        loop.close()


if __name__ == "__main__":
    main()
