/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// serenade-launcher: run a Serenity GUI application headlessly on a host
// system and capture a screenshot of the result.
//
// It brings up WindowServer (a virtual screen backend by default, or a real
// X11 display with --x11) plus optional IPC services and one app over plain
// Unix domain sockets, optionally replays a scripted input sequence, asks
// WindowServer for a PNG (SIGUSR1 + WINDOW_SERVER_SCREENSHOT), optionally
// compares it against a golden image, and tears everything down.
//
// Usage:
//   serenade-launcher --res <Base/res> --screenshot <out.png>
//                     [--delay <ms>] [--width <n>] [--height <n>]
//                     [--log-dir <dir>] [--sys-dir <dir>]
//                     [--service <socket-path>=<binary>]...
//                     [--broker-service <socket-path>=<binary>]...
//                     [--input-root <dir>]
//                     [--script <file>]
//                     [--golden <png>] [--tolerance <channel-delta>]
//                     [--x11]
//                     <window-server-binary> <app-binary> [app args...]
//
// Services are Serenity IPC servers (e.g. Clipboard) that adopt a pre-bound
// socket via the SOCKET_TAKEOVER mechanism, exactly like under SystemServer.
//
// Input: with --input-root, the launcher creates <root>/keyboard/kbd0 and
// <root>/mouse/mouse0 FIFOs and points WindowServer at them via
// WINDOW_SERVER_INPUT_ROOT; --script then replays events into those FIFOs
// using Serenity's on-the-wire KeyEvent/MousePacket formats. Script lines:
//   delay <ms>
//   mouse move <x> <y>
//   mouse click <x> <y> [left|right]
//   mouse drag <x1> <y1> <x2> <y2> [steps]
//   mouse press <x> <y> [left|right]
//   mouse release <x> <y>
//   key <name>            (A-Z, 0-9, Space, Tab, Return, Escape, Backspace, Delete)

#include "PngCompare.h"

#include <Kernel/API/KeyCode.h>
#include <Kernel/API/MousePacket.h>

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr int max_service_count = 8;

struct Service {
    const char* socket_path = nullptr;
    const char* binary = nullptr;
    int listener_fd = -1;
    pid_t child_pid = -1;
};

struct Options {
    const char* res_root = nullptr;
    const char* screenshot_path = nullptr;
    const char* log_dir = "/tmp";
    const char* sys_dir = nullptr; // generated /sys/kernel shim data (default: <log-dir>/sysfs)
    int delay_ms = 2000;
    int width = 1024;
    int height = 768;
    int scale = 1; // HiDPI scale factor: physical = logical * scale (written to the WS config)
    const char* cursor_theme = "Default"; // [Mouse] CursorTheme in the WS config ($SERENITY_RES/cursor-themes/<name>)
    Service services[max_service_count];
    int service_count = 0;
    // Single-client services (SystemServer model): the launcher accepts the
    // client's connection and hands the accepted socket to a freshly spawned
    // service instance, which takes it over via SOCKET_TAKEOVER. Used by
    // WebContent/RequestServer/ImageDecoder (take_over_accepted_client_from_
    // system_server), which expect a connected -- not listening -- socket.
    Service broker_services[max_service_count];
    int broker_service_count = 0;
    const char* input_root = nullptr;
    const char* script_path = nullptr;
    const char* home = nullptr; // set $HOME for all spawned children (real-session fidelity)
    const char* co_app = nullptr; // optional second app, run alongside the primary (same WindowServer)
    int co_app_delay_ms = 2000; // settle time after the primary before spawning the co-app
    const char* golden_path = nullptr;
    int tolerance = 16;
    double expect_window_min = -1.0; // if >= 0, assert a window appeared (non-bg fraction >= this) instead of a golden match
    bool x11 = false; // render to a real X display (Mode=X11) instead of headless Virtual
    pid_t window_server_pid = -1;
    const char* window_server = nullptr;
    const char* app = nullptr;
    char const* const* app_args = nullptr; // argv-style, null-terminated
};

[[noreturn]] void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s --res <Base/res> --screenshot <out.png> [--delay <ms>] [--width <n>] [--height <n>] [--scale <n>] [--cursor-theme <name>] [--log-dir <dir>] [--sys-dir <dir>]\n"
        "              [--service <socket-path>=<binary>]...\n"
        "              [--broker-service <socket-path>=<binary>]...  (single-client services: the accepted socket is handed to a spawned instance)\n"
        "              [--input-root <dir>] [--script <file>] [--home <dir>]\n"
        "              [--co-app <binary>] [--co-app-delay <ms>]\n"
        "              [--golden <png>] [--tolerance <channel-delta>] [--expect-window <min-fraction>]\n"
        "              [--x11]\n"
        "              <window-server-binary> <app-binary> [app args...]\n",
        program);
    exit(2);
}

void parse_args(int argc, char** argv, Options& options)
{
    int i = 1;
    for (; i < argc; ++i) {
        auto arg = argv[i];
        auto next = [&] {
            if (i + 1 >= argc)
                usage(argv[0]);
            return argv[++i];
        };
        if (!strcmp(arg, "--res"))
            options.res_root = next();
        else if (!strcmp(arg, "--screenshot"))
            options.screenshot_path = next();
        else if (!strcmp(arg, "--delay"))
            options.delay_ms = atoi(next());
        else if (!strcmp(arg, "--width"))
            options.width = atoi(next());
        else if (!strcmp(arg, "--height"))
            options.height = atoi(next());
        else if (!strcmp(arg, "--scale"))
            options.scale = atoi(next());
        else if (!strcmp(arg, "--cursor-theme"))
            options.cursor_theme = next();
        else if (!strcmp(arg, "--log-dir"))
            options.log_dir = next();
        else if (!strcmp(arg, "--sys-dir"))
            options.sys_dir = next();
        else if (!strcmp(arg, "--service") || !strcmp(arg, "--broker-service")) {
            auto spec = next();
            char* separator = strchr(const_cast<char*>(spec), '=');
            bool broker = !strcmp(arg, "--broker-service");
            Service& target = broker ? options.broker_services[options.broker_service_count]
                                     : options.services[options.service_count];
            int& count = broker ? options.broker_service_count : options.service_count;
            if (!separator || count >= max_service_count)
                usage(argv[0]);
            *separator = '\0';
            target.socket_path = spec;
            target.binary = separator + 1;
            ++count;
        } else if (!strcmp(arg, "--input-root"))
            options.input_root = next();
        else if (!strcmp(arg, "--script"))
            options.script_path = next();
        else if (!strcmp(arg, "--home"))
            options.home = next();
        else if (!strcmp(arg, "--co-app"))
            options.co_app = next();
        else if (!strcmp(arg, "--co-app-delay"))
            options.co_app_delay_ms = atoi(next());
        else if (!strcmp(arg, "--golden"))
            options.golden_path = next();
        else if (!strcmp(arg, "--tolerance"))
            options.tolerance = atoi(next());
        else if (!strcmp(arg, "--expect-window"))
            options.expect_window_min = atof(next());
        else if (!strcmp(arg, "--x11"))
            options.x11 = true;
        else if (arg[0] == '-' && arg[1] != '\0')
            usage(argv[0]);
        else
            break;
    }

    if (!options.res_root || !options.screenshot_path)
        usage(argv[0]);
    if (options.x11 && options.input_root)
        usage(argv[0]); // real X input and synthetic FIFO input are mutually exclusive
    if (i + 2 > argc)
        usage(argv[0]);
    options.window_server = argv[i];
    options.app = argv[i + 1];
    options.app_args = reinterpret_cast<char const* const*>(argv + i + 1);
}

