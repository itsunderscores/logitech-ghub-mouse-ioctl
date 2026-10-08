# Implementing Logitech G HUB mouse movement in your project

Use the **`logitech_ghub_mouse.hpp`** + **`logitech_ghub_mouse.cpp`** pair from this repository. They contain all `CreateFile` / `DeviceIoControl` logic; the demo executable (`main.cpp`) is optional.

> **Disclaimer:** Unsupported interface; verify against your G HUB build. Use only where synthetic input is allowed.

---

## Quick start (Visual Studio)

1. Copy into your project folder:
   - `logitech_ghub_mouse.hpp`
   - `logitech_ghub_mouse.cpp`
2. In Visual Studio: **Add → Existing Item** for both files (or drag into your project).
3. Ensure the project is **x64** (same as G HUB drivers).
4. Link libraries (already requested in the `.cpp` via `#pragma comment`):
   - `ntdll.lib`
   - `setupapi.lib`  
   If linking fails, add them under **Project → Properties → Linker → Input → Additional Dependencies**.
5. Include the header where you move the mouse:

```cpp
#include "logitech_ghub_mouse.hpp"
```

6. Confirm virtual mouse is active before relying on movement:

```cpp
if (!logitech_ghub::virtual_mouse_active()) {
    // C231 phantom/missing — see docs/FIX_VIRTUAL_MOUSE.md
}
```

---

## API overview

### Option A — One-liner (singleton, thread-safe)

Best for aim loops or quick tests. Opens the device on first call and reuses the handle.

```cpp
#include "logitech_ghub_mouse.hpp"

void tick_aim(int dx, int dy) {
    logitech_ghub::move_relative(dx, dy);
}

// Optional shutdown:
logitech_ghub::close_shared();
```

### Option B — `Mouse` class (your own instance)

Best when you want an explicit lifecycle or multiple configurations.

```cpp
#include "logitech_ghub_mouse.hpp"

logitech_ghub::Mouse mouse;
mouse.set_layout(logitech_ghub::ReportLayout::Bytes8);

if (mouse.open()) {
    mouse.move_relative(100, -40);
    mouse.click_left();
    mouse.close();
}
```

Force a specific device path (from `--list` / menu **4**):

```cpp
mouse.open(L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}");
```

### Health / debugging

```cpp
logitech_ghub::VirtualInputHealth h = logitech_ghub::query_virtual_input_health();
// h.mouse_present, h.mouse_phantom, h.mouse_instance_id

auto paths = logitech_ghub::enumerate_device_paths();
```

Constants: `logitech_ghub::kMouseIoctl` (`0x2A2010`).

---

## CMake example

```cmake
add_library(logitech_ghub_mouse STATIC
    third_party/logitech_ghub_mouse/logitech_ghub_mouse.cpp
)
target_include_directories(logitech_ghub_mouse PUBLIC
    third_party/logitech_ghub_mouse
)
target_link_libraries(logitech_ghub_mouse PUBLIC ntdll setupapi)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE logitech_ghub_mouse)
```

---

## Example: switch between input methods in code

Comment/uncomment the line you want (same pattern as a typical game loop):

```cpp
#include "logitech_ghub_mouse.hpp"

void apply_aim(float target_x, float target_y) {
    const int dx = static_cast<int>(target_x);
    const int dy = static_cast<int>(target_y);

    // your_memory_aim(dx, dy);
    // your_driver_move(dx, dy);
    logitech_ghub::move_relative(dx, dy);
}
```

---

## Prerequisites

| Requirement | Why |
|-------------|-----|
| Windows **x64** | G HUB kernel stack is x64 |
| **G HUB running** | Virtual devices and ROOT symlinks |
| **Active C231** | Phantom mouse → IOCTL success, no cursor move |

If diagnose shows **PHANTOM** or **MISS**, follow **[FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md)**.

---

## Protocol summary

| Field | Value |
|-------|--------|
| Interface GUID | `{1abc05c0-c378-41b9-9cef-df1aba82b015}` |
| Alternate GUID | `{dfbedcdb-2148-416d-9e4d-cecc2424128c}` |
| Mouse IOCTL | `0x2A2010` |
| Report (modern G HUB) | **8 bytes**: `buttons`, `reserved`, `int16 dx`, `int16 dy`, `wheel`, `unk` |

Movement is **relative**. Large deltas are split inside `move_relative()` (int16 per IOCTL).

---

## Driver stack (why diagnose matters)

```text
Your process → CreateFile(ROOT#SYSTEM#…)
  → logi_joy_xlcore → logi_joy_bus_enum (0x2A2010)
  → logi_joy_vir_hid → cursor
```

---

## Troubleshooting

| Symptom | Action |
|---------|--------|
| `open()` / first `move_relative` fails | Start G HUB; run `enumerate_device_paths()` |
| IOCTL OK, no movement | [FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md) |
| Movement stops after update | Re-check paths/report size on new G HUB build |

---

## What not to use this for

- Absolute cursor positioning (compute relative steps yourself)
- Physical Logitech HID++ on Windows (different stack)
- Linux (Windows-only)

Reference demo: **`main.cpp`** in this repo (interactive menu + CLI).
