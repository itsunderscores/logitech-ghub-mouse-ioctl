// Logitech G HUB virtual mouse — interactive test harness
//
// Verify IOCTL layout and device paths against your installed G HUB / driver build.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <cfgmgr32.h>
#include <setupapi.h>

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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "setupapi.lib")

namespace {

constexpr DWORD kIoctlMouse = 0x2A2010;
constexpr ACCESS_MASK kDirectoryQuery = 0x0001;
constexpr NTSTATUS kStatusSuccess = static_cast<NTSTATUS>(0);
constexpr NTSTATUS kStatusMoreEntries = static_cast<NTSTATUS>(0x00000105);

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
static_assert(sizeof(MouseIo8) == 8, "GHUB new report must be 8 bytes");

enum class MouseButton : std::uint8_t {
    None = 0,
    Left = 0x01,
    Right = 0x02,
    Middle = 0x04,
};

bool endsWith(std::wstring_view hay, std::wstring_view needle) {
    return hay.size() >= needle.size() &&
           hay.compare(hay.size() - needle.size(), needle.size(), needle) == 0;
}

bool startsWith(std::wstring_view hay, std::wstring_view needle) {
    return hay.size() >= needle.size() && hay.compare(0, needle.size(), needle) == 0;
}

bool isLogitechRootDevice(std::wstring_view name) {
    if (name.size() < 40)
        return false;
    const bool root = startsWith(name, L"ROOT#SYSTEM#") || startsWith(name, L"Root#SYSTEM#");
    if (!root)
        return false;
    return endsWith(name, L"#{1abc05c0-c378-41b9-9cef-df1aba82b015}") ||
           endsWith(name, L"#{dfbedcdb-2148-416d-9e4d-cecc2424128c}") ||
           endsWith(name, L"#{df31f106-d870-453d-8fa1-ec8ab43fa1d2}") ||
           endsWith(name, L"#{5bada891-842b-4296-a496-68ae931aa16c}");
}

int devicePriority(std::wstring_view name) {
    if (name.find(L"#0002#") != std::wstring_view::npos)
        return 0;
    if (name.find(L"#0001#") != std::wstring_view::npos)
        return 1;
    return 2;
}

std::vector<std::wstring> enumerateLogitechDevicePaths() {
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
            if (isLogitechRootDevice(sv)) {
                const std::wstring name(sv);
                found.emplace_back(L"\\\\?\\" + name);
                found.emplace_back(L"\\\\.\\" + name);
                found.emplace_back(L"\\??\\" + name);
            }
        }
        if (status != kStatusMoreEntries)
            break;
        status = NtQueryDirectoryObject(dir, buffer, sizeof(buffer), FALSE, FALSE, &context, nullptr);
    }

    CloseHandle(dir);

    std::sort(found.begin(), found.end(), [](const std::wstring& a, const std::wstring& b) {
        return devicePriority(a) < devicePriority(b);
    });

    return found;
}

std::vector<std::wstring> defaultCandidatePaths() {
    return {
        L"\\\\.\\ROOT#SYSTEM#0002#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
        L"\\\\.\\ROOT#SYSTEM#0001#{1abc05c0-c378-41b9-9cef-df1aba82b015}",
        L"\\\\.\\ROOT#SYSTEM#0001#{dfbedcdb-2148-416d-9e4d-cecc2424128c}",
        L"\\\\.\\ROOT#SYSTEM#0002#{dfbedcdb-2148-416d-9e4d-cecc2424128c}",
    };
}