void make_ancestor_directories(const char* path)
{
    char mutable_copy[4096];
    snprintf(mutable_copy, sizeof(mutable_copy), "%s", path);
    for (char* p = mutable_copy + 1; *p; ++p) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(mutable_copy, 0755) < 0 && errno != EEXIST) {
            fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", mutable_copy, strerror(errno));
            exit(1);
        }
        *p = '/';
    }
}

int create_listening_socket(const char* path)
{
    make_ancestor_directories(path);
    unlink(path); // Remove a stale socket file from a previous run.
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "serenade-launcher: socket: %s\n", strerror(errno));
        exit(1);
    }
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        fprintf(stderr, "serenade-launcher: bind %s: %s\n", path, strerror(errno));
        exit(1);
    }
    if (listen(fd, 5) < 0) {
        fprintf(stderr, "serenade-launcher: listen %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return fd;
}

int open_log_file(const char* log_dir, const char* name)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", log_dir, name);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fprintf(stderr, "serenade-launcher: open %s: %s\n", path, strerror(errno));
        exit(1);
    }
    return fd;
}

// Fork + exec a child that adopts the given listener socket(s) via
// SOCKET_TAKEOVER (the same mechanism Serenity's SystemServer uses).
pid_t spawn_with_takeover(const char* program, char const* const* argv, int log_fd,
    int const* listener_fds, int const* takeover_fds, char const* const* paths, int count)
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "serenade-launcher: fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid == 0) {
        for (int i = 0; i < count; ++i)
            if (dup2(listener_fds[i], takeover_fds[i]) < 0)
                _exit(126);

        char takeover_value[4096];
        int offset = 0;
        for (int i = 0; i < count; ++i) {
            int written = snprintf(takeover_value + offset, sizeof(takeover_value) - offset,
                "%s:%d%s", paths[i], takeover_fds[i], i + 1 < count ? ";" : "");
            if (written < 0 || static_cast<size_t>(offset + written) >= sizeof(takeover_value))
                _exit(126);
            offset += written;
        }
        setenv("SOCKET_TAKEOVER", takeover_value, 1);

        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        execv(program, const_cast<char* const*>(argv));
        fprintf(stderr, "serenade-launcher: exec %s: %s\n", program, strerror(errno));
        _exit(127);
    }
    close(log_fd);
    return pid;
}

pid_t spawn_plain(const char* program, char const* const* argv, int log_fd)
{
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "serenade-launcher: fork: %s\n", strerror(errno));
        exit(1);
    }
    if (pid == 0) {
        dup2(log_fd, STDERR_FILENO);
        close(log_fd);
        execv(program, const_cast<char* const*>(argv));
        fprintf(stderr, "serenade-launcher: exec %s: %s\n", program, strerror(errno));
        _exit(127);
    }
    close(log_fd);
    return pid;
}

long steady_clock_now_ms()
{
    timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
}

void sleep_ms(int ms)
{
    struct timespec nap { ms / 1000, (ms % 1000) * 1000 * 1000 };
    nanosleep(&nap, nullptr);
}

bool wait_for_socket(const char* path, int timeout_ms)
{
    auto deadline = steady_clock_now_ms() + timeout_ms;
    for (;;) {
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd >= 0) {
            sockaddr_un address {};
            address.sun_family = AF_UNIX;
            snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
            bool connected = connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
            close(fd);
            if (connected)
                return true;
        }
        if (steady_clock_now_ms() >= deadline)
            return false;
        sleep_ms(50);
    }
}

bool wait_for_file(const char* path, int timeout_ms)
{
    auto deadline = steady_clock_now_ms() + timeout_ms;
    for (;;) {
        struct stat st {};
        if (stat(path, &st) == 0 && st.st_size > 0)
            return true;
        if (steady_clock_now_ms() >= deadline)
            return false;
        sleep_ms(50);
    }
}

int reap(pid_t pid);
void kill_and_reap(pid_t pid, int sigterm_timeout_ms = 5000);

// Service instances spawned by broker_wait (SystemServer allows concurrent
// instances of the same service, one per accepted connection -- e.g. the
// browser and its WebContent process each hold a RequestServer).
constexpr int max_broker_children = 32;
pid_t s_broker_children[max_broker_children] = {};
int s_broker_child_count = 0;

void kill_and_reap_broker_children()
{
    for (int i = 0; i < s_broker_child_count; ++i) {
        if (s_broker_children[i] > 0)
            kill_and_reap(s_broker_children[i]);
    }
}

// Wait up to timeout_ms, accepting connections on the broker listeners as they
// arrive and spawning a service instance per connection. Mirrors SystemServer:
// these services take over an already-accepted client socket (fd 3), so the
// launcher -- not the service -- owns the listening side. With no brokers this
// is just a sleep, so existing call sites can use it unconditionally.
void broker_wait(Options& options, int timeout_ms)
{
    if (options.broker_service_count == 0) {
        sleep_ms(timeout_ms);
        return;
    }

    auto deadline = steady_clock_now_ms() + timeout_ms;
    for (;;) {
        long remaining = deadline - steady_clock_now_ms();
        if (remaining <= 0)
            return;

        fd_set readfds {};
        int max_fd = -1;
        for (int i = 0; i < options.broker_service_count; ++i) {
            auto& broker = options.broker_services[i];
            if (broker.listener_fd < 0)
                continue;
            FD_SET(broker.listener_fd, &readfds);
            max_fd = max(max_fd, broker.listener_fd);
        }
        if (max_fd < 0) {
            sleep_ms((int)min(remaining, 50L));
            continue;
        }

        timeval tv { (suseconds_t)(remaining / 1000), (suseconds_t)((remaining % 1000) * 1000) };
        int rc = select(max_fd + 1, &readfds, nullptr, nullptr, &tv);
        if (rc < 0 && errno == EINTR)
            continue;

        for (int i = 0; i < options.broker_service_count; ++i) {
            auto& broker = options.broker_services[i];
            if (broker.listener_fd < 0 || !FD_ISSET(broker.listener_fd, &readfds))
                continue;
            int client_fd = accept(broker.listener_fd, nullptr, nullptr);
            if (client_fd < 0)
                continue;

            char log_name[256];
            snprintf(log_name, sizeof(log_name), "serenade-broker-%d.log", i);
            int broker_log = open_log_file(options.log_dir, log_name);
            char const* const service_argv[] { broker.binary, nullptr };
            int listener_fds[] { client_fd };
            int takeover_fds[] { 3 };
            char const* paths[] { broker.socket_path };
            pid_t child_pid = spawn_with_takeover(broker.binary, service_argv,
                broker_log, listener_fds, takeover_fds, paths, 1);
            if (s_broker_child_count < max_broker_children)
                s_broker_children[s_broker_child_count++] = child_pid;
            // The child has its own copy on fd 3; drop ours so the connection
            // closes when the service exits.
            close(client_fd);
        }
    }
}

