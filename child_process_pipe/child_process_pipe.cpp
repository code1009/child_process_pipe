/////////////////////////////////////////////////////////////////////////////
//===========================================================================
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>





/////////////////////////////////////////////////////////////////////////////
//===========================================================================
static std::wstring get_module_path(HMODULE hmodule = nullptr)
{
	WCHAR path[MAX_PATH] = { 0 };
	DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
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





/////////////////////////////////////////////////////////////////////////////
//===========================================================================
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





/////////////////////////////////////////////////////////////////////////////
//===========================================================================
class process_pipe
{
public:
	HANDLE _hread{ nullptr };
	HANDLE _hwrite{ nullptr };
	SECURITY_ATTRIBUTES _sa = { 0 };

public:
	process_pipe();
	~process_pipe();

private:
	void create(void);
	void destroy(void);
};

/////////////////////////////////////////////////////////////////////////////
//===========================================================================
process_pipe::process_pipe()
{
	create();
}

process_pipe::~process_pipe()
{
	destroy();
}

void process_pipe::create(void)
{
	memset(&_sa, 0, sizeof(_sa));
	_sa.nLength = sizeof(_sa);
	_sa.bInheritHandle = TRUE;
	_sa.lpSecurityDescriptor = nullptr;

	BOOL rv;
	rv = CreatePipe(&_hread, &_hwrite, &_sa, 0);
	if (FALSE == rv)
	{
		destroy();
		throw std::runtime_error("Failed to CreatePipe()");
	}
}

void process_pipe::destroy(void)
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





/////////////////////////////////////////////////////////////////////////////
//===========================================================================
class process_command
{
private:
	std::wstring _process;
	std::wstring _current_directory;
	std::wstring _command_line;
	std::thread _thread;

	process_pipe _rpipe;
	process_pipe _wpipe;

	STARTUPINFOW _si = { 0 };
	PROCESS_INFORMATION _pi = { 0 };

public:
	explicit process_command(std::wstring const& process);
	explicit process_command(std::wstring const& process, std::wstring const& current_directory);
	~process_command();
	
private:
	std::wstring make_command_line(std::wstring const& command);

private:
	void create(void);
	void destroy(void);

	void thread_entry(void);

private:
	void read_output(void);

public:
	void write_input(const std::wstring& s);

	bool wait(std::uint32_t timeout = INFINITE);
	bool kill(std::uint32_t exit_code=0xffffffff);

public:
	std::uint32_t get_exit_code(void);
};

/////////////////////////////////////////////////////////////////////////////
//===========================================================================
process_command::process_command(std::wstring const& process):
	_process(process),
	_current_directory{}
{
	_command_line = make_command_line(_process);

	create();
}

process_command::process_command(std::wstring const& process, std::wstring const& current_directory) :
	_process(process),
	_current_directory(current_directory)
{
	_command_line = make_command_line(_process);

	create();
}

process_command::~process_command()
{
	destroy();
}

//===========================================================================
std::wstring process_command::make_command_line(std::wstring const& command)
{
#if 0
	return command;
#else
	std::wstring cmd_file_path;
	WCHAR szCmdFilePath[MAX_PATH] = { 0 };
	DWORD dwReturn;
	dwReturn = GetEnvironmentVariableW(L"ComSpec", szCmdFilePath, _countof(szCmdFilePath));
	if (0==dwReturn)
	{
		cmd_file_path = L"cmd.exe";
	}
	else
	{
		cmd_file_path = szCmdFilePath;
	}


	std::wstring command_line;
	command_line = cmd_file_path;
	command_line += L" /D ";
	//command_line += L" /U ";
	command_line += L" /C \"";
	command_line += command;
	command_line += L"\"";
	return command_line;
#endif
}

//===========================================================================
void process_command::create(void)
{
	std::wcout << L"Launch: " << _command_line << std::endl;

	
	LPCWSTR lpCurrentDirectory = nullptr;
	if (!_current_directory.empty())
	{
		lpCurrentDirectory = _current_directory.c_str();
	}
	else
	{
		lpCurrentDirectory = nullptr;
	}


	LPVOID lpEnvironment = nullptr;



	memset(&_pi, 0, sizeof(_pi));
	memset(&_si, 0, sizeof(_si));
	_si.cb = sizeof(_si);
	_si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	_si.hStdOutput = _rpipe._hwrite;
	_si.hStdError = _rpipe._hwrite;
	_si.hStdInput = _wpipe._hread;
	_si.wShowWindow = SW_HIDE;


	BOOL rv;
	rv = CreateProcessW(
		nullptr, 
		const_cast<LPWSTR>(_command_line.c_str()), 
		nullptr, 
		nullptr, 
		TRUE, 
		0, 
		lpEnvironment,
		lpCurrentDirectory,
		&_si, 
		&_pi
	);
	if (FALSE == rv)
	{
		destroy();
		throw std::runtime_error("Failed to CreateProcessW()");
	}


	_thread = std::thread(&process_command::thread_entry, this);
}

void process_command::destroy(void)
{
	if (_pi.hProcess)
	{
		DWORD object;
		object = WaitForSingleObject(_pi.hProcess, 1000);
		switch (object)
		{
		case WAIT_OBJECT_0:
		case WAIT_ABANDONED:
			break;

		case WAIT_TIMEOUT:
			if (FALSE == TerminateProcess(_pi.hProcess, 0xffffffff))
			{
				std::wcerr << L"Failed to TerminateProcess()" << std::endl;
			}
			break;

		case WAIT_FAILED:
		default:
			break;
		}
	}


	if (_thread.joinable())
	{
		_thread.join();
	}


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

void process_command::thread_entry(void)
{
	bool loop = true;
	do
	{
		DWORD object;
		object = WaitForSingleObject(_pi.hProcess, 100);
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
					//std::wstring s = mbcs_to_wcs(std::string(buffer.data(), NumberOfBytesRead), CP_THREAD_ACP);
					std::wstring s = mbcs_to_wcs(std::string(buffer.data(), NumberOfBytesRead), CP_UTF8);
					std::wcout << "Output: " << std::endl << s << std::endl;
				}
				else
				{
					std::wcerr << L"Failed to ReadFile() - NumberOfBytesRead is 0." << std::endl;
				}
			}
		}
	}
}

