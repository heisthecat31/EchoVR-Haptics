#include "patches.h"
#include <windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        Initialize();
    }
    return TRUE;
}

// Dummy export to fool the game
extern "C" __declspec(dllexport) void DetoursExportPlaceholder() { }