int reap(pid_t pid)
{
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

// Send SIGTERM and wait for the child to exit, escalating to SIGKILL after a
// bounded timeout. A plain blocking reap() can hang forever if a child never
// dies (observed in practice: a Browser that ignored SIGTERM kept a whole test
// run wedged for hours).
void kill_and_reap(pid_t pid, int sigterm_timeout_ms)
{
    if (pid <= 0)
        return;
    kill(pid, SIGTERM);
    auto deadline = steady_clock_now_ms() + sigterm_timeout_ms;
    for (;;) {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || (rc < 0 && errno == ECHILD))
            return; // exited (or was already reaped by a grouped sibling)
        if (steady_clock_now_ms() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                continue;
            return;
        }
        sleep_ms(50);
    }
}

void write_window_server_config(const char* log_dir, int width, int height, int scale, const char* cursor_theme, const char* mode)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/serenade-WindowServer.ini", log_dir);
    FILE* file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "serenade-launcher: open %s: %s\n", path, strerror(errno));
        exit(1);
    }
    // Note: Core::ConfigFile does not trim whitespace from keys, so the
    // entries must be written as Key=Value (matching Base/etc/WindowServer.ini).
    fprintf(file,
        "[Screens]\n"
        "MainScreen=0\n"
        "[Screen0]\n"
        "Mode=%s\n"
        "Left=0\n"
        "Top=0\n"
        "Width=%d\n"
        "Height=%d\n"
        "ScaleFactor=%d\n"
        // [Theme] must be present: WindowServer::get_system_theme() returns the
        // raw config entry (no default), and apps like DisplaySettings match it
        // against /res/themes by name -- an empty name matches nothing.
        "[Theme]\n"
        "Name=Default\n"
        "[Mouse]\n"
        "CursorTheme=%s\n",
        mode, width, height, scale, cursor_theme);
    fclose(file);
    setenv("WINDOW_SERVER_CONFIG", path, 1);
}

// ---------------------------------------------------------------------------
// Synthetic input (M2)
//
// WindowServer drains its input sources as raw KeyEvent / MousePacket structs
// (the same format DeviceMapper writes on Serenity). We feed it through FIFOs
// under a private input root, so no other part of the system is involved.
// ---------------------------------------------------------------------------

struct InputChannels {
    int keyboard_fd = -1;
    int mouse_fd = -1;
    int screen_width = 0;
    int screen_height = 0;
};

InputChannels setup_input_root(const char* input_root, int screen_width, int screen_height)
{
    InputChannels channels;
    channels.screen_width = screen_width;
    channels.screen_height = screen_height;
    char path[4096];

    make_ancestor_directories(input_root);
    if (mkdir(input_root, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", input_root, strerror(errno));
        exit(1);
    }

    snprintf(path, sizeof(path), "%s/keyboard", input_root);
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", path, strerror(errno));
        exit(1);
    }
    snprintf(path, sizeof(path), "%s/mouse", input_root);
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", path, strerror(errno));
        exit(1);
    }

    auto make_fifo = [&](const char* fifo_path) {
        unlink(fifo_path);
        if (mkfifo(fifo_path, 0644) < 0) {
            fprintf(stderr, "serenade-launcher: mkfifo %s: %s\n", fifo_path, strerror(errno));
            exit(1);
        }
    };

    snprintf(path, sizeof(path), "%s/keyboard/kbd0", input_root);
    make_fifo(path);
    snprintf(path, sizeof(path), "%s/mouse/mouse0", input_root);
    make_fifo(path);

    // Open the FIFOs read-write: on Linux, opening a FIFO write-only (even
    // with O_NONBLOCK) fails with ENXIO while no reader is attached, while
    // O_RDWR|O_NONBLOCK always succeeds. WindowServer opens the read side
    // during its device scan; our writes reach it through the shared FIFO.
    snprintf(path, sizeof(path), "%s/keyboard/kbd0", input_root);
    channels.keyboard_fd = open(path, O_RDWR | O_NONBLOCK);
    snprintf(path, sizeof(path), "%s/mouse/mouse0", input_root);
    channels.mouse_fd = open(path, O_RDWR | O_NONBLOCK);
    if (channels.keyboard_fd < 0 || channels.mouse_fd < 0) {
        fprintf(stderr, "serenade-launcher: opening input FIFOs: %s\n", strerror(errno));
        exit(1);
    }
    return channels;
}

void write_mouse_packet(InputChannels& channels, int x, int y, unsigned char buttons, bool is_relative)
{
    MousePacket packet {};
    if (is_relative) {
        packet.x = x;
        packet.y = y;
    } else {
        // ScreenInput scales absolute coordinates from a 16-bit range onto the
        // screen (packet.x * width / 0xffff), so convert screen pixels to that
        // wire format here.
        packet.x = x * 0xffff / channels.screen_width;
        packet.y = y * 0xffff / channels.screen_height;
    }
    packet.z = 0;
    packet.w = 0;
    packet.buttons = buttons;
    packet.is_relative = is_relative;
    if (write(channels.mouse_fd, &packet, sizeof(packet)) != static_cast<ssize_t>(sizeof(packet))) {
        fprintf(stderr, "serenade-launcher: writing mouse packet: %s\n", strerror(errno));
        exit(1);
    }
}

void write_key_event(InputChannels& channels, KeyCode key, bool press)
{
    ::KeyEvent event {};
    event.key = key;
    event.map_entry_index = 0;
    event.scancode = 0;
    event.code_point = 0;
    event.flags = press ? KeyModifier::Is_Press : 0;
    event.caps_lock_on = false;
    if (write(channels.keyboard_fd, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event))) {
        fprintf(stderr, "serenade-launcher: writing key event: %s\n", strerror(errno));
        exit(1);
    }
}

