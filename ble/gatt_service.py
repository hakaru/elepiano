"""BlueZ D-Bus GATT サービス定義 (dbus-next)

BlueZ の RegisterApplication は ObjectManager.GetManagedObjects() で
GATT 階層を列挙するため、アプリケーションルートに ObjectManager を実装する。
"""

import json
import asyncio
from typing import Callable, Optional
from dbus_next.service import ServiceInterface, method, dbus_property, signal
from dbus_next.signature import Variant
from dbus_next import BusType, PropertyAccess
from dbus_next.aio import MessageBus

# UUIDs
SERVICE_UUID = "e1e00000-0001-4000-8000-00805f9b34fb"
STATUS_UUID = "e1e00001-0001-4000-8000-00805f9b34fb"
CC_CONTROL_UUID = "e1e00002-0001-4000-8000-00805f9b34fb"
PROGRAM_CHANGE_UUID = "e1e00003-0001-4000-8000-00805f9b34fb"
AUDIO_DEVICE_UUID = "e1e00004-0001-4000-8000-00805f9b34fb"
BATCH_CC_UUID = "e1e00005-0001-4000-8000-00805f9b34fb"
COMMAND_UUID = "e1e00006-0001-4000-8000-00805f9b34fb"

# BlueZ D-Bus
BLUEZ_SERVICE = "org.bluez"
LE_ADVERTISING_MANAGER_IFACE = "org.bluez.LEAdvertisingManager1"
GATT_MANAGER_IFACE = "org.bluez.GattManager1"

# Application root path
APP_PATH = "/org/bluez/elepiano"
SVC_PATH = f"{APP_PATH}/service0"


# ── ObjectManager ──────────────────────────────────────────────
# BlueZ calls GetManagedObjects on the application root to discover
# all services and characteristics.

class ObjectManager(ServiceInterface):
    """org.freedesktop.DBus.ObjectManager implementation"""

    def __init__(self):
        super().__init__("org.freedesktop.DBus.ObjectManager")
        self._objects: dict[str, dict[str, dict[str, Variant]]] = {}

    def add_object(self, path: str, iface: str, props: dict[str, Variant]):
        if path not in self._objects:
            self._objects[path] = {}
        self._objects[path][iface] = props

    @method()
    def GetManagedObjects(self) -> "a{oa{sa{sv}}}":  # type: ignore  # noqa: N802
        return self._objects


# ── Advertisement ──────────────────────────────────────────────

class Advertisement(ServiceInterface):
    def __init__(self):
        super().__init__("org.bluez.LEAdvertisement1")

    @dbus_property(access=PropertyAccess.READ)
    def Type(self) -> "s":  # type: ignore  # noqa: N802
        return "peripheral"

    @dbus_property(access=PropertyAccess.READ)
    def ServiceUUIDs(self) -> "as":  # type: ignore  # noqa: N802
        return [SERVICE_UUID]

    @dbus_property(access=PropertyAccess.READ)
    def LocalName(self) -> "s":  # type: ignore  # noqa: N802
        return "elepiano"

    @dbus_property(access=PropertyAccess.READ)
    def Includes(self) -> "as":  # type: ignore  # noqa: N802
        return ["tx-power"]

    @method()
    def Release(self) -> None:  # noqa: N802
        print("[Advertisement] Released")


# ── GATT Service ──────────────────────────────────────────────

