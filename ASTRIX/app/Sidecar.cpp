#include "Sidecar.h"

#include "TextUtils.h"

#include <mutex>

namespace {
    std::once_flag g_jobOnce;
    HANDLE g_job = nullptr;

    // Всё, что в этом «задании», Windows закроет вместе с Аней
    HANDLE Job() {
        std::call_once(g_jobOnce, [] {
            g_job = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        });
        return g_job;
    }

    bool FileExists(const std::wstring& path) {
        const DWORD a = GetFileAttributesW(path.c_str());
        return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
    }
}

HANDLE Sidecar::Launch(const std::wstring& exe, const std::wstring& arguments, const std::wstring& logName) {
    SECURITY_ATTRIBUTES inherit{ sizeof(inherit), nullptr, TRUE };
    const std::wstring logPath = Text::ExecutableDirectory() + L"\\" + logName;
    const HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    std::wstring command = L"\"" + exe + L"\" " + arguments;
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = log;
    PROCESS_INFORMATION pi{};
    const std::wstring workDir = exe.substr(0, exe.find_last_of(L'\\'));
    const BOOL ok = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_SUSPENDED | BELOW_NORMAL_PRIORITY_CLASS, nullptr, workDir.c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) return nullptr;

    AssignProcessToJobObject(Job(), pi.hProcess); // до запуска: даже первую миллисекунду процесс «привязан» к Ане
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

void Sidecar::Stop(HANDLE& process) {
    if (!process) return;
    TerminateProcess(process, 0);
    WaitForSingleObject(process, 3000);
    CloseHandle(process);
    process = nullptr;
}

bool Sidecar::HasExited(HANDLE process) { return !process || WaitForSingleObject(process, 0) == WAIT_OBJECT_0; }

std::wstring Sidecar::FindUpwards(std::initializer_list<const wchar_t*> relativePaths) {
    std::wstring dir = Text::ExecutableDirectory();
    for (int level = 0; level < 5; ++level) {
        for (const wchar_t* relative : relativePaths) {
            const std::wstring path = dir + L"\\" + relative;
            if (FileExists(path)) return path;
        }
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
    }
    return {};
}
