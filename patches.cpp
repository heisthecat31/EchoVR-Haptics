#include "patches.h"
#include "detours.h" // Local header
#include <windows.h>
#include <thread>
#include <chrono>
#include <fstream>

// Auto-link the library
#pragma comment(lib, "detours.lib")

// Haptics Config
float g_HapticStrength = 1.4f;

void LoadConfig() {
    std::ifstream file("haptics_config.txt");
    if (file.is_open()) {
        float val;
        if (file >> val) {
            if (val > 5.0f) val = 5.0f;
            if (val < 0.0f) val = 0.0f;
            g_HapticStrength = val;
        }
    }
}

// OVR Definitions
typedef int ovrResult;
typedef void* ovrSession;
typedef int ovrControllerType;

struct ovrHapticsBuffer {
    const void* Samples;
    int SamplesCount;
    int SubmitMode;
};

typedef ovrResult(__cdecl* pf_SetControllerVibration)(ovrSession, ovrControllerType, float, float);
typedef ovrResult(__cdecl* pf_SubmitControllerVibration)(ovrSession, ovrControllerType, const ovrHapticsBuffer*);

pf_SetControllerVibration Real_SetControllerVibration = nullptr;
pf_SubmitControllerVibration Real_SubmitControllerVibration = nullptr;

// Haptics Hook Logic
ovrResult __cdecl Hooked_SubmitControllerVibration(ovrSession session, ovrControllerType type, const ovrHapticsBuffer* buffer) {
    if (!Real_SetControllerVibration) {
        HMODULE h = GetModuleHandleA("LibOVRRT64_1.dll");
        if (h) Real_SetControllerVibration = (pf_SetControllerVibration)GetProcAddress(h, "ovr_SetControllerVibration");
    }

    if (!Real_SetControllerVibration) return 0;

    bool vibrate = false;
    float strength = 0.0f;

    if (buffer && buffer->SamplesCount > 0) {
        const unsigned char* samples = (const unsigned char*)buffer->Samples;
        long total = 0;
        for (int i = 0; i < buffer->SamplesCount; i++) total += samples[i];
        float avg = (float)total / buffer->SamplesCount;
        float amp = avg / 255.0f;

        if (amp > 0.01f) {
            vibrate = true;
            strength = amp * g_HapticStrength;
            if (strength > 1.0f) strength = 1.0f;
        }
    }

    if (vibrate)
        Real_SetControllerVibration(session, type, 1.0f, strength);
    else
        Real_SetControllerVibration(session, type, 0.0f, 0.0f);

    return 0;
}

// Installer Thread
void InstallHook() {
    LoadConfig();

    HMODULE hLibOVR = nullptr;
    while (!hLibOVR) {
        hLibOVR = GetModuleHandleA("LibOVRRT64_1.dll");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    Real_SubmitControllerVibration = (pf_SubmitControllerVibration)GetProcAddress(hLibOVR, "ovr_SubmitControllerVibration");

    if (Real_SubmitControllerVibration) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)Real_SubmitControllerVibration, Hooked_SubmitControllerVibration);
        DetourTransactionCommit();
    }
}

void Initialize() {
    std::thread(InstallHook).detach();
}