class GhubMouseDevice {
public:
    bool open(const std::wstring* forcePath = nullptr) {
        close();

        std::vector<std::wstring> paths;
        if (forcePath != nullptr && !forcePath->empty()) {
            paths.push_back(*forcePath);
        } else {
            paths = enumerateLogitechDevicePaths();
            for (const auto& p : defaultCandidatePaths())
                paths.push_back(p);
        }

        for (const auto& path : paths) {
            if (path.empty())
                continue;
            if (tryOpenPath(path.c_str())) {
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

    bool isOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    const std::wstring& pathUsed() const { return path_used_; }
    void setLayout(InputLayout layout) { layout_ = layout; }
    InputLayout resolvedLayout() const { return layout_; }

    bool moveRelative(std::int32_t dx, std::int32_t dy, std::int8_t wheel = 0) {
        if (handle_ == INVALID_HANDLE_VALUE)
            return false;

        std::vector<InputLayout> tryOrder;
        if (layout_ == InputLayout::Auto)
            tryOrder = {InputLayout::Bytes8, InputLayout::Bytes5, InputLayout::Bytes7};
        else
            tryOrder = {layout_};

        for (InputLayout lay : tryOrder) {
            if (moveRelativeWithLayout(lay, dx, dy, wheel)) {
                layout_ = lay;
                return true;
            }
        }
        return false;
    }

    bool clickLeft() {
        return moveButton(MouseButton::Left, true) && moveButton(MouseButton::Left, false);
    }

private:
    bool tryOpenPath(const wchar_t* path) {
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

    bool moveRelativeWithLayout(InputLayout lay, std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        switch (lay) {
        case InputLayout::Bytes8:
            return moveRelative8(dx, dy, wheel);
        case InputLayout::Bytes5:
            return moveRelative5(dx, dy, wheel);
        case InputLayout::Bytes7:
            return moveRelative7(dx, dy, wheel);
        default:
            return false;
        }
    }

    bool moveRelative8(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            const auto sx = static_cast<std::int16_t>(clamp16(dx));
            const auto sy = static_cast<std::int16_t>(clamp16(dy));
            MouseIo8 io{};
            io.button = 0;
            io.reserved = 0;
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            io.unk1 = 0;
            if (!ioctl(&io, sizeof(io)))
                return false;
            dx -= sx;
            dy -= sy;
        }
        return true;
    }

    bool moveRelative7(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            const auto sx = static_cast<std::int16_t>(clamp16(dx));
            const auto sy = static_cast<std::int16_t>(clamp16(dy));
            MouseIo7 io{};
            io.button = 0;
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            io.unk1 = 0;
            if (!ioctl(&io, sizeof(io)))
                return false;
            dx -= sx;
            dy -= sy;
        }
        return true;
    }

    bool moveRelative5(std::int32_t dx, std::int32_t dy, std::int8_t wheel) {
        while (dx != 0 || dy != 0) {
            const auto sx = static_cast<std::int8_t>(clamp8(dx));
            const auto sy = static_cast<std::int8_t>(clamp8(dy));
            MouseIo5 io{};
            io.button = 0;
            io.x = sx;
            io.y = sy;
            io.wheel = wheel;
            io.unk1 = 0;
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

    bool moveButton(MouseButton btn, bool down) {
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

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::wstring path_used_;
    InputLayout layout_ = InputLayout::Bytes8;
};

const char* layoutName(InputLayout l) {
    switch (l) {
    case InputLayout::Auto:
        return "auto";
    case InputLayout::Bytes5:
        return "5-byte";
    case InputLayout::Bytes7:
        return "7-byte";
    case InputLayout::Bytes8:
        return "8-byte";
    }
    return "?";
}

InputLayout parseLayout(const char* s) {
    if (std::strcmp(s, "5") == 0 || _stricmp(s, "5-byte") == 0)
        return InputLayout::Bytes5;
    if (std::strcmp(s, "7") == 0 || _stricmp(s, "7-byte") == 0)
        return InputLayout::Bytes7;
    if (std::strcmp(s, "8") == 0 || _stricmp(s, "8-byte") == 0)
        return InputLayout::Bytes8;
    return InputLayout::Auto;
}

struct VirtualInputHealth {
    bool keyboardPresent = false;
    bool mousePresent = false;
    bool mousePhantom = false;
    std::wstring mouseInstanceId;
};

VirtualInputHealth queryVirtualInputHealth() {
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

        const bool isC232 = wcsstr(id, L"PID_C232") != nullptr;
        const bool isC231 = wcsstr(id, L"PID_C231") != nullptr;
        if (!isC231 && !isC232)
            continue;

        ULONG status = 0;
        ULONG problem = 0;
        const CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0);
        const bool present =
            (cr == CR_SUCCESS) && ((status & DN_STARTED) != 0 || (status & DN_DRIVER_LOADED) != 0);

        if (isC232 && present)
            h.keyboardPresent = true;

        if (isC231) {
            if (startsWith(id, L"LGHUBDEVICE\\") || h.mouseInstanceId.empty())
                h.mouseInstanceId = id;
            if (problem == CM_PROB_PHANTOM || (startsWith(id, L"LGHUBDEVICE\\") && !present))
                h.mousePhantom = true;
            if (startsWith(id, L"LGHUBDEVICE\\") && present) {
                h.mousePresent = true;
                h.mousePhantom = false;
            }
        }
    }

    SetupDiDestroyDeviceInfoList(devs);
    return h;
}

int runDiagnose() {
    std::puts("=== Logitech G HUB virtual input (PnP) ===");
    const VirtualInputHealth health = queryVirtualInputHealth();
    std::puts(health.keyboardPresent ? "[OK]   Virtual keyboard (PID_C232) is active"
                                     : "[MISS] Virtual keyboard (PID_C232) not active");
    if (health.mousePresent) {
        std::puts("[OK]   Virtual mouse (PID_C231) is active - IOCTL injection can move the cursor");
    } else if (health.mousePhantom) {
        std::puts("[DEAD] Virtual mouse (PID_C231) is PHANTOM (ghost entry only)");
        std::puts("       IOCTL to ROOT#SYSTEM#0001 may still return success but will NOT move the mouse.");
    } else {
        std::puts("[MISS] Virtual mouse (PID_C231) not installed / not started");
    }
    if (!health.mouseInstanceId.empty())
        std::wprintf(L"       Instance: %s\n", health.mouseInstanceId.c_str());

    std::puts("\n=== ROOT device symlinks ===");
    const auto paths = enumerateLogitechDevicePaths();
    if (paths.empty())
        std::puts("(none - is G HUB running?)");
    for (const auto& p : paths)
        std::wprintf(L"%s\n", p.c_str());

    const bool has0002 = std::any_of(paths.begin(), paths.end(), [](const std::wstring& p) {
        return p.find(L"#0002#") != std::wstring_view::npos;
    });
    if (!has0002)
        std::puts("\n[WARN] No ROOT#SYSTEM#0002 link (mouse control node is usually 0002).");

    if (!health.mousePresent) {
        std::puts("\n=== Fix PHANTOM / MISS virtual mouse (PID_C231) ===");
        std::puts("1. Quit G HUB completely (tray + Task Manager: lghub_*, logi_*).");
        std::puts("2. devmgmt.msc -> View -> Show hidden devices.");
        std::puts("3. Human Interface Devices: uninstall 'Logitech G HUB Virtual Mouse'.");
        std::puts("   Enable 'Delete the driver software' if offered. Repeat for any extra PID_C231 mouse ghosts.");
        std::puts("4. Reboot.");
        std::puts("5. Reinstall Logitech G HUB. Do NOT use 'Transfer my current settings' / import profile.");
        std::puts("6. Start G HUB, wait ~30s, run Diagnose again. Device Manager should show Virtual Mouse OK.");
        std::puts("7. Optional: G HUB profile action Mouse > Move - if that fails, virtual mouse is still dead.");
        std::puts("\nFull guide: docs/FIX_VIRTUAL_MOUSE.md in this repository.");
        return 2;
    }
    return 0;
}

void runListDevices() {
    const auto paths = enumerateLogitechDevicePaths();
    if (paths.empty()) {
        std::puts("No Logitech ROOT#SYSTEM links found (is G HUB running?).");
        return;
    }
    std::puts("=== Logitech ROOT symlinks (\\GLOBAL??) ===");
    for (const auto& p : paths)
        std::wprintf(L"%s\n", p.c_str());
}

void printUsage() {
    std::puts("Usage: logitech_ghub_mouse [options] [dx] [dy]");
    std::puts("  (no arguments)  interactive demo menu");
    std::puts("  dx dy             one-shot move (legacy CLI)");
    std::puts("  --click           left click after move");
    std::puts("  --list            list Logitech ROOT devices");
    std::puts("  --diagnose        PnP + ROOT link check");
    std::puts("  --layout 5|7|8    IOCTL payload (default 8-byte)");
    std::puts("  --device PATH     force CreateFile path");
    std::puts("  --force           send IOCTL even if virtual mouse is missing");
}

bool isDecimalInteger(const char* arg) {
    if (arg == nullptr || *arg == '\0')
        return false;
    const char* p = arg;
    if (*p == '+' || *p == '-')
        ++p;
    if (*p == '\0')
        return false;
    while (*p != '\0') {
        if (!std::isdigit(static_cast<unsigned char>(*p)))
            return false;
        ++p;
    }
    return true;
}

void setupConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void clearConsole() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == nullptr)
        return;

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(out, &info))
        return;

