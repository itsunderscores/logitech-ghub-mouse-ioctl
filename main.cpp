// Logitech G HUB virtual mouse — interactive test harness

#include "logitech_ghub_mouse.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

using logitech_ghub::Mouse;
using logitech_ghub::ReportLayout;
using logitech_ghub::VirtualInputHealth;
using logitech_ghub::enumerate_device_paths;
using logitech_ghub::query_virtual_input_health;

namespace {

const char* layout_name(ReportLayout l) {
    switch (l) {
    case ReportLayout::Auto:
        return "auto";
    case ReportLayout::Bytes5:
        return "5-byte";
    case ReportLayout::Bytes7:
        return "7-byte";
    case ReportLayout::Bytes8:
        return "8-byte";
    }
    return "?";
}

ReportLayout parse_layout(const char* s) {
    if (std::strcmp(s, "5") == 0 || _stricmp(s, "5-byte") == 0)
        return ReportLayout::Bytes5;
    if (std::strcmp(s, "7") == 0 || _stricmp(s, "7-byte") == 0)
        return ReportLayout::Bytes7;
    if (std::strcmp(s, "8") == 0 || _stricmp(s, "8-byte") == 0)
        return ReportLayout::Bytes8;
    return ReportLayout::Auto;
}

int run_diagnose() {
    std::puts("=== Logitech G HUB virtual input (PnP) ===");
    const VirtualInputHealth health = query_virtual_input_health();
    std::puts(health.keyboard_present ? "[OK]   Virtual keyboard (PID_C232) is active"
                                      : "[MISS] Virtual keyboard (PID_C232) not active");
    if (health.mouse_present) {
        std::puts("[OK]   Virtual mouse (PID_C231) is active - IOCTL injection can move the cursor");
    } else if (health.mouse_phantom) {
        std::puts("[DEAD] Virtual mouse (PID_C231) is PHANTOM (ghost entry only)");
        std::puts("       IOCTL to ROOT#SYSTEM#0001 may still return success but will NOT move the mouse.");
    } else {
        std::puts("[MISS] Virtual mouse (PID_C231) not installed / not started");
    }
    if (!health.mouse_instance_id.empty())
        std::wprintf(L"       Instance: %s\n", health.mouse_instance_id.c_str());

    std::puts("\n=== ROOT device symlinks ===");
    const auto paths = enumerate_device_paths();
    if (paths.empty())
        std::puts("(none - is G HUB running?)");
    for (const auto& p : paths)
        std::wprintf(L"%s\n", p.c_str());

    const bool has0002 = std::any_of(paths.begin(), paths.end(), [](const std::wstring& p) {
        return p.find(L"#0002#") != std::wstring::npos;
    });
    if (!has0002)
        std::puts("\n[WARN] No ROOT#SYSTEM#0002 link (mouse control node is usually 0002).");

    if (!health.mouse_present) {
        std::puts("\n=== Mouse simulator not moving / virtual mouse not OK ===");
        std::puts("No physical Logitech mouse is required - only G HUB software.");
        std::puts("Fix: uninstall Logitech G HUB, reinstall, and do NOT keep / transfer previous settings.");
        std::puts("After a reboot you may need to reinstall G HUB again before movement works.");
        std::puts("Then start G HUB, wait ~30s, and run Diagnose again.");
        std::puts("Details: docs/FIX_VIRTUAL_MOUSE.md");
        return 2;
    }
    return 0;
}

void run_list_devices() {
    const auto paths = enumerate_device_paths();
    if (paths.empty()) {
        std::puts("No Logitech ROOT#SYSTEM links found (is G HUB running?).");
        return;
    }
    std::puts("=== Logitech ROOT symlinks (\\GLOBAL??) ===");
    for (const auto& p : paths)
        std::wprintf(L"%s\n", p.c_str());
}

void print_usage() {
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

bool is_decimal_integer(const char* arg) {
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

void setup_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void clear_console() {
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

void drain_stdin_line() {
    int c = 0;
    while ((c = std::getchar()) != '\n' && c != EOF) {
    }
    if (c == EOF)
        std::clearerr(stdin);
}

void wait_for_enter() {
    std::puts("\nPress Enter to return to the menu...");
    std::fflush(stdout);
    drain_stdin_line();
}

int parse_menu_choice(const char* line) {
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

int random_axis_100_to_200(std::mt19937& rng) {
    std::uniform_int_distribution<int> mag(100, 200);
    std::uniform_int_distribution<int> sign(0, 1);
    const int value = mag(rng);
    return sign(rng) == 0 ? -value : value;
}

bool ensure_device_open(Mouse& dev) {
    if (dev.is_open())
        return true;
    if (!dev.open()) {
        std::fprintf(stderr, "CreateFile failed (error %lu). Is G HUB running?\n", GetLastError());
        std::puts("Try menu option 3 (Diagnose) first.");
        return false;
    }
    std::wprintf(L"Opened device: %s\n", dev.path_used().c_str());
    return true;
}

void menu_option_random_move(Mouse& dev) {
    if (!ensure_device_open(dev))
        return;

    std::mt19937 rng{std::random_device{}()};
    const int dx = random_axis_100_to_200(rng);
    const int dy = random_axis_100_to_200(rng);

    std::printf("\nMoving relative (%d, %d) - watch the cursor.\n", dx, dy);
    if (!dev.move_relative(dx, dy)) {
        std::fprintf(stderr, "DeviceIoControl(0x%lX) failed (error %lu).\n", logitech_ghub::kMouseIoctl,
                     GetLastError());
        dev.close();
        return;
    }
    std::printf("OK - sent %s layout report.\n", layout_name(dev.resolved_layout()));
}

bool is_key_pressed(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void menu_option_human_smooth_move(Mouse& dev) {
    if (!ensure_device_open(dev))
        return;

    std::puts("\n=== Smooth human-like mouse demo ===");
    std::puts("The cursor will drift with small, smooth steps.");
    std::puts("Press and hold the  A  key to stop and return to the menu.\n");
    std::puts("(Focus this console or keep it visible so you can press A.)\n");

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<float> jitter(-0.35f, 0.35f);
    std::uniform_int_distribution<int> sleep_ms(7, 14);
    std::uniform_int_distribution<int> micro_pause_chance(0, 999);

    float vx = 0.f;
    float vy = 0.f;
    float heading = std::uniform_real_distribution<float>(0.f, 6.2831853f)(rng);
    int ticks_until_heading_change = 40;

    while (!is_key_pressed('A')) {
        if (--ticks_until_heading_change <= 0) {
            heading += std::uniform_real_distribution<float>(-0.9f, 0.9f)(rng);
            ticks_until_heading_change = std::uniform_int_distribution<int>(25, 70)(rng);
        }

        const float target_speed = std::uniform_real_distribution<float>(2.5f, 7.5f)(rng);
        vx = vx * 0.82f + std::cos(heading) * target_speed * 0.18f + jitter(rng);
        vy = vy * 0.82f + std::sin(heading) * target_speed * 0.18f + jitter(rng);

        const float speed = std::sqrt(vx * vx + vy * vy);
        constexpr float kMaxSpeed = 12.f;
        if (speed > kMaxSpeed) {
            vx = vx / speed * kMaxSpeed;
            vy = vy / speed * kMaxSpeed;
        }

        const int mx = static_cast<int>(std::lround(vx));
        const int my = static_cast<int>(std::lround(vy));

        if (mx != 0 || my != 0) {
            if (!dev.move_relative(mx, my)) {
                std::fprintf(stderr, "\nDeviceIoControl failed (error %lu). Stopping demo.\n", GetLastError());
                dev.close();
                break;
            }
        }

        if (micro_pause_chance(rng) < 8)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(25, 55)(rng)));
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms(rng)));
    }

    while (is_key_pressed('A'))
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::puts("\nStopped (A released).");
}

void print_main_menu(const VirtualInputHealth& health, const Mouse& dev) {
    std::puts("========================================");
    std::puts("  Logitech G HUB Mouse - Demo Menu");
    std::puts("========================================");
    if (health.mouse_present)
        std::puts("  Virtual mouse: [OK]");
    else if (health.mouse_phantom)
        std::puts("  Virtual mouse: [PHANTOM - moves may not work]");
    else
        std::puts("  Virtual mouse: [NOT READY]");

    if (dev.is_open())
        std::wprintf(L"  Device handle: open (%s)\n", dev.path_used().c_str());
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

int run_interactive_menu() {
    Mouse dev;
    dev.set_layout(ReportLayout::Bytes8);

    for (;;) {
        clear_console();
        const VirtualInputHealth health = query_virtual_input_health();
        print_main_menu(health, dev);

        char line[64]{};
        if (std::fgets(line, sizeof(line), stdin) == nullptr) {
            std::puts("\nInput ended.");
            break;
        }

        switch (parse_menu_choice(line)) {
        case 0:
            dev.close();
            std::puts("Bye.");
            return 0;
        case 1:
            menu_option_random_move(dev);
            wait_for_enter();
            break;
        case 2:
            menu_option_human_smooth_move(dev);
            wait_for_enter();
            break;
        case 3:
            run_diagnose();
            wait_for_enter();
            break;
        case 4:
            run_list_devices();
            wait_for_enter();
            break;
        default:
            std::puts("Invalid choice (enter 0-4).");
            wait_for_enter();
            break;
        }
    }
    return 0;
}

int run_legacy_cli(int argc, char* argv[]) {
    bool do_click = false;
    bool list_only = false;
    bool diagnose_only = false;
    bool force_send = false;
    std::int32_t dx = 100;
    std::int32_t dy = 0;
    ReportLayout layout = ReportLayout::Bytes8;
    std::wstring device_path;
    std::vector<const char*> nums;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--click") == 0) {
            do_click = true;
            continue;
        }
        if (std::strcmp(arg, "--list") == 0) {
            list_only = true;
            continue;
        }
        if (std::strcmp(arg, "--diagnose") == 0) {
            diagnose_only = true;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force_send = true;
            continue;
        }
        if (std::strcmp(arg, "--layout") == 0 && i + 1 < argc) {
            layout = parse_layout(argv[++i]);
            continue;
        }
        if (std::strcmp(arg, "--device") == 0 && i + 1 < argc) {
            const int n = MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, nullptr, 0);
            device_path.resize(static_cast<size_t>(n));
            MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, device_path.data(), n);
            if (!device_path.empty() && device_path.back() == L'\0')
                device_path.pop_back();
            continue;
        }
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (is_decimal_integer(arg)) {
            nums.push_back(arg);
            continue;
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", arg);
            print_usage();
            return 1;
        }
        nums.push_back(arg);
    }

    if (list_only) {
        run_list_devices();
        return 0;
    }
    if (diagnose_only)
        return run_diagnose();

    const VirtualInputHealth health = query_virtual_input_health();
    if (!health.mouse_present && !force_send) {
        std::fprintf(stderr,
                     "\nVirtual mouse (PID_C231) is not active - IOCTL will not move the cursor on this G HUB build.\n"
                     "Run: logitech_ghub_mouse.exe --diagnose\n\n");
        if (health.mouse_phantom)
            return 2;
    }

    if (nums.size() >= 1)
        dx = std::strtol(nums[0], nullptr, 10);
    if (nums.size() >= 2)
        dy = std::strtol(nums[1], nullptr, 10);

    Mouse dev;
    dev.set_layout(layout);
    const wchar_t* force = device_path.empty() ? nullptr : device_path.c_str();
    if (!dev.open(force)) {
        std::fprintf(stderr, "CreateFile failed (error %lu). Is G HUB running?\n", GetLastError());
        return 1;
    }

    std::wprintf(L"Opened: %s\n", dev.path_used().c_str());
    std::printf("Moving relative (%d, %d) using layout %s...\n", dx, dy, layout_name(dev.resolved_layout()));
    if (!dev.move_relative(dx, dy)) {
        std::fprintf(stderr, "DeviceIoControl(0x%lX) failed (error %lu).\n", logitech_ghub::kMouseIoctl,
                     GetLastError());
        return 1;
    }

    std::printf("Sent using %s layout.\n", layout_name(dev.resolved_layout()));
    if (do_click && !dev.click_left()) {
        std::fprintf(stderr, "Click IOCTL failed (error %lu).\n", GetLastError());
        return 1;
    }

    std::puts("Done.");
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    setup_console();
    if (argc <= 1)
        return run_interactive_menu();
    return run_legacy_cli(argc, argv);
}
