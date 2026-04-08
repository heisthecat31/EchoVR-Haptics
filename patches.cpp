#include "patches.h"
#include "detours.h" 
#include <windows.h>
#include <thread>
#include <chrono>
#include <fstream>
#include <string>
#include <sstream>
#include <map>

#pragma comment(lib, "detours.lib")

float g_HapticStrength = 1.4f;   
float g_FovMultiplier  = 1.0f; 

// =============================================================
// OVR CONSTANTS & STRUCTURES
// =============================================================
const unsigned int OVR_BUTTON_A         = 0x00000001;
const unsigned int OVR_BUTTON_B         = 0x00000002;
const unsigned int OVR_BUTTON_RTHUMB    = 0x00000004;
const unsigned int OVR_BUTTON_RSHOULDER = 0x00000008;
const unsigned int OVR_BUTTON_X         = 0x00000100;
const unsigned int OVR_BUTTON_Y         = 0x00000200;
const unsigned int OVR_BUTTON_LTHUMB    = 0x00000400;
const unsigned int OVR_BUTTON_LSHOULDER = 0x00000800;
const unsigned int OVR_BUTTON_ENTER     = 0x00100000; 

typedef int ovrResult;
typedef void* ovrSession;
typedef int ovrControllerType;
typedef int ovrHmdType; 

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
    char _pad0[4]; 
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
    char _pad1[4]; 
};

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
typedef ovrResult(__cdecl* pf_GetInputState)(ovrSession, ovrControllerType, ovrInputState*);

pf_SetControllerVibration Real_SetControllerVibration = nullptr;
pf_SubmitControllerVibration Real_SubmitControllerVibration = nullptr;
pf_GetHmdDesc Real_GetHmdDesc = nullptr;
pf_GetInputState Real_GetInputState = nullptr;

// =============================================================
// CONFIGURATION
// =============================================================
std::map<unsigned int, unsigned int> g_ButtonMappings;
std::map<int, int> g_AnalogMappings; 
std::map<int, int> g_StickMappings;
int g_StickRemapMode = 0; // 0=Both, 1=TurningOnly, 2=ButtonsOnly

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return str;
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

unsigned int StringToButton(const std::string& name) {
    if (name == "A") return OVR_BUTTON_A;
    if (name == "B") return OVR_BUTTON_B;
    if (name == "X") return OVR_BUTTON_X;
    if (name == "Y") return OVR_BUTTON_Y;
    if (name == "LStick") return OVR_BUTTON_LTHUMB;
    if (name == "RStick") return OVR_BUTTON_RTHUMB;
    if (name == "Menu") return OVR_BUTTON_ENTER;
    if (name == "LGrip_Btn") return OVR_BUTTON_LSHOULDER;
    if (name == "RGrip_Btn") return OVR_BUTTON_RSHOULDER;
    return 0;
}

int StringToAnalog(const std::string& name) {
    if (name == "LTrigger") return 0;
    if (name == "RTrigger") return 1;
    if (name == "LGrip") return 2;
    if (name == "RGrip") return 3;
    return -1;
}

int StringToStick(const std::string& name) {
    if (name == "LStick") return 0;
    if (name == "RStick") return 1;
    return -1;
}

void LoadConfig() {
    g_ButtonMappings.clear();
    g_AnalogMappings.clear();
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
                else if (key == "StickRemapMode") g_StickRemapMode = std::stoi(value);
                else if (key.find("Map_") == 0) {
                    std::string btnName = key.substr(4);
                    
                    // Analog (Triggers/Grips)
                    int fromA = StringToAnalog(btnName);
                    int toA = StringToAnalog(value);
                    if (fromA != -1 && toA != -1) {
                        g_AnalogMappings[fromA] = toA;
                        continue;
                    }

                    // Stick Analog
                    int fromS = StringToStick(btnName);
                    int toS = StringToStick(value);
                    if (fromS != -1 && toS != -1) {
                        if (g_StickRemapMode == 0 || g_StickRemapMode == 1) {
                            g_StickMappings[fromS] = toS;
                        }
                        if (g_StickRemapMode == 1) continue; // Only turning, skip button map
                    }
                    
                    // Buttons (including Stick Click)
                    if (g_StickRemapMode == 1 && fromS != -1) continue; // Turning Only mode, don't map stick buttons

                    unsigned int fromB = StringToButton(btnName);
                    unsigned int toB = StringToButton(value);
                    if (fromB != 0 && toB != 0) {
                        // If it's a stick button, check if we should map it
                        if (fromS != -1) {
                            if (g_StickRemapMode == 0 || g_StickRemapMode == 2) {
                                g_ButtonMappings[fromB] = toB;
                            }
                        } else {
                            g_ButtonMappings[fromB] = toB;
                        }
                    }
                }
            }
        }
    }
    if (g_HapticStrength > 5.0f) g_HapticStrength = 5.0f;
    if (g_FovMultiplier < 0.1f) g_FovMultiplier = 1.0f; 
}

