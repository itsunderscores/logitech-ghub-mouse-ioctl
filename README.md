# logitech-ghub-mouse-ioctl

Windows console tool to test **relative mouse movement** through **Logitech G HUB**’s virtual mouse driver (`PID_C231`) via a documented-in-the-wild IOCTL path—not an official Logitech SDK.

Use it to confirm your G HUB install can inject cursor input before wiring the same logic into your own C++ code.

---

## Features

- **Interactive demo menu** (default when run with no arguments)
  - Random move (100–200 px on X and Y)
  - Smooth human-like drift (hold **A** to stop)
  - PnP **diagnose** (virtual keyboard/mouse + ROOT symlinks)
  - List Logitech **ROOT#SYSTEM#** device paths
- **Legacy CLI** for scripts: `logitech_ghub_mouse.exe 100 0`, `--diagnose`, `--list`, etc.
- Reference implementation in **`main.cpp`** (`GhubMouseDevice`, 8-byte G HUB report)

---

## Requirements

- Windows **x64**
- **Logitech G HUB** installed and running
- Kernel drivers loaded, e.g. `logi_joy_xlcore`, `logi_joy_bus_enum`, `logi_joy_vir_hid`

```bat
driverquery /v | findstr /i logi_joy
```

- **Logitech G HUB Virtual Mouse** (`VID_046D&PID_C231`) **working** in Device Manager (not phantom)

Run diagnose in the app (menu **3**) or:

```bat
logitech_ghub_mouse.exe --diagnose
```

You want: `[OK] Virtual mouse (PID_C231) is active`.

If you see **PHANTOM** or **MISS**, use **[docs/FIX_VIRTUAL_MOUSE.md](docs/FIX_VIRTUAL_MOUSE.md)** (uninstall virtual mouse in Device Manager, reboot, reinstall G HUB **without** importing/transferring settings).

---

## Build

**Visual Studio 2022:** open `logitech_ghub_mouse.sln`, configuration **Release | x64**, build.

Output (typical): `bin\x64\Release\logitech_ghub_mouse.exe` (path may vary by project settings).

**MSBuild:**

```bat
msbuild logitech_ghub_mouse.sln /p:Configuration=Release /p:Platform=x64
```

Dependencies: `ntdll.lib`, `setupapi.lib` (linked via `#pragma comment` in `main.cpp`).

---

## Run

### Demo menu

```bat
logitech_ghub_mouse.exe
```

| Key | Action |
|-----|--------|
| **1** | One random relative move (100–200 px per axis) |
| **2** | Continuous smooth movement; hold **A** to stop |
| **3** | Diagnose virtual devices and symlinks |
| **4** | List ROOT device paths |
| **0** | Exit |

### Legacy CLI

```bat
logitech_ghub_mouse.exe 200 -50
logitech_ghub_mouse.exe --diagnose
logitech_ghub_mouse.exe --list
logitech_ghub_mouse.exe --help
```

Movement is always **relative** (delta X/Y), not absolute screen coordinates.

---

## How it works

High-level flow:

```text
  logitech_ghub_mouse.exe
        |
        |  CreateFile("\\.\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}")
        v
  logi_joy_xlcore.sys          device interface + IRP forward
        |
        v
  logi_joy_bus_enum.sys        IOCTL 0x2A2010 -> virtual mouse instance (MouseID)
        |
        v
  logi_joy_vir_hid.sys         completes pending HID read with your report
        |
        v
  Windows HID / cursor         pointer moves on screen
```

**Usermode contract (current G HUB):**

| Item | Value |
|------|--------|
| Interface GUID | `{1abc05c0-c378-41b9-9cef-df1aba82b015}` |
| Alternate GUID | `{dfbedcdb-2148-416d-9e4d-cecc2424128c}` |
| Mouse IOCTL | `0x2A2010` |
| Payload | **8 bytes**: `buttons`, `reserved`, `int16 dx`, `int16 dy`, `wheel`, `unk` |

Logitech Gaming Software (legacy) used other GUIDs and sometimes **5-byte** reports; this repo defaults to **8-byte** for modern G HUB.

**Important:** If the virtual mouse is a **ghost/phantom** device, `DeviceIoControl` may still return success while the cursor does not move. Always check PnP state (menu **3**).

---

## Implement in your own project

See **[docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)** for:

- Minimal `CreateFile` + `DeviceIoControl` example
- Report struct and chunking large deltas
- Handle lifetime, threading, and troubleshooting
- Driver stack notes

Copy patterns from `GhubMouseDevice` in **`main.cpp`** or extract them into a header in your tree.

---

## Legal / ethics

- Reverse-engineered, **unsupported** interface; may break on any G HUB update.
- Synthetic input may violate game or software terms of service and anti-cheat policies.
- Intended for **research, debugging, and tooling on systems you control**.

---

## License

Specify a license before publishing (e.g. MIT) if you release on GitHub.