    const DWORD cellCount = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
    COORD home{0, 0};
    DWORD written = 0;
    FillConsoleOutputCharacterW(out, L' ', cellCount, home, &written);
    FillConsoleOutputAttribute(out, info.wAttributes, cellCount, home, &written);
    SetConsoleCursorPosition(out, home);
}

void drainStdinLine() {
    int c = 0;
    while ((c = std::getchar()) != '\n' && c != EOF) {
    }
    if (c == EOF)
        std::clearerr(stdin);
}

void waitForEnter() {
    std::puts("\nPress Enter to return to the menu...");
    std::fflush(stdout);
    drainStdinLine();
}

int parseMenuChoice(const char* line) {
    if (line == nullptr)
        return -1;
    while (*line == ' ' || *line == '\t' || *line == '\r')
        ++line;
    if (*line == '\0' || *line == '\n')
        return -1;
    char* end = nullptr;
    const long v = std::strtol(line, &end, 10);
    if (end == line)
        return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r')
        ++end;
    if (*end != '\0' && *end != '\n')
        return -1;
    if (v < 0 || v > 4)
        return -1;
    return static_cast<int>(v);
}

int randomAxis100to200(std::mt19937& rng) {
    std::uniform_int_distribution<int> mag(100, 200);
    std::uniform_int_distribution<int> sign(0, 1);
    const int value = mag(rng);
    return sign(rng) == 0 ? -value : value;
}

