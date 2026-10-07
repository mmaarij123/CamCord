// Integration tests use only their isolated output folder, never the user's settings.ini.
#include "../src/SettingsManager.h"
#include "../src/OutputManager.h"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

static void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Only a fresh test directory receives this ACL, and its original ACL is always restored.
class BlockDirectoryWrites {
public:
    explicit BlockDirectoryWrites(const fs::path& directory) : path_(directory.wstring()) {
        DWORD bytes = 0;
        GetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &bytes);
        Check(bytes != 0, "Could not determine the test directory's original ACL size");
        original_.resize(bytes);
        Check(GetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION,
            original_.data(), bytes, &bytes) != FALSE, "Could not read test directory ACL");
        PSECURITY_DESCRIPTOR denied = nullptr;
        // Deny creating files and child directories, retain read/delete/ACL restore rights.
        Check(ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(D;;0x00000006;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1, &denied, nullptr) != FALSE,
            "Could not build test-only write-deny ACL");
        const BOOL applied = SetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, denied);
        LocalFree(denied);
        Check(applied != FALSE, "Could not apply test-only write-deny ACL");
        active_ = true;
    }
    ~BlockDirectoryWrites() {
        if (active_ && !SetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, original_.data()))
            std::wcerr << L"WARNING: restore the test-only directory ACL manually: " << path_ << L'\n';
    }
    void Restore() {
        Check(SetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, original_.data()) != FALSE,
            "Could not restore test directory ACL");
        active_ = false;
    }
private:
    std::wstring path_;
    std::vector<unsigned char> original_;
    bool active_ = false;
};