KeyCode key_code_for_name(char const* name)
{
    // Values come from the pinned KeyCode enum -- never hardcode numbers.
    struct Entry {
        char const* name;
        KeyCode code;
    } static const table[] = {
        { "A", KeyCode::Key_A }, { "B", KeyCode::Key_B }, { "C", KeyCode::Key_C },
        { "D", KeyCode::Key_D }, { "E", KeyCode::Key_E }, { "F", KeyCode::Key_F },
        { "G", KeyCode::Key_G }, { "H", KeyCode::Key_H }, { "I", KeyCode::Key_I },
        { "J", KeyCode::Key_J }, { "K", KeyCode::Key_K }, { "L", KeyCode::Key_L },
        { "M", KeyCode::Key_M }, { "N", KeyCode::Key_N }, { "O", KeyCode::Key_O },
        { "P", KeyCode::Key_P }, { "Q", KeyCode::Key_Q }, { "R", KeyCode::Key_R },
        { "S", KeyCode::Key_S }, { "T", KeyCode::Key_T }, { "U", KeyCode::Key_U },
        { "V", KeyCode::Key_V }, { "W", KeyCode::Key_W }, { "X", KeyCode::Key_X },
        { "Y", KeyCode::Key_Y }, { "Z", KeyCode::Key_Z },
        { "0", KeyCode::Key_0 }, { "1", KeyCode::Key_1 }, { "2", KeyCode::Key_2 },
        { "3", KeyCode::Key_3 }, { "4", KeyCode::Key_4 }, { "5", KeyCode::Key_5 },
        { "6", KeyCode::Key_6 }, { "7", KeyCode::Key_7 }, { "8", KeyCode::Key_8 },
        { "9", KeyCode::Key_9 },
        { "Space", KeyCode::Key_Space },
        { "Tab", KeyCode::Key_Tab },
        { "Return", KeyCode::Key_Return },
        { "Escape", KeyCode::Key_Escape },
        { "Backspace", KeyCode::Key_Backspace },
        { "Delete", KeyCode::Key_Delete },
    };

    char single[2] {};
    if (strlen(name) == 1) {
        single[0] = name[0];
        if (single[0] >= 'a' && single[0] <= 'z')
            single[0] = static_cast<char>(single[0] - 'a' + 'A');
        for (auto const& entry : table)
            if (!strcmp(entry.name, single))
                return entry.code;
    }
    for (auto const& entry : table)
        if (!strcmp(entry.name, name))
            return entry.code;
    fprintf(stderr, "serenade-launcher: unknown key name '%s'\n", name);
    exit(2);
}

void mouse_move(InputChannels& channels, int x, int y)
{
    write_mouse_packet(channels, x, y, 0, false);
}

void mouse_click(InputChannels& channels, int x, int y, char const* button)
{
    auto which = !strcmp(button, "right") ? MousePacket::Button::RightButton : MousePacket::Button::LeftButton;
    write_mouse_packet(channels, x, y, 0, false); // position
    sleep_ms(20);
    write_mouse_packet(channels, x, y, static_cast<unsigned char>(which), false); // press
    sleep_ms(40);
    write_mouse_packet(channels, x, y, 0, false); // release
}

void mouse_press(InputChannels& channels, int x, int y, char const* button)
{
    auto which = !strcmp(button, "right") ? MousePacket::Button::RightButton : MousePacket::Button::LeftButton;
    write_mouse_packet(channels, x, y, 0, false); // position
    sleep_ms(20);
    write_mouse_packet(channels, x, y, static_cast<unsigned char>(which), false); // press
}

void mouse_release(InputChannels& channels, int x, int y)
{
    write_mouse_packet(channels, x, y, 0, false); // release
}

void mouse_drag(InputChannels& channels, int x1, int y1, int x2, int y2, int steps)
{
    if (steps < 2)
        steps = 10;
    write_mouse_packet(channels, x1, y1, 0, false);
    sleep_ms(20);
    write_mouse_packet(channels, x1, y1, MousePacket::Button::LeftButton, false); // press
    for (int i = 1; i <= steps; ++i) {
        int x = x1 + (x2 - x1) * i / steps;
        int y = y1 + (y2 - y1) * i / steps;
        sleep_ms(15);
        write_mouse_packet(channels, x, y, MousePacket::Button::LeftButton, false);
    }
    sleep_ms(20);
    write_mouse_packet(channels, x2, y2, 0, false); // release
}

void key_press(InputChannels& channels, char const* name)
{
    auto code = key_code_for_name(name);
    write_key_event(channels, code, true);
    sleep_ms(30);
    write_key_event(channels, code, false);
}

void replay_script(const char* script_path, InputChannels& channels)
{
    FILE* file = fopen(script_path, "r");
    if (!file) {
        fprintf(stderr, "serenade-launcher: open %s: %s\n", script_path, strerror(errno));
        exit(1);
    }
    char line[512];
    int line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        ++line_number;
        // Strip trailing newline.
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#')
            continue;

        char command[32] {};
        int n = sscanf(line, "%31s", command);
        if (n != 1) {
            fprintf(stderr, "serenade-launcher: %s:%d: bad line\n", script_path, line_number);
            exit(2);
        }

        if (!strcmp(command, "delay")) {
            int ms = 0;
            if (sscanf(line + 5, "%d", &ms) != 1 || ms < 0) {
                fprintf(stderr, "serenade-launcher: %s:%d: bad delay\n", script_path, line_number);
                exit(2);
            }
            sleep_ms(ms);
        } else if (!strcmp(command, "mouse")) {
            char action[32] {};
            int a = sscanf(line + 5, "%31s", action);
            if (a != 1) {
                fprintf(stderr, "serenade-launcher: %s:%d: bad mouse command\n", script_path, line_number);
                exit(2);
            }
            if (!strcmp(action, "move")) {
                int x = 0, y = 0;
                if (sscanf(line + 10, "%d %d", &x, &y) != 2) {
                    fprintf(stderr, "serenade-launcher: %s:%d: bad mouse move\n", script_path, line_number);
                    exit(2);
                }
                mouse_move(channels, x, y);
            } else if (!strcmp(action, "click")) {
                int x = 0, y = 0;
                char button[16] = "left";
                int parsed = sscanf(line + 11, "%d %d %15s", &x, &y, button);
                if (parsed < 2) {
                    fprintf(stderr, "serenade-launcher: %s:%d: bad mouse click\n", script_path, line_number);
                    exit(2);
                }
                mouse_click(channels, x, y, button);
            } else if (!strcmp(action, "drag")) {
                int x1 = 0, y1 = 0, x2 = 0, y2 = 0, steps = 0;
                if (sscanf(line + 10, "%d %d %d %d %d", &x1, &y1, &x2, &y2, &steps) < 4) {
                    fprintf(stderr, "serenade-launcher: %s:%d: bad mouse drag\n", script_path, line_number);
                    exit(2);
                }
                mouse_drag(channels, x1, y1, x2, y2, steps);
            } else if (!strcmp(action, "press")) {
                int x = 0, y = 0;
                char button[16] = "left";
                int parsed = sscanf(line + 11, "%d %d %15s", &x, &y, button);
                if (parsed < 2) {
                    fprintf(stderr, "serenade-launcher: %s:%d: bad mouse press\n", script_path, line_number);
                    exit(2);
                }
                mouse_press(channels, x, y, button);
            } else if (!strcmp(action, "release")) {
                int x = 0, y = 0;
                if (sscanf(line + 13, "%d %d", &x, &y) != 2) {
                    fprintf(stderr, "serenade-launcher: %s:%d: bad mouse release\n", script_path, line_number);
                    exit(2);
                }
                mouse_release(channels, x, y);
            } else {
                fprintf(stderr, "serenade-launcher: %s:%d: unknown mouse action '%s'\n", script_path, line_number, action);
                exit(2);
            }
        } else if (!strcmp(command, "key")) {
            char name[32] {};
            if (sscanf(line + 4, "%31s", name) != 1) {
                fprintf(stderr, "serenade-launcher: %s:%d: bad key command\n", script_path, line_number);
                exit(2);
            }
            key_press(channels, name);
        } else {
            fprintf(stderr, "serenade-launcher: %s:%d: unknown command '%s'\n", script_path, line_number, command);
            exit(2);
        }
    }
    fclose(file);
}