bool ensureDeviceOpen(GhubMouseDevice& dev) {
    if (dev.isOpen())
        return true;
    if (!dev.open()) {
        std::fprintf(stderr, "CreateFile failed (error %lu). Is G HUB running?\n", GetLastError());
        std::puts("Try menu option 3 (Diagnose) first.");
        return false;
    }
    std::wprintf(L"Opened device: %s\n", dev.pathUsed().c_str());
    return true;
}

void menuOptionRandomMove(GhubMouseDevice& dev) {
    if (!ensureDeviceOpen(dev))
        return;

    std::mt19937 rng{std::random_device{}()};
    const int dx = randomAxis100to200(rng);
    const int dy = randomAxis100to200(rng);

    std::printf("\nMoving relative (%d, %d) - watch the cursor.\n", dx, dy);
    if (!dev.moveRelative(dx, dy)) {
        std::fprintf(stderr, "DeviceIoControl(0x%lX) failed (error %lu).\n", kIoctlMouse, GetLastError());
        dev.close();
        return;
    }
    std::printf("OK - sent %s layout report.\n", layoutName(dev.resolvedLayout()));
}

bool isKeyPressed(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void menuOptionHumanSmoothMove(GhubMouseDevice& dev) {
    if (!ensureDeviceOpen(dev))
        return;

    std::puts("\n=== Smooth human-like mouse demo ===");
    std::puts("The cursor will drift with small, smooth steps.");
    std::puts("Press and hold the  A  key to stop and return to the menu.\n");
    std::puts("(Focus this console or keep it visible so you can press A.)\n");

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.35f, 0.35f);
    std::uniform_int_distribution<int> sleepMs(7, 14);
    std::uniform_int_distribution<int> microPauseChance(0, 999);

    float vx = 0.f;
    float vy = 0.f;
    float heading = std::uniform_real_distribution<float>(0.f, 6.2831853f)(rng);
    int ticksUntilHeadingChange = 40;

    while (!isKeyPressed('A')) {
        if (--ticksUntilHeadingChange <= 0) {
            heading += std::uniform_real_distribution<float>(-0.9f, 0.9f)(rng);
            ticksUntilHeadingChange = std::uniform_int_distribution<int>(25, 70)(rng);
        }

        const float targetSpeed = std::uniform_real_distribution<float>(2.5f, 7.5f)(rng);
        vx = vx * 0.82f + std::cos(heading) * targetSpeed * 0.18f + jitter(rng);
        vy = vy * 0.82f + std::sin(heading) * targetSpeed * 0.18f + jitter(rng);

        const float speed = std::sqrt(vx * vx + vy * vy);
        constexpr float kMaxSpeed = 12.f;
        if (speed > kMaxSpeed) {
            vx = vx / speed * kMaxSpeed;
            vy = vy / speed * kMaxSpeed;
        }

        const int mx = static_cast<int>(std::lround(vx));
        const int my = static_cast<int>(std::lround(vy));

        if (mx != 0 || my != 0) {
            if (!dev.moveRelative(mx, my)) {
                std::fprintf(stderr, "\nDeviceIoControl failed (error %lu). Stopping demo.\n", GetLastError());
                dev.close();
                break;
            }
        }

        if (microPauseChance(rng) < 8)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(25, 55)(rng)));
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs(rng)));
    }

    while (isKeyPressed('A'))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::puts("\nStopped (A released).");
}

