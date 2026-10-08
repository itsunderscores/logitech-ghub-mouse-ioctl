# Implementing Logitech G HUB mouse movement in your project

This guide shows how to move the **system cursor** from your own C/C++ usermode code using Logitech G HUB’s **virtual mouse** driver stack. It matches what `logitech_ghub_mouse.exe` does internally.

> **Disclaimer:** Logitech does not document this as a public API. Paths and IOCTL behavior can change with G HUB updates. Use only on machines you own and where synthetic input is allowed (testing, accessibility tooling, etc.).

---

## Prerequisites

| Requirement | Why |
|-------------|-----|
| Windows **x64** | Drivers in this stack are x64 kernel modules |
| **Logitech G HUB** installed and running | Creates virtual HID devices and registers device interfaces |
| **Virtual mouse `PID_C231` active** | Without a live `LGHUBDEVICE\…PID_C231` node, IOCTL may **succeed but not move** the cursor |
| Link **`ntdll.lib`** (if you enumerate `\GLOBAL??`) | Optional; fixed paths work without it |

Verify with this tool:

```bat
logitech_ghub_mouse.exe
```

Choose **3) Diagnose**. You want:

```text
[OK] Virtual mouse (PID_C231) is active
```

If you see **PHANTOM** or **MISS**, follow **[FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md)**:

1. Quit G HUB completely (tray + Task Manager).
2. Device Manager → **View → Show hidden devices** → uninstall **Logitech G HUB Virtual Mouse** (and any ghost **PID_C231** entries); delete driver software if offered.
3. **Reboot**.
4. **Reinstall G HUB** — do **not** use **Transfer my current settings** / import profile.
5. Start G HUB, run Diagnose again until `[OK] Virtual mouse (PID_C231) is active`.

---

## Protocol summary

| Field | Value |
|-------|--------|
| Device interface (common) | `{1abc05c0-c378-41b9-9cef-df1aba82b015}` (xlcore) |
| Alternate (bus enumerator) | `{dfbedcdb-2148-416d-9e4d-cecc2424128c}` |
| Win32 path example | `\\.\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}` |
| Mouse IOCTL | `0x2A2010` |
| Report (current G HUB) | **8 bytes**, little-endian |

### 8-byte report layout (modern G HUB)

```cpp
#pragma pack(push, 1)
struct GHubMouseReport8 {
    std::uint8_t buttons;   // 0 = none; 0x01 left down, etc.
    std::uint8_t reserved;  // 0
    std::int16_t dx;        // relative X (pixels per report, driver-dependent)
    std::int16_t dy;        // relative Y
    std::int8_t  wheel;     // vertical wheel delta
    std::uint8_t unknown;   // 0
};
#pragma pack(pop)
static_assert(sizeof(GHubMouseReport8) == 8);
```

Movement is **relative** only. For large deltas, send multiple IOCTLs with `dx`/`dy` clamped to **`int16_t`** range (see helper below).

---

## Minimal integration (single `.cpp` file)

### 1. Open the device once

Try known paths (newest / mouse node first), or enumerate `\GLOBAL??` for `ROOT#SYSTEM#…` names ending with the GUID.

```cpp
#include <windows.h>

static const wchar_t* kGHubPaths[] = {
    L"\\\\.\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
    L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
    L"\\\\.\\ROOT#SYSTEM#0001#{dfbedcdb-2148-416d-9e4d-cecc2424128c}",
};

HANDLE OpenGHubMouseDevice() {
    for (const wchar_t* path : kGHubPaths) {
        HANDLE h = CreateFileW(
            path, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;
    }
    return INVALID_HANDLE_VALUE;
}
```

Keep the handle for the lifetime of your app (do **not** `CreateFile` on every frame).

### 2. Send relative movement

