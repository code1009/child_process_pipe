#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <coroutine>
#include <memory>

/////////////////////////////////////////////////////////////////////////////
// 간단한 generator 코루틴 구현
struct process_coro {
    struct promise_type {
        process_coro get_return_object() { return process_coro{ std::coroutine_handle<promise_type>::from_promise(*this) }; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
    std::coroutine_handle<promise_type> handle;
    explicit process_coro(std::coroutine_handle<promise_type> h) : handle(h) {}
    process_coro(const process_coro&) = delete;
    process_coro& operator=(const process_coro&) = delete;
    process_coro(process_coro&& other) noexcept : handle(other.handle) { other.handle = nullptr; }
    process_coro& operator=(process_coro&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }
    ~process_coro() { if (handle) handle.destroy(); }
    bool resume() { if (!handle.done()) { handle.resume(); return !handle.done(); } return false; }
    bool done() const { return handle.done(); }
};

/////////////////////////////////////////////////////////////////////////////
// 기존 함수 및 클래스 정의(생략, 기존 코드와 동일)

static std::wstring get_module_path(HMODULE hmodule = nullptr)
{
    WCHAR path[MAX_PATH] = { 0 };
    DWORD length = GetModuleFileNameW(hmodule, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH)
        return L"";
    return std::wstring(path, length);
}

static std::wstring get_module_directory(HMODULE hmodule = nullptr)
{
    std::wstring path = get_module_path(hmodule);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        return path.substr(0, pos);
    return L"";
}

std::wstring mbcs_to_wcs(std::string input, UINT codepage)
{
    int length = MultiByteToWideChar(codepage, 0, input.c_str(), -1, nullptr, 0);
    if (length > 0)
    {
        std::vector<wchar_t> buf(length);
        MultiByteToWideChar(codepage, 0, input.c_str(), -1, &buf[0], length);
        return std::wstring(&buf[0]);
    }
    return std::wstring();
}

std::string wcs_to_mbcs(std::wstring input, UINT codepage)
{
    int length = WideCharToMultiByte(codepage, 0, input.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length > 0)
    {
        std::vector<char> buf(length);
        WideCharToMultiByte(codepage, 0, input.c_str(), -1, &buf[0], length, nullptr, nullptr);
        return std::string(&buf[0]);
    }
    return std::string();
}

class process_pipe
{
public:
    HANDLE _hread{ nullptr };
    HANDLE _hwrite{ nullptr };
    SECURITY_ATTRIBUTES _sa = { 0 };

    process_pipe() { create(); }
    ~process_pipe() { destroy(); }
private:
    void create(void)
    {
        memset(&_sa, 0, sizeof(_sa));
        _sa.nLength = sizeof(_sa);
        _sa.bInheritHandle = TRUE;
        _sa.lpSecurityDescriptor = nullptr;

        BOOL rv = CreatePipe(&_hread, &_hwrite, &_sa, 0);
        if (FALSE == rv)
        {
            destroy();
            throw std::runtime_error("Failed to CreatePipe()");
        }
    }
    void destroy(void)
    {
        if (_hread != nullptr)
        {
            CloseHandle(_hread);
            _hread = nullptr;
        }
        if (_hwrite != nullptr)
        {
            CloseHandle(_hwrite);
            _hwrite = nullptr;
        }
    }
};

/////////////////////////////////////////////////////////////////////////////
// 코루틴 기반 process_command
class process_command
{
public:
    std::wstring _command;
    std::wstring _command_line;

    process_pipe _rpipe;
    process_pipe _wpipe;

    STARTUPINFOW _si = { 0 };
    PROCESS_INFORMATION _pi = { 0 };

    std::unique_ptr<process_coro> _coro;

    explicit process_command(std::wstring const& command);
    ~process_command();

private:
    std::wstring make_command_line(std::wstring const& command);
    void create(void);
    void destroy(void);

    process_coro coroutine_entry();

    void read_output(void);

public:
    void write_input(const std::wstring& s);

