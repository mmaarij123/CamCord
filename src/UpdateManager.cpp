#include "UpdateManager.h"
#include "AppVersion.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {
constexpr unsigned long long MAX_SETUP_BYTES = 512ull * 1024 * 1024;
constexpr wchar_t LATEST_URL[] = L"https://api.github.com/repos/mmaarij123/CamCord/releases/latest";

bool VersionParts(std::string value, std::array<unsigned int, 3>& parts) {
    if (!value.empty() && value.front() == 'v') value.erase(0, 1);
    size_t begin = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        const auto end = value.find('.', begin);
        if ((index < 2 && end == std::string::npos) || (index == 2 && end != std::string::npos)) return false;
        const auto stop = end == std::string::npos ? value.size() : end;
        if (stop == begin || stop - begin > 10) return false;
        const auto parsed = std::from_chars(value.data() + begin, value.data() + stop, parts[index]);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + stop) return false;
        begin = stop + 1;
    }
    return true;
}

bool ValidInfo(const UpdateInfo& info) {
    std::array<unsigned int, 3> parts{};
    if (!VersionParts(info.version, parts) || info.version.empty() || info.version.front() == 'v') return false;
    if (info.url != "https://github.com/mmaarij123/CamCord/releases/download/v" + info.version + "/CamCord-Setup.exe") return false;
    return info.size && info.size <= MAX_SETUP_BYTES && info.sha256.size() == 64 &&
        std::all_of(info.sha256.begin(), info.sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

struct InternetHandle {
    HINTERNET value = nullptr;
    explicit InternetHandle(HINTERNET handle) : value(handle) { if (!value) throw std::runtime_error("Network request failed"); }
    ~InternetHandle() { WinHttpCloseHandle(value); }
    InternetHandle(const InternetHandle&) = delete;
};

void Receive(const std::wstring& url, unsigned long long limit, const std::atomic<bool>& cancelled,
    const std::function<void(const BYTE*, DWORD)>& consume) {
    URL_COMPONENTS components{ sizeof(components) };
    components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components) || components.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("Invalid update URL");
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    const std::wstring agent = std::wstring(L"CamCord/") + CAMCORD_VERSION_W;
    InternetHandle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000)) throw std::runtime_error("Timeout setup failed");
    InternetHandle connection(WinHttpConnect(session.value, host.c_str(), components.nPort, 0));
    InternetHandle request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy)))
        throw std::runtime_error("Redirect policy failed");
    if (cancelled) throw std::runtime_error("Update cancelled");
    if (!WinHttpSendRequest(request.value, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",
        static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr))
        throw std::runtime_error("Could not contact update server");
    DWORD status = 0, bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX) || status != 200)
        throw std::runtime_error(status == 403 || status == 429 ? "Update server limit reached; try again later" : "Update server returned an error");
    unsigned long long total = 0;
    std::array<BYTE, 65536> buffer{};
    while (!cancelled) {
        DWORD read = 0;
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            throw std::runtime_error("Update download interrupted");
        if (!read) return;
        total += read;
        if (total > limit) throw std::runtime_error("Update response exceeded its expected size");
        consume(buffer.data(), read);
    }
    throw std::runtime_error("Update cancelled");
}

std::wstring CacheFolder() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) || !local)
        throw std::runtime_error("Update folder unavailable");
    const auto path = (std::filesystem::path(local) / L"CamCord" / L"Updates").wstring();
    CoTaskMemFree(local);
    return path;
}
}

bool UpdateManager::NewerVersion(const std::string& candidate, const std::string& current) {
    std::array<unsigned int, 3> left{}, right{};
    return VersionParts(candidate, left) && VersionParts(current, right) && left > right;
}

OperationResult UpdateManager::ParseRelease(const std::string& document, const std::string& currentVersion, UpdateInfo& info) {
    info = {};
    try {
        const auto release = nlohmann::json::parse(document);
        if (!release.is_object() || release.value("draft", true) || release.value("prerelease", true))
            return OperationResult::Failure(L"No stable update is available.");
        auto tag = release.at("tag_name").get<std::string>();
        std::array<unsigned int, 3> version{}, current{};
        if (!VersionParts(tag, version) || !VersionParts(currentVersion, current))
            return OperationResult::Failure(L"The release version could not be read.");
        if (version <= current) return OperationResult::Success(L"You're up to date.");
        if (tag.empty() || tag.front() != 'v') return OperationResult::Failure(L"The update release tag is invalid.");
        info.version = tag.substr(1);
        const auto& assets = release.at("assets");
        if (!assets.is_array()) throw std::runtime_error("Missing assets");
        size_t matches = 0;
        for (const auto& asset : assets) {
            if (asset.value("name", std::string{}) != "CamCord-Setup.exe") continue;
            ++matches;
            if (asset.value("state", std::string{}) != "uploaded" || !asset.at("size").is_number_unsigned())
                throw std::runtime_error("Incomplete asset");
            info.size = asset.at("size").get<unsigned long long>();
            info.url = asset.at("browser_download_url").get<std::string>();
            const auto digest = asset.at("digest").get<std::string>();
            if (digest.compare(0, 7, "sha256:") != 0) throw std::runtime_error("Missing SHA256");
            info.sha256 = digest.substr(7);
        }
        if (matches != 1 || !ValidInfo(info)) throw std::runtime_error("Invalid update metadata");
        info.available = true;
        return OperationResult::Success(L"A new version is available.");
    } catch (...) {
        info = {};
        return OperationResult::Failure(L"The release does not contain a complete, verified Windows setup. Try again later.");
    }
}

