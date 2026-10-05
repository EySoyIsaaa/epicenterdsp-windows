#pragma once

/**
 * @file ApoIpcClient.hpp
 * @brief Win32 shared memory helper with explicit DACL for cross-session audiodg.exe access.
 */

#include "EpicenterApoIpc.h"

#include <windows.h>
#include <sddl.h>
#include <string>

namespace epicenter {
namespace apo {

class SharedMemoryChannel {
public:
    SharedMemoryChannel() = default;
    ~SharedMemoryChannel() { close(); }

    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;

    /**
     * Opens or creates the shared memory segment.
     * @param createIfMissing If true, attempts to create the mapping with custom DACL if it doesn't exist.
     */
    bool open(bool createIfMissing = true) {
        if (view_) return true;

#if defined(EPICENTER_APO_TEST_ISOLATION)
        // Test instances must never impersonate an audiodg APO to a running UI,
        // or let that UI mutate fixture controls. Production v1 IPC is unchanged.
        const auto testName = std::wstring(L"Local\\EpicenterDspIpc_Test_") + std::to_wstring(GetCurrentProcessId());
        return tryOpen(testName.c_str(), createIfMissing);
#else

        // Try Global namespace first (required for Session 0 audiodg.exe <-> user Session 1)
        if (tryOpen(EPICENTER_IPC_SHARED_MEMORY_NAME, createIfMissing)) {
            return true;
        }

        // Fallback to Local namespace (for unit tests running in a single session)
        return tryOpen(EPICENTER_IPC_LOCAL_FALLBACK_NAME, createIfMissing);
#endif
    }

    void close() {
        if (view_) {
            UnmapViewOfFile(view_);
            view_ = nullptr;
        }
        if (hMap_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
        }
        state_ = nullptr;
    }

    bool isOpen() const { return state_ != nullptr; }
    EpicenterIpcSharedState* state() { return state_; }
    const EpicenterIpcSharedState* state() const { return state_; }

private:
    bool tryOpen(const wchar_t* name, bool createIfMissing) {
        bool wasCreated = false;
        // First attempt to open existing with Read/Write access (matching GRGW in SDDL)
        hMap_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!hMap_ && createIfMissing) {
            // Create with custom security descriptor allowing Local Service and interactive users
            SECURITY_ATTRIBUTES sa;
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = FALSE;
            sa.lpSecurityDescriptor = nullptr;

            // SDDL: Allow Local Service (LS), Interactive Users (IU), Admins (BA), All App Packages (AC)
            const wchar_t* sddl = L"D:(A;;GA;;;LS)(A;;GA;;;BA)(A;;GRGW;;;IU)(A;;GRGW;;;AC)S:(ML;;NW;;;LW)";
            if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
                    sddl, SDDL_REVISION_1, &(sa.lpSecurityDescriptor), nullptr)) {
                hMap_ = CreateFileMappingW(
                    INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0,
                    sizeof(EpicenterIpcSharedState), name);
                LocalFree(sa.lpSecurityDescriptor);
            } else {
                // Fallback to default security attributes
                hMap_ = CreateFileMappingW(
                    INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                    sizeof(EpicenterIpcSharedState), name);
            }

            if (hMap_ && GetLastError() != ERROR_ALREADY_EXISTS) {
                wasCreated = true;
            }
        }

        if (!hMap_) return false;

        view_ = MapViewOfFile(hMap_, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(EpicenterIpcSharedState));
        if (!view_) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
            return false;
        }

        state_ = static_cast<EpicenterIpcSharedState*>(view_);
        if (wasCreated) {
            epicenter::ipc::initSharedState(state_);
        }
        return true;
    }

    HANDLE hMap_ = nullptr;
    void*  view_ = nullptr;
    EpicenterIpcSharedState* state_ = nullptr;
};

} // namespace apo
} // namespace epicenter
