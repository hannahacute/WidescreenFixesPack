#include "stdafx.h"

void Init()
{
    CIniReader iniReader("");
    bool bWriteSettingsToFile = iniReader.ReadInteger("MAIN", "WriteSettingsToFile", 1) != 0;

    if (bWriteSettingsToFile)
    {
        auto szSettingsSavePath = GetExeModulePath<std::string>() + "savegame\\settings.bin";
        RegistryWrapper("Illusion Softworks", szSettingsSavePath);
        RegistryWrapper::OverrideTypeREG_NONE = REG_BINARY;

        auto hExecutableInstance = (size_t)GetModuleHandle(NULL);
        IMAGE_NT_HEADERS*           ntHeader = (IMAGE_NT_HEADERS*)(hExecutableInstance + ((IMAGE_DOS_HEADER*)hExecutableInstance)->e_lfanew);
        IMAGE_IMPORT_DESCRIPTOR*    pImports = (IMAGE_IMPORT_DESCRIPTOR*)(hExecutableInstance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        size_t                      nNumImports = ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size / sizeof(IMAGE_IMPORT_DESCRIPTOR) - 1;

        auto PatchIAT = [&](size_t start, size_t end, size_t exe_end)
        {
            for (size_t i = 0; i < nNumImports; i++)
            {
                if (hExecutableInstance + (pImports + i)->FirstThunk > start && !(end && hExecutableInstance + (pImports + i)->FirstThunk > end))
                    end = hExecutableInstance + (pImports + i)->FirstThunk;
            }

            if (!end) { end = start + 0x100; }
            if (end > exe_end) //for very broken exes
            {
                start = hExecutableInstance;
                end = exe_end;
            }

            for (auto i = start; i < end; i += sizeof(size_t))
            {
                DWORD dwProtect[2];
                VirtualProtect((size_t*)i, sizeof(size_t), PAGE_EXECUTE_READWRITE, &dwProtect[0]);

                auto ptr = *(size_t*)i;
                if (!ptr)
                    continue;

                if (ptr == (size_t)::RegCloseKey)
                {
                    *(size_t*)i = (size_t)RegistryWrapper::RegCloseKey;
                }
                else if (ptr == (size_t)::RegQueryValueExA)
                {
                    *(size_t*)i = (size_t)RegistryWrapper::RegQueryValueExA;
                }
                else if (ptr == (size_t)::RegCreateKeyExA)
                {
                    *(size_t*)i = (size_t)RegistryWrapper::RegCreateKeyExA;
                }
                else if (ptr == (size_t)::RegSetValueExA)
                {
                    *(size_t*)i = (size_t)RegistryWrapper::RegSetValueExA;
                }
                else if (ptr == (size_t)::RegOpenKeyA)
                {
                    *(size_t*)i = (size_t)RegistryWrapper::RegOpenKeyA;
                }

                VirtualProtect((size_t*)i, sizeof(size_t), dwProtect[0], &dwProtect[1]);
            }
        };

        static auto getSection = [](const PIMAGE_NT_HEADERS nt_headers, unsigned section) -> PIMAGE_SECTION_HEADER
        {
            return reinterpret_cast<PIMAGE_SECTION_HEADER>(
                (UCHAR*)nt_headers->OptionalHeader.DataDirectory +
                nt_headers->OptionalHeader.NumberOfRvaAndSizes * sizeof(IMAGE_DATA_DIRECTORY) +
                section * sizeof(IMAGE_SECTION_HEADER));
        };

        static auto getSectionEnd = [](IMAGE_NT_HEADERS* ntHeader, size_t inst) -> auto
        {
            auto sec = getSection(ntHeader, ntHeader->FileHeader.NumberOfSections - 1);
            auto secSize = max(sec->SizeOfRawData, sec->Misc.VirtualSize);
            auto end = inst + max(sec->PointerToRawData, sec->VirtualAddress) + secSize;
            return end;
        };

        auto hExecutableInstance_end = getSectionEnd(ntHeader, hExecutableInstance);

        for (size_t i = 0; i < nNumImports; i++)
        {
            if ((size_t)(hExecutableInstance + (pImports + i)->Name) < hExecutableInstance_end)
            {
                if (!_stricmp((const char*)(hExecutableInstance + (pImports + i)->Name), "ADVAPI32.dll"))
                    PatchIAT(hExecutableInstance + (pImports + i)->FirstThunk, 0, hExecutableInstance_end);
            }
        }
    }
}

int WINAPI GetSystemMetricsHook(int nIndex)
{
    return 0;
}

HWND WINAPI CreateWindowExAHook(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    auto hwnd = ::CreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle & ~WS_OVERLAPPEDWINDOW, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);

    if (dwStyle != 0x50000000)
    {
        auto[DesktopResW, DesktopResH] = GetDesktopRes();
        tagRECT rect;
        rect.left = (LONG)(((float)DesktopResW / 2.0f) - (nWidth / 2.0f));
        rect.top = (LONG)(((float)DesktopResH / 2.0f) - (nHeight / 2.0f));
        rect.right = nWidth;
        rect.bottom = nHeight;
        SetWindowPos(hwnd, NULL, rect.left, rect.top, rect.right, rect.bottom, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    return hwnd;
}

void InitLS3DF()
{
    CIniReader iniReader("");

    if (iniReader.ReadInteger("MAIN", "BorderlessWindowed", 1) != 0)
    {
        auto pattern = hook::module_pattern(GetModuleHandle(L"LS3DF"), "FF 15 ? ? ? ? 85 C0 0F 95 C0 A2 ? ? ? ? 8B 0E 56 FF 51 40");
        injector::WriteMemory(*pattern.get_first<uint32_t*>(2), GetSystemMetricsHook, true);
        pattern = hook::module_pattern(GetModuleHandle(L"LS3DF"), "FF 15 ? ? ? ? 3B C5 A3 ? ? ? ? 0F 84 ? ? ? ? 50");
        injector::WriteMemory(*pattern.get_first<uint32_t*>(2), CreateWindowExAHook, true);
    }
}

CEXP void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
        {
            CallbackHandler::RegisterCallback(Init);
            CallbackHandler::RegisterCallback(L"LS3DF.dll", InitLS3DF);
        });
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {

    }
    return TRUE;
}