void printMainMenu(const VirtualInputHealth& health, const GhubMouseDevice& dev) {
    std::puts("========================================");
    std::puts("  Logitech G HUB Mouse - Demo Menu");
    std::puts("========================================");
    if (health.mousePresent)
        std::puts("  Virtual mouse: [OK]");
    else if (health.mousePhantom)
        std::puts("  Virtual mouse: [PHANTOM - moves may not work]");
    else
        std::puts("  Virtual mouse: [NOT READY]");

    if (dev.isOpen())
        std::wprintf(L"  Device handle: open (%s)\n", dev.pathUsed().c_str());
    else
        std::puts("  Device handle: (not open yet)");

    std::puts("");
    std::puts("  1) Random move (100-200 px horizontal & vertical)");
    std::puts("  2) Smooth human-like movement (press A to stop)");
    std::puts("  3) Diagnose");
    std::puts("  4) List devices");
    std::puts("  0) Exit");
    std::puts("");
    std::printf("Choice: ");
    std::fflush(stdout);
}

int runInteractiveMenu() {
    GhubMouseDevice dev;
    dev.setLayout(InputLayout::Bytes8);

    for (;;) {
        clearConsole();
        const VirtualInputHealth health = queryVirtualInputHealth();
        printMainMenu(health, dev);

        char line[64]{};
        if (std::fgets(line, sizeof(line), stdin) == nullptr) {
            std::puts("\nInput ended.");
            break;
        }

        const int choice = parseMenuChoice(line);
        switch (choice) {
        case 0:
            dev.close();
            std::puts("Bye.");
            return 0;
        case 1:
            menuOptionRandomMove(dev);
            waitForEnter();
            break;
        case 2:
            menuOptionHumanSmoothMove(dev);
            waitForEnter();
            break;
        case 3:
            runDiagnose();
            waitForEnter();
            break;
        case 4:
            runListDevices();
            waitForEnter();
            break;
        default:
            std::puts("Invalid choice (enter 0-4).");
            waitForEnter();
            break;
        }
    }
    return 0;
}