    bool wait(DWORD timeout = INFINITE);
    bool kill(void);
};

process_command::process_command(std::wstring const& command)
    : _command(command)
{
    _command_line = make_command_line(command);
    create();
}

process_command::~process_command()
{
    destroy();
}

std::wstring process_command::make_command_line(std::wstring const& file_path)
{
    return file_path;
}

void process_command::create(void)
{
    std::wcout << L"Launch: " << _command_line << std::endl;

    memset(&_pi, 0, sizeof(_pi));
    memset(&_si, 0, sizeof(_si));
    _si.cb = sizeof(_si);
    _si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    _si.hStdOutput = _rpipe._hwrite;
    _si.hStdError = _rpipe._hwrite;
    _si.hStdInput = _wpipe._hread;
    _si.wShowWindow = SW_HIDE;

    BOOL rv = CreateProcessW(nullptr, const_cast<LPWSTR>(_command_line.c_str()), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &_si, &_pi);
    if (FALSE == rv)
    {
        destroy();
        throw std::runtime_error("Failed to CreateProcessW()");
    }

    // 코루틴 생성
    _coro = std::make_unique<process_coro>(coroutine_entry());
}

void process_command::destroy(void)
{
    if (_pi.hProcess)
    {
        DWORD object = WaitForSingleObject(_pi.hProcess, 1000);
        if (object == WAIT_TIMEOUT)
        {
            if (FALSE == TerminateProcess(_pi.hProcess, 0))
            {
                std::wcerr << L"Failed to TerminateProcess()" << std::endl;
            }
        }
    }

    _coro.reset();

    if (_pi.hThread != nullptr)
    {
        CloseHandle(_pi.hThread);
        _pi.hThread = nullptr;
    }
    if (_pi.hProcess != nullptr)
    {
        CloseHandle(_pi.hProcess);
        _pi.hProcess = nullptr;
    }
}

// 코루틴 진입점
process_coro process_command::coroutine_entry()
{
    bool loop = true;
    do
    {
        DWORD object = WaitForSingleObject(_pi.hProcess, 1000);
        switch (object)
        {
        case WAIT_OBJECT_0:
            read_output();
            std::wcout << L"WAIT_OBJECT_0" << std::endl;
            loop = false;
            break;
        case WAIT_ABANDONED:
            read_output();
            std::wcout << L"WAIT_ABANDONED" << std::endl;
            loop = false;
            break;
        case WAIT_TIMEOUT:
            read_output();
            co_await std::suspend_always{};
            break;
        case WAIT_FAILED:
            std::wcerr << L"WAIT_FAILED" << std::endl;
            loop = false;
            break;
        default:
            std::wcout << L"?" << object << std::endl;
            loop = false;
            break;
        }
    } while (loop);

    std::wcout << L"Process has exited." << std::endl;
    co_return;
}

void process_command::read_output(void)
{
    DWORD TotalBytesAvail;
    if (PeekNamedPipe(_rpipe._hread, nullptr, 0, nullptr, &TotalBytesAvail, nullptr))
    {
        if (TotalBytesAvail > 0)
        {
            std::vector<char> buffer(TotalBytesAvail);
            DWORD NumberOfBytesRead;
            if (ReadFile(_rpipe._hread, buffer.data(), static_cast<DWORD>(buffer.size()), &NumberOfBytesRead, nullptr))
            {
                if (NumberOfBytesRead > 0)
                {
                    std::wstring s = mbcs_to_wcs(std::string(buffer.data(), NumberOfBytesRead), CP_THREAD_ACP);
                    std::wcout << L"Output: " << std::endl << s << std::endl;
                }
            }
        }
    }
}

void process_command::write_input(const std::wstring& input)
{
    std::wcout << L"Input: " << input << std::endl;
    std::wstring input_command = input + L"\n";
    std::string command = wcs_to_mbcs(input_command, CP_THREAD_ACP);

    if (_wpipe._hwrite != nullptr)
    {
        DWORD NumberOfBytesWritten;
        WriteFile(_wpipe._hwrite, command.c_str(), static_cast<DWORD>(command.size()), &NumberOfBytesWritten, nullptr);
    }
}

bool process_command::wait(DWORD timeout)
{
    if (nullptr == _pi.hProcess)
    {
        throw std::runtime_error("process handle is null!");
    }

    // 코루틴을 모두 실행
    while (_coro && !_coro->done()) {
        _coro->resume();
    }

    DWORD object = WaitForSingleObject(_pi.hProcess, timeout);
    switch (object)
    {
    case WAIT_OBJECT_0:
    case WAIT_ABANDONED:
        return true;
    case WAIT_TIMEOUT:
        return false;
    case WAIT_FAILED:
    default:
        throw std::runtime_error("Failed to WaitForSingleObject()");
    }
}

bool process_command::kill(void)
{
    if (nullptr == _pi.hProcess)
    {
        throw std::runtime_error("process handle is null!");
    }

    DWORD object = WaitForSingleObject(_pi.hProcess, 0);
    switch (object)
    {
    case WAIT_OBJECT_0:
    case WAIT_ABANDONED:
        return true;
    case WAIT_TIMEOUT:
        if (FALSE == TerminateProcess(_pi.hProcess, 0))
        {
            return false;
        }
        else
        {
            return true;
        }
    case WAIT_FAILED:
    default:
        throw std::runtime_error("Failed to WaitForSingleObject()");
    }
}

/////////////////////////////////////////////////////////////////////////////
//===========================================================================
int main()
{
    std::wstring file_path = get_module_directory() + L"\\child_process.exe \"aa a\" bb b";
    process_command cmd(file_path);

    cmd.write_input(L"this_is_message_from_parent_process");
    cmd.wait();

    return 0;
}