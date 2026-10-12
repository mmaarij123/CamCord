#pragma once
#include <string>
#include <windows.h>
inline int NormalizeReplaySeconds(int value) {
    for (int seconds : {30,60,90,120,180,240,300}) if (value == seconds) return value;
    return 60;
}
inline bool ValidReplayHotkey(const std::wstring& value) {
    for (const auto* key : {L"Ctrl+Shift+F8",L"Ctrl+Shift+F9",L"Ctrl+Shift+F10",L"Ctrl+Alt+F8",L"Ctrl+Alt+F9",L"Ctrl+Alt+F10"}) if (value == key) return true;
    return false;
}
inline std::wstring NormalizeReplayHotkey(const std::wstring& value) { return ValidReplayHotkey(value) ? value : L"Ctrl+Shift+F8"; }
inline UINT ReplayModifiers(const std::wstring& value) { return MOD_CONTROL | MOD_NOREPEAT | (value.find(L"Shift") != std::wstring::npos ? MOD_SHIFT : MOD_ALT); }
inline UINT ReplayKey(const std::wstring& value) { return value.substr(value.size()-2) == L"10" ? VK_F10 : value.back() == L'9' ? VK_F9 : VK_F8; }