class GattService1(ServiceInterface):
    """org.bluez.GattService1"""

    def __init__(self):
        super().__init__("org.bluez.GattService1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return SERVICE_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Primary(self) -> "b":  # type: ignore  # noqa: N802
        return True


# ── GATT Characteristics ──────────────────────────────────────

class StatusCharacteristic(ServiceInterface):
    """Status (Read, Notify)"""

    def __init__(self):
        super().__init__("org.bluez.GattCharacteristic1")
        self._value = bytearray()
        self._notifying = False

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return STATUS_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return SVC_PATH

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["read", "notify"]

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return list(self._value)

    @method()
    def StartNotify(self) -> None:  # noqa: N802
        self._notifying = True
        print("[StatusChar] notify started")

    @method()
    def StopNotify(self) -> None:  # noqa: N802
        self._notifying = False
        print("[StatusChar] notify stopped")

    def update_value(self, status_json: str) -> None:
        self._value = bytearray(status_json.encode("utf-8")[:512])


class CCControlCharacteristic(ServiceInterface):
    """CC Control (Write Without Response) - [cc, val] 2B"""

    def __init__(self, on_cc: Callable[[int, int], None]):
        super().__init__("org.bluez.GattCharacteristic1")
        self._on_cc = on_cc

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return CC_CONTROL_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return SVC_PATH

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["write-without-response"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        if len(value) >= 2:
            self._on_cc(value[0], min(value[1], 127))

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return []


class ProgramChangeCharacteristic(ServiceInterface):
    """Program Change (Read, Write, Notify) - [pg] 1B"""

    def __init__(self, on_pc: Callable[[int], None]):
        super().__init__("org.bluez.GattCharacteristic1")
        self._on_pc = on_pc
        self._value = bytearray([0])

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return PROGRAM_CHANGE_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return SVC_PATH

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["read", "write", "notify"]

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return list(self._value)

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        if len(value) >= 1:
            pg = value[0]
            self._value = bytearray([pg])
            self._on_pc(pg)

    @method()
    def StartNotify(self) -> None:  # noqa: N802
        pass

    @method()
    def StopNotify(self) -> None:  # noqa: N802
        pass

    def update_value(self, pg: int) -> None:
        self._value = bytearray([pg])


class BatchCCCharacteristic(ServiceInterface):
    """Batch CC (Write Without Response) - [cc1,v1,...ccN,vN]"""

    def __init__(self, on_cc: Callable[[int, int], None]):
        super().__init__("org.bluez.GattCharacteristic1")
        self._on_cc = on_cc

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return BATCH_CC_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return SVC_PATH

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["write-without-response"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        for i in range(0, len(value) - 1, 2):
            self._on_cc(value[i], min(value[i + 1], 127))

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return []


class CommandCharacteristic(ServiceInterface):
    """Command (Write) - UTF-8 コマンド文字列"""

    def __init__(self, on_command: Callable[[str], None]):
        super().__init__("org.bluez.GattCharacteristic1")
        self._on_command = on_command

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return COMMAND_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return SVC_PATH

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["write"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        try:
            cmd = bytes(value).decode("utf-8").strip()
            if cmd:
                self._on_command(cmd)
        except Exception as e:
            print(f"[CommandChar] error: {e}")

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return []


# ── GattApplication ───────────────────────────────────────────

def _char_props(uuid: str, service: str, flags: list[str]) -> dict[str, Variant]:
    """GetManagedObjects 用の characteristic プロパティ dict"""
    return {
        "UUID": Variant("s", uuid),
        "Service": Variant("o", service),
        "Flags": Variant("as", flags),
    }


def _svc_props(uuid: str, primary: bool) -> dict[str, Variant]:
    return {
        "UUID": Variant("s", uuid),
        "Primary": Variant("b", primary),
    }


class GattApplication:
    """BlueZ に登録する GATT アプリケーション"""

    def __init__(
        self,
        on_cc: Callable[[int, int], None],
        on_pc: Callable[[int], None],
        on_command: Callable[[str], None] = lambda cmd: None,
    ):
        self.obj_mgr = ObjectManager()
        self.gatt_svc = GattService1()
        self.status_char = StatusCharacteristic()
        self.cc_char = CCControlCharacteristic(on_cc)
        self.pc_char = ProgramChangeCharacteristic(on_pc)
        self.batch_cc_char = BatchCCCharacteristic(on_cc)
        self.command_char = CommandCharacteristic(on_command)
        self.advertisement = Advertisement()

        # ObjectManager に GATT 階層を登録
        CHAR_IFACE = "org.bluez.GattCharacteristic1"
        SVC_IFACE = "org.bluez.GattService1"

        self.obj_mgr.add_object(SVC_PATH, SVC_IFACE,
                                _svc_props(SERVICE_UUID, True))
        self.obj_mgr.add_object(f"{SVC_PATH}/char0", CHAR_IFACE,
                                _char_props(STATUS_UUID, SVC_PATH, ["read", "notify"]))
        self.obj_mgr.add_object(f"{SVC_PATH}/char1", CHAR_IFACE,
                                _char_props(CC_CONTROL_UUID, SVC_PATH, ["write-without-response"]))
        self.obj_mgr.add_object(f"{SVC_PATH}/char2", CHAR_IFACE,
                                _char_props(PROGRAM_CHANGE_UUID, SVC_PATH, ["read", "write", "notify"]))
        self.obj_mgr.add_object(f"{SVC_PATH}/char3", CHAR_IFACE,
                                _char_props(BATCH_CC_UUID, SVC_PATH, ["write-without-response"]))
        self.obj_mgr.add_object(f"{SVC_PATH}/char4", CHAR_IFACE,
                                _char_props(COMMAND_UUID, SVC_PATH, ["write"]))

    async def register(self, bus: MessageBus) -> None:
        """BlueZ に GATT サービスと Advertisement を登録する"""
        # ObjectManager をアプリケーションルートにエクスポート
        bus.export(APP_PATH, self.obj_mgr)

        # GattService1 をサービスパスにエクスポート
        bus.export(SVC_PATH, self.gatt_svc)

        # 各 characteristic をエクスポート（BlueZ が D-Bus メソッドを呼ぶため）
        bus.export(f"{SVC_PATH}/char0", self.status_char)
        bus.export(f"{SVC_PATH}/char1", self.cc_char)
        bus.export(f"{SVC_PATH}/char2", self.pc_char)
        bus.export(f"{SVC_PATH}/char3", self.batch_cc_char)
        bus.export(f"{SVC_PATH}/char4", self.command_char)

        # Advertisement
        ad_path = f"{APP_PATH}/ad0"
        bus.export(ad_path, self.advertisement)

        # BlueZ adapter
        introspection = await bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        proxy = bus.get_proxy_object(BLUEZ_SERVICE, "/org/bluez/hci0", introspection)

        # GATT Manager に登録（ObjectManager のあるパスを渡す）
        gatt_mgr = proxy.get_interface(GATT_MANAGER_IFACE)
        await gatt_mgr.call_register_application(APP_PATH, {})  # type: ignore
        print("[GATT] application registered")

        # Advertisement を登録
        ad_mgr = proxy.get_interface(LE_ADVERTISING_MANAGER_IFACE)
        await ad_mgr.call_register_advertisement(ad_path, {})  # type: ignore
        print("[GATT] advertisement registered")
