#include <iostream>
#include <Windows.h>


int main()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    //std::locale::global(std::locale("ko_KR.UTF8"));
    std::wcout.imbue(std::locale("ko_KR.UTF-8"));
    std::wcin.imbue(std::locale("ko_KR.UTF-8"));


    std::wcout << L"[child_process.exe] 시작" << std::endl;


    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv)
    {
        std::wcout << L"[child_process.exe] Command line parameters:" << std::endl;
        for (int i = 0; i < argc; ++i)
        {
            std::wcout << L"[child_process.exe] argv[" << i << L"]: " << argv[i] << std::endl;
        }
        LocalFree(argv);
    }
    else
    {
        std::wcerr << L"[child_process.exe] CommandLineToArgvW failed." << std::endl;
    }


    std::size_t i;
    std::size_t count;

    count = 10;
    for (i = 0; i < count; i++)
    {
        std::wcout << L"[child_process.exe] 안녕, 세상!" << std::endl;
        Sleep(100);
    }


    std::wstring s;
    std::wcin >> s;
    std::wcout << L"[child_process.exe] Input:" << s << std::endl;
    for(std::size_t j = 0; j < s.size(); j++)
    {
		std::wcout << L"[child_process.exe] [" << j << L"] " << std::hex << static_cast<std::uint16_t>(s[j]) << std::endl;
	}

    std::wcout << L"[child_process.exe] End" << std::endl;

    return 2;
}