int runLegacyCli(int argc, char* argv[]) {
    bool doClick = false;
    bool listOnly = false;
    bool diagnoseOnly = false;
    bool forceSend = false;
    std::int32_t dx = 100;
    std::int32_t dy = 0;
    InputLayout layout = InputLayout::Bytes8;
    std::wstring devicePath;
    std::vector<const char*> nums;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--click") == 0) {
            doClick = true;
            continue;
        }
        if (std::strcmp(arg, "--list") == 0) {
            listOnly = true;
            continue;
        }
        if (std::strcmp(arg, "--diagnose") == 0) {
            diagnoseOnly = true;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            forceSend = true;
            continue;
        }
        if (std::strcmp(arg, "--layout") == 0 && i + 1 < argc) {
            layout = parseLayout(argv[++i]);
            continue;
        }
        if (std::strcmp(arg, "--device") == 0 && i + 1 < argc) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, nullptr, 0);
            devicePath.resize(static_cast<size_t>(n));
            MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, devicePath.data(), n);
            if (!devicePath.empty() && devicePath.back() == L'\0')
                devicePath.pop_back();
            continue;
        }
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            printUsage();
            return 0;
        }
        if (isDecimalInteger(arg)) {
            nums.push_back(arg);
            continue;
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", arg);
            printUsage();
            return 1;
        }
        nums.push_back(arg);
    }

    if (listOnly) {
        runListDevices();
        return 0;
    }
    if (diagnoseOnly)
        return runDiagnose();

    const VirtualInputHealth health = queryVirtualInputHealth();
    if (!health.mousePresent && !forceSend) {
        std::fprintf(stderr,
                     "\nVirtual mouse (PID_C231) is not active - IOCTL will not move the cursor on this G HUB build.\n"
                     "Run: logitech_ghub_mouse.exe --diagnose\n\n");
        if (health.mousePhantom)
            return 2;
    }

    if (nums.size() >= 1)
        dx = std::strtol(nums[0], nullptr, 10);
    if (nums.size() >= 2)
        dy = std::strtol(nums[1], nullptr, 10);

    GhubMouseDevice dev;
    dev.setLayout(layout);
    const std::wstring* force = devicePath.empty() ? nullptr : &devicePath;
    if (!dev.open(force)) {
        std::fprintf(stderr, "CreateFile failed (error %lu). Is G HUB running?\n", GetLastError());
        return 1;
    }

    std::wprintf(L"Opened: %s\n", dev.pathUsed().c_str());
    std::printf("Moving relative (%d, %d) using layout %s...\n", dx, dy, layoutName(dev.resolvedLayout()));
    if (!dev.moveRelative(dx, dy)) {
        std::fprintf(stderr, "DeviceIoControl(0x%lX) failed (error %lu).\n", kIoctlMouse, GetLastError());
        return 1;
    }

    std::printf("Sent using %s layout.\n", layoutName(dev.resolvedLayout()));
    if (doClick && !dev.clickLeft()) {
        std::fprintf(stderr, "Click IOCTL failed (error %lu).\n", GetLastError());
        return 1;
    }

    std::puts("Done.");
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    setupConsole();
    if (argc <= 1)
        return runInteractiveMenu();
    return runLegacyCli(argc, argv);
}