bool compare_golden(const char* screenshot_path, const char* golden_path, int tolerance)
{
    auto report_load_failure = [](char const* path, AK::Error const& error) {
        auto message = error.string_literal();
        fprintf(stderr, "serenade-launcher: reading %s: %.*s\n",
            path, static_cast<int>(message.length()), message.characters_without_null_termination());
    };

    auto actual_or_error = Serenade::load_png_bitmap({ screenshot_path, strlen(screenshot_path) });
    if (actual_or_error.is_error()) {
        report_load_failure(screenshot_path, actual_or_error.error());
        return false;
    }
    auto golden_or_error = Serenade::load_png_bitmap({ golden_path, strlen(golden_path) });
    if (golden_or_error.is_error()) {
        report_load_failure(golden_path, golden_or_error.error());
        return false;
    }
    auto const& actual = actual_or_error.value();
    auto const& golden = golden_or_error.value();
    auto result = Serenade::compare_pngs(actual, golden, tolerance);
    if (!result.sizes_match) {
        fprintf(stderr, "serenade-launcher: size mismatch: actual %dx%d vs golden %dx%d\n",
            result.actual_width, result.actual_height, result.golden_width, result.golden_height);
        return false;
    }
    size_t total = static_cast<size_t>(result.actual_width) * static_cast<size_t>(result.actual_height);
    double fraction = static_cast<double>(result.differing_pixels) / static_cast<double>(total);
    printf("serenade-launcher: golden comparison: %zu/%zu pixels differ (max delta %d, tolerance %d)\n",
        result.differing_pixels, total, result.max_channel_delta, tolerance);
    // Same-machine rendering is deterministic; allow a tiny sliver for
    // anti-aliasing edge cases.
    return fraction <= 0.001;
}

void cleanup(Options& options, pid_t window_server_pid)
{
    for (int i = 0; i < options.service_count; ++i)
        unlink(options.services[i].socket_path);
    for (int i = 0; i < options.broker_service_count; ++i)
        unlink(options.broker_services[i].socket_path);
    kill_and_reap_broker_children();
    // Grouped services share a pid; kill_and_reap tolerates the duplicates.
    for (int i = 0; i < options.service_count; ++i)
        kill_and_reap(options.services[i].child_pid);
    kill_and_reap(window_server_pid);
    unlink("/tmp/portal/window");
    unlink("/tmp/portal/wm");
}

pid_t s_window_server_pid = -1;
pid_t s_app_pid = -1;
pid_t s_co_app_pid = -1;
Options* s_options = nullptr;

void on_signal(int)
{
    // Async-signal-safe best effort: kill the children so nothing is left
    // running, then exit. (kill/_exit are async-signal-safe.)
    if (s_app_pid > 0)
        kill(s_app_pid, SIGKILL);
    if (s_co_app_pid > 0)
        kill(s_co_app_pid, SIGKILL);
    if (s_window_server_pid > 0)
        kill(s_window_server_pid, SIGKILL);
    if (s_options) {
        for (int i = 0; i < s_options->service_count; ++i)
            if (s_options->services[i].child_pid > 0)
                kill(s_options->services[i].child_pid, SIGKILL);
    }
    // Broker children are tracked globally (concurrent instances per path).
    for (int i = 0; i < s_broker_child_count; ++i)
        if (s_broker_children[i] > 0)
            kill(s_broker_children[i], SIGKILL);
    _exit(130);
}

}

// --- sysfs shim ---------------------------------------------------------------
//
// SystemMonitor, NetworkSettings and SpaceAnalyzer read Serenity kernel
// pseudo-files (/sys/kernel/processes, memstat, cpuinfo, df, net/*). On a
// host there is no Serenity kernel, so the launcher generates equivalent JSON
// from host sources into a per-session directory and points $SERENITY_SYS at
// it; LibCore maps /sys/kernel/* reads onto that directory (patch 0073).
//
// Sources: memory/cpu info via POSIX sysconf, filesystem stats via statvfs,
// network adapters via getifaddrs -- all portable to BSDs. The process list
// uses Linux's /proc; other OSes get an empty (but valid) list until a
// sysctl-based backend is added (M8). See docs/PORTING.md.

void json_escape(FILE* out, const char* s)
{
    fputc('"', out);
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"')
            fputs("\\\"", out);
        else if (c == '\\')
            fputs("\\\\", out);
        else if (c < 0x20)
            fputs(" ", out);
        else
            fputc((char)c, out);
    }
    fputc('"', out);
}

// Read a whole small file; returns false if it could not be read.
bool read_small_file(const char* path, char* buffer, size_t buffer_size)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    size_t n = fread(buffer, 1, buffer_size - 1, f);
    fclose(f);
    buffer[n] = '\0';
    return true;
}

