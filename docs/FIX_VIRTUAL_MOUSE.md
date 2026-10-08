# Fix PHANTOM or MISS virtual mouse (PID_C231)

Diagnose shows **`[DEAD] PHANTOM`**, **`[MISS]`**, or Device Manager reports *“Currently, this hardware device is not connected to the computer”* (problem code **45**) for **Logitech G HUB Virtual Mouse**. IOCTL calls may still return success, but the **cursor will not move** until C231 is a live device.

Follow these steps in order.

---

## 1. Quit Logitech G HUB completely

- Exit from the system tray (right-click G HUB → Quit).
- Open Task Manager and end any remaining processes, for example:
  - `lghub.exe`, `lghub_agent.exe`, `lghub_updater.exe`
  - Other `logi_*` / `LGH*` processes tied to G HUB

---

## 2. Remove ghost virtual mouse entries (Device Manager)

1. Press **Win + R**, run **`devmgmt.msc`**.
2. Menu **View → Show hidden devices**.
3. Expand **Human Interface Devices**.
4. For each **Logitech G HUB Virtual Mouse** entry (and any broken duplicate):
   - Right-click → **Uninstall device**.
   - If you see **Delete the driver software for this device**, enable it, then confirm.
5. Expand **Mice and other pointing devices**.
6. Uninstall any **HID-compliant mouse** whose **Hardware Ids** (Properties → Details) include **`PID_C231`** if they show as unknown/phantom.
7. Optionally remove old grayed-out **Logitech / VID_046D** ghost devices you no longer use (not required, but reduces clutter).

---

## 3. Reboot

Restart Windows so the bus enumerator and phantom nodes are fully torn down.

---

## 4. Reinstall G HUB (clean settings)

1. Install the latest **Logitech G HUB** from Logitech’s website (or run your usual G HUB installer).
2. During first-run setup, **do not** use **Transfer my current settings**, **import settings**, or restore a cloud/backup profile. Use a **fresh** configuration so virtual devices are created cleanly.
3. If you already installed with import enabled, **uninstall G HUB** from Settings → Apps, reboot, then install again and skip transfer/import.

---

## 5. Verify

1. Start **Logitech G HUB** and wait ~30 seconds.
2. **Device Manager → Human Interface Devices** → **Logitech G HUB Virtual Mouse** should show **working properly** (no warning icon).
3. Run this tool → menu **3) Diagnose** (or `logitech_ghub_mouse.exe --diagnose`).

Expected:

```text
[OK] Virtual mouse (PID_C231) is active
```

4. Optional: in G HUB, assign a profile action **Mouse → Move**. If that does not move the cursor, the virtual mouse path is still broken on that install.

---

## 6. If it is still PHANTOM

- Repeat uninstall of **C231** / **Virtual Mouse** in Device Manager, full G HUB uninstall + reboot, reinstall without settings import.
- Ensure **`logi_joy_bus_enum`**, **`logi_joy_vir_hid`**, and **`logi_joy_xlcore`** are loaded (`driverquery /v | findstr /i logi_joy`).
- Try an older or newer G HUB build only if you accept the security/support tradeoffs of pinning versions.

Once diagnose reports **`[OK]`**, menu **1** or **2** should move the system cursor.
