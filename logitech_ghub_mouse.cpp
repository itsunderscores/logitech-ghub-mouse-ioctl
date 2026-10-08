#include "logitech_ghub_mouse.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <cfgmgr32.h>
#include <setupapi.h>

#include <algorithm>
#include <mutex>
#include <string_view>
#include <vector>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "setupapi.lib")

namespace logitech_ghub {
namespace {

constexpr DWORD kIoctlMouse = kMouseIoctl;
constexpr ACCESS_MASK kDirectoryQuery = 0x0001;
constexpr NTSTATUS kStatusMoreEntries = static_cast<NTSTATUS>(0x00000105);

typedef struct _OBJECT_DIRECTORY_INFORMATION {
    UNICODE_STRING Name;
    UNICODE_STRING TypeName;
} OBJECT_DIRECTORY_INFORMATION, *POBJECT_DIRECTORY_INFORMATION;

extern "C" {
NTSTATUS NTAPI NtOpenDirectoryObject(PHANDLE DirectoryHandle, ACCESS_MASK DesiredAccess,
                                     POBJECT_ATTRIBUTES ObjectAttributes);
NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE DirectoryHandle, PVOID Buffer, ULONG Length,
                                      BOOLEAN ReturnSingleEntry, BOOLEAN RestartScan, PULONG Context,
                                      PULONG ReturnLength);
}

enum class InputLayout { Auto, Bytes5, Bytes7, Bytes8 };

#pragma pack(push, 1)
struct MouseIo5 {
    std::uint8_t button;
    std::int8_t x;
    std::int8_t y;
    std::int8_t wheel;
    std::uint8_t unk1;
};

struct MouseIo7 {
    std::uint8_t button;
    std::int16_t x;
    std::int16_t y;
    std::int8_t wheel;
    std::uint8_t unk1;
};
#pragma pack(pop)

struct MouseIo8 {
    std::uint8_t button;
    std::uint8_t reserved;
    std::int16_t x;
    std::int16_t y;
    std::int8_t wheel;
    std::uint8_t unk1;
};

enum class MouseButton : std::uint8_t { None = 0, Left = 0x01, Right = 0x02, Middle = 0x04 };

InputLayout to_input(ReportLayout l) {
    switch (l) {
    case ReportLayout::Bytes5:
        return InputLayout::Bytes5;
    case ReportLayout::Bytes7:
        return InputLayout::Bytes7;
    case ReportLayout::Bytes8:
        return InputLayout::Bytes8;
    default:
        return InputLayout::Auto;
    }
}

ReportLayout from_input(InputLayout l) {
    switch (l) {
    case InputLayout::Bytes5:
        return ReportLayout::Bytes5;
    case InputLayout::Bytes7:
        return ReportLayout::Bytes7;
    case InputLayout::Bytes8:
        return ReportLayout::Bytes8;
    default:
        return ReportLayout::Auto;
    }
}

bool ends_with(std::wstring_view hay, std::wstring_view needle) {
    return hay.size() >= needle.size() &&
           hay.compare(hay.size() - needle.size(), needle.size(), needle) == 0;
}

bool starts_with(std::wstring_view hay, std::wstring_view needle) {
    return hay.size() >= needle.size() && hay.compare(0, needle.size(), needle) == 0;
}

bool is_logitech_root_device(std::wstring_view name) {
    if (name.size() < 40)
        return false;
    const bool root = starts_with(name, L"ROOT#SYSTEM#") || starts_with(name, L"Root#SYSTEM#");
    if (!root)
        return false;
    return ends_with(name, L"#{1abc05c0-c378-41b9-9cef-df1aba82b015}") ||
           ends_with(name, L"#{dfbedcdb-2148-416d-9e4d-cecc2424128c}") ||
           ends_with(name, L"#{df31f106-d870-453d-8fa1-ec8ab43fa1d2}") ||
           ends_with(name, L"#{5bada891-842b-4296-a496-68ae931aa16c}");
}

int device_priority(std::wstring_view name) {
    if (name.find(L"#0002#") != std::wstring_view::npos)
        return 0;
    if (name.find(L"#0001#") != std::wstring_view::npos)
        return 1;
    return 2;
}

std::vector<std::wstring> default_candidate_paths() {
    return {
        L"\\\\.\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
        L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
        L"\\\\.\\ROOT#SYSTEM#0001#{dfbedcdb-2148-416d-9e4d-cecc2424128c}",
        L"\\\\.\\ROOT#SYSTEM#0002#{dfbedcdb-2148-416d-9e4d-cecc2424128c}",
    };
}

class Device {
public:
    bool open(const wchar_t* forcePath) {
        close();

        std::vector<std::wstring> paths;
        if (forcePath != nullptr && forcePath[0] != L'\0') {
            paths.emplace_back(forcePath);
        } else {
            paths = enumerate_device_paths();
            for (const auto& p : default_candidate_paths())
                paths.push_back(p);
        }

        for (const auto& path : paths) {
            if (path.empty())
                continue;
            if (try_open(path.c_str())) {
                path_used_ = path;
                return true;
            }
        }
        return false;
    }

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
        path_used_.clear();
    }

