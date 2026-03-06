"""elepiano のステータスを UNIX domain socket から取得する"""

import asyncio
import json
from typing import Callable, Optional


class StatusMonitor:
    """elepiano の StatusReporter (UNIX socket) に接続してステータスを監視する"""

    def __init__(self, socket_path: str = "/tmp/elepiano-status.sock"):
        self.socket_path = socket_path
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._running = False
        self._last_status: dict = {}
        self._on_status: Optional[Callable[[dict], None]] = None

    @property
    def last_status(self) -> dict:
        return self._last_status

    def set_callback(self, cb: Callable[[dict], None]) -> None:
        self._on_status = cb

    async def connect(self) -> bool:
        try:
            self._reader, self._writer = await asyncio.open_unix_connection(
                self.socket_path
            )
            print(f"[StatusMonitor] connected to {self.socket_path}")
            return True
        except (FileNotFoundError, ConnectionRefusedError) as e:
            print(f"[StatusMonitor] connect failed: {e}")
            return False

    async def run(self) -> None:
        self._running = True
        while self._running:
            if not self._reader:
                if not await self.connect():
                    await asyncio.sleep(2.0)
                    continue

            try:
                line = await asyncio.wait_for(
                    self._reader.readline(), timeout=5.0
                )
                if not line:
                    # 接続切断
                    print("[StatusMonitor] disconnected, reconnecting...")
                    self._reader = None
                    self._writer = None
                    await asyncio.sleep(1.0)
                    continue

                status = json.loads(line.decode().strip())
                self._last_status = status
                if self._on_status:
                    self._on_status(status)

            except asyncio.TimeoutError:
                continue
            except (json.JSONDecodeError, ConnectionResetError):
                self._reader = None
                self._writer = None
                await asyncio.sleep(1.0)

    def stop(self) -> None:
        self._running = False
        if self._writer:
            self._writer.close()