// =============================================================
// HOOKS
// =============================================================

ovrResult __cdecl Hooked_SubmitControllerVibration(ovrSession session, ovrControllerType type, const ovrHapticsBuffer* buffer) {
    if (!Real_SetControllerVibration) {
        HMODULE h = GetModuleHandleA("LibOVRRT64_1.dll");
        if (h) Real_SetControllerVibration = (pf_SetControllerVibration)GetProcAddress(h, "ovr_SetControllerVibration");
    }
    if (!Real_SetControllerVibration) return 0;

    bool shouldVibrate = false;
    float finalAmplitude = 0.0f;
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

ovrHmdDesc __cdecl Hooked_GetHmdDesc(ovrSession session) {
    ovrHmdDesc desc = Real_GetHmdDesc(session);
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

ovrResult __cdecl Hooked_GetInputState(ovrSession session, ovrControllerType controllerType, ovrInputState* inputState) {
    ovrResult result = Real_GetInputState(session, controllerType, inputState);
    if (result >= 0 && inputState) {
        if (!g_ButtonMappings.empty()) {
            unsigned int originalButtons = inputState->Buttons;
            unsigned int buttonsToIgnore = 0;
            unsigned int buttonsToAdd = 0;
            for (std::map<unsigned int, unsigned int>::iterator it = g_ButtonMappings.begin(); it != g_ButtonMappings.end(); ++it) {
                unsigned int from = it->first;
                unsigned int to = it->second;
                if (originalButtons & from) {
                    buttonsToIgnore |= from;
                    buttonsToAdd |= to;
                }
            }
            inputState->Buttons = (originalButtons & ~buttonsToIgnore) | buttonsToAdd;
        }
        if (!g_AnalogMappings.empty()) {
            float origTrig[2] = {inputState->IndexTrigger[0], inputState->IndexTrigger[1]};
            float origGrip[2] = {inputState->HandTrigger[0], inputState->HandTrigger[1]};
            auto GetOrig = [&](int id) {
                if (id == 0) return origTrig[0];
                if (id == 1) return origTrig[1];
                if (id == 2) return origGrip[0];
                if (id == 3) return origGrip[1];
                return 0.0f;
            };
            for (std::map<int, int>::iterator it = g_AnalogMappings.begin(); it != g_AnalogMappings.end(); ++it) {
                int from = it->first;
                int to = it->second;
                float val = GetOrig(to);
                if (from == 0) inputState->IndexTrigger[0] = val;
                else if (from == 1) inputState->IndexTrigger[1] = val;
                else if (from == 2) inputState->HandTrigger[0] = val;
                else if (from == 3) inputState->HandTrigger[1] = val;
            }
        }
        if (!g_StickMappings.empty()) {
            ovrVector2f origStick[2] = { inputState->Thumbstick[0], inputState->Thumbstick[1] };
            ovrVector2f origStickND[2] = { inputState->ThumbstickNoDeadzone[0], inputState->ThumbstickNoDeadzone[1] };
            ovrVector2f origStickRaw[2] = { inputState->ThumbstickRaw[0], inputState->ThumbstickRaw[1] };

            for (std::map<int, int>::iterator it = g_StickMappings.begin(); it != g_StickMappings.end(); ++it) {
                int from = it->first;
                int to = it->second;
                inputState->Thumbstick[from] = origStick[to];
                inputState->ThumbstickNoDeadzone[from] = origStickND[to];
                inputState->ThumbstickRaw[from] = origStickRaw[to];
            }
        }
    }
    return result;
}

void InstallHooks() {
    LoadConfig();
    HMODULE hLibOVR = nullptr;
    int attempts = 0;
    while (!hLibOVR && attempts < 500) {
        hLibOVR = GetModuleHandleA("LibOVRRT64_1.dll");
        if(!hLibOVR) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        attempts++;
    }
    if (hLibOVR) {
        Real_SubmitControllerVibration = (pf_SubmitControllerVibration)GetProcAddress(hLibOVR, "ovr_SubmitControllerVibration");
        Real_GetHmdDesc = (pf_GetHmdDesc)GetProcAddress(hLibOVR, "ovr_GetHmdDesc");
        Real_GetInputState = (pf_GetInputState)GetProcAddress(hLibOVR, "ovr_GetInputState");
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (Real_SubmitControllerVibration) DetourAttach(&(PVOID&)Real_SubmitControllerVibration, Hooked_SubmitControllerVibration);
        if (Real_GetHmdDesc) DetourAttach(&(PVOID&)Real_GetHmdDesc, Hooked_GetHmdDesc);
        if (Real_GetInputState) DetourAttach(&(PVOID&)Real_GetInputState, Hooked_GetInputState);
        DetourTransactionCommit();
    }
}

void Initialize() {
    std::thread(InstallHooks).detach();
}