    bool is_open() const { return handle_ != INVALID_HANDLE_VALUE; }
    const std::wstring& path_used() const { return path_used_; }

    void set_layout(InputLayout layout) { layout_ = layout; }
    InputLayout resolved_layout() const { return layout_; }

    bool move_relative(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        if (handle_ == INVALID_HANDLE_VALUE)
            return false;

        std::vector<InputLayout> try_order;
        if (layout_ == InputLayout::Auto)
            try_order = {InputLayout::Bytes8, InputLayout::Bytes5, InputLayout::Bytes7};
        else
            try_order = {layout_};

        for (InputLayout lay : try_order) {
            if (move_with_layout(lay, dx, dy, wheel)) {
                layout_ = lay;
                return true;
            }
        }
        return false;
    }

    bool click_left() {
        return move_button(MouseButton::Left, true) && move_button(MouseButton::Left, false);
    }

private:
    bool try_open(const wchar_t* path) {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                               nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        handle_ = h;
        return true;
    }

    bool ioctl(const void* in, DWORD inSize) {
        DWORD bytesReturned = 0;
        return DeviceIoControl(handle_, kIoctlMouse, const_cast<void*>(in), inSize, nullptr, 0, &bytesReturned,
                               nullptr) != FALSE;
    }

    bool move_with_layout(InputLayout lay, std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        switch (lay) {
        case InputLayout::Bytes8:
            return move8(dx, dy, wheel);
        case InputLayout::Bytes5:
            return move5(dx, dy, wheel);
        case InputLayout::Bytes7:
            return move7(dx, dy, wheel);
        default:
            return false;
        }
    }

    static std::int32_t clamp8(std::int32_t v) {
        if (v > 127)
            return 127;
        if (v < -127)
            return -127;
        return v;
    }

    static std::int32_t clamp16(std::int32_t v) {
        if (v > 32767)
            return 32767;
        if (v < -32767)
            return -32767;
        return v;
    }

    bool move8(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            const auto sx = static_cast<std::int16_t>(clamp16(dx));
            const auto sy = static_cast<std::int16_t>(clamp16(dy));
            MouseIo8 io{};
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            if (!ioctl(&io, sizeof(io)))
                return false;
            dx -= sx;
            dy -= sy;
        }
        return true;
    }

    bool move7(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            const auto sx = static_cast<std::int16_t>(clamp16(dx));
            const auto sy = static_cast<std::int16_t>(clamp16(dy));
            MouseIo7 io{};
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            if (!ioctl(&io, sizeof(io)))
                return false;
            dx -= sx;
            dy -= sy;
        }
        return true;
    }

    bool move5(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            auto sx = static_cast<std::int8_t>(clamp8(dx));
            auto sy = static_cast<std::int8_t>(clamp8(dy));
            MouseIo5 io{};
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            if (io.x == -128)
                io.x = -127;
            if (io.y == -128)
                io.y = -127;
            if (!ioctl(&io, sizeof(io)))
                return false;
            dx -= sx;
            dy -= sy;
        }
        return true;
    }

    bool move_button(MouseButton btn, bool down) {
        const std::uint8_t b = down ? static_cast<std::uint8_t>(btn) : 0;
        switch (layout_) {
        case InputLayout::Bytes8: {
            MouseIo8 io{};
            io.button = b;
            return ioctl(&io, sizeof(io));
        }
        case InputLayout::Bytes7: {
            MouseIo7 io{};
            io.button = b;
            return ioctl(&io, sizeof(io));
        }
        case InputLayout::Bytes5:
        case InputLayout::Auto: {
            MouseIo5 io{};
            io.button = b;
            return ioctl(&io, sizeof(io));
        }
        }
        return false;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring path_used_;
    InputLayout layout_ = InputLayout::Bytes8;
};

std::mutex g_shared_mutex;
Device g_shared_device;

}  // namespace

