#include "FileSearch.h"

#include "Platform.h"
#include "Settings.h"
#include "TextUtils.h"

#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
    std::wstring Everything() {
        wchar_t path[MAX_PATH] = {};
        for (const wchar_t* variable : { L"ProgramFiles", L"ProgramFiles(x86)" })
            if (GetEnvironmentVariableW(variable, path, MAX_PATH)) {
                const std::wstring full = std::wstring(path) + L"\\Everything\\Everything.exe";
                if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES) return full;
            }
        if (SearchPathW(nullptr, L"Everything.exe", nullptr, MAX_PATH, path, nullptr)) return path;
        return {};
    }

    // Свой поиск: папки пользователя, не дольше нескольких секунд
    std::optional<fs::path> FindOwn(const std::wstring& query) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        for (const KNOWNFOLDERID* id : { &FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures, &FOLDERID_Videos, &FOLDERID_Music }) {
            PWSTR folder = nullptr;
            if (FAILED(SHGetKnownFolderPath(*id, 0, nullptr, &folder))) continue;
            const fs::path root = folder;
            CoTaskMemFree(folder);
            std::error_code ec;
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                if (std::chrono::steady_clock::now() > deadline) return std::nullopt;
                if (Text::LowerRu(it->path().filename().wstring()).find(query) != std::wstring::npos) return it->path();
            }
        }
        return std::nullopt;
    }
}

std::optional<std::string> FileSearch::Handle(const std::string& command) {
    const std::wstring text = Text::LowerRu(Text::ToWide(Text::Trim(command)));
    std::wstring query;
    for (const wchar_t* prefix : { L"найди файл ", L"найди папку ", L"поищи файл ", L"где файл ", L"где папка " })
        if (text.rfind(prefix, 0) == 0) query = text.substr(wcslen(prefix));
    if (query.empty()) return std::nullopt;

    const std::string engine = Config::Value("search", "files", "0");
    if (engine == "1") {
        const std::wstring url = L"search-ms:query=" + Text::ToWide(Text::UrlEncode(Text::ToUtf8(query)));
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return "Открыла поиск Windows";
    }
    if (engine == "2") {
        const std::wstring exe = Everything();
        if (exe.empty()) return "Программа Everything не установлена — поменяй способ поиска в настройках";
        const std::wstring arguments = L"-search \"" + query + L"\"";
        ShellExecuteW(nullptr, L"open", exe.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL);
        return "Открыла поиск в Everything";
    }
    if (const auto found = FindOwn(query)) {
        const std::wstring arguments = L"/select,\"" + found->wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
        return "Нашла: " + Text::ToUtf8(found->filename().wstring());
    }
    return "Не нашла такой файл в твоих папках";
}
