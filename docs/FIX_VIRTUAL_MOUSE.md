# Mouse simulator not moving the cursor

Diagnose shows **`[DEAD] PHANTOM`**, **`[MISS]`**, or the cursor never moves. The **Logitech G HUB Virtual Mouse** (`PID_C231`) is not active on that install.

**No physical Logitech mouse is required**—only G HUB’s software virtual device.

---

## After every reboot

On many systems the virtual mouse stops working after a **Windows reboot**. You do **not** need to uninstall G HUB.

1. Run the **Logitech G HUB installer** (same `.exe` you used to install G HUB).
2. Choose **Repair** (or the equivalent repair option).
3. When asked about settings, **do not** transfer or restore previous settings—wording is usually **Transfer my current settings** (leave it **off** / decline import).
4. Finish the repair, start G HUB, wait ~30 seconds.
5. Run **Diagnose** in this tool. You want:

```text
[OK] Virtual mouse (PID_C231) is active
```

Repeat **Repair** after each reboot if movement stops again.

---

## First-time fix (never worked, or Repair did not help)

Use the same **Repair** flow above first.

If it still fails:

1. Quit G HUB (tray → Quit).
2. Run the G HUB installer → **Repair** again, **no** settings transfer.
3. Optional: **Device Manager** → **View → Show hidden devices** → uninstall broken **Logitech G HUB Virtual Mouse** entries, then **Repair** once more.
4. Run **Diagnose** and test menu **1**.

When Diagnose shows **`[OK]`**, the drop-in library should move the mouse the same way as the test app.