#ifdef __linux__
// Parse /proc/<pid>/stat: comm is between the first '(' and last ')'; the
// numeric fields after it start with the state (field 3).
bool parse_proc_stat(const char* pid_path, const char** comm_out, int* ppid, int* pgid, int* sid, unsigned long long* creation_ns)
{
    char stat[4096];
    if (!read_small_file(pid_path, stat, sizeof(stat)))
        return false;
    char* open = strchr(stat, '(');
    char* close = strrchr(stat, ')');
    if (!open || !close || close <= open)
        return false;
    *close = '\0'; // terminate comm in place (it may contain spaces or parens)
    *comm_out = open + 1;
    char* rest = close + 1;
    char* tokens[32];
    int token_count = 0;
    for (char* p = rest; p && token_count < 32; ) {
        while (*p == ' ')
            ++p;
        if (!*p)
            break;
        tokens[token_count++] = p;
        p = strchr(p, ' ');
    }
    // tokens[0]=state [1]=ppid [2]=pgrp [3]=session ... [19]=starttime (ns since boot)
    if (token_count < 4)
        return false;
    *ppid = atoi(tokens[1]);
    *pgid = atoi(tokens[2]);
    *sid = atoi(tokens[3]);
    unsigned long long starttime_ticks = token_count > 19 ? strtoull(tokens[19], nullptr, 10) : 0;
    // /proc/<pid>/stat reports starttime in clock ticks since boot.
    long hz = sysconf(_SC_CLK_TCK);
    if (hz < 1)
        hz = 100;
    unsigned long long starttime_ns = starttime_ticks * 1000000000ULL / (unsigned long)hz;
    char btime_buf[65536];
    if (read_small_file("/proc/stat", btime_buf, sizeof(btime_buf))) {
        char* btime = strstr(btime_buf, "btime ");
        unsigned long long boot_ns = btime ? strtoull(btime + 6, nullptr, 10) * 1000000000ULL : 0;
        *creation_ns = boot_ns + starttime_ns;
    } else {
        *creation_ns = 0;
    }
    return true;
}

unsigned long long read_status_field(const char* path, const char* key)
{
    char buf[8192];
    if (!read_small_file(path, buf, sizeof(buf)))
        return 0;
    char* line = strstr(buf, key);
    if (!line)
        return 0;
    return strtoull(line + strlen(key), nullptr, 10);
}

void write_processes_json(FILE* out)
{
    fputs("{\"processes\":[", out);
    DIR* proc = opendir("/proc");
    if (!proc) {
        fputs("],\"total_time\":0,\"total_time_kernel\":0}\n", out);
        return;
    }
    bool first = true;
    for (struct dirent* d = readdir(proc); d; d = readdir(proc)) {
        if (d->d_name[0] < '1' || d->d_name[0] > '9')
            continue;
        int pid = atoi(d->d_name);
        char stat_path[64];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
        const char* comm = nullptr;
        int ppid = 0, pgid = 0, sid = 0;
        unsigned long long creation_ns = 0;
        if (!parse_proc_stat(stat_path, &comm, &ppid, &pgid, &sid, &creation_ns))
            continue;

        char status_path[64];
        snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
        unsigned long long vm_size = read_status_field(status_path, "VmSize:");
        unsigned long long vm_rss = read_status_field(status_path, "VmRSS:");
        char status_buf[8192];
        unsigned uid = 0, gid = 0;
        if (read_small_file(status_path, status_buf, sizeof(status_buf))) {
            char* line = strstr(status_buf, "Uid:");
            if (line)
                uid = strtoull(line + 4, nullptr, 10);
            line = strstr(status_buf, "Gid:");
            if (line)
                gid = strtoull(line + 4, nullptr, 10);
        }

        char exe[256];
        char exe_link[64];
        snprintf(exe_link, sizeof(exe_link), "/proc/%d/exe", pid);
        ssize_t n = readlink(exe_link, exe, sizeof(exe) - 1);
        exe[n < 0 ? 0 : n] = '\0';

        if (!first)
            fputc(',', out);
        first = false;
        fprintf(out, "{\"pid\":%d,\"pgid\":%d,\"pgp\":%d,\"sid\":%d,\"uid\":%u,\"gid\":%u,\"ppid\":%d,\"kernel\":false,"
                     "\"name\":", pid, pgid, pgid, sid, uid, gid, ppid);
        json_escape(out, comm);
        fprintf(out, ",\"executable\":");
        json_escape(out, n > 0 ? exe : "");
        fprintf(out, ",\"tty\":\"\",\"pledge\":\"\",\"veil\":\"\",\"creation_time\":%llu,"
                     "\"amount_virtual\":%llu,\"amount_resident\":%llu,\"amount_shared\":0,"
                     "\"amount_dirty_private\":0,\"amount_clean_inode\":0,"
                     "\"amount_purgeable_volatile\":0,\"amount_purgeable_nonvolatile\":0,\"threads\":[",
                     creation_ns, vm_size, vm_rss);

        char task_dir[64];
        snprintf(task_dir, sizeof(task_dir), "/proc/%d/task", pid);
        DIR* tasks = opendir(task_dir);
        bool first_thread = true;
        if (tasks) {
            for (struct dirent* t = readdir(tasks); t; t = readdir(tasks)) {
                if (t->d_name[0] < '1' || t->d_name[0] > '9')
                    continue;
                int tid = atoi(t->d_name);
                char task_stat[64];
                snprintf(task_stat, sizeof(task_stat), "/proc/%d/task/%d/stat", pid, tid);
                const char* tcomm = "";
                int dummy_ppid = 0, dummy_pgid = 0, dummy_sid = 0;
                unsigned long long dummy_creation = 0;
                if (parse_proc_stat(task_stat, &tcomm, &dummy_ppid, &dummy_pgid, &dummy_sid, &dummy_creation)) {
                    if (!first_thread)
                        fputc(',', out);
                    first_thread = false;
                    fprintf(out, "{\"tid\":%d,\"times_scheduled\":0,\"name\":", tid);
                    json_escape(out, tcomm);
                    fprintf(out, ",\"state\":\"Running\",\"time_user\":0,\"time_kernel\":0,\"cpu\":0,\"priority\":0,"
                                "\"syscall_count\":0,\"inode_faults\":0,\"zero_faults\":0,\"cow_faults\":0,"
                                "\"unix_socket_read_bytes\":0,\"unix_socket_write_bytes\":0,"
                                "\"ipv4_socket_read_bytes\":0,\"ipv4_socket_write_bytes\":0,"
                                "\"file_read_bytes\":0,\"file_write_bytes\":0}");
                }
            }
            closedir(tasks);
        }
        fputs("]}", out);
    }
    closedir(proc);
    fputs("],\"total_time\":0,\"total_time_kernel\":0}\n", out);
}
#endif

void write_sysfs_data(const char* dir)
{
    if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", dir, strerror(errno));
        exit(1);
    }
    char net_dir[4200];
    snprintf(net_dir, sizeof(net_dir), "%s/net", dir);
    if (mkdir(net_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "serenade-launcher: mkdir %s: %s\n", net_dir, strerror(errno));
        exit(1);
    }

    // processes: host process list (Linux /proc backend; empty elsewhere).
    char path[4200];
    snprintf(path, sizeof(path), "%s/processes", dir);
    FILE* out = fopen(path, "w");
    if (!out) {
        fprintf(stderr, "serenade-launcher: open %s: %s\n", path, strerror(errno));
        exit(1);
    }
#ifdef __linux__
    write_processes_json(out);
