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

public:
	void close(void);

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

void process_pipe::close(void)
{
	destroy();
}



/////////////////////////////////////////////////////////////////////////////
//===========================================================================
class process_output
{
private:
	std::thread _thread;
	HANDLE _hfile{ nullptr };

	const DWORD BUFFER_SIZE = 4096;
	std::vector<char> _buffer;
	DWORD _NumberOfBytesRead;
	bool _loop;

public:
	process_output();
	~process_output();

private:
	void create(void);
	void destroy(void);

public:
	void start(HANDLE hfile);
	void stop();

private:
	void thread_entry(void);
};

/////////////////////////////////////////////////////////////////////////////
//===========================================================================
process_output::process_output()
{
	create();
}

process_output::~process_output()
{
	destroy();
}

void process_output::create(void)
{
	_buffer.assign(BUFFER_SIZE, 0);
}

void process_output::destroy(void)
{
}

void process_output::start(HANDLE hfile)
{
	if (_thread.joinable())
	{
		_thread.join();
	}

	_hfile = hfile;
	_thread = std::thread(&process_output::thread_entry, this);
}

void process_output::stop(void)
{
	if (_thread.joinable())
	{
		_thread.join();
	}
}

void process_output::thread_entry(void)
{
	_loop = true;


	BOOL result;
	DWORD error;
	while (_loop)
	{
		result = ReadFile(_hfile, _buffer.data(), BUFFER_SIZE, &_NumberOfBytesRead, nullptr);
		if (TRUE == result)
		{
			if (_NumberOfBytesRead > 0)
			{
				std::wstring s = mbcs_to_wcs(std::string(_buffer.data(), _NumberOfBytesRead), CP_THREAD_ACP);
				std::wcout << "Output: " << std::endl << s << std::endl;
			}
			continue;
		}


		error = GetLastError();
		switch (error)
		{
		case NO_ERROR:
			std::cerr << "NO_ERROR" << std::endl;
			_loop = false;
			break;

		case ERROR_BROKEN_PIPE:
			std::cerr << "ERROR_BROKEN_PIPE" << std::endl;
			_loop = false;
			break;

		case ERROR_INVALID_HANDLE:
			std::cerr << "ERROR_INVALID_HANDLE" << std::endl;
			_loop = false;
			break;

		default:
			std::cerr << "?:" << error << std::endl;
			_loop = false;
			break;
		}
	}
}




/////////////////////////////////////////////////////////////////////////////
//===========================================================================
class process_command
{
public:
	std::wstring _command;
	std::wstring _command_line;

	process_pipe _rpipe;
	process_pipe _wpipe;
	process_output _output;

	STARTUPINFOW _si = { 0 };
	PROCESS_INFORMATION _pi = { 0 };


public:
	explicit process_command(std::wstring const& command);
	~process_command();
	
private:
	std::wstring make_command_line(std::wstring const& command);

private:
	void create(void);
	void destroy(void);

public:
	void write_input(const std::wstring& s);

	bool wait(DWORD timeout = INFINITE);
	bool kill(void);
};

/////////////////////////////////////////////////////////////////////////////
//===========================================================================
process_command::process_command(std::wstring const& command):
	_command(command)
{
	_command_line = make_command_line(command);

	create();
}

process_command::~process_command()
{
	destroy();
}

//===========================================================================
std::wstring process_command::make_command_line(std::wstring const& file_path)
{
	return file_path;

#if 0
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
	command_line += L" /K \"";
	command_line += file_path;
	command_line += L"\"";
	return command_line;
#endif
}

//===========================================================================
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


	BOOL rv;
	rv = CreateProcessW(nullptr, const_cast<LPWSTR>(_command_line.c_str()), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &_si, &_pi);
	if (FALSE == rv)
	{
		destroy();
		throw std::runtime_error("Failed to CreateProcessW()");
	}


	_output.start(_rpipe._hread);
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
			if (FALSE == TerminateProcess(_pi.hProcess, 0))
			{
				std::wcerr << L"Failed to TerminateProcess()" << std::endl;
			}
			break;

		case WAIT_FAILED:
		default:
			break;
		}
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


	_rpipe.close();
	_output.stop();
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

bool process_command::kill(void)
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
		if (FALSE==TerminateProcess(_pi.hProcess, 0))
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
