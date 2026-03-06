"""BlueZ D-Bus GATT サービス定義 (dbus-next)"""

import json
import asyncio
from typing import Callable, Optional
from dbus_next.service import ServiceInterface, method, dbus_property
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

# BlueZ D-Bus GATT interfaces
BLUEZ_SERVICE = "org.bluez"
LE_ADVERTISING_MANAGER_IFACE = "org.bluez.LEAdvertisingManager1"
GATT_MANAGER_IFACE = "org.bluez.GattManager1"
ADAPTER_IFACE = "org.bluez.Adapter1"


class ElepianoCharacteristic:
    """GATT キャラクタリスティックの共通ベース"""

    def __init__(self, uuid: str, flags: list[str]):
        self.uuid = uuid
        self.flags = flags
        self.value = bytearray()


class Advertisement(ServiceInterface):
    """BLE Advertisement (LEAdvertisement1)"""

    def __init__(self, index: int = 0):
        self._index = index
        self._path = f"/org/bluez/elepiano/ad{index}"
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


class GattService(ServiceInterface):
    """GATT Service (GattService1)"""

    def __init__(self):
        self._path = "/org/bluez/elepiano/service0"
        super().__init__("org.bluez.GattService1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return SERVICE_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Primary(self) -> "b":  # type: ignore  # noqa: N802
        return True


class StatusCharacteristic(ServiceInterface):
    """Status (Read, Notify) - JSON ステータス"""

    def __init__(self, service_path: str):
        self._path = f"{service_path}/char0"
        self._value = bytearray()
        self._notifying = False
        super().__init__("org.bluez.GattCharacteristic1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return STATUS_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return "/org/bluez/elepiano/service0"

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
    """CC Control (Write Without Response) - [cc, val] 2 bytes"""

    def __init__(self, service_path: str, on_cc: Callable[[int, int], None]):
        self._path = f"{service_path}/char1"
        self._on_cc = on_cc
        super().__init__("org.bluez.GattCharacteristic1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return CC_CONTROL_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return "/org/bluez/elepiano/service0"

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["write-without-response"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        if len(value) >= 2:
            cc, val = value[0], value[1]
            self._on_cc(cc, min(val, 127))

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return []


class ProgramChangeCharacteristic(ServiceInterface):
    """Program Change (Read, Write, Notify) - [pg] 1 byte"""

    def __init__(self, service_path: str, on_pc: Callable[[int], None]):
        self._path = f"{service_path}/char2"
        self._on_pc = on_pc
        self._value = bytearray([0])
        super().__init__("org.bluez.GattCharacteristic1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return PROGRAM_CHANGE_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return "/org/bluez/elepiano/service0"

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
    """Batch CC (Write Without Response) - [cc1,v1,...ccN,vN] max 20 bytes"""

    def __init__(self, service_path: str, on_cc: Callable[[int, int], None]):
        self._path = f"{service_path}/char4"
        self._on_cc = on_cc
        super().__init__("org.bluez.GattCharacteristic1")

    @dbus_property(access=PropertyAccess.READ)
    def UUID(self) -> "s":  # type: ignore  # noqa: N802
        return BATCH_CC_UUID

    @dbus_property(access=PropertyAccess.READ)
    def Service(self) -> "o":  # type: ignore  # noqa: N802
        return "/org/bluez/elepiano/service0"

    @dbus_property(access=PropertyAccess.READ)
    def Flags(self) -> "as":  # type: ignore  # noqa: N802
        return ["write-without-response"]

    @method()
    def WriteValue(self, value: "ay", options: "a{sv}") -> None:  # type: ignore  # noqa: N802
        # [cc1, val1, cc2, val2, ...]
        for i in range(0, len(value) - 1, 2):
            cc, val = value[i], value[i + 1]
            self._on_cc(cc, min(val, 127))

    @method()
    def ReadValue(self, options: "a{sv}") -> "ay":  # type: ignore  # noqa: N802
        return []


class GattApplication:
    """BlueZ に登録する GATT アプリケーション"""

    def __init__(
        self,
        on_cc: Callable[[int, int], None],
        on_pc: Callable[[int], None],
    ):
        self.service = GattService()
        self.status_char = StatusCharacteristic(self.service._path)
        self.cc_char = CCControlCharacteristic(self.service._path, on_cc)
        self.pc_char = ProgramChangeCharacteristic(self.service._path, on_pc)
        self.batch_cc_char = BatchCCCharacteristic(self.service._path, on_cc)
        self.advertisement = Advertisement()

    async def register(self, bus: MessageBus) -> None:
        """BlueZ に GATT サービスと Advertisement を登録する"""
        # オブジェクトをエクスポート
        bus.export(self.service._path, self.service)
        bus.export(self.status_char._path, self.status_char)
        bus.export(self.cc_char._path, self.cc_char)
        bus.export(self.pc_char._path, self.pc_char)
        bus.export(self.batch_cc_char._path, self.batch_cc_char)
        bus.export(self.advertisement._path, self.advertisement)

        # BlueZ adapter を取得
        introspection = await bus.introspect(BLUEZ_SERVICE, "/org/bluez/hci0")
        proxy = bus.get_proxy_object(BLUEZ_SERVICE, "/org/bluez/hci0", introspection)

        # GATT Manager に登録
        gatt_mgr = proxy.get_interface(GATT_MANAGER_IFACE)
        await gatt_mgr.call_register_application(  # type: ignore
            "/org/bluez/elepiano/service0", {}
        )
        print("[GATT] application registered")

        # Advertisement を登録
        ad_mgr = proxy.get_interface(LE_ADVERTISING_MANAGER_IFACE)
        await ad_mgr.call_register_advertisement(  # type: ignore
            self.advertisement._path, {}
        )
        print("[GATT] advertisement registered")
