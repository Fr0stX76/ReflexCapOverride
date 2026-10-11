#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <strsafe.h>
#include <math.h>
#include <wchar.h>
#include "MinHook.h"

#define NVAPI_OK 0
#define NVAPI_D3D_SETSLEEPMODE_ID 0xAC1CA9E0
#define NVAPI_D3D_SLEEP_ID 0x852CD1D2
#define MAKE_NVAPI_VERSION(type, ver) ((DWORD)(sizeof(type) | ((ver) << 16)))
#define MAX_DEVICES 16
#define RETRY_DELAY 1000

typedef int NvAPI_Status;

typedef struct {
    DWORD version;
    BYTE bLowLatencyMode;
    BYTE bLowLatencyBoost;
    DWORD minimumIntervalUs;
    BYTE bUseMarkersToOptimize;
    BYTE bUseMinQueueTime;
    BYTE rsvd[30];
} NV_SET_SLEEP_MODE_PARAMS;

C_ASSERT(sizeof(NV_SET_SLEEP_MODE_PARAMS) == 44);

typedef void* (__cdecl* NvAPI_QueryInterface_t)(DWORD id);
typedef NvAPI_Status (__cdecl* NvAPI_D3D_SetSleepMode_t)(void* device, NV_SET_SLEEP_MODE_PARAMS* params);
typedef NvAPI_Status (__cdecl* NvAPI_D3D_Sleep_t)(void* device);

typedef struct {
    void* device;
    ULONGLONG retryAt;
    BOOL configured;
} DEVICE_STATE;

static const char* const EXPORT_NAMES[] = {
    "GetFileVersionInfoA", "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW", "GetFileVersionInfoSizeA", "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW", "GetFileVersionInfoW",
    "VerFindFileA", "VerFindFileW", "VerInstallFileA", "VerInstallFileW",
    "VerLanguageNameA", "VerLanguageNameW", "VerQueryValueA",
    "VerQueryValueIndexA", "VerQueryValueIndexW", "VerQueryValueW"
};

FARPROC g_exports[ARRAYSIZE(EXPORT_NAMES)];

static DWORD g_intervalUs;
static NvAPI_D3D_SetSleepMode_t g_setSleepMode;
static NvAPI_D3D_Sleep_t g_sleep;
static SRWLOCK g_deviceLock = SRWLOCK_INIT;
static DEVICE_STATE g_devices[MAX_DEVICES];
static UINT g_deviceCount;
static UINT g_nextEviction;

static INT_PTR WINAPI MissingExport(void) {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

static BOOL LoadRealVersion(void) {
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || FAILED(StringCchCatW(path, MAX_PATH, L"\\version.dll")))
        return FALSE;

    HMODULE module = LoadLibraryW(path);
    if (!module)
        return FALSE;

    for (UINT i = 0; i < ARRAYSIZE(EXPORT_NAMES); i++) {
        g_exports[i] = GetProcAddress(module, EXPORT_NAMES[i]);
        if (!g_exports[i])
            g_exports[i] = (FARPROC)MissingExport;
    }
    return TRUE;
}

static DWORD ParseTargetFps(const wchar_t* text) {
    double fps = 0.0, scale = 1.0;
    BOOL digits = FALSE;

    while (*text == L' ' || *text == L'\t')
        text++;
    for (; *text >= L'0' && *text <= L'9'; text++) {
        fps = fps * 10.0 + (*text - L'0');
        digits = TRUE;
    }
    if (*text == L'.' || *text == L',') {
        for (text++; *text >= L'0' && *text <= L'9'; text++) {
            fps = fps * 10.0 + (*text - L'0');
            scale *= 10.0;
            digits = TRUE;
        }
    }
    while (*text == L' ' || *text == L'\t')
        text++;

    fps /= scale;
    if (!digits || *text || fps < 1.0 || fps > 1000.0)
        return 0;
    return (DWORD)ceil(1000000.0 / fps);
}