std::vector<std::wstring> enumerate_device_paths() {
    std::vector<std::wstring> found;

    UNICODE_STRING dirName{};
    RtlInitUnicodeString(&dirName, L"\\GLOBAL??");
    OBJECT_ATTRIBUTES oa{};
    InitializeObjectAttributes(&oa, &dirName, 0, nullptr, nullptr);

    HANDLE dir = nullptr;
    if (!NT_SUCCESS(NtOpenDirectoryObject(&dir, kDirectoryQuery, &oa)))
        return found;

    alignas(8) std::uint8_t buffer[8192]{};
    ULONG context = 0;
    NTSTATUS status = NtQueryDirectoryObject(dir, buffer, sizeof(buffer), FALSE, TRUE, &context, nullptr);

    while (NT_SUCCESS(status)) {
        const auto* entries = reinterpret_cast<const OBJECT_DIRECTORY_INFORMATION*>(buffer);
        for (ULONG i = 0; entries[i].Name.Buffer != nullptr; ++i) {
            const ULONG charLen = entries[i].Name.Length / sizeof(wchar_t);
            std::wstring_view sv(entries[i].Name.Buffer, charLen);
            if (is_logitech_root_device(sv)) {
                const std::wstring name(sv);
                found.emplace_back(L"\\\\?\\" + name);
                found.emplace_back(L"\\\\.\\" + name);
            }
        }
        if (status != kStatusMoreEntries)
            break;
        status = NtQueryDirectoryObject(dir, buffer, sizeof(buffer), FALSE, FALSE, &context, nullptr);
    }

    CloseHandle(dir);

    std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
        return device_priority(a) < device_priority(b);
    });

    return found;
}

VirtualInputHealth query_virtual_input_health() {
    VirtualInputHealth h{};
    HDEVINFO devs = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (devs == INVALID_HANDLE_VALUE)
        return h;

    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD idx = 0; SetupDiEnumDeviceInfo(devs, idx, &info); ++idx) {
        wchar_t id[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(devs, &info, id, MAX_DEVICE_ID_LEN, nullptr))
            continue;

        const bool is_c232 = wcsstr(id, L"PID_C232") != nullptr;
        const bool is_c231 = wcsstr(id, L"PID_C231") != nullptr;
        if (!is_c231 && !is_c232)
            continue;

        ULONG status = 0;
        ULONG problem = 0;
        const CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0);
        const bool present =
            (cr == CR_SUCCESS) && ((status & DN_STARTED) != 0 || (status & DN_DRIVER_LOADED) != 0);

        if (is_c232 && present)
            h.keyboard_present = true;

        if (is_c231) {
            if (starts_with(id, L"LGHUBDEVICE\\") || h.mouse_instance_id.empty())
                h.mouse_instance_id = id;
            if (problem == CM_PROB_PHANTOM || (starts_with(id, L"LGHUBDEVICE\\") && !present))
                h.mouse_phantom = true;
            if (starts_with(id, L"LGHUBDEVICE\\") && present) {
                h.mouse_present = true;
                h.mouse_phantom = false;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    return h;
}

struct Mouse::Impl {
    Device device;
};

Mouse::Mouse() : impl_(new Impl()) {}

Mouse::~Mouse() {
    impl_->device.close();
    delete impl_;
}

void Mouse::set_layout(ReportLayout layout) {
    impl_->device.set_layout(to_input(layout));
}

bool Mouse::open(const wchar_t* optional_path) {
    return impl_->device.open(optional_path);
}

void Mouse::close() {
    impl_->device.close();
}

bool Mouse::is_open() const {
    return impl_->device.is_open();
}

const std::wstring& Mouse::path_used() const {
    return impl_->device.path_used();
}

ReportLayout Mouse::resolved_layout() const {
    return from_input(impl_->device.resolved_layout());
}

bool Mouse::move_relative(int dx, int dy, int wheel) {
    return impl_->device.move_relative(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy),
                                       static_cast<std::int8_t>(wheel));
}

bool Mouse::click_left() {
    return impl_->device.click_left();
}

bool virtual_mouse_active() {
    return query_virtual_input_health().mouse_present;
}

bool ensure_open() {
    std::lock_guard<std::mutex> lock(g_shared_mutex);
    if (g_shared_device.is_open())
        return true;
    return g_shared_device.open(nullptr);
}

void close_shared() {
    std::lock_guard<std::mutex> lock(g_shared_mutex);
    g_shared_device.close();
}

bool move_relative(int dx, int dy) {
    std::lock_guard<std::mutex> lock(g_shared_mutex);
    if (!g_shared_device.is_open() && !g_shared_device.open(nullptr))
        return false;
    if (!g_shared_device.move_relative(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy), 0)) {
        g_shared_device.close();
        return false;
    }
    return true;
}

}  // namespace logitech_ghub
