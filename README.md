# Logitech Mouse Simulator (G HUB)

Windows **Logitech mouse simulator** that moves the system cursor through **Logitech G HUB software alone**. It talks to G HUB’s built-in **virtual mouse** (`PID_C231`) over a kernel device interface—**you do not need a physical Logitech mouse plugged in**.

This works because G HUB installs a software virtual HID device and exposes a usermode path any local process can write to. That behavior is an **unintended trust boundary in Logitech’s driver stack** (install G HUB → virtual mouse appears → IOCTL injection moves the cursor).

Use the included **test app** to verify movement, then drop the **`.hpp` / `.cpp` library** into your own project.

---

## Features

- **Interactive demo menu** (run the exe with no arguments)
  - Random relative move (100–200 px per axis)
  - Smooth continuous movement (hold **A** to stop)
  - **Diagnose** virtual mouse / keyboard and device paths
  - List G HUB **ROOT#SYSTEM#** symlinks
- **CLI** for scripts: `logitech_ghub_mouse.exe 100 0`, `--diagnose`, `--list`
- **Drop-in simulator module:** `logitech_ghub_mouse.hpp` + `logitech_ghub_mouse.cpp`
- Optional demo UI in `main.cpp` (not required for integration)

---

## What you need

| Required | Not required |
|----------|----------------|
| Windows **x64** | Physical Logitech mouse |
| **Logitech G HUB** installed and running | Official Logitech SDK or API |
| Virtual mouse **active** (see Diagnose) | |

G HUB loads drivers such as `logi_joy_xlcore`, `logi_joy_bus_enum`, and `logi_joy_vir_hid` when it is running.

Check virtual mouse status: run the app → **3) Diagnose**, or:

```bat
logitech_ghub_mouse.exe --diagnose
```

You want: **`[OK] Virtual mouse (PID_C231) is active`**.

### Mouse not moving?

**Reinstall Logitech G HUB and do not keep your previous settings.**

1. Uninstall G HUB (Settings → Apps).
2. Reinstall from Logitech.
3. On first setup, **decline** **Transfer my current settings**, **import profile**, or any “restore backup” option—use a **clean** install.
4. Start G HUB, wait ~30 seconds, run **Diagnose** again.

More detail: **[docs/FIX_VIRTUAL_MOUSE.md](docs/FIX_VIRTUAL_MOUSE.md)**.

---

## Build

**Visual Studio 2022:** open `logitech_ghub_mouse.sln`, **Release | x64**, build.

**MSBuild:**

```bat
msbuild logitech_ghub_mouse.sln /p:Configuration=Release /p:Platform=x64
```

Links `ntdll.lib` and `setupapi.lib` (via `#pragma comment` in `logitech_ghub_mouse.cpp`).

---

## Run the simulator test app

```bat
logitech_ghub_mouse.exe
```

| Input | Action |
|-------|--------|
| **1** | Random relative move |
| **2** | Smooth movement (hold **A** to stop) |
| **3** | Diagnose |
| **4** | List device paths |
| **0** | Exit |

```bat
logitech_ghub_mouse.exe 200 -50
logitech_ghub_mouse.exe --diagnose
```

Movement is **relative** (delta X/Y), not absolute screen position.

---

## How the simulator works

G HUB creates a **fake Logitech mouse** in the kernel. Your program opens a ROOT device object and sends an **8-byte HID-style report** with IOCTL **`0x2A2010`**. The driver delivers that report as if it came from the virtual device; Windows moves the cursor.

```text
  Your app (simulator)
        |
        |  CreateFile("\\.\ROOT#SYSTEM#…#{1abc05c0-…}")
        v
  logi_joy_xlcore.sys       forwards IOCTL
        v
  logi_joy_bus_enum.sys     0x2A2010 → virtual mouse (C231)
        v
  logi_joy_vir_hid.sys      HID read completion
        v
  Cursor moves
```

| Item | Value |
|------|--------|
| Device GUID | `{1abc05c0-c378-41b9-9cef-df1aba82b015}` |
| Alternate GUID | `{dfbedcdb-2148-416d-9e4d-cecc2424128c}` |
| Mouse IOCTL | `0x2A2010` |
| Report | 8 bytes: buttons, reserved, int16 dx, int16 dy, wheel, unk |

If the virtual mouse is **phantom**, IOCTL may still return success but the cursor will **not** move—run **Diagnose** and reinstall G HUB without saved settings.

---

## Add the simulator to your C++ project

Copy into your tree and add both files to your **x64** target:

| File | Purpose |
|------|---------|
| `logitech_ghub_mouse.hpp` | API |
| `logitech_ghub_mouse.cpp` | G HUB IOCTL backend |

```cpp
#include "logitech_ghub_mouse.hpp"

logitech_ghub::move_relative(50, -10);
```

Full guide: **[docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)**.

---

## Legal / ethics

- Unofficial interface; Logitech may change or remove it in any G HUB update.
- Synthetic input may break application terms of service or trigger anti-cheat.
- For research and tooling on systems you control.
