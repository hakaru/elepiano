#!/usr/bin/env python3
"""elepiano BLE Bridge - iOS から BLE 経由で CC/PC を制御する"""

import asyncio
import signal
import subprocess
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
        print(f"[BLE→MIDI] CC {cc} = {value}")
        self.midi.send_cc(0, cc, value)

    def on_pc(self, program: int) -> None:
        """BLE Program Change → ALSA MIDI PC 送信"""
        print(f"[BLE→MIDI] PC {program}")
        self.midi.send_program_change(0, program)

    def on_command(self, cmd: str) -> None:
        """BLE コマンド書き込み → systemctl 等の操作"""
        print(f"[BLE→CMD] {cmd}")
        if cmd == "restart_elepiano":
            subprocess.Popen(
                ["systemctl", "restart", "elepiano"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        elif cmd == "restart_ble_bridge":
            subprocess.Popen(
                ["systemctl", "restart", "ble-bridge"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            )
        else:
            print(f"[BLE→CMD] unknown command: {cmd}")

    def on_status(self, status: dict) -> None:
        """elepiano ステータス更新 → BLE Status Notify"""
        # elepiano_connected フラグを追加
        status["elepiano_connected"] = True
        if self.gatt:
            self.gatt.status_char.update_value(json.dumps(status))
            if "program" in status:
                self.gatt.pc_char.update_value(status["program"] - 1)

    def _send_disconnected_status(self) -> None:
        """elepiano 未接続時のステータスを送信"""
        if self.gatt:
            status = {"elepiano_connected": False, "running": False}
            self.gatt.status_char.update_value(json.dumps(status))

    async def run(self) -> None:
        print("elepiano BLE Bridge starting...")

        # D-Bus 接続
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()

        # GATT アプリケーション
        self.gatt = GattApplication(
            on_cc=self.on_cc,
            on_pc=self.on_pc,
            on_command=self.on_command,
        )
        await self.gatt.register(bus)

        # ステータス監視
        self.status.set_callback(self.on_status)
        status_task = asyncio.create_task(self.status.run())

        print("BLE Bridge running. Ctrl+C to stop.")

        # メインループ — elepiano 未接続時は定期的に通知
        try:
            while self._running:
                await asyncio.sleep(2.0)
                # elepiano 未接続なら disconnected ステータスを送信
                if not self.status.last_status:
                    self._send_disconnected_status()
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