#else
    fputs("{\"processes\":[],\"total_time\":0,\"total_time_kernel\":0}\n", out);
#endif
    fclose(out);

    // memstat: page counts (MemoryStatsWidget multiplies them by 4096).
    unsigned long long phys_pages = (unsigned long long)sysconf(_SC_PHYS_PAGES);
    unsigned long long avail_pages = (unsigned long long)sysconf(_SC_AVPHYS_PAGES);
    if (phys_pages == 0)
        phys_pages = 1; // GraphWidget divides by the total; never zero.
    if (avail_pages > phys_pages)
        avail_pages = phys_pages;
    snprintf(path, sizeof(path), "%s/memstat", dir);
    out = fopen(path, "w");
    fprintf(out, "{\"kmalloc_allocated\":0,\"kmalloc_available\":0,"
                 "\"physical_allocated\":%llu,\"physical_available\":%llu,"
                 "\"physical_committed\":0,\"physical_uncommitted\":0,"
                 "\"kmalloc_call_count\":0,\"kfree_call_count\":0}\n",
            phys_pages - avail_pages, avail_pages);
    fclose(out);

    // cpuinfo: one entry per online CPU.
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1)
        ncpu = 1;
    snprintf(path, sizeof(path), "%s/cpuinfo", dir);
    out = fopen(path, "w");
    fputs("[", out);
    for (long i = 0; i < ncpu; ++i)
        fprintf(out, "%s{\"processor\":%ld}", i ? "," : "", i);
    fputs("]\n", out);
    fclose(out);

    // df: the session home is the shim's "mount"; statvfs gives real stats.
    const char* home = getenv("HOME");
    if (!home)
        home = ".";
    struct statvfs sf {};
    if (statvfs(home, &sf) == 0) {
        // Inode counters are named differently on Linux vs. the BSDs.
        unsigned long total_inodes = 0;
        unsigned long free_inodes = 0;
#ifdef __linux__
        total_inodes = (unsigned long)sf.f_files;
        free_inodes = (unsigned long)sf.f_ffree;
#else
        total_inodes = (unsigned long)sf.f_inodes;
        free_inodes = (unsigned long)sf.f_ifree;
#endif
        snprintf(path, sizeof(path), "%s/df", dir);
        out = fopen(path, "w");
        fprintf(out, "[{\"mount_point\":");
        json_escape(out, home);
        fprintf(out, ",\"source\":");
        json_escape(out, "serenade-shim");
        fprintf(out, ",\"readonly\":false,\"mount_flags\":0,"
                     "\"block_size\":%lu,\"total_block_count\":%lu,\"free_block_count\":%lu,"
                     "\"total_inode_count\":%lu,\"free_inode_count\":%lu}]\n",
                (unsigned long)sf.f_bsize, (unsigned long)sf.f_blocks, (unsigned long)sf.f_bfree,
                total_inodes, free_inodes);
        fclose(out);
    } else {
        snprintf(path, sizeof(path), "%s/df", dir);
        out = fopen(path, "w");
        fputs("[]\n", out);
        fclose(out);
    }

    // net/adapters: interfaces with an IPv4 address (getifaddrs is POSIX).
    snprintf(path, sizeof(path), "%s/net/adapters", dir);
    out = fopen(path, "w");
    fputs("[", out);
    bool first_adapter = true;
    ifaddrs* addrs = nullptr;
    if (getifaddrs(&addrs) == 0) {
        for (ifaddrs* a = addrs; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
                continue;
            if (!first_adapter)
                fputc(',', out);
            first_adapter = false;
            fprintf(out, "{\"name\":");
            json_escape(out, a->ifa_name);
            fprintf(out, ",\"ipv4_address\":");
            json_escape(out, inet_ntoa(((sockaddr_in*)a->ifa_addr)->sin_addr));
            fprintf(out, ",\"link_speed\":0,\"link_full_duplex\":false,"
                         "\"packets_in\":0,\"packets_out\":0,\"bytes_in\":0,\"bytes_out\":0,"
                         "\"packets_dropped\":0}");
        }
        freeifaddrs(addrs);
    }
    fputs("]\n", out);
    fclose(out);

    // Socket tables: no portable way to enumerate host sockets; empty is
    // valid and renders as an empty table.
    snprintf(path, sizeof(path), "%s/net/tcp", dir);
    out = fopen(path, "w");
    fputs("[]\n", out);
    fclose(out);
    snprintf(path, sizeof(path), "%s/net/udp", dir);
    out = fopen(path, "w");
    fputs("[]\n", out);
    fclose(out);
}

