import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "ble" / "status_monitor.py"

spec = importlib.util.spec_from_file_location("status_monitor", MODULE_PATH)
status_monitor = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(status_monitor)


class _FakeReader:
    def __init__(self, lines):
        self._lines = list(lines)

    async def readline(self):
        if self._lines:
            return self._lines.pop(0)
        return b""


class _FakeWriter:
    def __init__(self):
        self.closed = False

    def close(self):
        self.closed = True


class StatusMonitorTest(unittest.IsolatedAsyncioTestCase):
    async def test_connect_returns_false_on_missing_socket(self) -> None:
        mon = status_monitor.StatusMonitor("/tmp/not-found.sock")

        with patch.object(
            status_monitor.asyncio,
            "open_unix_connection",
            side_effect=FileNotFoundError("missing"),
        ):
            ok = await mon.connect()

        self.assertFalse(ok)

    async def test_run_updates_last_status_and_calls_callback(self) -> None:
        mon = status_monitor.StatusMonitor("/tmp/test.sock")
        reader = _FakeReader([b'{"running": true, "program": 1}\n'])
        writer = _FakeWriter()
        seen = []

        def _on_status(st):
            seen.append(st)
            mon.stop()

        mon.set_callback(_on_status)

        with patch.object(
            status_monitor.asyncio,
            "open_unix_connection",
            return_value=(reader, writer),
        ):
            await mon.run()

        self.assertEqual(1, len(seen))
        self.assertTrue(mon.last_status["running"])
        self.assertEqual(1, mon.last_status["program"])

    async def test_disconnect_resets_stream_handles(self) -> None:
        mon = status_monitor.StatusMonitor("/tmp/test.sock")
        mon._reader = _FakeReader([b""])
        mon._writer = _FakeWriter()
        mon._last_status = {"running": True}

        async def _fast_sleep(_):
            mon.stop()

        with patch.object(status_monitor.asyncio, "sleep", side_effect=_fast_sleep):
            await mon.run()

        self.assertIsNone(mon._reader)
        self.assertIsNone(mon._writer)
        # 現状実装では切断時に last_status は維持される（既知課題は expectedFailure で別テスト化）
        self.assertTrue(mon.last_status.get("running"))

    @unittest.expectedFailure
    async def test_disconnect_should_clear_last_status(self) -> None:
        # 望ましい挙動: 切断時に stale status をクリアし、UI へ disconnected を反映しやすくする。
        mon = status_monitor.StatusMonitor("/tmp/test.sock")
        mon._reader = _FakeReader([b""])
        mon._writer = _FakeWriter()
        mon._last_status = {"running": True}

        async def _fast_sleep(_):
            mon.stop()

        with patch.object(status_monitor.asyncio, "sleep", side_effect=_fast_sleep):
            await mon.run()

        self.assertEqual({}, mon.last_status)


if __name__ == "__main__":
    unittest.main()
