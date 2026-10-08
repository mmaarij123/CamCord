#include "../src/UpdateManager.h"
#include "../src/StartupManager.h"
#include "../src/SettingsManager.h"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

static json Release(std::string version = "1.5.0") {
    return { {"tag_name", "v" + version}, {"draft", false}, {"prerelease", false},
        {"assets", json::array({ { {"name", "CamCord-Setup.exe"}, {"state", "uploaded"}, {"size", 3},
            {"browser_download_url", "https://github.com/mmaarij123/CamCord/releases/download/v" + version + "/CamCord-Setup.exe"},
            {"digest", "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"} } })} };
}

struct TestRegistry {
    std::wstring key = L"Software\\CamCord\\Tests\\AppFeatures-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    ~TestRegistry() { RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()); }
};

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) return 2;
    const auto root = fs::absolute(argv[1]);
    if (fs::exists(root)) { std::cerr << "Supply a fresh test folder.\n"; return 2; }
    try {
        fs::create_directories(root);
        Check(UpdateManager::NewerVersion("v1.10.0", "1.9.99"), "Versions compared as strings instead of numbers");
        for (const auto& candidate : { "1.4.0", "1.3.99", "1.4.0-beta", "2.0", "-1.0.0", "9999999999999.0.0", "2.0.0/../../x" })
            Check(!UpdateManager::NewerVersion(candidate, "1.4.0"), "Invalid, equal or older version accepted");
        UpdateInfo info;
        Check(UpdateManager::ParseRelease(Release().dump(), "1.4.0", info).ok && info.available && info.version == "1.5.0", "Valid release was rejected");
        Check(UpdateManager::ParseRelease(Release("1.4.0").dump(), "1.4.0", info).ok && !info.available, "Equal version prompted an update");
        Check(UpdateManager::ParseRelease(Release("1.3.1").dump(), "1.4.0", info).ok && !info.available, "Older release prompted a downgrade");
        std::cout << "PASS: semantic versions, valid release, equal version and downgrade prevention\n";

        for (int variation = 0; variation < 10; ++variation) {
            auto release = Release();
            switch (variation) {
            case 0: release["draft"] = true; break;
            case 1: release["prerelease"] = true; break;
            case 2: release["assets"] = json::array(); break;
            case 3: release["assets"][0]["digest"] = nullptr; break;
            case 4: release["assets"][0]["browser_download_url"] = "https://example.com/evil.exe"; break;
            case 5: release["assets"][0]["size"] = 0; break;
            case 6: release["assets"][0]["size"] = 600ull * 1024 * 1024; break;
            case 7: release["assets"].push_back(release["assets"][0]); break;
            case 8: release["tag_name"] = "v2.0.0/../../escape"; break;
            case 9: release["assets"][0]["state"] = "new"; break;
            }
            Check(!UpdateManager::ParseRelease(release.dump(), "1.4.0", info).ok && !info.available,
                "Incomplete or untrusted release metadata was accepted");
        }
        Check(!UpdateManager::ParseRelease("{broken", "1.4.0", info).ok, "Malformed JSON was accepted");
        std::cout << "PASS: draft/prerelease, missing hash, foreign URL, excessive size and duplicate asset rejection\n";

        Check(UpdateManager::ParseRelease(Release().dump(), "1.4.0", info).ok, "Could not prepare hash test");
        const auto cached = root / L"cache" / L"1.5.0" / L"CamCord-Setup.exe";
        fs::create_directories(cached.parent_path());
        { std::ofstream file(cached, std::ios::binary); file << "abc"; }
        Check(UpdateManager::FileSha256(cached.wstring()) == info.sha256, "SHA256 disagrees with known abc digest");
        UpdateManager updates;
        Check(updates.Verify(info, cached.wstring()).ok, "Valid cached setup failed verification");
        const auto hit = updates.Download(info, (root / L"cache").wstring());
        Check(hit.result.ok && hit.installer == cached.wstring() && updates.Progress() == 100, "Verified cached setup was not reused");
        { std::ofstream file(cached, std::ios::binary | std::ios::trunc); file << "abd"; }
        Check(!updates.Verify(info, cached.wstring()).ok, "Tampered setup passed checksum verification");
        updates.Cancel();
        Check(!updates.Download(info, (root / L"cache").wstring()).result.ok, "Cancelled update downloaded or executed");
        auto foreign = info; foreign.url = "https://evil.invalid/setup.exe";
        updates.Reset();
        Check(!updates.Download(foreign, (root / L"cache").wstring()).result.ok, "Foreign download source was accepted");
        std::cout << "PASS: SHA256, verified cache reuse, tamper rejection and cancellation (no installer executed)\n";

        Check(UpdateManager::CanInstall(RecorderState::Idle, false, false, true), "Idle installation unexpectedly blocked");
        for (auto state : { RecorderState::Recording, RecorderState::Paused, RecorderState::Saving })
            Check(!UpdateManager::CanInstall(state, false, false, true), "Installation could interrupt an active recording");
        Check(!UpdateManager::CanInstall(RecorderState::Idle, true, false, true) &&
            !UpdateManager::CanInstall(RecorderState::Idle, false, true, true) &&
            !UpdateManager::CanInstall(RecorderState::Idle, false, false, false), "Busy or unverified update was allowed to install");
        std::cout << "PASS: install guard for recording, pause, saving, pending actions and unverified update\n";

        TestRegistry registry;
        const auto executable = root / L"CamCord with spaces.exe";
        { std::ofstream file(executable); file << "test-only marker"; }
        StartupManager startup(registry.key, executable.wstring());
        Check(!startup.IsEnabled(), "Startup unexpectedly enabled by default");
        Check(startup.SetEnabled(true).ok && startup.IsEnabled(), "Startup opt-in failed");
        HKEY key = nullptr;
        Check(RegOpenKeyExW(HKEY_CURRENT_USER, registry.key.c_str(), 0, KEY_READ | KEY_WRITE, &key) == ERROR_SUCCESS, "Cannot read isolated test key");
        wchar_t command[1024]{}; DWORD bytes = sizeof(command);
        const auto read = RegQueryValueExW(key, L"CamCord", nullptr, nullptr, reinterpret_cast<BYTE*>(command), &bytes);
        const DWORD marker = 123;
        RegSetValueExW(key, L"OtherApplication", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&marker), sizeof(marker));
        RegCloseKey(key);
        Check(read == ERROR_SUCCESS && std::wstring(command) == L"\"" + executable.wstring() + L"\" --startup", "Startup command lost quotes or started recording");
        Check(startup.SetEnabled(false).ok && !startup.IsEnabled(), "Startup opt-out failed");
        DWORD sibling = 0; bytes = sizeof(sibling);
        Check(RegGetValueW(HKEY_CURRENT_USER, registry.key.c_str(), L"OtherApplication", RRF_RT_REG_DWORD, nullptr, &sibling, &bytes) == ERROR_SUCCESS && sibling == marker,
            "Disabling startup altered another application");
        std::cout << "PASS: isolated startup opt-in/out, quoted executable, launch-only flag and unrelated entry preservation\n";

        SettingsManager settings((root / L"settings.ini").wstring());
        Check(settings.Load().autoCheckUpdates, "Automatic update checks should default on");
        auto preferences = settings.Load(); preferences.autoCheckUpdates = false;
        Check(settings.Save(preferences).ok && !settings.Load().autoCheckUpdates, "Update opt-out was not saved");
        preferences.autoCheckUpdates = true;
        Check(settings.Save(preferences).ok && settings.Load().autoCheckUpdates, "Update opt-in was not saved");
        std::cout << "PASS: automatic update preferences persist across reloads\n";

        if (argc == 3 && std::wstring(argv[2]) == L"--live") {
            updates.Reset();
            const auto check = updates.Check("0.0.0");
            if (!check.result.ok) std::wcerr << check.result.message << L'\n';
            Check(check.result.ok, "Live HTTPS latest-release check failed");
            std::cout << "PASS: live GitHub HTTPS latest-release check\n";
            Check(check.info.available, "Live release did not expose a downloadable setup");
            updates.Reset();
            const auto downloaded = updates.Download(check.info, (root / L"live-download").wstring());
            if (!downloaded.result.ok) std::wcerr << downloaded.result.message << L'\n';
            Check(downloaded.result.ok && updates.Verify(check.info, downloaded.installer).ok, "Live update download or checksum verification failed");
            std::cout << "PASS: live GitHub setup download, redirect and SHA256 verification (not executed)\n";
        }
        std::cout << "All app-feature tests passed. The real Windows startup entry was never changed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
