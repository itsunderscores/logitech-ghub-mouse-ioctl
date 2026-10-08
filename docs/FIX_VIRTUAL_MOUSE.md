# Mouse simulator not moving the cursor

Diagnose shows **`[DEAD] PHANTOM`**, **`[MISS]`**, or the cursor never moves even when the test app reports success. The virtual **Logitech G HUB Virtual Mouse** (`PID_C231`) is not working on that install.

**You do not need a real Logitech mouse for this simulator**—only a working **G HUB software** virtual device.

---

## After every reboot

On many systems the virtual mouse (**PID_C231**) does not stay usable across a **Windows reboot**. Plan to **reinstall G HUB after each reboot** (same clean reinstall below—**do not** restore previous settings) before using the simulator or your integrated code.

---

## Fix (recommended)

**Reinstall Logitech G HUB and do not keep your previous settings.**

1. Quit G HUB (system tray → Quit). End `lghub.exe` / related tasks in Task Manager if needed.
2. Uninstall **Logitech G HUB** from **Settings → Apps**.
3. Reboot (recommended).
4. Install G HUB again from Logitech.
5. On first-run / setup, **do not** use:
   - **Transfer my current settings**
   - **Import settings** / cloud profile restore  
   Choose a **fresh** setup so G HUB recreates the virtual mouse cleanly.
6. Start G HUB and wait ~30 seconds.
7. Run the test app → **3) Diagnose**. You want:

```text
[OK] Virtual mouse (PID_C231) is active
```

Then try menu **1** (random move) and watch the cursor.

---

## If it still fails

- Repeat: uninstall G HUB → reboot → clean reinstall (no settings transfer) again.
- In **Device Manager** → **View → Show hidden devices** → **Human Interface Devices**, uninstall any broken **Logitech G HUB Virtual Mouse** entries, then reinstall G HUB as above.
- Confirm G HUB is running and drivers are loaded:

```bat
driverquery /v | findstr /i logi_joy
```

When Diagnose shows **`[OK]`**, the simulator module in your own code should move the mouse the same way as the test exe.
