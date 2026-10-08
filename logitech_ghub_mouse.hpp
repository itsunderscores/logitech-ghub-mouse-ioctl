#pragma once

// Logitech G HUB virtual mouse — drop-in module for your C++ project.
// Opens the G HUB ROOT device and sends relative movement via IOCTL 0x2A2010.

#include <cstdint>
#include <string>
#include <vector>

namespace logitech_ghub {

inline constexpr unsigned kMouseIoctl = 0x2A2010;

enum class ReportLayout { Auto, Bytes5, Bytes7, Bytes8 };

struct VirtualInputHealth {
    bool keyboard_present = false;
    bool mouse_present = false;
    bool mouse_phantom = false;
    std::wstring mouse_instance_id;
};

// PnP state of virtual keyboard/mouse (PID_C232 / PID_C231).
VirtualInputHealth query_virtual_input_health();

// Logitech ROOT#SYSTEM# symlink paths under \GLOBAL?? (for debugging).
std::vector<std::wstring> enumerate_device_paths();

// --- Device handle (open once, reuse for many moves) ---

class Mouse {
public:
    Mouse();
    ~Mouse();

    Mouse(const Mouse&) = delete;
    Mouse& operator=(const Mouse&) = delete;

    void set_layout(ReportLayout layout);

    // Opens default or enumerated paths. optional_path: Win32 path, e.g.
    // L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}"
    bool open(const wchar_t* optional_path = nullptr);
    void close();

    bool is_open() const;
    const std::wstring& path_used() const;
    ReportLayout resolved_layout() const;

    bool move_relative(int dx, int dy, int wheel = 0);
    bool click_left();

private:
    struct Impl;
    Impl* impl_;
};

// --- Convenience API (process-wide singleton, mutex-protected) ---

bool virtual_mouse_active();
bool ensure_open();
void close_shared();
bool move_relative(int dx, int dy);

}  // namespace logitech_ghub
