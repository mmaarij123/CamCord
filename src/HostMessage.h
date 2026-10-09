#pragma once
#include <windows.h>
#include <json.hpp>
#include "RecordingQuality.h"

// Parse once and read only root fields/the explicit settings object. Searching
// text for a key confuses escaped strings, nested keys and malformed JSON.
class HostMessage {
public:
    bool Parse(const std::wstring& text) {
        value_ = {};
        if (text.empty() || text.size() > 4096) return false;
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (!size) return false;
        std::string utf8(size, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr)) return false;
        value_ = nlohmann::json::parse(utf8, nullptr, false);
        return value_.is_object() && value_.contains("type") && value_["type"].is_string();
    }
    std::wstring String(const char* key) const {
        const auto found = value_.find(key);
        if (found == value_.end() || !found->is_string()) return L"";
        const auto& utf8 = found->get_ref<const std::string&>();
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (!size) return L"";
        std::wstring text(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), text.data(), size);
        return text;
    }
    bool Boolean(const char* key, bool fallback) const { return Boolean(value_, key, fallback); }
    RecordingSettings Settings(RecordingSettings settings) const {
        const auto found = value_.find("settings");
        if (found == value_.end() || !found->is_object()) return settings;
        const auto& data = *found;
        settings.height = Integer(data, "height", settings.height);
        if (settings.height != 480 && settings.height != 720 && settings.height != 1080) settings.height = 1080;
        settings.width = settings.height == 480 ? 854 : settings.height == 720 ? 1280 : 1920;
        settings.fps = Integer(data, "fps", settings.fps);
        if (settings.fps != 15 && settings.fps != 30 && settings.fps != 60 && settings.fps != 120) settings.fps = 60;
        if (settings.height != 1080 && settings.fps == 120) settings.fps = 60;
        settings.bitrateMbps = NormalizeBitrateMbps(Integer(data, "bitrateMbps", settings.bitrateMbps));
        settings.systemAudio = Boolean(data, "systemAudio", settings.systemAudio);
        settings.microphone = Boolean(data, "microphone", settings.microphone);
        settings.autoCheckUpdates = Boolean(data, "autoCheckUpdates", settings.autoCheckUpdates);
        return settings;
    }
private:
    static int Integer(const nlohmann::json& data, const char* key, int fallback) {
        const auto found = data.find(key);
        if (found == data.end() || !found->is_number_integer()) return fallback;
        // nlohmann's get<int> permits narrowing; reject overflow explicitly.
        if (*found < INT_MIN || *found > INT_MAX) return fallback;
        return found->get<int>();
    }
    static bool Boolean(const nlohmann::json& data, const char* key, bool fallback) {
        const auto found = data.find(key);
        return found != data.end() && found->is_boolean() ? found->get<bool>() : fallback;
    }
    nlohmann::json value_;
};