static void ReadConfig(void) {
    wchar_t path[MAX_PATH], value[64];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    wchar_t* name = wcsrchr(path, L'\\');
    if (!name)
        return;
    name[1] = L'\0';
    if (FAILED(StringCchCatW(path, MAX_PATH, L"ReflexCapOverride.ini")))
        return;

    GetPrivateProfileStringW(L"ReflexCapOverride", L"TargetFPS", L"0", value, ARRAYSIZE(value), path);
    g_intervalUs = ParseTargetFps(value);
}

static NvAPI_Status ApplySleepMode(void* device) {
    NV_SET_SLEEP_MODE_PARAMS params = {0};
    params.version = MAKE_NVAPI_VERSION(NV_SET_SLEEP_MODE_PARAMS, 1);
    params.minimumIntervalUs = g_intervalUs;
    return g_setSleepMode(device, &params);
}

static NvAPI_Status __cdecl HookedSetSleepMode(void* device, NV_SET_SLEEP_MODE_PARAMS* params) {
    UNREFERENCED_PARAMETER(params);
    return ApplySleepMode(device);
}

static DEVICE_STATE* FindDevice(void* device) {
    for (UINT i = 0; i < g_deviceCount; i++) {
        if (g_devices[i].device == device)
            return &g_devices[i];
    }

    DEVICE_STATE* state;
    if (g_deviceCount < MAX_DEVICES)
        state = &g_devices[g_deviceCount++];
    else
        state = &g_devices[g_nextEviction++ % MAX_DEVICES];
    ZeroMemory(state, sizeof(*state));
    state->device = device;
    return state;
}

static NvAPI_Status __cdecl HookedSleep(void* device) {
    if (device) {
        AcquireSRWLockExclusive(&g_deviceLock);
        DEVICE_STATE* state = FindDevice(device);
        if (!state->configured) {
            ULONGLONG now = GetTickCount64();
            if (now >= state->retryAt) {
                state->configured = ApplySleepMode(device) == NVAPI_OK;
                state->retryAt = now + RETRY_DELAY;
            }
        }
        ReleaseSRWLockExclusive(&g_deviceLock);
    }
    return g_sleep(device);
}

static BOOL FindSleepFunctions(void** pSetSleepMode, void** pSleep) {
    HMODULE nvapi;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"nvapi64.dll", &nvapi))
        return FALSE;

    NvAPI_QueryInterface_t queryInterface = (NvAPI_QueryInterface_t)GetProcAddress(nvapi, "nvapi_QueryInterface");
    if (!queryInterface)
        return FALSE;

    *pSetSleepMode = queryInterface(NVAPI_D3D_SETSLEEPMODE_ID);
    *pSleep = queryInterface(NVAPI_D3D_SLEEP_ID);
    return *pSetSleepMode && *pSleep;
}

static DWORD WINAPI InitThread(LPVOID param) {
    void* pSetSleepMode;
    void* pSleep;
    UINT attempt = 0;

    UNREFERENCED_PARAMETER(param);
    ReadConfig();
    if (MH_Initialize() != MH_OK) {
        OutputDebugStringW(L"ReflexCapOverride: unable to initialize MinHook\n");
        return 0;
    }

    while (!FindSleepFunctions(&pSetSleepMode, &pSleep))
        Sleep(attempt++ < 400 ? 5 : 1000);

    if (MH_CreateHook(pSetSleepMode, (LPVOID)HookedSetSleepMode, (LPVOID*)&g_setSleepMode) != MH_OK ||
        MH_CreateHook(pSleep, (LPVOID)HookedSleep, (LPVOID*)&g_sleep) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        OutputDebugStringW(L"ReflexCapOverride: unable to hook the NVAPI Reflex functions\n");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    UNREFERENCED_PARAMETER(reserved);
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    if (!LoadRealVersion())
        return FALSE;

    HMODULE self;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)instance, &self);

    HANDLE thread = CreateThread(NULL, 0, InitThread, NULL, 0, NULL);
    if (thread)
        CloseHandle(thread);
    else
        OutputDebugStringW(L"ReflexCapOverride: unable to start the initialization thread\n");
    return TRUE;
}
