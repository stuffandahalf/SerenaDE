/*
 * Copyright (c) 2026, Gregory Norton.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// serenade-launcher: run a Serenity GUI application headlessly on a host
// system and capture a screenshot of the result.
//
// This is the M1 vertical slice: it brings up WindowServer (virtual screen
// backend) plus optional IPC services and one app over plain Unix domain
// sockets, waits for the app to render, asks WindowServer for a PNG
// (SIGUSR1 + WINDOW_SERVER_SCREENSHOT), and tears everything down. No X11
// involved yet.
//
// Usage:
//   serenade-launcher --res <Base/res> --screenshot <out.png>
//                     [--delay <ms>] [--width <n>] [--height <n>]
//                     [--log-dir <dir>]
//                     [--service <socket-path>=<binary>]...
//                     <window-server-binary> <app-binary> [app args...]
//
// Services are Serenity IPC servers (e.g. Clipboard) that adopt a pre-bound
// socket via the SOCKET_TAKEOVER mechanism, exactly like under SystemServer.
// Only POSIX APIs are used; this must stay portable across Linux and BSDs.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
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
    int delay_ms = 2000;
    int width = 1024;
    int height = 768;
    Service services[max_service_count];
    int service_count = 0;
    pid_t window_server_pid = -1;
    const char* window_server = nullptr;
    const char* app = nullptr;
    char const* const* app_args = nullptr; // argv-style, null-terminated
};

[[noreturn]] void usage(const char* program)
{
    fprintf(stderr,
        "Usage: %s --res <Base/res> --screenshot <out.png> [--delay <ms>] [--width <n>] [--height <n>] [--log-dir <dir>]\n"
        "              [--service <socket-path>=<binary>]...\n"
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
        else if (!strcmp(arg, "--log-dir"))
            options.log_dir = next();
        else if (!strcmp(arg, "--service")) {
            auto spec = next();
            char* separator = strchr(const_cast<char*>(spec), '=');
            if (!separator || options.service_count >= max_service_count)
                usage(argv[0]);
            *separator = '\0';
            options.services[options.service_count].socket_path = spec;
            options.services[options.service_count].binary = separator + 1;
            ++options.service_count;
        } else if (arg[0] == '-' && arg[1] != '\0')
            usage(argv[0]);
        else
            break;
    }

    if (!options.res_root || !options.screenshot_path)
        usage(argv[0]);
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
// takeover_env is "path:fd[;path:fd]"; the child dups each listener onto its
// fd number before exec.
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
        struct timespec nap { 0, 50 * 1000 * 1000 };
        nanosleep(&nap, nullptr);
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
        struct timespec nap { 0, 50 * 1000 * 1000 };
        nanosleep(&nap, nullptr);
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

void write_window_server_config(const char* log_dir, int width, int height)
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
        "Mode=Virtual\n"
        "Left=0\n"
        "Top=0\n"
        "Width=%d\n"
        "Height=%d\n"
        "ScaleFactor=1\n",
        width, height);
    fclose(file);
    setenv("WINDOW_SERVER_CONFIG", path, 1);
}

void cleanup(Options& options, pid_t window_server_pid)
{
    for (int i = 0; i < options.service_count; ++i) {
        if (options.services[i].child_pid > 0)
            kill(options.services[i].child_pid, SIGTERM);
        unlink(options.services[i].socket_path);
    }
    if (window_server_pid > 0)
        kill(window_server_pid, SIGTERM);
    for (int i = 0; i < options.service_count; ++i) {
        if (options.services[i].child_pid > 0)
            reap(options.services[i].child_pid);
    }
    if (window_server_pid > 0)
        reap(window_server_pid);
    unlink("/tmp/portal/window");
    unlink("/tmp/portal/wm");
}

pid_t s_window_server_pid = -1;
pid_t s_app_pid = -1;
Options* s_options = nullptr;

void on_signal(int)
{
    // Async-signal-safe best effort: kill the children so nothing is left
    // running, then exit. (kill/_exit are async-signal-safe.)
    if (s_app_pid > 0)
        kill(s_app_pid, SIGKILL);
    if (s_window_server_pid > 0)
        kill(s_window_server_pid, SIGKILL);
    if (s_options) {
        for (int i = 0; i < s_options->service_count; ++i)
            if (s_options->services[i].child_pid > 0)
                kill(s_options->services[i].child_pid, SIGKILL);
    }
    _exit(130);
}

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

    setenv("SERENITY_RES", options.res_root, 1);
    setenv("WINDOW_SERVER_SCREENSHOT", options.screenshot_path, 1);
    write_window_server_config(options.log_dir, options.width, options.height);

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

    for (int i = 0; i < options.service_count; ++i) {
        auto& service = options.services[i];
        char log_name[256];
        snprintf(log_name, sizeof(log_name), "serenade-service-%d.log", i);
        int service_log = open_log_file(options.log_dir, log_name);
        char const* const service_argv[] { service.binary, nullptr };
        int listener_fds[] { service.listener_fd };
        int takeover_fds[] { 3 };
        char const* paths[] { service.socket_path };
        service.child_pid = spawn_with_takeover(service.binary, service_argv,
            service_log, listener_fds, takeover_fds, paths, 1);
        if (!wait_for_socket(service.socket_path, 10000)) {
            fprintf(stderr, "serenade-launcher: service %s did not become ready (see %s/%s)\n",
                service.socket_path, options.log_dir, log_name);
            cleanup(options, options.window_server_pid);
            return 1;
        }
    }

    int app_log = open_log_file(options.log_dir, "serenade-app.log");
    pid_t app_pid = spawn_plain(options.app, options.app_args, app_log);
    s_app_pid = app_pid;

    {
        struct timespec nap { options.delay_ms / 1000, (options.delay_ms % 1000) * 1000 * 1000 };
        nanosleep(&nap, nullptr);
    }

    kill(options.window_server_pid, SIGUSR1);
    bool got_screenshot = wait_for_file(options.screenshot_path, 10000);

    if (app_pid > 0) {
        kill(app_pid, SIGTERM);
        reap(app_pid);
    }
    cleanup(options, options.window_server_pid);

    if (!got_screenshot) {
        fprintf(stderr, "serenade-launcher: no screenshot produced at %s (see logs in %s)\n", options.screenshot_path, options.log_dir);
        return 1;
    }
    printf("serenade-launcher: wrote %s\n", options.screenshot_path);
    return 0;
}
