#include <iostream>
#include <Windows.h>


int main()
{
    std::wcout << L"[child_process.exe] Start" << std::endl;;


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
        std::wcout << L"[child_process.exe] Hello World!" << std::endl;;
        Sleep(100);
    }


    std::wstring s;
    std::wcin >> s;
    std::wcout << L"[child_process.exe] Input:" << s << std::endl;;


    std::wcout << L"[child_process.exe] End" << std::endl;;

    return 2;
}