static std::vector<unsigned char> Bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) { std::cerr << "Usage: settings_tests.exe isolated-output-folder\n"; return 2; }
    const auto root = fs::absolute(argv[1]);
    if (fs::exists(root)) { std::cerr << "Refusing to reuse an existing test folder. Supply a fresh path.\n"; return 2; }
    try {
        fs::create_directories(root);
        const auto settingsPath = root / L"settings.ini";
        SettingsManager settings(settingsPath.wstring());
        const auto defaults = settings.Load();
        Check(defaults.height == 1080 && defaults.width == 1920 && defaults.fps == 60 &&
            defaults.systemAudio && !defaults.microphone && defaults.outputFolder.empty(),
            "A fresh settings file did not load expected defaults");

        RecordingSettings expected;
        expected.height = 720; expected.width = 1280; expected.fps = 30;
        expected.systemAudio = false; expected.microphone = true;
        expected.outputFolder = (root / L"Recordings \u0631\u06cc\u06a9\u0627\u0631\u0688\u0646\u06af \u5f55\u5236 \U0001F3AC").wstring();
        Check(settings.Save(expected).ok, "Unicode settings save failed");
        const auto actual = settings.Load();
        Check(actual.height == expected.height && actual.width == expected.width && actual.fps == expected.fps &&
            actual.systemAudio == expected.systemAudio && actual.microphone == expected.microphone &&
            actual.outputFolder == expected.outputFolder, "Unicode settings did not round-trip exactly");
        const auto savedBytes = Bytes(settingsPath);
        Check(savedBytes.size() >= 2 && savedBytes[0] == 0xff && savedBytes[1] == 0xfe,
            "Unicode settings did not retain the UTF-16 BOM");
        std::cout << "PASS: isolated defaults and Unicode settings round-trip (Urdu, CJK, emoji)\n";

        auto invalid = expected;
        invalid.outputFolder += L"\r\nMicrophone=0";
        Check(!settings.Save(invalid).ok && Bytes(settingsPath) == savedBytes,
            "A newline-bearing output folder changed the saved settings");
        invalid = expected; invalid.height = 480; invalid.fps = 120;
        Check(settings.Save(invalid).ok && settings.Load().fps == 60,
            "A 120 FPS preference was not normalized outside 1080p");
        Check(settings.Save(expected).ok, "Could not restore baseline test settings");
        std::cout << "PASS: invalid INI path content is rejected; unsupported resolution/FPS is normalized\n";

        SettingsManager missingParent((root / L"does-not-exist" / L"settings.ini").wstring());
        const auto missingResult = missingParent.Save(expected);
        Check(!missingResult.ok && !missingResult.message.empty() && !fs::exists(root / L"does-not-exist"),
            "Unavailable settings destination was reported saved");
        const auto directoryTarget = root / L"directory-is-not-an-ini";
        fs::create_directory(directoryTarget);
        SettingsManager invalidDestination(directoryTarget.wstring());
        Check(!invalidDestination.Save(expected).ok && fs::is_directory(directoryTarget),
            "Failed atomic replacement did not preserve the existing destination directory");
        const auto replacementTemp = directoryTarget.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        Check(!fs::exists(replacementTemp), "Failed settings replacement left its temporary file behind");
        std::cout << "PASS: failed settings destinations return failure and discard only temporary output\n";

        OutputManager output;
        output.SetCaptureFolder(expected.outputFolder);
        Check(output.EnsureCaptureFolder().ok && fs::is_directory(expected.outputFolder),
            "Unicode output folder was not created");
        Check(fs::is_empty(expected.outputFolder), "Writeability probe was not removed after closing");
        Check(output.EnsureFreeSpace(0).ok, "Available output disk space could not be queried");
        Check(!output.EnsureFreeSpace(std::numeric_limits<unsigned long long>::max()).ok,
            "An impossible free-space requirement was accepted");
        const fs::path recording = output.NewOutputPath();
        Check(recording.parent_path() == fs::path(expected.outputFolder) && recording.extension() == L".mp4",
            "Recording output escaped the chosen save folder");
        const fs::path session = output.NewSessionFolder();
        Check(!session.empty() && fs::is_directory(session) &&
            session.parent_path() == fs::path(expected.outputFolder) / L".camcord-sessions" &&
            session.root_name() == fs::path(expected.outputFolder).root_name(),
            "Scratch recording data is not on the chosen output drive/folder");
        std::cout << "PASS: Unicode folder, disk-space guard and scratch data inside selected output folder/drive\n";

        const auto blockedFolder = root / L"write-denied";
        fs::create_directory(blockedFolder);
        SettingsManager blockedSettings((blockedFolder / L"settings.ini").wstring());
        Check(blockedSettings.Save(expected).ok, "Could not create baseline INI in the test folder");
        const auto originalIni = Bytes(blockedFolder / L"settings.ini");
        {
            BlockDirectoryWrites block(blockedFolder);
            const auto directPath = blockedFolder / L"direct-write-probe";
            HANDLE probe = CreateFileW(directPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (probe != INVALID_HANDLE_VALUE) CloseHandle(probe);
            Check(probe == INVALID_HANDLE_VALUE, "The test folder's write-deny ACL did not take effect");
            OutputManager deniedOutput;
            deniedOutput.SetCaptureFolder(blockedFolder.wstring());
            const auto deniedResult = deniedOutput.EnsureCaptureFolder();
            Check(!deniedResult.ok && !deniedResult.message.empty(), "Unwritable output folder was accepted");
            Check(deniedOutput.NewSessionFolder().empty(), "Scratch creation in a denied folder was reported successful");
            auto changed = expected; changed.fps = 15;
            const auto saveResult = blockedSettings.Save(changed);
            Check(!saveResult.ok && !saveResult.message.empty() && Bytes(blockedFolder / L"settings.ini") == originalIni,
                "A failed settings save changed the user's previous preferences");
            block.Restore();
        }
        Check(blockedSettings.Save(expected).ok, "The test folder's original permissions were not restored");
        std::cout << "PASS: unwritable folder is rejected; failed save preserves old INI; test ACL restored\n";

        OutputManager relative;
        relative.SetCaptureFolder(L"relative-recordings");
        Check(!relative.EnsureCaptureFolder().ok, "Relative capture folder was accepted");
        OutputManager fileAsDirectory;
        fileAsDirectory.SetCaptureFolder(settingsPath.wstring());
        Check(!fileAsDirectory.EnsureCaptureFolder().ok, "A regular file was accepted as an output folder");
        std::cout << "PASS: relative paths and file-as-folder destinations are rejected\n";
        std::cout << "All settings/output integration tests passed; no real user settings were accessed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::wcerr << L"Test artifacts retained at: " << root.wstring() << L'\n';
        return 1;
    }
}