```cpp
constexpr DWORD IOCTL_MOUSE = 0x2A2010;

bool GHubMoveRelative(HANDLE device, int dx, int dy) {
    while (dx != 0 || dy != 0) {
        const auto sx = static_cast<std::int16_t>(
            dx > 32767 ? 32767 : dx < -32767 ? -32767 : dx);
        const auto sy = static_cast<std::int16_t>(
            dy > 32767 ? 32767 : dy < -32767 ? -32767 : dy);

        GHubMouseReport8 rep{};
        rep.dx = sx;
        rep.dy = sy;

        DWORD bytes = 0;
        if (!DeviceIoControl(device, IOCTL_MOUSE, &rep, sizeof(rep),
                             nullptr, 0, &bytes, nullptr))
            return false;

        dx -= sx;
        dy -= sy;
    }
    return true;
}
```

### 3. Example usage

```cpp
HANDLE mouse = OpenGHubMouseDevice();
if (mouse == INVALID_HANDLE_VALUE) {
    // G HUB not running, wrong GUID, or access denied
    return;
}

GHubMoveRelative(mouse, 100, 0);   // ~100 px right
GHubMoveRelative(mouse, -50, 25);

CloseHandle(mouse);
```

---

## Recommended project structure

For anything beyond a quick test, split logic into a small module:

```text
your_project/
  src/
    logitech_ghub_mouse.hpp   // open + move + optional diagnose helpers
    your_app.cpp
```

Pattern used in production-style code:

1. **Lazy open** on first move; **reuse** handle.
2. **Mutex** if multiple threads can call move (IOCTL + handle are not thread-safe unless you serialize).
3. On `DeviceIoControl` failure, **close** handle and reopen on next move.
4. **Clamp** per-step delta (e.g. ±200) in aim/smooth loops so one frame = one IOCTL.

Example call site (comment-switch between input methods):

```cpp
// WriteMemoryAim(...);
// DriverMoveMouse(dx, dy);
GHubMoveRelative(g_ghub_device, (long)target_x, (long)target_y);
```

---

## Optional: enumerate `\GLOBAL??`

If fixed paths fail on a new G HUB build, scan the object directory (same as `main.cpp`):

- `NtOpenDirectoryObject` + `NtQueryDirectoryObject` on `\GLOBAL??`
- Filter names: `ROOT#SYSTEM#` + suffix `{1abc05c0-…}` or `{dfbedcdb-…}`
- Prefer `#0002#` over `#0001#` when both exist
- Open with `\\.\` + name (see `enumerateLogitechDevicePaths()` in `main.cpp`)

Link: `#pragma comment(lib, "ntdll.lib")`

---

## Driver stack (why diagnose matters)

```text
Your process
  CreateFile(ROOT#SYSTEM#…{1abc…})
    -> logi_joy_xlcore (filter, forwards IOCTL)
    -> logi_joy_bus_enum (handles 0x2A2010, finds virtual mouse PDO)
    -> logi_joy_vir_hid -> HID class -> cursor
```

If **C231** is missing or **phantom**, the bus driver may complete the IOCTL with **success** and do **nothing**. Always treat **PnP state** as part of your health check, not only `DeviceIoControl` return value.

---

## Troubleshooting

| Symptom | Likely cause | Action |
|---------|----------------|--------|
| `CreateFile` fails | G HUB off, wrong path | Run `--list` / menu **4**; start G HUB |
| IOCTL OK, no movement | Phantom **C231** | Follow [FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md) |
| Movement stops after G HUB update | Driver/API change | Re-check GUID, report size, IOCTL in updated `.sys` |
| Only small moves work | Clamping / game raw input | Expected; use smooth multi-step motion |

### Optional IOCTL `0x2A2044`

Some driver builds register a process whitelist and may alter report handling when active. If movement fails despite `[OK] C231`, research `0x2A2044` on your `logi_joy_bus_enum` build (register current process PID before `0x2A2010`). Not required on all installs.

---

## What not to use this for

- **Absolute** cursor position (use another API or compute relative steps yourself)
- **Logitech physical mouse HID++** on Windows (different path: G HUB agent / USB HID)
- **Linux** (this IOCTL stack is Windows-only)

For a working reference implementation, see **`main.cpp`** in this repository (`GhubMouseDevice` class and interactive menu).
