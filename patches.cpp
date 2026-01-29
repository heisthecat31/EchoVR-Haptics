#include "patches.h"
#include "detours.h" 
#include <windows.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>

#pragma comment(lib, "detours.lib")

float g_HapticStrength = 1.4f;   
float g_FovMultiplier  = 1.0f; // 1.0 = Default, 1.2 = Wider

// Helper to clean strings
std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void LoadConfig() {
    std::ifstream file("haptics_config.txt");
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '/') continue;

        std::istringstream is_line(line);
        std::string key;
        if (std::getline(is_line, key, '=')) {
            std::string value;
            if (std::getline(is_line, value)) {
                key = Trim(key);
                value = Trim(value);

                if (key == "HapticStrength") g_HapticStrength = std::stof(value);
                else if (key == "FovMultiplier") g_FovMultiplier = std::stof(value);
            }
        }
    }
    // Safety Clamps
    if (g_HapticStrength > 5.0f) g_HapticStrength = 5.0f;
    if (g_FovMultiplier < 0.1f) g_FovMultiplier = 1.0f; 
}

// =============================================================
// OVR STRUCTS EXACTLY MATCHING OVR_CAPI.H
// =============================================================
typedef int ovrResult;
typedef void* ovrSession;
typedef int ovrControllerType;
typedef int ovrHmdType; 

// Basic Math
struct ovrVector2f { float x, y; };
struct ovrSizei { int w, h; };
struct ovrFovPort { float UpTan; float DownTan; float LeftTan; float RightTan; };

struct ovrHapticsBuffer {
    const void* Samples;
    int SamplesCount;
    int SubmitMode;
};

struct ovrHmdDesc {
    ovrHmdType Type;
    char _pad0[4]; //(4 bytes)
    
    char ProductName[64];
    char Manufacturer[64];
    short VendorId;
    short ProductId;
    char SerialNumber[24];
    short FirmwareMajor;
    short FirmwareMinor;
    unsigned int AvailableHmdCaps;
    unsigned int DefaultHmdCaps;
    unsigned int AvailableTrackingCaps;
    unsigned int DefaultTrackingCaps;
    
    ovrFovPort DefaultEyeFov[2];
    ovrFovPort MaxEyeFov[2];
    ovrSizei Resolution;
    float DisplayRefreshRate;
    
    char _pad1[4]; // (4 bytes)
};

// -------------------------------------------------------------
// ovrInputState
// Based on OVR_CAPI.h from 2017 version sdk
// -------------------------------------------------------------
struct ovrInputState {
    double TimeInSeconds;
    unsigned int Buttons;
    unsigned int Touches;
    float IndexTrigger[2];
    float HandTrigger[2];
    ovrVector2f Thumbstick[2];
    int ControllerType;
    float IndexTriggerNoDeadzone[2];
    float HandTriggerNoDeadzone[2];
    ovrVector2f ThumbstickNoDeadzone[2];
    float IndexTriggerRaw[2];
    float HandTriggerRaw[2];
    ovrVector2f ThumbstickRaw[2];
};

typedef ovrResult(__cdecl* pf_SetControllerVibration)(ovrSession, ovrControllerType, float, float);
typedef ovrResult(__cdecl* pf_SubmitControllerVibration)(ovrSession, ovrControllerType, const ovrHapticsBuffer*);
typedef ovrHmdDesc(__cdecl* pf_GetHmdDesc)(ovrSession);

pf_SetControllerVibration Real_SetControllerVibration = nullptr;
pf_SubmitControllerVibration Real_SubmitControllerVibration = nullptr;
pf_GetHmdDesc Real_GetHmdDesc = nullptr;

// =============================================================
// HOOKS
// =============================================================

// 1. HAPTICS MOD
ovrResult __cdecl Hooked_SubmitControllerVibration(ovrSession session, ovrControllerType type, const ovrHapticsBuffer* buffer) {
    // Lazy load the real function if missing
    if (!Real_SetControllerVibration) {
        HMODULE h = GetModuleHandleA("LibOVRRT64_1.dll");
        if (h) Real_SetControllerVibration = (pf_SetControllerVibration)GetProcAddress(h, "ovr_SetControllerVibration");
    }
    if (!Real_SetControllerVibration) return 0;

    bool shouldVibrate = false;
    float finalAmplitude = 0.0f;

    // Analyze the buffer intensity
    if (buffer && buffer->SamplesCount > 0) {
        const unsigned char* samples = (const unsigned char*)buffer->Samples;
        long total = 0;
        for (int i = 0; i < buffer->SamplesCount; i++) total += samples[i];
        float amp = ((float)total / buffer->SamplesCount) / 255.0f; 

        if (amp > 0.01f) { 
            shouldVibrate = true;
            finalAmplitude = amp * g_HapticStrength;
            if (finalAmplitude > 1.0f) finalAmplitude = 1.0f;
        }
    }

    if (shouldVibrate) Real_SetControllerVibration(session, type, 1.0f, finalAmplitude);
    else Real_SetControllerVibration(session, type, 0.0f, 0.0f);

    return 0; 
}

// 2. FOV MOD
ovrHmdDesc __cdecl Hooked_GetHmdDesc(ovrSession session) {
    // Get the real hardware data
    ovrHmdDesc desc = Real_GetHmdDesc(session);

    // Modify the tangent angles if multiplier is active
    if (g_FovMultiplier != 1.0f) {
        for (int i = 0; i < 2; ++i) {
            desc.DefaultEyeFov[i].UpTan    *= g_FovMultiplier;
            desc.DefaultEyeFov[i].DownTan  *= g_FovMultiplier;
            desc.DefaultEyeFov[i].LeftTan  *= g_FovMultiplier;
            desc.DefaultEyeFov[i].RightTan *= g_FovMultiplier;
        }
    }
    return desc;
}

void InstallHooks() {
    LoadConfig();

    HMODULE hLibOVR = nullptr;
    int attempts = 0;
    // Fast loop to catch OVR initialization early
    while (!hLibOVR && attempts < 500) {
        hLibOVR = GetModuleHandleA("LibOVRRT64_1.dll");
        if(!hLibOVR) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }

    if (hLibOVR) {
        Real_SubmitControllerVibration = (pf_SubmitControllerVibration)GetProcAddress(hLibOVR, "ovr_SubmitControllerVibration");
        Real_GetHmdDesc = (pf_GetHmdDesc)GetProcAddress(hLibOVR, "ovr_GetHmdDesc");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (Real_SubmitControllerVibration) DetourAttach(&(PVOID&)Real_SubmitControllerVibration, Hooked_SubmitControllerVibration);
        if (Real_GetHmdDesc) DetourAttach(&(PVOID&)Real_GetHmdDesc, Hooked_GetHmdDesc);

        DetourTransactionCommit();
    }
}

void Initialize() {
    std::thread(InstallHooks).detach();
}
