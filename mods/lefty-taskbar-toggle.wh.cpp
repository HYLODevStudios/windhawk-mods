// ==WindhawkMod==
// @id              lefty-taskbar-toggle
// @name            Lefty Taskbar Toggle
// @description     Toggle between your normal taskbar alignment and Left alignment instantly.
// @version         0.4
// @author          HYLODevGX
// @include         explorer.exe
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Lefty Taskbar Toggle
When enabled, forces the Windows taskbar alignment to **Left** instantly.  
When disabled, restores your original alignment instantly.

## Features
- Remembers your original TaskbarAl value.
- Hooks registry reads so Explorer always “sees” left when enabled.
- Broadcasts WM_SETTINGCHANGE so the change is immediate.
- Works instantly — no Explorer restart required.

## Screenshot
![Taskbar aligned to the left](https://i.imgur.com/iLVjCQr.png)

## How to Use
1. Install the mod from Windhawk.
2. Toggle it **on** to force the taskbar to the left.
3. Toggle it **off** to restore your original alignment.
4. Changes apply instantly without logging out or restarting Explorer.

## Notes
- The mod saves your original alignment the first time you enable it.
- Disabling or uninstalling the mod will restore that saved alignment.
- Compatible with Windows 11 taskbar alignment settings.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- forceLeft: true
  $name: Force left alignment
  $description: When enabled, align taskbar to Left. When disabled, restore original alignment.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <wchar.h>
#include <unordered_set>
#include <mutex>

static const wchar_t* kAdvancedSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
static const wchar_t* kTaskbarAlValue = L"TaskbarAl";

struct {
    bool forceLeft;
} g_settings;

static DWORD g_originalValue = 1; // Default to Center if unknown
static bool g_originalSaved = false;

static std::unordered_set<HKEY> g_advancedKeys;
static std::mutex g_keysMutex;

using RegOpenKeyExW_t    = decltype(&RegOpenKeyExW);
using RegQueryValueExW_t = decltype(&RegQueryValueExW);
using RegGetValueW_t     = decltype(&RegGetValueW);
using RegCloseKey_t      = decltype(&RegCloseKey);

static RegOpenKeyExW_t    pRegOpenKeyExW_Original;
static RegQueryValueExW_t pRegQueryValueExW_Original;
static RegGetValueW_t     pRegGetValueW_Original;
static RegCloseKey_t      pRegCloseKey_Original;

static inline bool IEquals(const wchar_t* a, const wchar_t* b) {
    return _wcsicmp(a, b) == 0;
}

static inline bool PathEqualsAdvancedHKCU(HKEY root, const wchar_t* subKey) {
    return root == HKEY_CURRENT_USER && subKey && IEquals(subKey, kAdvancedSubKey);
}

static void TrackAdvancedKey(HKEY hKey) {
    std::lock_guard<std::mutex> lock(g_keysMutex);
    g_advancedKeys.insert(hKey);
}

static void UntrackKey(HKEY hKey) {
    std::lock_guard<std::mutex> lock(g_keysMutex);
    g_advancedKeys.erase(hKey);
}

static bool IsAdvancedKey(HKEY hKey) {
    std::lock_guard<std::mutex> lock(g_keysMutex);
    return g_advancedKeys.find(hKey) != g_advancedKeys.end();
}

static void SaveOriginalIfNeeded() {
    if (g_originalSaved) return;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kAdvancedSubKey, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD val = 1;
        DWORD size = sizeof(val);
        DWORD type = REG_DWORD;
        if (RegQueryValueExW(hKey, kTaskbarAlValue, nullptr, &type, (LPBYTE)&val, &size) == ERROR_SUCCESS && type == REG_DWORD) {
            g_originalValue = val;
            g_originalSaved = true;
            Wh_Log(L"[LeftyTaskbar] Saved original TaskbarAl=%u", val);
        }
        RegCloseKey(hKey);
    }
}

static void RefreshTaskbarNow() {
    SendMessageTimeoutW(HWND_BROADCAST,
                        WM_SETTINGCHANGE,
                        0,
                        (LPARAM)L"TraySettings",
                        SMTO_ABORTIFHUNG,
                        200,
                        nullptr);
}

static void WriteTaskbarAlignment(DWORD val) {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kAdvancedSubKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, kTaskbarAlValue, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
        RegCloseKey(hKey);
        Wh_Log(L"[LeftyTaskbar] Wrote TaskbarAl=%u", val);
        RefreshTaskbarNow(); // 🔄 Instant apply
    }
}

static void OverrideToLeftIfNeeded(LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    if (lpType && *lpType == REG_DWORD && lpData && lpcbData && *lpcbData >= sizeof(DWORD)) {
        DWORD* val = (DWORD*)lpData;
        if (*val != 0) {
            *val = 0;
        }
    }
}

