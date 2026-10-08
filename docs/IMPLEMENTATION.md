# Integrate the Logitech mouse simulator (G HUB)

Add **software-simulated** relative mouse movement to your C++ app. This uses G HUB’s **virtual mouse** driver path—the same mechanism as the test executable. **No physical Logitech hardware is required**; only G HUB installed on Windows x64.

G HUB exposes a usermode IOCTL on its virtual device node. Sending reports there makes Windows treat input as if it came from Logitech’s simulated mouse—an **unintended capability in Logitech’s software stack**, not a supported public API.

> **Reboot behavior:** After a **reboot**, run the **G HUB installer → Repair** and **do not** transfer previous settings before `move_relative()` works again. See **[FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md)**.

---

## Files to copy

| File | Role |
|------|------|
| `logitech_ghub_mouse.hpp` | Public API |
| `logitech_ghub_mouse.cpp` | Opens G HUB device, sends `0x2A2010` reports |

The demo `main.cpp` in this repo is optional.

---

## Visual Studio

1. Copy both files into your project.
2. **Add → Existing Item** for `.hpp` and `.cpp`.
3. Build **x64** (matches G HUB drivers).
4. Linker needs `ntdll.lib` and `setupapi.lib` (already `#pragma comment` in the `.cpp`; add manually if link fails).

```cpp
#include "logitech_ghub_mouse.hpp"

if (!logitech_ghub::virtual_mouse_active()) {
    // G HUB Repair, no settings transfer — see FIX_VIRTUAL_MOUSE.md
}

logitech_ghub::move_relative(dx, dy);
```

---

## API

### Quick: `move_relative`

Singleton handle, mutex-protected, opens on first use.

```cpp
logitech_ghub::move_relative(100, 0);
logitech_ghub::close_shared();  // optional
```

### Explicit: `Mouse` class

```cpp
logitech_ghub::Mouse sim;
sim.set_layout(logitech_ghub::ReportLayout::Bytes8);
if (sim.open()) {
    sim.move_relative(-20, 15);
    sim.click_left();
}
```

Optional fixed path (from Diagnose / list):

```cpp
sim.open(L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}");
```

### Diagnostics

```cpp
auto h = logitech_ghub::query_virtual_input_health();
// h.mouse_present, h.mouse_phantom

auto paths = logitech_ghub::enumerate_device_paths();
```

`logitech_ghub::kMouseIoctl` = `0x2A2010`.

---

## CMake

```cmake
add_library(logitech_ghub_mouse STATIC logitech_ghub_mouse.cpp)
target_include_directories(logitech_ghub_mouse PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(logitech_ghub_mouse PUBLIC ntdll setupapi)
target_link_libraries(your_app PRIVATE logitech_ghub_mouse)
```

---

## Switch input methods in your code

```cpp
// other_aim_method(dx, dy);
logitech_ghub::move_relative(static_cast<int>(dx), static_cast<int>(dy));
```

---

## Requirements

| Need | Notes |
|------|--------|
| Windows x64 | |
| G HUB running | Creates virtual C231 |
| Active virtual mouse | Diagnose → `[OK]` |

**Not needed:** USB Logitech mouse, Logitech SDK.

---

## Protocol (modern G HUB)

| Field | Value |
|-------|--------|
| GUID | `{1abc05c0-c378-41b9-9cef-df1aba82b015}` |
| IOCTL | `0x2A2010` |
| Report | 8 bytes: `buttons`, `reserved`, `int16 dx`, `int16 dy`, `wheel`, `unk` |

Relative only; large deltas are split inside `move_relative()`.

---

## Stack

```text
Your app → ROOT device → xlcore → bus_enum (0x2A2010) → vir_hid → cursor
```

---

## Troubleshooting

| Issue | What to do |
|-------|------------|
| Stopped working after **reboot** | G HUB installer → **Repair**, **no** settings transfer (often every reboot) |
| `open()` fails | Start G HUB; list paths with `enumerate_device_paths()` |
| IOCTL OK, no movement | **[FIX_VIRTUAL_MOUSE.md](FIX_VIRTUAL_MOUSE.md)** — **Repair**, **do not** transfer settings |
| Broke after G HUB update | Repair again; re-check GUID/report size if needed |

---

## Scope

- Windows only; relative movement only.
- Not for absolute cursor teleport (accumulate deltas yourself).

Reference behavior: **`main.cpp`** interactive menu in this repository.