int main(int argc, char** argv)
{
    Options options;
    parse_args(argc, argv, options);
    s_options = &options;

    // Pre-bind all service sockets and hand them to their servers via
    // SOCKET_TAKEOVER (the same mechanism Serenity's SystemServer uses).
    int window_fd = create_listening_socket("/tmp/portal/window");
    int wm_fd = create_listening_socket("/tmp/portal/wm");
    for (int i = 0; i < options.service_count; ++i)
        options.services[i].listener_fd = create_listening_socket(options.services[i].socket_path);
    for (int i = 0; i < options.broker_service_count; ++i)
        options.broker_services[i].listener_fd = create_listening_socket(options.broker_services[i].socket_path);

    setenv("SERENITY_RES", options.res_root, 1);
    setenv("WINDOW_SERVER_SCREENSHOT", options.screenshot_path, 1);
    if (options.home)
        setenv("HOME", options.home, 1); // apps read $HOME for config and default paths

    // Generate the /sys/kernel shim data before any app starts so that
    // SystemMonitor/NetworkSettings/SpaceAnalyzer find it on first read.
    char sys_dir_path[4200];
    if (options.sys_dir)
        snprintf(sys_dir_path, sizeof(sys_dir_path), "%s", options.sys_dir);
    else
        snprintf(sys_dir_path, sizeof(sys_dir_path), "%s/sysfs", options.log_dir);
    make_ancestor_directories(sys_dir_path);
    write_sysfs_data(sys_dir_path);
    setenv("SERENITY_SYS", sys_dir_path, 1);
    write_window_server_config(options.log_dir, options.width, options.height, options.scale, options.cursor_theme,
        options.x11 ? "X11" : "Virtual");

    // Create the input FIFOs before WindowServer starts: its device scan runs
    // during construction and there is no devicemap watcher on host systems to
    // trigger a rescan later. (Opening the write ends early is safe -- with
    // O_NONBLOCK the data just buffers in the FIFO until WS drains it.)
    InputChannels input {};
    if (options.input_root) {
        setenv("WINDOW_SERVER_INPUT_ROOT", options.input_root, 1);
        input = setup_input_root(options.input_root, options.width, options.height);
    }

    struct sigaction action {};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    // WindowServer adopts two sockets: /tmp/portal/window (fd 3) and
    // /tmp/portal/wm (fd 4).
    {
        int ws_log = open_log_file(options.log_dir, "serenade-windowserver.log");
        char const* const ws_argv[] { options.window_server, nullptr };
        int listener_fds[] { window_fd, wm_fd };
        int takeover_fds[] { 3, 4 };
        char const* paths[] { "/tmp/portal/window", "/tmp/portal/wm" };
        options.window_server_pid = spawn_with_takeover(options.window_server, ws_argv,
            ws_log, listener_fds, takeover_fds, paths, 2);
        s_window_server_pid = options.window_server_pid;
    }

    if (!wait_for_socket("/tmp/portal/window", 15000)) {
        fprintf(stderr, "serenade-launcher: WindowServer did not become ready (see %s/serenade-windowserver.log)\n", options.log_dir);
        cleanup(options, options.window_server_pid);
        return 1;
    }

    // Record the WindowServer's pid so external drivers can poke it while the
    // session runs (e.g. SIGUSR1 for mid-session screenshot probes).
    if (options.log_dir) {
        char pid_path[4096];
        snprintf(pid_path, sizeof(pid_path), "%s/serenade-windowserver.pid", options.log_dir);
        int pid_file = open(pid_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (pid_file >= 0) {
            char buf[16];
            int n = snprintf(buf, sizeof(buf), "%d\n", (int)options.window_server_pid);
            (void)!write(pid_file, buf, (size_t)n);
            close(pid_file);
        }
    }

    // Services that share a binary must run as a single process: Serenity's
    // SystemServer hands all of a server's sockets to one process via
    // SOCKET_TAKEOVER (e.g. AudioServer owns both the audio and audiomanager
    // sockets). Group the service entries by binary and spawn one process per
    // group, passing every socket in the group on consecutive fds from 3.
    {
        int group_count = 0;
        const char* group_binaries[max_service_count] = {};
        int group_members[max_service_count][max_service_count];
        int group_member_count[max_service_count] = {};

        for (int i = 0; i < options.service_count; ++i) {
            int group = -1;
            for (int g = 0; g < group_count; ++g) {
                if (!strcmp(group_binaries[g], options.services[i].binary)) {
                    group = g;
                    break;
                }
            }
            if (group < 0) {
                group = group_count++;
                group_binaries[group] = options.services[i].binary;
            }
            group_members[group][group_member_count[group]++] = i;
        }

        for (int g = 0; g < group_count; ++g) {
            int const member_count = group_member_count[g];
            char log_name[256];
            snprintf(log_name, sizeof(log_name), "serenade-service-%d.log", group_members[g][0]);
            int service_log = open_log_file(options.log_dir, log_name);
            char const* const service_argv[] { group_binaries[g], nullptr };
            int listener_fds[max_service_count];
            int takeover_fds[max_service_count];
            char const* paths[max_service_count];
            for (int m = 0; m < member_count; ++m) {
                auto& service = options.services[group_members[g][m]];
                listener_fds[m] = service.listener_fd;
                takeover_fds[m] = 3 + m;
                paths[m] = service.socket_path;
            }
            pid_t child_pid = spawn_with_takeover(group_binaries[g], service_argv,
                service_log, listener_fds, takeover_fds, paths, member_count);
            for (int m = 0; m < member_count; ++m)
                options.services[group_members[g][m]].child_pid = child_pid;
            for (int m = 0; m < member_count; ++m) {
                auto& service = options.services[group_members[g][m]];
                if (!wait_for_socket(service.socket_path, 10000)) {
                    fprintf(stderr, "serenade-launcher: service %s did not become ready (see %s/%s)\n",
                        service.socket_path, options.log_dir, log_name);
                    cleanup(options, options.window_server_pid);
                    return 1;
                }
            }
        }
    }

    int app_log = open_log_file(options.log_dir, "serenade-app.log");
    pid_t app_pid = spawn_plain(options.app, options.app_args, app_log);
    s_app_pid = app_pid;

    if (options.script_path) {
        // Give the app a moment to create its window before driving it.
        broker_wait(options, 1500);
        replay_script(options.script_path, input);
    }

    pid_t co_app_pid = -1;
    if (options.co_app) {
        // Let the primary act first (e.g. publish text to the clipboard), then bring
        // up a second app against the same WindowServer for cross-app tests.
        broker_wait(options, options.co_app_delay_ms);
        int co_log = open_log_file(options.log_dir, "serenade-co-app.log");
        char const* const co_argv[] { options.co_app, nullptr };
        co_app_pid = spawn_plain(options.co_app, co_argv, co_log);
        s_co_app_pid = co_app_pid;
    }

    // Wait out the settle delay; also accept brokered-service connections that
    // arrive during this window (e.g. the app's first IPC connect).
    broker_wait(options, options.delay_ms);

    // Remove any stale screenshot from an earlier run: WindowServer (re)creates the file
    // on SIGUSR1, so a pre-existing file must not satisfy wait_for_file below.
    unlink(options.screenshot_path);
    kill(options.window_server_pid, SIGUSR1);
    bool got_screenshot = wait_for_file(options.screenshot_path, 10000);

    kill_and_reap(app_pid);
    kill_and_reap(co_app_pid);
    cleanup(options, options.window_server_pid);

    if (!got_screenshot) {
        fprintf(stderr, "serenade-launcher: no screenshot produced at %s (see logs in %s)\n", options.screenshot_path, options.log_dir);
        return 1;
    }
    printf("serenade-launcher: wrote %s\n", options.screenshot_path);

    if (options.expect_window_min >= 0.0) {
        // Functional check (no golden): assert a window with content appeared. Used
        // for targets whose pixels are non-deterministic, e.g. an interactive terminal
        // launched through LaunchServer -- we only care that *a* window is on screen.
        auto bmp_or_error = Serenade::load_png_bitmap({ options.screenshot_path, strlen(options.screenshot_path) });
        if (bmp_or_error.is_error()) {
            fprintf(stderr, "serenade-launcher: reading %s failed\n", options.screenshot_path);
            return 1;
        }
        double fraction = Serenade::non_background_fraction(*bmp_or_error.value(), 24);
        printf("serenade-launcher: expect-window: %.3f of pixels are non-background (min %.3f)\n", fraction, options.expect_window_min);
        if (fraction < options.expect_window_min) {
            fprintf(stderr, "serenade-launcher: no window appeared (non-background fraction %.3f < %.3f)\n", fraction, options.expect_window_min);
            return 1;
        }
    } else if (options.golden_path) {
        if (!compare_golden(options.screenshot_path, options.golden_path, options.tolerance)) {
            fprintf(stderr, "serenade-launcher: screenshot does not match golden %s\n", options.golden_path);
            return 1;
        }
        printf("serenade-launcher: screenshot matches golden %s\n", options.golden_path);
    }
    return 0;
}
