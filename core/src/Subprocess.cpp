// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Subprocess.h"

#include "asma/core/Fs.h"

#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace asma {

namespace {

#ifdef _WIN32

std::wstring widen(const std::string& utf8)
{
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), size);
    return out;
}

// Quotes one argument so CommandLineToArgvW gives it back unchanged.
void appendQuoted(std::wstring& line, const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    std::size_t backslashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless they precede a quote.
        line.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        line += c;
    }
    line.append(backslashes * 2, L'\\'); // before the closing quote
    line += L'"';
}

std::string lastError(const char* what)
{
    return std::string(what) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

#else

char** currentEnvironment()
{
#ifdef __APPLE__
    return *_NSGetEnviron(); // `environ` is not available to a plugin bundle
#else
    return environ;
#endif
}

#endif

} // namespace

#ifdef _WIN32

Subprocess Subprocess::start(const std::filesystem::path& program, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) throw SubprocessError(lastError("CreatePipe"));
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, nullptr);
    if (nul == INVALID_HANDLE_VALUE) {
        const std::string error = lastError("opening NUL");
        CloseHandle(readEnd);
        CloseHandle(writeEnd);
        throw SubprocessError(error);
    }

    // Only these handles reach the child, so a pipe of another instance
    // started at the same moment cannot leak into it and hold it open.
    HANDLE inherited[2] = {writeEnd, nul};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrBuffer(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof inherited, nullptr,
                              nullptr);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof startup;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = nul;
    startup.StartupInfo.hStdOutput = writeEnd;
    startup.StartupInfo.hStdError = nul;
    startup.lpAttributeList = attrs;

    const std::wstring exe = program.wstring();
    std::wstring line;
    appendQuoted(line, exe);
    for (const auto& arg : args) {
        line += L' ';
        appendQuoted(line, widen(arg));
    }

    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                                   &startup.StartupInfo, &info);
    const std::string error = ok ? std::string() : lastError("CreateProcess");
    DeleteProcThreadAttributeList(attrs);
    CloseHandle(writeEnd);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(readEnd);
        throw SubprocessError(error + ": " + toUtf8(program));
    }
    CloseHandle(info.hThread);

    Subprocess p;
    p.process_ = info.hProcess;
    p.stdout_ = readEnd;
    return p;
}

bool Subprocess::fill()
{
    char chunk[4096];
    DWORD got = 0;
    if (!ReadFile(stdout_, chunk, sizeof chunk, &got, nullptr) || got == 0) return false;
    buffer_.append(chunk, got);
    return true;
}

ExitStatus Subprocess::wait()
{
    if (status_) return *status_;
    WaitForSingleObject(process_, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    ExitStatus status;
    status.signalled = code >= 0xC0000000u;
    status.code = static_cast<int>(code);
    status_ = status;
    return status;
}

void Subprocess::kill()
{
    std::lock_guard lock(*mutex_);
    // The handle keeps the process object alive, so this can never hit a
    // different process; on one that has exited it simply fails.
    if (process_) TerminateProcess(process_, 1);
}

void Subprocess::close() noexcept
{
    if (process_) {
        if (!status_) {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, INFINITE);
        }
        CloseHandle(process_);
    }
    if (stdout_) CloseHandle(stdout_);
    process_ = nullptr;
    stdout_ = nullptr;
}

Subprocess::Subprocess(Subprocess&& other) noexcept
    : process_(std::exchange(other.process_, nullptr)), stdout_(std::exchange(other.stdout_, nullptr)),
      buffer_(std::move(other.buffer_)), eof_(other.eof_), status_(other.status_), mutex_(std::move(other.mutex_))
{
    other.mutex_ = std::make_unique<std::mutex>();
}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept
{
    if (this != &other) {
        close();
        process_ = std::exchange(other.process_, nullptr);
        stdout_ = std::exchange(other.stdout_, nullptr);
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        status_ = other.status_;
        std::swap(mutex_, other.mutex_);
    }
    return *this;
}

#else