// Hooks

LSTATUS WINAPI RegOpenKeyExW_Hook(HKEY hKey, LPCWSTR lpSubKey, DWORD ulOptions,
                                  REGSAM samDesired, PHKEY phkResult) {
    LSTATUS s = pRegOpenKeyExW_Original(hKey, lpSubKey, ulOptions, samDesired, phkResult);
    if (g_settings.forceLeft && s == ERROR_SUCCESS && phkResult && PathEqualsAdvancedHKCU(hKey, lpSubKey)) {
        TrackAdvancedKey(*phkResult);
    }
    return s;
}

LSTATUS WINAPI RegQueryValueExW_Hook(HKEY hKey, LPCWSTR lpValueName, LPDWORD lpReserved,
                                     LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) {
    LSTATUS status = pRegQueryValueExW_Original(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);

    if (g_settings.forceLeft &&
        status == ERROR_SUCCESS &&
        IsAdvancedKey(hKey) &&
        lpValueName && IEquals(lpValueName, kTaskbarAlValue)) {
        OverrideToLeftIfNeeded(lpType, lpData, lpcbData);
    }

    return status;
}

LSTATUS WINAPI RegGetValueW_Hook(HKEY hkey, LPCWSTR lpSubKey, LPCWSTR lpValue,
                                 DWORD dwFlags, LPDWORD pdwType, PVOID pvData, LPDWORD pcbData) {
    LSTATUS status = pRegGetValueW_Original(hkey, lpSubKey, lpValue, dwFlags, pdwType, pvData, pcbData);

    if (g_settings.forceLeft &&
        status == ERROR_SUCCESS &&
        hkey == HKEY_CURRENT_USER &&
        lpSubKey && IEquals(lpSubKey, kAdvancedSubKey) &&
        lpValue && IEquals(lpValue, kTaskbarAlValue)) {
        OverrideToLeftIfNeeded(pdwType, (LPBYTE)pvData, pcbData);
    }

    return status;
}

LSTATUS WINAPI RegCloseKey_Hook(HKEY hKey) {
    if (g_settings.forceLeft && IsAdvancedKey(hKey)) {
        UntrackKey(hKey);
    }
    return pRegCloseKey_Original(hKey);
}

// Helpers

static void LoadSettings() {
    g_settings.forceLeft = Wh_GetIntSetting(L"forceLeft") != 0;
}

BOOL Wh_ModInit() {
    LoadSettings();
    SaveOriginalIfNeeded();

    if (g_settings.forceLeft) {
        WriteTaskbarAlignment(0); // Left
    } else {
        WriteTaskbarAlignment(g_originalValue); // Restore
    }

    HMODULE hAdvapi = GetModuleHandleW(L"advapi32.dll");
    if (!hAdvapi) hAdvapi = LoadLibraryW(L"advapi32.dll");

    if (hAdvapi) {
        pRegOpenKeyExW_Original    = (RegOpenKeyExW_t)   GetProcAddress(hAdvapi, "RegOpenKeyExW");
        pRegQueryValueExW_Original = (RegQueryValueExW_t)GetProcAddress(hAdvapi, "RegQueryValueExW");
        pRegGetValueW_Original     = (RegGetValueW_t)    GetProcAddress(hAdvapi, "RegGetValueW");
        pRegCloseKey_Original      = (RegCloseKey_t)     GetProcAddress(hAdvapi, "RegCloseKey");

        Wh_SetFunctionHook((void*)pRegOpenKeyExW_Original,    (void*)RegOpenKeyExW_Hook,    (void**)&pRegOpenKeyExW_Original);
        Wh_SetFunctionHook((void*)pRegQueryValueExW_Original, (void*)RegQueryValueExW_Hook, (void**)&pRegQueryValueExW_Original);
        Wh_SetFunctionHook((void*)pRegGetValueW_Original,     (void*)RegGetValueW_Hook,     (void**)&pRegGetValueW_Original);
        Wh_SetFunctionHook((void*)pRegCloseKey_Original,      (void*)RegCloseKey_Hook,      (void**)&pRegCloseKey_Original);
    }

    return TRUE;
}

void Wh_ModUninit() {
    // Restore original alignment when the mod is unloaded
    if (g_originalSaved) {
        WriteTaskbarAlignment(g_originalValue);
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();

    if (g_settings.forceLeft) {
        SaveOriginalIfNeeded();
        WriteTaskbarAlignment(0); // Left
    } else {
        if (g_originalSaved) {
            WriteTaskbarAlignment(g_originalValue); // Restore
        }
    }
}