std::string UpdateManager::FileSha256(const std::wstring& path) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return result;
    DWORD objectBytes = 0, received = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes),
        sizeof(objectBytes), &received, 0) >= 0) {
        std::vector<BYTE> object(objectBytes);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectBytes, nullptr, 0, 0) >= 0) {
            std::ifstream file(std::filesystem::path(path), std::ios::binary);
            std::array<char, 65536> buffer{};
            bool ok = file.is_open();
            while (ok && file) {
                file.read(buffer.data(), buffer.size());
                const auto count = file.gcount();
                if (count && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) ok = false;
            }
            std::array<BYTE, 32> digest{};
            if (ok && !file.bad() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0) {
                constexpr char digits[] = "0123456789abcdef";
                for (const BYTE byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
            }
            BCryptDestroyHash(hash);
        }
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

OperationResult UpdateManager::Verify(const UpdateInfo& info, const std::wstring& installer) const {
    std::error_code ec;
    if (!ValidInfo(info) || installer.empty() || !std::filesystem::is_regular_file(installer, ec) || ec ||
        std::filesystem::file_size(installer, ec) != info.size || ec || FileSha256(installer) != info.sha256)
        return OperationResult::Failure(L"The update could not be verified. Download it again before installing.");
    return OperationResult::Success();
}

UpdateResult UpdateManager::Check(const std::string& currentVersion) {
    UpdateResult result;
    try {
        std::string document;
        Receive(LATEST_URL, 2 * 1024 * 1024, cancelled_, [&](const BYTE* data, DWORD size) {
            document.append(reinterpret_cast<const char*>(data), size);
        });
        result.result = ParseRelease(document, currentVersion, result.info);
    } catch (...) { result.result = OperationResult::Failure(L"Could not check for updates. Check your internet connection and try again later."); }
    return result;
}

UpdateResult UpdateManager::Download(const UpdateInfo& info, const std::wstring& cacheDirectory) {
    UpdateResult result{ OperationResult::Failure(L"The update could not be downloaded. Check your connection and disk space, then try again."), info, L"" };
    std::filesystem::path temporary;
    try {
        if (!ValidInfo(info)) return result;
        const auto folder = std::filesystem::path(cacheDirectory.empty() ? CacheFolder() : cacheDirectory) / info.version;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) return result;
        const auto final = folder / L"CamCord-Setup.exe";
        if (!cancelled_ && Verify(info, final.wstring()).ok) {
            progress_ = 100; result.installer = final.wstring(); result.result = OperationResult::Success(); return result;
        }
        if (cancelled_) return result;
        temporary = folder / (L"CamCord-Setup." + std::to_wstring(GetCurrentProcessId()) + L".partial");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("Update file unavailable");
            unsigned long long written = 0;
            Receive(std::wstring(info.url.begin(), info.url.end()), info.size, cancelled_, [&](const BYTE* data, DWORD size) {
                file.write(reinterpret_cast<const char*>(data), size);
                if (!file) throw std::runtime_error("Update write failed");
                written += size; progress_ = static_cast<int>(written * 100 / info.size);
            });
            file.flush();
            if (!file || written != info.size || cancelled_) throw std::runtime_error("Incomplete download");
        }
        const auto verified = Verify(info, temporary.wstring());
        if (!verified.ok) { result.result = verified; throw std::runtime_error("Checksum mismatch"); }
        if (cancelled_ || !MoveFileExW(temporary.c_str(), final.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Update publish failed");
        temporary.clear(); progress_ = 100;
        result.installer = final.wstring(); result.result = OperationResult::Success();
    } catch (...) {}
    if (!temporary.empty()) { std::error_code ec; std::filesystem::remove(temporary, ec); }
    return result;
}