Subprocess Subprocess::start(const std::filesystem::path& program, const std::vector<std::string>& args)
{
    int fds[2];
#ifdef __APPLE__
    if (::pipe(fds) != 0) throw SubprocessError(std::string("pipe failed: ") + std::strerror(errno));
    // POSIX_SPAWN_CLOEXEC_DEFAULT below keeps these out of every other child.
#else
    // Close-on-exec from the start, so a child another thread spawns at this
    // moment cannot inherit the pipe and hold it open.
    if (::pipe2(fds, O_CLOEXEC) != 0) throw SubprocessError(std::string("pipe2 failed: ") + std::strerror(errno));
#endif

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef __APPLE__
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

    const std::string exe = program.string();
    std::vector<std::string> argvStrings;
    argvStrings.push_back(exe);
    argvStrings.insert(argvStrings.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : argvStrings) argv.push_back(a.data());
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), currentEnvironment());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        throw SubprocessError("cannot start " + exe + ": " + std::strerror(rc));
    }

    Subprocess p;
    p.pid_ = pid;
    p.stdout_ = fds[0];
    return p;
}

bool Subprocess::fill()
{
    char chunk[4096];
    for (;;) {
        const ssize_t got = ::read(stdout_, chunk, sizeof chunk);
        if (got > 0) {
            buffer_.append(chunk, static_cast<std::size_t>(got));
            return true;
        }
        if (got < 0 && errno == EINTR) continue;
        return false;
    }
}

ExitStatus Subprocess::wait()
{
    if (status_) return *status_;
    // Wait without reaping, so the pid stays ours (a zombie) until the reap
    // below, which happens under the lock kill() takes.
    siginfo_t info{};
    while (::waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOWAIT) != 0 && errno == EINTR) {
    }
    std::lock_guard lock(*mutex_);
    int raw = 0;
    while (::waitpid(pid_, &raw, 0) < 0 && errno == EINTR) {
    }
    ExitStatus status;
    if (WIFSIGNALED(raw)) {
        status.signalled = true;
        status.code = WTERMSIG(raw);
    } else {
        status.code = WEXITSTATUS(raw);
    }
    status_ = status;
    pid_ = -1;
    return status;
}

void Subprocess::kill()
{
    std::lock_guard lock(*mutex_);
    if (pid_ > 0) ::kill(pid_, SIGKILL);
}

void Subprocess::close() noexcept
{
    if (pid_ > 0) {
        ::kill(pid_, SIGKILL);
        int raw = 0;
        while (::waitpid(pid_, &raw, 0) < 0 && errno == EINTR) {
        }
    }
    if (stdout_ >= 0) ::close(stdout_);
    pid_ = -1;
    stdout_ = -1;
}

Subprocess::Subprocess(Subprocess&& other) noexcept
    : pid_(std::exchange(other.pid_, -1)), stdout_(std::exchange(other.stdout_, -1)), buffer_(std::move(other.buffer_)),
      eof_(other.eof_), status_(other.status_), mutex_(std::move(other.mutex_))
{
    other.mutex_ = std::make_unique<std::mutex>();
}

Subprocess& Subprocess::operator=(Subprocess&& other) noexcept
{
    if (this != &other) {
        close();
        pid_ = std::exchange(other.pid_, -1);
        stdout_ = std::exchange(other.stdout_, -1);
        buffer_ = std::move(other.buffer_);
        eof_ = other.eof_;
        status_ = other.status_;
        std::swap(mutex_, other.mutex_);
    }
    return *this;
}

#endif

Subprocess::~Subprocess() { close(); }

std::optional<std::string> Subprocess::readLine()
{
    for (;;) {
        if (const auto newline = buffer_.find('\n'); newline != std::string::npos) {
            std::string line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return line;
        }
        if (eof_ || !fill()) {
            eof_ = true;
            if (buffer_.empty()) return std::nullopt;
            std::string rest = std::move(buffer_);
            buffer_.clear();
            if (!rest.empty() && rest.back() == '\r') rest.pop_back();
            return rest;
        }
    }
}

} // namespace asma