void process_command::write_input(const std::wstring& input)
{
	std::wcout << L"Input: " << input << std::endl;


	std::wstring input_command;
	std::string command;
	input_command = input + L"\n";
	//command = wcs_to_mbcs(input_command, CP_THREAD_ACP);
	command = wcs_to_mbcs(input_command, CP_UTF8);
	if (_wpipe._hwrite != nullptr)
	{
		DWORD NumberOfBytesWritten;
		WriteFile(_wpipe._hwrite, command.c_str(), static_cast<DWORD>(command.size()), &NumberOfBytesWritten, nullptr);
	}
}

bool process_command::wait(std::uint32_t timeout)
{
	if (nullptr == _pi.hProcess)
	{
		throw std::runtime_error("process handle is null!");
	}

	
	bool result = false;


	DWORD object;
	object = WaitForSingleObject(_pi.hProcess, timeout);
	switch (object)
	{
	case WAIT_OBJECT_0:
		result = true;
		break;

	case WAIT_ABANDONED:
		result = true;
		break;

	case WAIT_TIMEOUT:
		result = false;
		break;

	case WAIT_FAILED:
		throw std::runtime_error("Failed to WaitForSingleObject()");
		break;

	default:
		throw std::runtime_error("Failed to WaitForSingleObject()");
		break;
	}

	return result;
}

bool process_command::kill(std::uint32_t exit_code)
{
	if (nullptr == _pi.hProcess)
	{
		throw std::runtime_error("process handle is null!");
	}


	bool result = false;


	DWORD object;
	object = WaitForSingleObject(_pi.hProcess, 0);
	switch (object)
	{
	case WAIT_OBJECT_0:
		result = true;
		break;

	case WAIT_ABANDONED:
		result = true;
		break;

	case WAIT_TIMEOUT:
		if (FALSE==TerminateProcess(_pi.hProcess, exit_code))
		{
			result = false;
		}
		else
		{
			result = true;
		}
		break;

	case WAIT_FAILED:
		throw std::runtime_error("Failed to WaitForSingleObject()");
		break;

	default:
		throw std::runtime_error("Failed to WaitForSingleObject()");
		break;
	}

	return result;
}

std::uint32_t process_command::get_exit_code(void)
{
	if (nullptr == _pi.hProcess)
	{
		throw std::runtime_error("process handle is null!");
	}


	DWORD exit_code = 0;
	if (GetExitCodeProcess(_pi.hProcess, &exit_code) == FALSE)
	{
		throw std::runtime_error("Failed to GetExitCodeProcess()");
	}

	return exit_code;
}




/////////////////////////////////////////////////////////////////////////////
//===========================================================================
int main()
{
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
	//std::locale::global(std::locale("ko_KR.UTF8"));
	std::wcout.imbue(std::locale("ko_KR.UTF-8"));
	std::wcin.imbue(std::locale("ko_KR.UTF-8"));


	std::wcout << L"부모프로세스시작" << std::endl;

	std::wstring file_path = get_module_directory() + L"\\child_process.exe \"aa a\" bb b";
	process_command cmd(file_path);

	cmd.write_input(L" 가-부모프로세스에서보냄");
	if (cmd.wait())
	{
		Sleep(1000);
		std::wcout << L"exit code: " << cmd.get_exit_code() << std::endl;
	}

	process_command cmd_dir(L"dir");
	cmd_dir.wait();

	return 0;
}

/*
부모프로세스시작
Launch: C:\WINDOWS\system32\cmd.exe /D  /C "D:\prj_my\child_process_pipe\child_process_pipe\x64\Debug\child_process.exe "aa a" bb b"
Input:  가-부모프로세스에서보냄
Output:
[child_process.exe] 시작
[child_process.exe] Command line parameters:
[child_process.exe] argv[0]: D:\prj_my\child_process_pipe\child_process_pipe\x64\Debug\child_process.exe
[child_process.exe] argv[1]: aa a
[child_process.exe] argv[2]: bb
[child_process.exe] argv[3]: b
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] 안녕, 세상!

Output:
[child_process.exe] Input:가-부모프로세스에서보냄
[child_process.exe] [0] a,c00
[child_process.exe] [1] 2d
[child_process.exe] [2] b,d80
[child_process.exe] [3] b,aa8
[child_process.exe] [4] d,504
[child_process.exe] [5] b,85c
[child_process.exe] [6] c,138
[child_process.exe] [7] c,2a4
[child_process.exe] [8] c,5d0
[child_process.exe] [9] c,11c
[child_process.exe] [a] b,cf4
[child_process.exe] [b] b,0c4
[child_process.exe] End

WAIT_OBJECT_0
Process has exited.
exit code: 2
Launch: C:\WINDOWS\system32\cmd.exe /D  /C "dir"
Output:
 Volume in drive D is DATA
 Volume Serial Number is A827-C9DE

 Directory of D:\prj_my\child_process_pipe\child_process_pipe\child_process_pipe

2025-07-01  오전 10:47    <DIR>          .
2025-07-01  오전 10:26    <DIR>          ..
2025-07-01  오전 10:47            13,226 child_process_pipe.cpp
2025-06-30  오후 09:13             6,480 child_process_pipe.vcxproj
2025-06-30  오후 05:05               996 child_process_pipe.vcxproj.filters
2025-06-30  오후 05:05               168 child_process_pipe.vcxproj.user
2025-06-30  오후 05:12    <DIR>          x64
			   4 File(s)         20,870 bytes
			   3 Dir(s)  411,204,358,144 bytes free

WAIT_OBJECT_0
Process has exited.

*/
