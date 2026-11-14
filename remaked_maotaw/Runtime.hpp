#pragma once

// external overlay + aim/rcs for cs2
// read-only memory, mouse_event for movement

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <condition_variable>
#include <functional>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>
#include <fstream>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "output/offsets.hpp"
#include "output/client_dll.hpp"
#include "weapon_icons.hpp"

#pragma comment(lib, "Psapi.lib")

namespace MaotawRuntime {

    namespace OffsetFile {
        [[nodiscard]] inline std::filesystem::path ExeDir() {
            wchar_t modulePath[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
            if (!n || n >= MAX_PATH) return std::filesystem::current_path();
            return std::filesystem::path(modulePath).parent_path();
        }

        inline void ApplyKey(const std::string& key, std::ptrdiff_t value) {
            using namespace cs2_dumper::offsets::client_dll;
            using namespace cs2_dumper::schemas::client_dll;

            if (key == "dwEntityList") dwEntityList = value;
            else if (key == "dwLocalPlayerPawn") dwLocalPlayerPawn = value;
            else if (key == "dwViewMatrix") dwViewMatrix = value;
            else if (key == "dwViewAngles") dwViewAngles = value;
            else if (key == "dwLocalPlayerController") dwLocalPlayerController = value;
            else if (key == "dwGlobalVars") dwGlobalVars = value;
            else if (key == "dwCSGOInput") dwCSGOInput = value;
            else if (key == "m_iszPlayerName") CBasePlayerController::m_iszPlayerName = value;
            else if (key == "m_hPlayerPawn") CCSPlayerController::m_hPlayerPawn = value;
            else if (key == "m_hActiveWeapon") CPlayer_WeaponServices::m_hActiveWeapon = value;
            else if (key == "m_modelState") CSkeletonInstance::m_modelState = value;
            else if (key == "m_Item") C_AttributeContainer::m_Item = value;
            else if (key == "m_pGameSceneNode") C_BaseEntity::m_pGameSceneNode = value;
            else if (key == "m_iHealth") C_BaseEntity::m_iHealth = value;
            else if (key == "m_iTeamNum") C_BaseEntity::m_iTeamNum = value;
            else if (key == "m_pWeaponServices") C_BasePlayerPawn::m_pWeaponServices = value;
            else if (key == "m_vOldOrigin") C_BasePlayerPawn::m_vOldOrigin = value;
            else if (key == "m_ArmorValue") C_CSPlayerPawn::m_ArmorValue = value;
            else if (key == "m_iShotsFired") C_CSPlayerPawn::m_iShotsFired = value;
            else if (key == "m_pAimPunchServices") C_CSPlayerPawn::m_pAimPunchServices = value;
            else if (key == "m_entitySpottedState") C_CSPlayerPawnBase::m_entitySpottedState = value;
            else if (key == "m_AttributeManager") C_EconEntity::m_AttributeManager = value;
            else if (key == "m_iItemDefinitionIndex") C_EconItemView::m_iItemDefinitionIndex = value;
            else if (key == "m_bSpotted") EntitySpottedState_t::m_bSpotted = value;
            else if (key == "m_aimPunchCache") CCSPlayer_AimPunchServices::m_aimPunchCache = value;
            else if (key == "m_bDormant") AimExtras::m_bDormant = value;
            else if (key == "m_vecViewOffset") AimExtras::m_vecViewOffset = value;
        }

        // pull name + value out of lines like:
        //   constexpr std::ptrdiff_t dwEntityList = 0x2709010;
        //   inline std::ptrdiff_t m_iHealth = 0x34C;
        //   public const nint dwLocalPlayerPawn = 0x2553D38;
        inline void ParseHppLine(const std::string& line) {
            auto isIdent = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_';
            };

            // find "= 0x..." or "= 123"
            const auto eq = line.find('=');
            if (eq == std::string::npos) return;

            std::size_t v0 = eq + 1;
            while (v0 < line.size() && (line[v0] == ' ' || line[v0] == '\t')) ++v0;
            if (v0 >= line.size()) return;

            std::size_t v1 = v0;
            if (v1 + 1 < line.size() && line[v1] == '0' && (line[v1 + 1] == 'x' || line[v1 + 1] == 'X'))
                v1 += 2;
            while (v1 < line.size() && (
                (line[v1] >= '0' && line[v1] <= '9') ||
                (line[v1] >= 'a' && line[v1] <= 'f') ||
                (line[v1] >= 'A' && line[v1] <= 'F')))
                ++v1;
            if (v1 == v0) return;

            std::ptrdiff_t value = 0;
            try {
                value = static_cast<std::ptrdiff_t>(std::stoull(line.substr(v0, v1 - v0), nullptr, 0));
            } catch (...) {
                return;
            }

            // walk left of '=' for the identifier
            std::size_t e = eq;
            while (e > 0 && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
            if (e == 0) return;
            std::size_t s = e;
            while (s > 0 && isIdent(line[s - 1])) --s;
            if (s == e) return;

            ApplyKey(line.substr(s, e - s), value);
        }

        inline bool ParseHppFile(const std::filesystem::path& path) {
            std::ifstream in(path);
            if (!in) return false;
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                    line.pop_back();
                if (line.empty()) continue;
                // skip comments
                const auto slash = line.find("//");
                if (slash != std::string::npos) line = line.substr(0, slash);
                if (line.find("ptrdiff_t") == std::string::npos &&
                    line.find("nint") == std::string::npos &&
                    line.find("intptr") == std::string::npos &&
                    line.find("constexpr") == std::string::npos &&
                    line.find("inline") == std::string::npos)
                {
                    // still try plain assignments in case of minimal dumps
                    if (line.find('=') == std::string::npos) continue;
                }
                ParseHppLine(line);
            }
            return true;
        }

        // returns false if output/ (or required hpp files) are missing
        [[nodiscard]] inline bool Load() {
            const auto dir = ExeDir() / L"output";
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec))
                return false;

            const auto offsetsHpp = dir / L"offsets.hpp";
            const auto clientHpp = dir / L"client_dll.hpp";
            const bool hasOffsets = std::filesystem::is_regular_file(offsetsHpp, ec);
            const bool hasClient = std::filesystem::is_regular_file(clientHpp, ec);
            if (!hasOffsets || !hasClient)
                return false;

            // parse required dumps; fail if either cannot be opened
            if (!ParseHppFile(offsetsHpp) || !ParseHppFile(clientHpp))
                return false;

            // optional extras
            ParseHppFile(dir / L"offsets.h");
            ParseHppFile(dir / L"client_dll.h");
            ParseHppFile(dir / L"offsets.ini");
            return true;
        }
    }

    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    struct Vec3 {
        float x{};
        float y{};
        float z{};
    };

    struct Matrix4x4 {
        float m[4][4]{};
    };

    namespace Memory {
        inline DWORD pid = 0;
        inline HANDLE process = nullptr;
        inline uintptr_t client = 0;
        inline TimePoint nextAttachTry{};
        inline TimePoint nextAliveCheck{};

        inline void Detach() {
            if (process) CloseHandle(process);
            process = nullptr;
            pid = 0;
            client = 0;
            nextAliveCheck = {};
        }

        [[nodiscard]] inline DWORD FindProcess(const wchar_t* name) {
            const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snap == INVALID_HANDLE_VALUE) return 0;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            DWORD result = 0;

            if (Process32FirstW(snap, &entry)) {
                do {
                    if (_wcsicmp(entry.szExeFile, name) == 0) {
                        result = entry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snap, &entry));
            }

            CloseHandle(snap);
            return result;
        }

        [[nodiscard]] inline uintptr_t FindModule(DWORD targetPid,
                                                   const wchar_t* moduleName) {
            const HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                         FALSE, targetPid);
            if (!h) return 0;

            HMODULE modules[1024]{};
            DWORD needed = 0;
            uintptr_t result = 0;

            if (EnumProcessModulesEx(h, modules, sizeof(modules), &needed,
                                     LIST_MODULES_ALL)) {
                const DWORD count = (std::min)(
                    needed / static_cast<DWORD>(sizeof(HMODULE)),
                    static_cast<DWORD>(std::size(modules)));

                for (DWORD i = 0; i < count; ++i) {
                    wchar_t name[MAX_PATH]{};
                    if (GetModuleBaseNameW(h, modules[i], name, MAX_PATH) &&
                        _wcsicmp(name, moduleName) == 0) {
                        result = reinterpret_cast<uintptr_t>(modules[i]);
                        break;
                    }
                }
            }

            CloseHandle(h);
            return result;
        }

        inline bool Attach() {
            Detach();

            pid = FindProcess(L"cs2.exe");
            if (!pid) return false;

            process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
                                  FALSE, pid);
            if (!process) {
                pid = 0;
                return false;
            }

            client = FindModule(pid, L"client.dll");
            if (!client) {
                Detach();
                return false;
            }

            return true;
        }

        [[nodiscard]] inline bool TryReadRaw(uintptr_t address,
                                             void* destination,
                                             std::size_t size) {
            if (!process || !address || !destination || !size) return false;
            SIZE_T read = 0;
            return ReadProcessMemory(process,
                                     reinterpret_cast<const void*>(address),
                                     destination, size, &read) &&
                   read == size;
        }

        template <typename T>
        [[nodiscard]] inline bool TryRead(uintptr_t address, T& out) {
            out = {};
            return TryReadRaw(address, &out, sizeof(T));
        }

        template <typename T>
        [[nodiscard]] inline T Read(uintptr_t address) {
            T value{};
            TryRead(address, value);
            return value;
        }

        inline bool EnsureAttached() {
            const auto now = Clock::now();

            if (process && client) {
                // Avoid a process-state syscall at render cadence. The handle is
                // only revalidated twice per second, which is more than enough
                // to detect CS2 closing while keeping the hot path lean.
                if (now < nextAliveCheck) return true;
                nextAliveCheck = now + std::chrono::milliseconds(500);

                DWORD exitCode = 0;
                if (GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE)
                    return true;
                Detach();
            }

            if (now < nextAttachTry) return false;
            nextAttachTry = now + std::chrono::seconds(1);
            const bool attached = Attach();
            if (attached) nextAliveCheck = Clock::now() + std::chrono::milliseconds(500);
            return attached;
        }
    }

    namespace WorkerPool {
        using Job = std::function<void(std::size_t, std::size_t)>;

        inline std::vector<std::thread> workers;
        inline std::mutex mutex;
        inline std::condition_variable wakeCv;
        inline std::condition_variable doneCv;
        inline Job currentJob;
        inline std::size_t currentCount = 0;
        inline unsigned activeWorkers = 0;
        inline unsigned completedWorkers = 0;
        inline std::uint64_t generation = 0;
        inline bool stopping = false;
        inline unsigned hardwareThreads = 1;

        [[nodiscard]] inline unsigned RecommendedWorkerCount() {
            const unsigned hw = (std::max)(1u, std::thread::hardware_concurrency());
            // Keep at least two logical processors free for the game/OS. Small
            // CPUs stay serial because a pool would cost more than it saves.
            if (hw < 4) return 0;
            if (hw < 8) return 1;
            if (hw < 16) return 2;
            return 3;
        }

        inline void RangeFor(unsigned id, unsigned participants,
                             std::size_t count, std::size_t& begin,
                             std::size_t& end) {
            const std::size_t base = count / participants;
            const std::size_t extra = count % participants;
            begin = static_cast<std::size_t>(id) * base +
                    (std::min)(static_cast<std::size_t>(id), extra);
            end = begin + base + (static_cast<std::size_t>(id) < extra ? 1u : 0u);
        }

        inline void WorkerMain(unsigned id) {
            std::uint64_t seenGeneration = 0;
            for (;;) {
                Job job;
                std::size_t count = 0;
                unsigned participants = 1;
                unsigned active = 0;

                {
                    std::unique_lock lock(mutex);
                    wakeCv.wait(lock, [&] {
                        return stopping || generation != seenGeneration;
                    });
                    if (stopping) return;

                    seenGeneration = generation;
                    active = activeWorkers;
                    if (id >= active) continue;

                    job = currentJob;
                    count = currentCount;
                    participants = active + 1;
                }

                std::size_t begin = 0, end = 0;
                RangeFor(id, participants, count, begin, end);
                if (begin < end && job) job(begin, end);

                {
                    std::lock_guard lock(mutex);
                    ++completedWorkers;
                    if (completedWorkers >= activeWorkers)
                        doneCv.notify_one();
                }
            }
        }

        inline void Start() {
            if (!workers.empty()) return;
            hardwareThreads = (std::max)(1u, std::thread::hardware_concurrency());
            const unsigned count = RecommendedWorkerCount();
            if (!count) return;

            stopping = false;
            workers.reserve(count);
            for (unsigned i = 0; i < count; ++i)
                workers.emplace_back(WorkerMain, i);
        }

        inline void Stop() {
            {
                std::lock_guard lock(mutex);
                stopping = true;
                ++generation;
            }
            wakeCv.notify_all();
            for (auto& worker : workers) {
                if (worker.joinable()) worker.join();
            }
            workers.clear();
            currentJob = {};
            activeWorkers = 0;
            completedWorkers = 0;
            stopping = false;
        }

        [[nodiscard]] inline unsigned TotalThreads() {
            // Includes the calling/render thread, which participates in each job.
            return static_cast<unsigned>(workers.size()) + 1u;
        }

        template <typename Fn>
        inline void RunRanges(std::size_t count, std::size_t minItemsPerThread,
                              Fn&& fn) {
            if (!count) return;
            if (workers.empty() || count < minItemsPerThread * 2u) {
                fn(0, count);
                return;
            }

            const std::size_t wantedParticipants =
                (std::max)(std::size_t{1},
                           (count + minItemsPerThread - 1u) / minItemsPerThread);
            const unsigned workerLimit = static_cast<unsigned>(workers.size());
            const unsigned useWorkers = (std::min)(
                workerLimit,
                wantedParticipants > 0
                    ? static_cast<unsigned>(wantedParticipants - 1u)
                    : 0u);

            if (!useWorkers) {
                fn(0, count);
                return;
            }

            Job localJob = std::forward<Fn>(fn);
            {
                std::lock_guard lock(mutex);
                currentJob = localJob;
                currentCount = count;
                activeWorkers = useWorkers;
                completedWorkers = 0;
                ++generation;
            }
            wakeCv.notify_all();

            const unsigned participants = useWorkers + 1u;
            std::size_t begin = 0, end = 0;
            RangeFor(useWorkers, participants, count, begin, end);
            if (begin < end) localJob(begin, end);

            std::unique_lock lock(mutex);
            doneCv.wait(lock, [&] { return completedWorkers >= activeWorkers; });
            currentJob = {};
        }
    }

    namespace Performance {
        inline TimePoint lastPresented{};
        inline float fps = 0.0f;
        inline float frameMs = 0.0f;
        inline float workMs = 0.0f;

        inline void Reset() {
            lastPresented = {};
            fps = 0.0f;
            frameMs = 0.0f;
            workMs = 0.0f;
        }

        inline void Record(TimePoint started, TimePoint presented) {
            const float work = std::chrono::duration<float, std::milli>(
                presented - started).count();
            constexpr float alpha = 0.12f;
            workMs = workMs <= 0.0f ? work : workMs + (work - workMs) * alpha;

            if (lastPresented != TimePoint{}) {
                const float delta = std::chrono::duration<float, std::milli>(
                    presented - lastPresented).count();
                if (delta > 0.01f && delta < 1000.0f) {
                    const float instantFps = 1000.0f / delta;
                    fps = fps <= 0.0f ? instantFps : fps + (instantFps - fps) * alpha;
                    frameMs = frameMs <= 0.0f ? delta : frameMs + (delta - frameMs) * alpha;
                }
            }
            lastPresented = presented;
        }
    }

    namespace Surface {
        inline constexpr wchar_t ClassName[] = L"MaotawESPOverlay";
        inline HINSTANCE instance = nullptr;
        inline HWND window = nullptr;
        inline HDC memoryDc = nullptr;
        inline HBITMAP bitmap = nullptr;
        inline HGDIOBJ oldBitmap = nullptr;
        inline uint32_t* pixels = nullptr;
        inline int width = 0;
        inline int height = 0;
        inline float halfWidth = 0.0f;
        inline float halfHeight = 0.0f;
        inline int screenX = 0;
        inline int screenY = 0;
        inline HWND targetWindow = nullptr;
        inline TimePoint nextTargetSearch{};
        inline TimePoint nextTargetValidate{};
        inline TimePoint nextGeometrySync{};
        inline bool forceFullPresent = false;

        struct DirtyRect {
            int l = 0, t = 0, r = 0, b = 0;
            bool valid = false;
        };

        inline DirtyRect previous{};
        inline DirtyRect current{};

        inline void MarkDirty(int l, int t, int r, int b) {
            if (width <= 0 || height <= 0) return;
            l = std::clamp(l, 0, width);
            t = std::clamp(t, 0, height);
            r = std::clamp(r, 0, width);
            b = std::clamp(b, 0, height);
            if (l >= r || t >= b) return;

            if (!current.valid) {
                current = { l, t, r, b, true };
                return;
            }
            current.l = (std::min)(current.l, l);
            current.t = (std::min)(current.t, t);
            current.r = (std::max)(current.r, r);
            current.b = (std::max)(current.b, b);
        }

        [[nodiscard]] inline DirtyRect Union(const DirtyRect& a,
                                             const DirtyRect& b) {
            if (!a.valid) return b;
            if (!b.valid) return a;
            return {
                (std::min)(a.l, b.l),
                (std::min)(a.t, b.t),
                (std::max)(a.r, b.r),
                (std::max)(a.b, b.b),
                true
            };
        }

        inline void ClearRect(const DirtyRect& d) {
            if (!pixels || !d.valid) return;
            for (int y = d.t; y < d.b; ++y) {
                std::memset(pixels + static_cast<size_t>(y) * width + d.l,
                            0, static_cast<size_t>(d.r - d.l) * sizeof(uint32_t));
            }
        }

        inline LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        inline void ReleaseBitmap() {
            if (memoryDc && oldBitmap) SelectObject(memoryDc, oldBitmap);
            if (bitmap) DeleteObject(bitmap);
            if (memoryDc) DeleteDC(memoryDc);
            oldBitmap = nullptr;
            bitmap = nullptr;
            memoryDc = nullptr;
            pixels = nullptr;
            width = 0;
            height = 0;
            halfWidth = 0.0f;
            halfHeight = 0.0f;
            previous = {};
            current = {};
        }

        inline bool CreateBitmap(int w, int h) {
            ReleaseBitmap();
            if (w <= 0 || h <= 0) return false;

            HDC screen = GetDC(nullptr);
            if (!screen) return false;

            memoryDc = CreateCompatibleDC(screen);

            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = w;
            info.bmiHeader.biHeight = -h;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;

            bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                      reinterpret_cast<void**>(&pixels), nullptr, 0);
            ReleaseDC(nullptr, screen);

            if (!memoryDc || !bitmap || !pixels) {
                ReleaseBitmap();
                return false;
            }

            oldBitmap = SelectObject(memoryDc, bitmap);
            if (!oldBitmap || oldBitmap == HGDI_ERROR) {
                ReleaseBitmap();
                return false;
            }

            width = w;
            height = h;
            halfWidth = static_cast<float>(w) * 0.5f;
            halfHeight = static_cast<float>(h) * 0.5f;
            std::memset(pixels, 0,
                        static_cast<size_t>(width) * height * sizeof(uint32_t));
            return true;
        }

        inline bool Initialize(HINSTANCE appInstance) {
            if (window) return true;

            instance = appInstance;
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = WndProc;
            wc.hInstance = instance;
            wc.lpszClassName = ClassName;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

            if (!RegisterClassExW(&wc) &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;

            window = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED |
                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                ClassName, L"", WS_POPUP,
                0, 0, 1, 1,
                nullptr, nullptr, instance, nullptr);

            if (!window) return false;
            // shown once we have a game window
            return true;
        }

        inline void Hide() {
            if (window && IsWindowVisible(window))
                ShowWindow(window, SW_HIDE);
            // force full present next time we show so nothing stale sticks
            forceFullPresent = true;
        }

        inline void Shutdown() {
            ReleaseBitmap();
            if (window) DestroyWindow(window);
            if (instance) UnregisterClassW(ClassName, instance);
            window = nullptr;
            instance = nullptr;
            targetWindow = nullptr;
            nextTargetSearch = {};
            nextTargetValidate = {};
            nextGeometrySync = {};
            forceFullPresent = false;
        }

        inline BOOL CALLBACK FindTargetWindowProc(HWND hwnd, LPARAM param) {
            DWORD windowPid = 0;
            GetWindowThreadProcessId(hwnd, &windowPid);
            if (windowPid != Memory::pid || !IsWindowVisible(hwnd)) return TRUE;

            RECT client{};
            if (!GetClientRect(hwnd, &client)) return TRUE;
            // Accept genuinely small/windowed game clients too. The overlay backing
            // bitmap is created to this exact client size, not to the monitor size.
            if (client.right - client.left < 64 || client.bottom - client.top < 64)
                return TRUE;

            auto* out = reinterpret_cast<HWND*>(param);
            *out = hwnd;
            return FALSE;
        }

        [[nodiscard]] inline HWND FindTargetWindow() {
            if (!Memory::pid) {
                targetWindow = nullptr;
                return nullptr;
            }

            const auto now = Clock::now();
            if (targetWindow && now < nextTargetValidate)
                return targetWindow;

            if (targetWindow && IsWindow(targetWindow) && IsWindowVisible(targetWindow)) {
                DWORD windowPid = 0;
                GetWindowThreadProcessId(targetWindow, &windowPid);
                if (windowPid == Memory::pid) {
                    nextTargetValidate = now + std::chrono::milliseconds(500);
                    return targetWindow;
                }
            }

            targetWindow = nullptr;
            if (now < nextTargetSearch) return nullptr;
            nextTargetSearch = now + std::chrono::milliseconds(750);

            HWND result = nullptr;
            EnumWindows(FindTargetWindowProc, reinterpret_cast<LPARAM>(&result));
            targetWindow = result;
            nextTargetValidate = now + std::chrono::milliseconds(500);
            return targetWindow;
        }

        [[nodiscard]] inline bool IsGameForeground() {
            const HWND game = FindTargetWindow();
            if (!game || IsIconic(game) || !IsWindowVisible(game))
                return false;

            const HWND foreground = GetForegroundWindow();
            if (!foreground)
                return false;

            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(foreground, &foregroundPid);
            if (foregroundPid == 0)
                return false;

            // cs2 focused, or our menu/overlay focused
            if (foregroundPid == Memory::pid)
                return true;

            // our process (menu)
            static const DWORD selfPid = GetCurrentProcessId();
            if (foregroundPid == selfPid)
                return true;

            return false;
        }

        inline bool SyncToGameWindow(bool forceGeometryRefresh = false) {
            const HWND game = FindTargetWindow();
            if (!game) return false;

            // geometry barely moves compared to entities, so don't spam SetWindowPos
            const auto now = Clock::now();
            if (!forceGeometryRefresh && IsWindowVisible(window) &&
                width > 0 && height > 0 && now < nextGeometrySync)
                return true;
            nextGeometrySync = now + std::chrono::milliseconds(100);

            RECT rc{};
            if (!GetClientRect(game, &rc)) return false;
            POINT pt{ rc.left, rc.top };
            if (!ClientToScreen(game, &pt)) return false;

            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;
            if (w <= 0 || h <= 0) return false;

            const bool resized = w != width || h != height;
            const bool moved = pt.x != screenX || pt.y != screenY;
            if (resized && !CreateBitmap(w, h)) return false;

            screenX = pt.x;
            screenY = pt.y;
            if (resized || moved || !IsWindowVisible(window)) {
                SetWindowPos(window, HWND_TOPMOST, screenX, screenY, width, height,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            return true;
        }

        [[nodiscard]] inline uint32_t Premultiply(uint8_t r, uint8_t g,
                                                   uint8_t b, uint8_t a) {
            const uint32_t pr = static_cast<uint32_t>(r) * a / 255u;
            const uint32_t pg = static_cast<uint32_t>(g) * a / 255u;
            const uint32_t pb = static_cast<uint32_t>(b) * a / 255u;
            return (static_cast<uint32_t>(a) << 24) |
                   (pr << 16) | (pg << 8) | pb;
        }

        [[nodiscard]] inline uint32_t FromHSV(const HSV& hsv,
                                              uint8_t alpha = 255) {
            const float h = hsv.h * 6.0f;
            const int i = static_cast<int>(std::floor(h)) % 6;
            const float f = h - std::floor(h);
            const float p = hsv.v * (1.0f - hsv.s);
            const float q = hsv.v * (1.0f - f * hsv.s);
            const float t = hsv.v * (1.0f - (1.0f - f) * hsv.s);

            float r{}, g{}, b{};
            switch (i) {
            case 0: r = hsv.v; g = t; b = p; break;
            case 1: r = q; g = hsv.v; b = p; break;
            case 2: r = p; g = hsv.v; b = t; break;
            case 3: r = p; g = q; b = hsv.v; break;
            case 4: r = t; g = p; b = hsv.v; break;
            default:r = hsv.v; g = p; b = q; break;
            }

            return Premultiply(
                static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
                static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f),
                alpha);
        }

        inline void PutPixel(int x, int y, uint32_t color) {
            if (!pixels || x < 0 || y < 0 || x >= width || y >= height) return;
            pixels[static_cast<size_t>(y) * width + x] = color;
            MarkDirty(x, y, x + 1, y + 1);
        }

        inline void FillRectPixels(int x, int y, int w, int h, uint32_t color) {
            if (!pixels || w <= 0 || h <= 0) return;
            const int x1 = std::clamp(x, 0, width);
            const int y1 = std::clamp(y, 0, height);
            const int x2 = std::clamp(x + w, 0, width);
            const int y2 = std::clamp(y + h, 0, height);
            if (x1 >= x2 || y1 >= y2) return;

            for (int yy = y1; yy < y2; ++yy) {
                uint32_t* row = pixels + static_cast<size_t>(yy) * width + x1;
                std::fill(row, row + (x2 - x1), color);
            }
        }

        inline void FillRect(int x, int y, int w, int h, uint32_t color) {
            if (!pixels || w <= 0 || h <= 0) return;
            const int x1 = std::clamp(x, 0, width);
            const int y1 = std::clamp(y, 0, height);
            const int x2 = std::clamp(x + w, 0, width);
            const int y2 = std::clamp(y + h, 0, height);
            if (x1 >= x2 || y1 >= y2) return;
            FillRectPixels(x1, y1, x2 - x1, y2 - y1, color);
            MarkDirty(x1, y1, x2, y2);
        }

        inline void FillRoundedRectElliptic(int x, int y, int w, int h,
                                             int radiusX, int radiusY,
                                             uint32_t color) {
            if (!pixels || w <= 0 || h <= 0) return;
            radiusX = std::clamp(radiusX, 0, (w + 1) / 2);
            radiusY = std::clamp(radiusY, 0, (h + 1) / 2);
            if (!radiusX || !radiusY) {
                FillRect(x, y, w, h, color);
                return;
            }

            const int middleY = y + radiusY;
            const int middleH = h - radiusY * 2;
            if (middleH > 0)
                FillRectPixels(x, middleY, w, middleH, color);

            for (int row = 0; row < radiusY; ++row) {
                const float dy = (static_cast<float>(radiusY - row) - 0.5f) /
                                 static_cast<float>(radiusY);
                const float span = static_cast<float>(radiusX) *
                    std::sqrt((std::max)(0.0f, 1.0f - dy * dy));
                const int inset = std::clamp(
                    static_cast<int>(std::ceil(static_cast<float>(radiusX) - span)),
                    0, radiusX);
                const int lineW = (std::max)(1, w - inset * 2);
                const int lineX = x + (w - lineW) / 2;
                FillRectPixels(lineX, y + row, lineW, 1, color);
                FillRectPixels(lineX, y + h - row - 1, lineW, 1, color);
            }
            MarkDirty(x, y, x + w, y + h);
        }

        [[nodiscard]] inline uint32_t BlendColor(float value, uint32_t lowColor, uint32_t highColor) {
            value = std::clamp(value, 0.0f, 1.0f);
            const int lr = static_cast<int>((lowColor >> 16) & 0xFF);
            const int lg = static_cast<int>((lowColor >> 8) & 0xFF);
            const int lb = static_cast<int>(lowColor & 0xFF);
            const int hr = static_cast<int>((highColor >> 16) & 0xFF);
            const int hg = static_cast<int>((highColor >> 8) & 0xFF);
            const int hb = static_cast<int>(highColor & 0xFF);
            const int red = static_cast<int>(lr + (hr - lr) * value);
            const int green = static_cast<int>(lg + (hg - lg) * value);
            const int blue = static_cast<int>(lb + (hb - lb) * value);
            return Premultiply(static_cast<uint8_t>(red),
                               static_cast<uint8_t>(green),
                               static_cast<uint8_t>(blue), 255);
        }

        [[nodiscard]] inline uint32_t HealthGradient(float value, uint32_t topColor) {
            return BlendColor(value, Premultiply(255, 35, 35, 255), topColor);
        }

        inline bool ClipLineToSurface(int& x0, int& y0, int& x1, int& y1) {
            if (width <= 0 || height <= 0) return false;
            constexpr int Left = 1, Right = 2, Top = 4, Bottom = 8;
            const int maxX = width - 1;
            const int maxY = height - 1;

            auto code = [&](int x, int y) {
                int c = 0;
                if (x < 0) c |= Left;
                else if (x > maxX) c |= Right;
                if (y < 0) c |= Top;
                else if (y > maxY) c |= Bottom;
                return c;
            };

            int c0 = code(x0, y0);
            int c1 = code(x1, y1);
            for (int iteration = 0; iteration < 8; ++iteration) {
                if (!(c0 | c1)) return true;
                if (c0 & c1) return false;

                const int out = c0 ? c0 : c1;
                double x = 0.0, y = 0.0;
                if (out & Top) {
                    if (y1 == y0) return false;
                    y = 0.0;
                    x = x0 + static_cast<double>(x1 - x0) * (0.0 - y0) /
                             static_cast<double>(y1 - y0);
                } else if (out & Bottom) {
                    if (y1 == y0) return false;
                    y = static_cast<double>(maxY);
                    x = x0 + static_cast<double>(x1 - x0) * (maxY - y0) /
                             static_cast<double>(y1 - y0);
                } else if (out & Right) {
                    if (x1 == x0) return false;
                    x = static_cast<double>(maxX);
                    y = y0 + static_cast<double>(y1 - y0) * (maxX - x0) /
                             static_cast<double>(x1 - x0);
                } else {
                    if (x1 == x0) return false;
                    x = 0.0;
                    y = y0 + static_cast<double>(y1 - y0) * (0.0 - x0) /
                             static_cast<double>(x1 - x0);
                }

                const int nx = static_cast<int>(std::lround(x));
                const int ny = static_cast<int>(std::lround(y));
                if (out == c0) {
                    x0 = nx; y0 = ny; c0 = code(x0, y0);
                } else {
                    x1 = nx; y1 = ny; c1 = code(x1, y1);
                }
            }
            return false;
        }

        inline void Line(int x0, int y0, int x1, int y1,
                         uint32_t color, int thickness = 1) {
            if (!pixels) return;
            thickness = std::clamp(thickness, 1, 5);
            if (!ClipLineToSurface(x0, y0, x1, y1)) return;

            const int startX = x0;
            const int startY = y0;
            const int endX = x1;
            const int endY = y1;
            const int dx = std::abs(x1 - x0);
            const int sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0);
            const int sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;

            if (thickness == 1) {
                for (;;) {
                    pixels[static_cast<size_t>(y0) * width + x0] = color;
                    if (x0 == x1 && y0 == y1) break;
                    const int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; x0 += sx; }
                    if (e2 <= dx) { err += dx; y0 += sy; }
                }
                MarkDirty((std::min)(startX, endX),
                          (std::min)(startY, endY),
                          (std::max)(startX, endX) + 1,
                          (std::max)(startY, endY) + 1);
                return;
            }

            const int radius = thickness / 2;
            for (;;) {
                const int yMin = (std::max)(0, y0 - radius);
                const int yMax = (std::min)(height - 1, y0 + radius);
                const int xMin = (std::max)(0, x0 - radius);
                const int xMax = (std::min)(width - 1, x0 + radius);
                for (int yy = yMin; yy <= yMax; ++yy) {
                    uint32_t* row = pixels + static_cast<size_t>(yy) * width;
                    std::fill(row + xMin, row + xMax + 1, color);
                }
                if (x0 == x1 && y0 == y1) break;
                const int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }

            MarkDirty((std::min)(startX, endX) - radius - 1,
                      (std::min)(startY, endY) - radius - 1,
                      (std::max)(startX, endX) + radius + 2,
                      (std::max)(startY, endY) + radius + 2);
        }

        [[nodiscard]] inline uint32_t ScalePremultipliedAlpha(uint32_t color, float opacity) {
            opacity = std::clamp(opacity, 0.0f, 1.0f);
            const auto scale = [opacity](uint32_t channel) {
                return static_cast<uint32_t>(std::clamp(
                    static_cast<int>(std::lround(static_cast<float>(channel) * opacity)), 0, 255));
            };
            const uint32_t a = scale((color >> 24) & 0xFFu);
            const uint32_t r = scale((color >> 16) & 0xFFu);
            const uint32_t g = scale((color >> 8) & 0xFFu);
            const uint32_t b = scale(color & 0xFFu);
            return (a << 24) | (r << 16) | (g << 8) | b;
        }

        inline void LineSmooth(int x0, int y0, int x1, int y1,
                               uint32_t color, float thickness) {
            thickness = std::clamp(thickness, 1.0f, 5.0f);
            const int base = std::clamp(static_cast<int>(std::floor(thickness)), 1, 5);
            const float fraction = thickness - static_cast<float>(base);
            if (fraction <= 0.001f || base >= 5) {
                Line(x0, y0, x1, y1, color, base);
                return;
            }
            // Draw the partial outer pixel band first, then the solid inner stroke.
            Line(x0, y0, x1, y1, ScalePremultipliedAlpha(color, fraction), base + 1);
            Line(x0, y0, x1, y1, color, base);
        }

        inline void OutlineBoxSmooth(int x, int y, int w, int h,
                                     uint32_t color, float thickness) {
            if (w <= 0 || h <= 0) return;
            thickness = std::clamp(thickness, 1.0f, 4.0f);
            const int base = std::clamp(static_cast<int>(std::floor(thickness)), 1, 4);
            const float fraction = thickness - static_cast<float>(base);
            FillRect(x, y, w, base, color);
            FillRect(x, y + h - base, w, base, color);
            FillRect(x, y, base, h, color);
            FillRect(x + w - base, y, base, h, color);
            if (fraction > 0.001f && w > base * 2 && h > base * 2) {
                const uint32_t soft = ScalePremultipliedAlpha(color, fraction);
                FillRect(x + base, y + base, w - base * 2, 1, soft);
                FillRect(x + base, y + h - base - 1, w - base * 2, 1, soft);
                FillRect(x + base, y + base + 1, 1, h - base * 2 - 2, soft);
                FillRect(x + w - base - 1, y + base + 1, 1, h - base * 2 - 2, soft);
            }
        }

        inline void OutlineBox(int x, int y, int w, int h,
                               uint32_t color, int thickness = 1) {
            FillRect(x, y, w, thickness, color);
            FillRect(x, y + h - thickness, w, thickness, color);
            FillRect(x, y, thickness, h, color);
            FillRect(x + w - thickness, y, thickness, h, color);
        }

        inline void CornerBox(int x, int y, int w, int h,
                              uint32_t color, int thickness = 1) {
            const int cw = (std::max)(4, w / 4);
            const int ch = (std::max)(5, h / 5);
            FillRect(x, y, cw, thickness, color);
            FillRect(x, y, thickness, ch, color);
            FillRect(x + w - cw, y, cw, thickness, color);
            FillRect(x + w - thickness, y, thickness, ch, color);
            FillRect(x, y + h - thickness, cw, thickness, color);
            FillRect(x, y + h - ch, thickness, ch, color);
            FillRect(x + w - cw, y + h - thickness, cw, thickness, color);
            FillRect(x + w - thickness, y + h - ch, thickness, ch, color);
        }

        inline void BeginFrame() {
            ClearRect(previous);
            current = {};
        }

        inline void Present() {
            if (!window || !memoryDc || !pixels || width <= 0 || height <= 0)
                return;

            const DirtyRect full{ 0, 0, width, height, true };
            const DirtyRect dirty = forceFullPresent ? full : Union(previous, current);
            if (!dirty.valid) return;

            POINT dst{ screenX, screenY };
            POINT src{};
            SIZE size{ width, height };
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            RECT dirtyRect{ dirty.l, dirty.t, dirty.r, dirty.b };
            UPDATELAYEREDWINDOWINFO info{};
            info.cbSize = sizeof(info);
            info.pptDst = &dst;
            info.psize = &size;
            info.hdcSrc = memoryDc;
            info.pptSrc = &src;
            info.pblend = &blend;
            info.dwFlags = ULW_ALPHA;
            info.prcDirty = &dirtyRect;

            bool ok = UpdateLayeredWindowIndirect(window, &info) != FALSE;
            if (!ok) {
                ok = UpdateLayeredWindow(window, nullptr, &dst, &size,
                                         memoryDc, &src, 0, &blend,
                                         ULW_ALPHA) != FALSE;
            }

            if (ok) {
                previous = current;
                forceFullPresent = false;
            }
        }
    }

    // ============================================================
    // IMMUTABLE PER-TICK SETTINGS SNAPSHOT
    // ============================================================
    // The UI globals are copied once at the beginning of Tick(). Every worker
    // then reads the same immutable snapshot instead of repeatedly touching
    // menu state or recomputing colors/distance limits.
    namespace FrameLimiter {
        inline TimePoint nextFrame{};
        inline TimePoint nextRefreshProbe{};
        inline int cachedDisplayHz = 60;

        [[nodiscard]] inline int DisplayRefreshHz() {
            const auto now = Clock::now();
            if (now < nextRefreshProbe && cachedDisplayHz > 0)
                return cachedDisplayHz;
            nextRefreshProbe = now + std::chrono::seconds(2);

            HWND window = Surface::targetWindow;
            if (!window || !IsWindow(window))
                window = GetForegroundWindow();

            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (monitor && GetMonitorInfoW(monitor, &info) &&
                EnumDisplaySettingsExW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode, 0) &&
                mode.dmDisplayFrequency > 1 && mode.dmDisplayFrequency < 1000) {
                cachedDisplayHz = static_cast<int>(mode.dmDisplayFrequency);
            }
            return (std::max)(30, cachedDisplayHz);
        }

        [[nodiscard]] inline int TargetFps() {
            switch (std::clamp(g_overlayFpsMode, 0, 7)) {
            case 0: return 60;
            case 1: return 120;
            case 2: return 144;
            case 3: return 165;
            case 4: return 240;
            case 5: return 360;
            case 6: return DisplayRefreshHz();
            default: return 0; // Unlimited
            }
        }

        [[nodiscard]] inline bool Ready(TimePoint now) {
            const int fps = TargetFps();
            if (fps <= 0) {
                nextFrame = now;
                return true;
            }

            const auto interval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(fps)));
            if (nextFrame == TimePoint{} || now > nextFrame + interval * 4)
                nextFrame = now;
            if (now < nextFrame)
                return false;

            nextFrame += interval;
            if (nextFrame < now)
                nextFrame = now + interval;
            return true;
        }

        inline void Reset() {
            nextFrame = {};
            nextRefreshProbe = {};
            cachedDisplayHz = 60;
        }
    }

    struct RuntimeTeamSnapshot {
        TeamVisuals visuals{};
        float maxDistanceSq = 0.0f;
        uint32_t boxColor = 0;
        uint32_t boxFillColor = 0;
        uint32_t boxOutlineColor = 0;
        uint32_t healthColor = 0;
        uint32_t healthSecondaryColor = 0;
        uint32_t armorColor = 0;
        uint32_t armorSecondaryColor = 0;
        uint32_t skeletonColor = 0;
        uint32_t snaplineColor = 0;
        uint32_t trailColor = 0;
        uint32_t nameColor = 0;
        uint32_t weaponColor = 0;
        uint32_t distanceColor = 0;
    };

    struct RuntimeSettingsSnapshot {
        RuntimeTeamSnapshot local{};
        RuntimeTeamSnapshot enemy{};
        bool worldRadar = false;
        HSV worldRadarColor{};
        uint32_t worldRadarPacked = 0;
        float worldRadarRange = 0.0f;
        float worldRadarSize = 0.0f;
    };

    inline RuntimeSettingsSnapshot runtimeSettings{};

    inline RuntimeTeamSnapshot MakeRuntimeTeamSnapshot(const TeamVisuals& source) {
        RuntimeTeamSnapshot out{};
        out.visuals = source;
        const float maxMeters = 20.0f + source.maxDistance * 480.0f;
        const float maxUnits = maxMeters * 39.37f;
        out.maxDistanceSq = maxUnits * maxUnits;
        out.boxColor = Surface::FromHSV(source.box.color);
        const uint8_t fillAlpha = static_cast<uint8_t>(std::clamp(
            static_cast<int>(std::lround(source.boxFillOpacity * 255.0f)), 0, 255));
        out.boxFillColor = Surface::FromHSV(source.boxFill, fillAlpha);
        out.boxOutlineColor = Surface::FromHSV(source.boxOutline);
        out.healthColor = Surface::FromHSV(source.health.color);
        out.healthSecondaryColor = Surface::FromHSV(source.healthSecondary);
        out.armorColor = Surface::FromHSV(source.armor.color);
        out.armorSecondaryColor = Surface::FromHSV(source.armorSecondary);
        out.skeletonColor = Surface::FromHSV(source.skeleton.color);
        out.snaplineColor = Surface::FromHSV(source.snapline.color, 220);
        out.trailColor = Surface::FromHSV(source.trail.color, 170);
        out.nameColor = Surface::FromHSV(source.name.color);
        out.weaponColor = Surface::FromHSV(source.weapon.color);
        out.distanceColor = Surface::FromHSV(source.distance.color);
        return out;
    }

    inline void SnapshotRuntimeSettings() {
        runtimeSettings.local = MakeRuntimeTeamSnapshot(g_local);
        runtimeSettings.enemy = MakeRuntimeTeamSnapshot(g_enemy);
        runtimeSettings.worldRadar = g_worldRadar;
        runtimeSettings.worldRadarColor = g_worldRadarColor;
        runtimeSettings.worldRadarPacked = Surface::FromHSV(g_worldRadarColor, 220);
        runtimeSettings.worldRadarRange = g_worldRadarRange;
        runtimeSettings.worldRadarSize = g_worldRadarSize;
    }

    [[nodiscard]] inline const RuntimeTeamSnapshot& TeamSnapshot(
        int team, int localTeam) {
        return team == localTeam ? runtimeSettings.local : runtimeSettings.enemy;
    }

    namespace Cache {
        using namespace cs2_dumper;

        inline constexpr int MaxSlots = 64;
        inline constexpr int SlotsPerTick = 8;
        inline constexpr int BoneEntitiesPerTick = 8;
        inline constexpr std::size_t BoneCount = 74;
        inline constexpr std::size_t BoneStride = 32;

        // Only these bones are used by the renderer. Their final remote addresses
        // are cached per entity when the skeleton pointer is resolved.
        inline constexpr std::array<std::size_t, 23> UsedBones{{
            0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12,
            13, 14, 17, 18, 19, 20, 21, 22, 50, 71, 73
        }};
        inline constexpr std::size_t UsedBoneCount = UsedBones.size();
        inline constexpr std::size_t BoneBytes =
            (BoneCount - 1) * BoneStride + sizeof(Vec3);

        inline constexpr auto RefreshInterval = std::chrono::seconds(5);
        inline constexpr auto FrameFreshness = std::chrono::milliseconds(100);
        inline constexpr auto LocalOriginRefreshInterval = std::chrono::milliseconds(8);
        inline constexpr auto RadarAngleRefreshInterval = std::chrono::milliseconds(32);
        inline constexpr auto OriginFreshness = std::chrono::milliseconds(180);
        inline constexpr auto EntityOriginRefreshInterval = std::chrono::milliseconds(8);
        inline constexpr auto VitalsRefreshInterval = std::chrono::milliseconds(96);
        inline constexpr auto SpottedRefreshInterval = std::chrono::milliseconds(160);
        inline constexpr auto WeaponRefreshInterval = std::chrono::milliseconds(180);
        // Skeleton data is independent from slower vitals/spotted caches.
        // 8 ms caps remote bone reads near 125 Hz while the overlay can still
        // render faster using the most recent valid pose.
        inline constexpr auto BoneRefreshInterval = std::chrono::milliseconds(8);

        struct Entity {
            int slot = 0;
            uintptr_t controller = 0;
            uintptr_t pawn = 0;
            uintptr_t sceneNode = 0;
            uintptr_t boneArray = 0;
            std::array<uintptr_t, UsedBoneCount> boneAddresses{};
            int team = 0;
            int health = 0;
            float displayedHealth = 0.0f;
            float healthVelocity = 0.0f;
            TimePoint healthHoldUntil{};
            TimePoint lastHealthAnim{};
            int armor = 0;
            bool spotted = true;
            std::array<char, 32> name{};
            uint16_t weaponId = 0;
            TimePoint lastWeapon{};
            Vec3 origin{};
            bool originValid = false;
            std::array<Vec3, BoneCount> bones{};
            Vec3 boneOrigin{};
            bool bonesValid = false;
            TimePoint lastOrigin{};
            TimePoint lastHealth{};
            TimePoint lastSpotted{};
            TimePoint lastBones{};
            int boneFailures = 0;
            std::array<Vec3, 20> trail{};
            std::array<TimePoint, 20> trailTime{};
            int trailCount = 0;
            int trailHead = 0;
        };

        struct Frame {
            uintptr_t localPawn = 0;
            int localTeam = 0;
            Vec3 localOrigin{};
            Matrix4x4 view{};
            Vec3 viewAngles{};
            bool valid = false;
            bool anglesValid = false;
            TimePoint lastOrigin{};
            TimePoint lastView{};
            TimePoint lastAngles{};
        };

        inline std::vector<Entity> entities;
        inline std::vector<Entity> pending;
        inline Frame frame;
        inline uintptr_t entityList = 0;
        inline int nextSlot = MaxSlots + 1;
        inline std::size_t boneCursor = 0;
        inline TimePoint nextRefresh{};

        [[nodiscard]] inline bool Fresh(TimePoint stamp, TimePoint now,
                                        Clock::duration age) {
            return stamp != TimePoint{} && now >= stamp && now - stamp <= age;
        }

        [[nodiscard]] inline float DistanceSq(const Vec3& a, const Vec3& b) {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] inline uintptr_t ResolvePawn(uintptr_t list,
                                                   uint32_t handle) {
            if (!list || !handle || handle == 0xFFFFFFFFu) return 0;
            const uintptr_t entry = Memory::Read<uintptr_t>(
                list + 8 * ((handle & 0x7FFFu) >> 9) + 16);
            if (!entry) return 0;
            return Memory::Read<uintptr_t>(entry + 112 * (handle & 0x1FFu));
        }

        inline void ReadCachedPlayerName(Entity& entity) {
            entity.name.fill(0);
            if (!entity.controller) return;
            // m_iszPlayerName is an inline char[128]; 31 chars are enough for
            // compact ESP labels and keep the entity cache small.
            Memory::TryReadRaw(
                entity.controller + schemas::client_dll::CBasePlayerController::m_iszPlayerName,
                entity.name.data(), entity.name.size() - 1);
            entity.name.back() = 0;
            for (auto& ch : entity.name) {
                if (!ch) break;
                const unsigned char c = static_cast<unsigned char>(ch);
                if (c < 32 || c > 126) ch = '?';
            }
        }

        inline void RefreshActiveWeapon(Entity& entity, TimePoint now) {
            uintptr_t services = 0;
            if (!Memory::TryRead(
                    entity.pawn + schemas::client_dll::C_BasePlayerPawn::m_pWeaponServices,
                    services) || !services) {
                entity.weaponId = 0;
                entity.lastWeapon = now;
                return;
            }

            uint32_t handle = 0;
            if (!Memory::TryRead(
                    services + schemas::client_dll::CPlayer_WeaponServices::m_hActiveWeapon,
                    handle) || !handle) {
                entity.weaponId = 0;
                entity.lastWeapon = now;
                return;
            }

            const uintptr_t weapon = ResolvePawn(entityList, handle);
            if (!weapon) {
                entity.weaponId = 0;
                entity.lastWeapon = now;
                return;
            }

            uint16_t itemDefinition = 0;
            const uintptr_t itemView =
                weapon +
                schemas::client_dll::C_EconEntity::m_AttributeManager +
                schemas::client_dll::C_AttributeContainer::m_Item;
            const uintptr_t defIndex =
                itemView + schemas::client_dll::C_EconItemView::m_iItemDefinitionIndex;
            Memory::TryRead(defIndex, itemDefinition);
            // if schema offset looks dead, try common alternate client layouts
            if (itemDefinition == 0 || itemDefinition > 6000) {
                uint16_t alt = 0;
                if (Memory::TryRead(itemView + 0x1BA, alt) && alt > 0 && alt < 6000)
                    itemDefinition = alt;
                else if (Memory::TryRead(itemView + 0x38, alt) && alt > 0 && alt < 6000)
                    itemDefinition = alt;
            }
            entity.weaponId = itemDefinition;
            entity.lastWeapon = now;
        }

        inline void CacheBoneAddresses(Entity& entity, uintptr_t boneArray) {
            entity.boneArray = boneArray;
            if (!boneArray) {
                entity.boneAddresses.fill(0);
                return;
            }
            for (std::size_t i = 0; i < UsedBoneCount; ++i)
                entity.boneAddresses[i] = boneArray + UsedBones[i] * BoneStride;
        }

        inline bool ResolveBones(Entity& entity) {
            // Reuse the cached scene node first. Only fall back to the pawn ->
            // scene-node pointer read when the cached node is no longer valid.
            uintptr_t boneArray = 0;
            if (entity.sceneNode &&
                Memory::TryRead(entity.sceneNode +
                                schemas::client_dll::CSkeletonInstance::m_modelState + 0x80,
                                boneArray) && boneArray) {
                CacheBoneAddresses(entity, boneArray);
                return true;
            }

            uintptr_t sceneNode = 0;
            if (!Memory::TryRead(entity.pawn + schemas::client_dll::C_BaseEntity::m_pGameSceneNode,
                                 sceneNode) || !sceneNode) {
                entity.sceneNode = 0;
                CacheBoneAddresses(entity, 0);
                return false;
            }

            if (!Memory::TryRead(sceneNode +
                                 schemas::client_dll::CSkeletonInstance::m_modelState + 0x80,
                                 boneArray) || !boneArray) {
                entity.sceneNode = sceneNode;
                CacheBoneAddresses(entity, 0);
                return false;
            }

            entity.sceneNode = sceneNode;
            CacheBoneAddresses(entity, boneArray);
            return true;
        }

        inline bool ReadBonesFast(const Entity& entity,
                                  std::array<Vec3, BoneCount>& out) {
            if (!entity.boneArray) return false;

            // Keep one cross-process syscall. Reading every cached final bone
            // address separately would require 23 ReadProcessMemory calls and is
            // slower than one small packed block read. Only used positions are
            // copied out of that block.
            // Uninitialized stack buffer is intentional: the full range is
            // overwritten by RPM on success, avoiding the generic TryRead() zero-fill.
            std::array<std::byte, BoneBytes> block;
            if (!Memory::TryReadRaw(entity.boneArray, block.data(), block.size()))
                return false;

            for (std::size_t i = 0; i < UsedBoneCount; ++i) {
                const uintptr_t finalAddress = entity.boneAddresses[i];
                if (!finalAddress || finalAddress < entity.boneArray) return false;
                const std::size_t offset = static_cast<std::size_t>(
                    finalAddress - entity.boneArray);
                if (offset + sizeof(Vec3) > block.size()) return false;
                std::memcpy(&out[UsedBones[i]], block.data() + offset, sizeof(Vec3));
            }
            return true;
        }

        inline bool Finite(const Vec3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        inline bool ValidateBones(const Entity& e,
                                  const std::array<Vec3, BoneCount>& candidate) {
            if (!e.originValid || !Finite(e.origin)) return false;
            constexpr float maxDistSq = 155.0f * 155.0f;
            for (const auto index : UsedBones) {
                if (!Finite(candidate[index])) return false;
                if (DistanceSq(candidate[index], e.origin) > maxDistSq) return false;
            }

            // Never let a one-frame bad pointer overwrite the last good pose.
            if (e.bonesValid) {
                constexpr float maxRelativeJumpSq = 52.0f * 52.0f;
                for (const auto index : UsedBones) {
                    const Vec3 oldRelative{
                        e.bones[index].x - e.boneOrigin.x,
                        e.bones[index].y - e.boneOrigin.y,
                        e.bones[index].z - e.boneOrigin.z
                    };
                    const Vec3 newRelative{
                        candidate[index].x - e.origin.x,
                        candidate[index].y - e.origin.y,
                        candidate[index].z - e.origin.z
                    };
                    if (DistanceSq(oldRelative, newRelative) > maxRelativeJumpSq)
                        return false;
                }
            }
            return true;
        }

        inline constexpr auto TrailLifetime = std::chrono::milliseconds(1250);

        inline void ClearTrail(Entity& e) {
            e.trailCount = 0;
            e.trailHead = 0;
            e.trailTime.fill(TimePoint{});
        }

        inline void PruneTrail(Entity& e, TimePoint now) {
            const int cap = static_cast<int>(e.trail.size());
            while (e.trailCount > 0) {
                const int oldest = (e.trailHead - e.trailCount + cap) % cap;
                const TimePoint stamp = e.trailTime[oldest];
                if (stamp != TimePoint{} && now >= stamp && now - stamp <= TrailLifetime)
                    break;
                --e.trailCount;
            }
            if (e.trailCount == 0)
                e.trailHead = 0;
        }

        inline void AddTrailPoint(Entity& e, const Vec3& p, TimePoint now) {
            PruneTrail(e, now);
            if (e.trailCount > 0) {
                const int last = (e.trailHead - 1 + static_cast<int>(e.trail.size())) %
                                 static_cast<int>(e.trail.size());
                if (DistanceSq(e.trail[last], p) < 5.0f * 5.0f) return;
            }
            e.trail[e.trailHead] = p;
            e.trailTime[e.trailHead] = now;
            e.trailHead = (e.trailHead + 1) % static_cast<int>(e.trail.size());
            e.trailCount = (std::min)(e.trailCount + 1,
                                      static_cast<int>(e.trail.size()));
        }

        inline void BeginRefresh() {
            // Tick() already verified the process handle for this frame.
            uintptr_t localPawn = 0;
            uintptr_t list = 0;
            Memory::TryRead(Memory::client + offsets::client_dll::dwLocalPlayerPawn,
                            localPawn);
            Memory::TryRead(Memory::client + offsets::client_dll::dwEntityList,
                            list);

            if (!localPawn || !list) {
                frame = {};
                entityList = 0;
                pending.clear();
                entities.clear();
                nextSlot = MaxSlots + 1;
                nextRefresh = Clock::now() + RefreshInterval;
                return;
            }

            frame.localPawn = localPawn;
            entityList = list;
            uint8_t localTeam = 0;
            if (Memory::TryRead(localPawn + schemas::client_dll::C_BaseEntity::m_iTeamNum,
                                localTeam))
                frame.localTeam = static_cast<int>(localTeam);
            pending.clear();
            pending.reserve(MaxSlots);
            nextSlot = 1;
        }

        inline void ScanSlots() {
            if (!entityList || nextSlot > MaxSlots) return;

            const int end = (std::min)(MaxSlots + 1, nextSlot + SlotsPerTick);
            for (; nextSlot < end; ++nextSlot) {
                const uintptr_t entry = Memory::Read<uintptr_t>(
                    entityList + 8 * ((nextSlot & 0x7FFF) >> 9) + 16);
                if (!entry) continue;

                const uintptr_t controller = Memory::Read<uintptr_t>(
                    entry + 112 * (nextSlot & 0x1FF));
                if (!controller) continue;

                const uint32_t handle = Memory::Read<uint32_t>(
                    controller + schemas::client_dll::CCSPlayerController::m_hPlayerPawn);
                const uintptr_t pawn = ResolvePawn(entityList, handle);
                if (!pawn || pawn == frame.localPawn) continue;

                Entity e{};
                e.slot = nextSlot;
                e.controller = controller;
                e.pawn = pawn;
                ReadCachedPlayerName(e);
                uint8_t team = 0;
                if (Memory::TryRead(pawn + schemas::client_dll::C_BaseEntity::m_iTeamNum,
                                    team))
                    e.team = static_cast<int>(team);

                const auto old = std::find_if(
                    entities.begin(), entities.end(),
                    [pawn](const Entity& x) { return x.pawn == pawn; });
                const bool hadOld = old != entities.end();
                if (hadOld) {
                    e.origin = old->origin;
                    e.originValid = old->originValid;
                    e.health = old->health;
                    e.displayedHealth = old->displayedHealth;
                    e.healthVelocity = old->healthVelocity;
                    e.healthHoldUntil = old->healthHoldUntil;
                    e.lastHealthAnim = old->lastHealthAnim;
                    e.armor = old->armor;
                    e.spotted = old->spotted;
                    // Name comes from the current controller scan; keep dynamic weapon state.
                    e.weaponId = old->weaponId;
                    e.lastWeapon = old->lastWeapon;
                    e.sceneNode = old->sceneNode;
                    e.boneArray = old->boneArray;
                    e.boneAddresses = old->boneAddresses;
                    e.bones = old->bones;
                    e.boneOrigin = old->boneOrigin;
                    e.bonesValid = old->bonesValid;
                    e.lastOrigin = old->lastOrigin;
                    e.lastHealth = old->lastHealth;
                    e.lastSpotted = old->lastSpotted;
                    e.lastBones = old->lastBones;
                    e.boneFailures = old->boneFailures;
                    e.trail = old->trail;
                    e.trailTime = old->trailTime;
                    e.trailCount = old->trailCount;
                    e.trailHead = old->trailHead;
                }

                // Entity cache also owns the scene-node/bone-array address.
                // Resolve it once during the 5 second entity rebuild so the
                // render-time bone updater can read directly from the cached
                // address instead of walking the pointer chain every frame.
                Vec3 initialOrigin{};
                if (Memory::TryRead(
                        pawn + schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin,
                        initialOrigin) && Finite(initialOrigin)) {
                    e.origin = initialOrigin;
                    e.originValid = true;
                    e.lastOrigin = Clock::now();
                }

                if (!e.boneArray)
                    ResolveBones(e);

                const TeamVisuals& settings =
                    TeamSnapshot(e.team, frame.localTeam).visuals;
                if (!hadOld &&
                    settings.enabled && settings.skeleton.enabled &&
                    e.originValid && e.boneArray) {
                    std::array<Vec3, BoneCount> firstBones{};
                    if (ReadBonesFast(e, firstBones) &&
                        ValidateBones(e, firstBones)) {
                        e.bones = firstBones;
                        e.boneOrigin = e.origin;
                        e.bonesValid = true;
                        e.lastBones = Clock::now();
                        e.boneFailures = 0;
                    }
                }

                pending.push_back(e);
            }

            if (nextSlot > MaxSlots) {
                // The rebuild is intentionally spread across several ticks. Before
                // swapping it in, merge the newest dynamic state from the live
                // cache so a slot scanned early cannot roll origins/bones/vitals
                // backward when the 5-second rebuild completes.
                for (auto& freshEntity : pending) {
                    const auto live = std::find_if(
                        entities.begin(), entities.end(),
                        [&](const Entity& x) { return x.pawn == freshEntity.pawn; });
                    if (live == entities.end()) continue;

                    freshEntity.origin = live->origin;
                    freshEntity.originValid = live->originValid;
                    freshEntity.health = live->health;
                    freshEntity.displayedHealth = live->displayedHealth;
                    freshEntity.healthVelocity = live->healthVelocity;
                    freshEntity.healthHoldUntil = live->healthHoldUntil;
                    freshEntity.lastHealthAnim = live->lastHealthAnim;
                    freshEntity.armor = live->armor;
                    freshEntity.spotted = live->spotted;
                    freshEntity.weaponId = live->weaponId;
                    freshEntity.lastWeapon = live->lastWeapon;
                    freshEntity.lastOrigin = live->lastOrigin;
                    freshEntity.lastHealth = live->lastHealth;
                    freshEntity.lastSpotted = live->lastSpotted;

                    // Keep the last-good skeleton snapshot and the most recently
                    // validated cached addresses. A rebuild must never replace a
                    // newer pose with the older copy taken at the beginning of the
                    // staggered scan.
                    freshEntity.sceneNode = live->sceneNode;
                    freshEntity.boneArray = live->boneArray;
                    freshEntity.boneAddresses = live->boneAddresses;
                    freshEntity.bones = live->bones;
                    freshEntity.boneOrigin = live->boneOrigin;
                    freshEntity.bonesValid = live->bonesValid;
                    freshEntity.lastBones = live->lastBones;
                    freshEntity.boneFailures = live->boneFailures;

                    freshEntity.trail = live->trail;
                    freshEntity.trailTime = live->trailTime;
                    freshEntity.trailCount = live->trailCount;
                    freshEntity.trailHead = live->trailHead;
                }

                entities.swap(pending);
                pending.clear();
                if (boneCursor >= entities.size()) boneCursor = 0;
                nextRefresh = Clock::now() + RefreshInterval;
            }
        }

        inline void UpdateFrame(TimePoint now) {
            if (!frame.localPawn) return;

            if (!Fresh(frame.lastOrigin, now, LocalOriginRefreshInterval)) {
                Vec3 local{};
                if (Memory::TryRead(
                        frame.localPawn +
                        schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin,
                        local) && Finite(local)) {
                    frame.localOrigin = local;
                    frame.lastOrigin = now;
                }
            }

            // View matrix stays at render cadence because it is the only value
            // that directly affects every projected point while the camera moves.
            Matrix4x4 matrix{};
            if (Memory::TryRead(Memory::client + offsets::client_dll::dwViewMatrix,
                                matrix)) {
                frame.view = matrix;
                frame.lastView = now;
            }

            if (runtimeSettings.worldRadar) {
                if (!Fresh(frame.lastAngles, now, RadarAngleRefreshInterval)) {
                    Vec3 angles{};
                    if (Memory::TryRead(
                            Memory::client + offsets::client_dll::dwViewAngles,
                            angles) && Finite(angles)) {
                        frame.viewAngles = angles;
                        frame.lastAngles = now;
                    }
                }
                frame.anglesValid = Fresh(frame.lastAngles, now, FrameFreshness);
            } else {
                frame.anglesValid = false;
            }

            frame.valid = Fresh(frame.lastOrigin, now, FrameFreshness) &&
                          Fresh(frame.lastView, now, FrameFreshness);
        }

        inline void UpdateEntities(TimePoint now) {
            std::atomic_bool stalePawn{ false };

            // Entity reads are independent, so split them into a few persistent
            // worker ranges on CPUs that have spare logical processors. Workers
            // sleep on a condition variable when idle; there is no per-frame
            // thread creation/destruction overhead.
            WorkerPool::RunRanges(entities.size(), 4,
                [&](std::size_t begin, std::size_t end) {
                    bool localStalePawn = false;
                    for (std::size_t index = begin; index < end; ++index) {
                        auto& e = entities[index];
                        if (!e.pawn) continue;

                        const TeamVisuals& settings =
                            TeamSnapshot(e.team, frame.localTeam).visuals;

                        // Trail is time based: disabled/team-hidden trails disappear
                        // immediately, and enabled trails prune their oldest points
                        // even while the player stands still.
                        if (!settings.enabled) {
                            ClearTrail(e);
                            continue;
                        }
                        if (!settings.trail.enabled) ClearTrail(e);
                        else PruneTrail(e, now);

                        if (!Fresh(e.lastOrigin, now, EntityOriginRefreshInterval)) {
                            Vec3 origin{};
                            if (Memory::TryRead(
                                    e.pawn +
                                    schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin,
                                    origin) && Finite(origin)) {
                                e.origin = origin;
                                e.originValid = true;
                                e.lastOrigin = now;
                                if (settings.trail.enabled)
                                    AddTrailPoint(e, origin, now);
                            } else if (!Fresh(e.lastOrigin, now, OriginFreshness)) {
                                e.originValid = false;
                            }
                        }

                        if (!Fresh(e.lastHealth, now, VitalsRefreshInterval)) {
                            int health = 0;
                            if (Memory::TryRead(
                                    e.pawn + schemas::client_dll::C_BaseEntity::m_iHealth,
                                    health)) {
                                const bool firstHealth = e.lastHealth == TimePoint{};
                                const int previousHealth = e.health;
                                if (!firstHealth && health < previousHealth &&
                                    std::clamp(settings.healthAnimationMode, 0, 3) == 2) {
                                    e.healthHoldUntil = now + std::chrono::milliseconds(170);
                                }
                                e.health = health;
                                e.lastHealth = now;
                                if (firstHealth || e.lastHealthAnim == TimePoint{})
                                    e.displayedHealth = static_cast<float>(std::clamp(health, 0, 100));
                                if (health < 0 || health > 100)
                                    localStalePawn = true;
                            }

                            if (settings.armor.enabled) {
                                int armor = 0;
                                if (Memory::TryRead(
                                        e.pawn +
                                        schemas::client_dll::C_CSPlayerPawn::m_ArmorValue,
                                        armor)) {
                                    e.armor = std::clamp(armor, 0, 100);
                                }
                            }
                        }

                        if (settings.weapon.enabled &&
                            !Fresh(e.lastWeapon, now, WeaponRefreshInterval)) {
                            RefreshActiveWeapon(e, now);
                        }

                        // Per-team health animation personality. All modes are frame-rate
                        // independent and operate only on the cached health value.
                        if (e.lastHealth != TimePoint{}) {
                            const float target = static_cast<float>(std::clamp(e.health, 0, 100));
                            const int mode = std::clamp(settings.healthAnimationMode, 0, 3);
                            if (e.lastHealthAnim == TimePoint{}) {
                                e.displayedHealth = target;
                                e.healthVelocity = 0.0f;
                            } else {
                                const float dt = std::clamp(
                                    std::chrono::duration<float>(now - e.lastHealthAnim).count(),
                                    0.0f, 0.10f);
                                switch (mode) {
                                case 0: // Instant
                                    e.displayedHealth = target;
                                    e.healthVelocity = 0.0f;
                                    break;
                                case 2: // Delayed damage: briefly hold the old amount, then glide down.
                                    if (target >= e.displayedHealth || now >= e.healthHoldUntil) {
                                        const float speed = target < e.displayedHealth ? 6.5f : 14.0f;
                                        const float alpha = 1.0f - std::exp(-speed * dt);
                                        e.displayedHealth += (target - e.displayedHealth) * alpha;
                                    }
                                    e.healthVelocity = 0.0f;
                                    break;
                                case 3: { // Spring: soft overshoot/settle without becoming unstable.
                                    constexpr float stiffness = 115.0f;
                                    constexpr float damping = 17.0f;
                                    e.healthVelocity += (target - e.displayedHealth) * stiffness * dt;
                                    e.healthVelocity *= std::exp(-damping * dt);
                                    e.displayedHealth += e.healthVelocity * dt;
                                    if ((target <= 0.0f && e.displayedHealth < 0.0f) ||
                                        (target >= 100.0f && e.displayedHealth > 100.0f)) {
                                        e.displayedHealth = target;
                                        e.healthVelocity = 0.0f;
                                    }
                                    break;
                                }
                                default: { // Smooth
                                    const float speed = target < e.displayedHealth ? 9.0f : 14.0f;
                                    const float alpha = 1.0f - std::exp(-speed * dt);
                                    e.displayedHealth += (target - e.displayedHealth) * alpha;
                                    e.healthVelocity = 0.0f;
                                    break;
                                }
                                }
                                e.displayedHealth = std::clamp(e.displayedHealth, -3.0f, 103.0f);
                                if (std::fabs(target - e.displayedHealth) < 0.04f &&
                                    std::fabs(e.healthVelocity) < 0.08f) {
                                    e.displayedHealth = target;
                                    e.healthVelocity = 0.0f;
                                }
                            }
                            e.lastHealthAnim = now;
                        }

                        if (e.health <= 0)
                            ClearTrail(e);

                        if (settings.onlyVisible || g_aimAssist.visibleOnly) {
                            if (!Fresh(e.lastSpotted, now, SpottedRefreshInterval)) {
                                bool spotted = true;
                                if (Memory::TryRead(
                                        e.pawn +
                                        schemas::client_dll::C_CSPlayerPawnBase::m_entitySpottedState +
                                        schemas::client_dll::EntitySpottedState_t::m_bSpotted,
                                        spotted)) {
                                    e.spotted = spotted;
                                    e.lastSpotted = now;
                                }
                            }
                        } else {
                            e.spotted = true;
                        }
                    }

                    if (localStalePawn)
                        stalePawn.store(true, std::memory_order_relaxed);
                });

            if (stalePawn.load(std::memory_order_relaxed))
                nextRefresh = now;
        }

        inline bool SkeletonWantedForTeam(int team) {
            const auto& settings = TeamSnapshot(team, frame.localTeam).visuals;
            return settings.enabled && settings.skeleton.enabled;
        }

        inline void UpdateBones(TimePoint now) {
            if (entities.empty()) return;

            const bool anySkeleton =
                (runtimeSettings.local.visuals.enabled && runtimeSettings.local.visuals.skeleton.enabled) ||
                (runtimeSettings.enemy.visuals.enabled && runtimeSettings.enemy.visuals.skeleton.enabled);
            if (!anySkeleton) return;

            std::array<std::size_t, BoneEntitiesPerTick> candidates{};
            std::size_t candidateCount = 0;
            const std::size_t attempts = entities.size();

            // Selection stays serial so boneCursor remains deterministic. The
            // expensive cross-process bone reads below can then run in parallel.
            for (std::size_t i = 0;
                 i < attempts && candidateCount < candidates.size();
                 ++i) {
                if (boneCursor >= entities.size()) boneCursor = 0;
                const std::size_t index = boneCursor++;
                auto& e = entities[index];

                if (!e.pawn || !e.originValid ||
                    e.health <= 0 || e.health > 100 ||
                    !SkeletonWantedForTeam(e.team)) {
                    continue;
                }

                const auto& team = TeamSnapshot(e.team, frame.localTeam);
                if (DistanceSq(e.origin, frame.localOrigin) > team.maxDistanceSq)
                    continue;

                if (Fresh(e.lastBones, now, BoneRefreshInterval))
                    continue;

                candidates[candidateCount++] = index;
            }

            WorkerPool::RunRanges(candidateCount, 2,
                [&](std::size_t begin, std::size_t end) {
                    for (std::size_t i = begin; i < end; ++i) {
                        auto& e = entities[candidates[i]];

                        if (!e.boneArray && !ResolveBones(e))
                            continue;

                        // Start from the previous valid pose because the fast
                        // reader only overwrites the positions used by the renderer.
                        std::array<Vec3, BoneCount> fresh = e.bones;
                        bool valid = ReadBonesFast(e, fresh) && ValidateBones(e, fresh);

                        // Skeleton pointers can move on spawn/death/model changes.
                        // Retry from the cached scene node quickly rather than waiting
                        // several visible frames with a stale pointer.
                        if (!valid && ++e.boneFailures >= 2) {
                            CacheBoneAddresses(e, 0);
                            if (ResolveBones(e)) {
                                fresh = e.bones;
                                valid = ReadBonesFast(e, fresh) && ValidateBones(e, fresh);
                            }
                        }

                        if (valid) {
                            e.bones = fresh;
                            e.boneOrigin = e.origin;
                            e.bonesValid = true;
                            e.lastBones = now;
                            e.boneFailures = 0;
                        } else if (e.boneFailures >= 4) {
                            e.sceneNode = 0;
                            CacheBoneAddresses(e, 0);
                            e.boneFailures = 0;
                        }
                    }
                });
        }

        inline void Update() {
            // Tick() owns attachment validation; avoid a second process-state
            // syscall from the cache on every frame.
            const auto now = Clock::now();
            if (nextSlot > MaxSlots && now >= nextRefresh) BeginRefresh();
            ScanSlots();
            UpdateFrame(now);
            UpdateEntities(now);
            UpdateBones(now);
        }
    }

    namespace AimAssist {
        // Offsets supplied with the project update. The general entity-list,
        // local-pawn and view-matrix addresses already come from output/offsets.hpp.
        // from output/client_dll.hpp
        inline uintptr_t ShotsFired() {
            return static_cast<uintptr_t>(cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iShotsFired);
        }
        inline uintptr_t AimPunchService() {
            return static_cast<uintptr_t>(cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_pAimPunchServices);
        }
        // punch cache is rarely in dumps; default 0x88, overridable via offsets.ini
        inline uintptr_t PunchCache() {
            return static_cast<uintptr_t>(cs2_dumper::schemas::client_dll::CCSPlayer_AimPunchServices::m_aimPunchCache);
        }
        inline uintptr_t Dormant() {
            return static_cast<uintptr_t>(cs2_dumper::schemas::client_dll::AimExtras::m_bDormant);
        }
        inline uintptr_t ViewOffset() {
            return static_cast<uintptr_t>(cs2_dumper::schemas::client_dll::AimExtras::m_vecViewOffset);
        }
        inline constexpr float UnitsPerMeter = 39.37f;

        // matches standalone: { uint64_t cnt, ptr; }
        struct RemoteVector {
            uint64_t cnt = 0;   // element count (first)
            uint64_t ptr = 0;   // data pointer (second)
        };

        inline Vec3 lastPunch{};
        inline float recoilX = 0.0f;
        inline float recoilY = 0.0f;
        inline std::mt19937 randomEngine{ std::random_device{}() };
        inline std::normal_distribution<float> jitterDistribution{ 0.0f, 1.0f };

        [[nodiscard]] inline bool IsDown(int virtualKey) {
            return virtualKey > 0 &&
                (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        }

        [[nodiscard]] inline bool IsActivationDown(int encodedKey) {
            const int virtualKey = encodedKey & 0x00FF;
            if (!IsDown(virtualKey)) return false;
            const bool ctrl = IsDown(VK_CONTROL);
            const bool alt = IsDown(VK_MENU);
            const bool shift = IsDown(VK_SHIFT);
            return ctrl == ((encodedKey & 0x0100) != 0) &&
                   alt == ((encodedKey & 0x0200) != 0) &&
                   shift == ((encodedKey & 0x0400) != 0);
        }

        [[nodiscard]] inline bool Project(const Vec3& point,
                                          const Matrix4x4& matrix,
                                          float& screenX,
                                          float& screenY) {
            const float w = matrix.m[3][0] * point.x +
                            matrix.m[3][1] * point.y +
                            matrix.m[3][2] * point.z +
                            matrix.m[3][3];
            if (!(w > 0.01f) || !std::isfinite(w) ||
                Surface::halfWidth <= 0.0f || Surface::halfHeight <= 0.0f)
                return false;

            const float inverseW = 1.0f / w;
            const float clipX = matrix.m[0][0] * point.x +
                                matrix.m[0][1] * point.y +
                                matrix.m[0][2] * point.z +
                                matrix.m[0][3];
            const float clipY = matrix.m[1][0] * point.x +
                                matrix.m[1][1] * point.y +
                                matrix.m[1][2] * point.z +
                                matrix.m[1][3];

            screenX = Surface::halfWidth + clipX * inverseW * Surface::halfWidth;
            screenY = Surface::halfHeight - clipY * inverseW * Surface::halfHeight;
            return std::isfinite(screenX) && std::isfinite(screenY);
        }

        inline void ResetRecoil() {
            lastPunch = {};
            recoilX = 0.0f;
            recoilY = 0.0f;
        }

        inline void MoveMouse(float deltaX, float deltaY) {
            const LONG moveX = static_cast<LONG>(std::clamp(
                std::lround(deltaX), -32767L, 32767L));
            const LONG moveY = static_cast<LONG>(std::clamp(
                std::lround(deltaY), -32767L, 32767L));
            if (moveX == 0 && moveY == 0) return;
            mouse_event(MOUSEEVENTF_MOVE,
                static_cast<DWORD>(moveX), static_cast<DWORD>(moveY), 0, 0);
        }

        inline void Tick() {
            const AimAssistSettings settings = g_aimAssist;
            const auto& frame = Cache::frame;
            if (!frame.valid || !frame.localPawn ||
                !Surface::IsGameForeground()) {
                ResetRecoil();
                return;
            }

            // don't aim/rcs while the menu is open
            if (settings.menuOpen) {
                ResetRecoil();
                return;
            }

            const bool aimActive = settings.enabled &&
                IsActivationDown(settings.activationKey);
            const bool recoilActive = settings.recoilEnabled && IsDown(VK_LBUTTON);
            if (!aimActive && !recoilActive) {
                ResetRecoil();
                return;
            }

            int shots = 0;
            const bool shotsValid = Memory::TryRead(frame.localPawn + ShotsFired(), shots) &&
                shots >= 0 && shots < 128;
            if (!shotsValid)
                ResetRecoil();

            float moveX = 0.0f;
            float moveY = 0.0f;
            bool shouldMove = false;

            if (aimActive) {
                float bestDistance = (std::numeric_limits<float>::max)();
                float bestDeltaX = 0.0f;
                float bestDeltaY = 0.0f;
                const float maximumDistanceUnits =
                    std::max(1.0f, settings.maxDistanceMeters) * UnitsPerMeter;
                const float maximumDistanceSq =
                    maximumDistanceUnits * maximumDistanceUnits;

                for (const auto& entity : Cache::entities) {
                    if (!entity.pawn || entity.pawn == frame.localPawn ||
                        !entity.originValid || entity.health <= 0 || entity.health > 100)
                        continue;
                    if (settings.teamCheck && entity.team == frame.localTeam)
                        continue;
                    if (!entity.sceneNode) continue;

                    bool dormant = false;
                    if (Memory::TryRead(entity.sceneNode + Dormant(), dormant) && dormant)
                        continue;

                    if (settings.visibleOnly && !entity.spotted)
                        continue;

                    if (Cache::DistanceSq(entity.origin, frame.localOrigin) > maximumDistanceSq)
                        continue;

                    Vec3 viewOffset{};
                    if (!Memory::TryRead(entity.pawn + ViewOffset(), viewOffset) ||
                        !std::isfinite(viewOffset.x) ||
                        !std::isfinite(viewOffset.y) ||
                        !std::isfinite(viewOffset.z))
                        continue;

                    const Vec3 head{ entity.origin.x + viewOffset.x,
                                     entity.origin.y + viewOffset.y,
                                     entity.origin.z + viewOffset.z };
                    const Vec3 targetPoints[] = {
                        head,
                        Vec3{ head.x, head.y, head.z - 5.0f },
                        Vec3{ head.x, head.y, head.z - 14.0f },
                        Vec3{ head.x, head.y, head.z - 25.0f }
                    };
                    const int selectedPoint = std::clamp(settings.targetBone, 0, 4);
                    const int firstPoint = selectedPoint == 4 ? 0 : selectedPoint;
                    const int pointCount = selectedPoint == 4 ? 4 : 1;
                    for (int pointIndex = firstPoint;
                         pointIndex < firstPoint + pointCount; ++pointIndex) {
                        float targetX = 0.0f;
                        float targetY = 0.0f;
                        if (!Project(targetPoints[pointIndex], frame.view, targetX, targetY))
                            continue;

                        const float deltaX = targetX - Surface::halfWidth;
                        const float deltaY = targetY - Surface::halfHeight;
                        const float distance = std::sqrt(deltaX * deltaX + deltaY * deltaY);
                        if (distance <= settings.fovPixels && distance < bestDistance) {
                            bestDistance = distance;
                            bestDeltaX = deltaX;
                            bestDeltaY = deltaY;
                        }
                    }
                }

                if (bestDistance < (std::numeric_limits<float>::max)()) {
                    const float divisor = std::max(1.0f, settings.smoothing);
                    moveX += bestDeltaX / divisor;
                    if (!shotsValid || shots <= 1)
                        moveY += bestDeltaY / divisor;
                    shouldMove = true;
                }
            }

            // rcs – same punch-cache path and 0.023 conversion as the working standalone
            if (recoilActive && shotsValid && shots > 1) {
                uintptr_t punchService = 0;
                if (Memory::TryRead(frame.localPawn + AimPunchService(), punchService) && punchService) {
                    RemoteVector pc{};
                    if (Memory::TryRead(punchService + PunchCache(), pc) &&
                        pc.ptr && pc.cnt > 0 && pc.cnt < 0xFFFF) {

                        Vec3 ap{};
                        const uintptr_t lastAddr = pc.ptr + (static_cast<uintptr_t>(pc.cnt) - 1) * sizeof(Vec3);
                        if (Memory::TryRead(lastAddr, ap) &&
                            std::isfinite(ap.x) && std::isfinite(ap.y)) {

                            // skip if punch didn't change
                            if (ap.x != lastPunch.x || ap.y != lastPunch.y) {
                                const float pdx = (ap.x - lastPunch.x) * -1.0f;
                                const float pdy = (ap.y - lastPunch.y) * -1.0f;

                                // same conversion the standalone uses
                                const float sens = std::max(0.1f, settings.sensitivity);
                                const float scale = (settings.recoilScale > 0.01f) ? settings.recoilScale : 2.0f;
                                const float cmx = (pdy * scale / sens) / -0.023f;
                                const float cmy = (pdx * scale / sens) /  0.023f;

                                const float smooth = std::max(1.0f, settings.recoilSmoothing);
                                recoilX += (cmx - recoilX) / smooth;
                                recoilY += (cmy - recoilY) / smooth;

                                float jx = 0.0f, jy = 0.0f;
                                if (settings.recoilJitter > 0.0f) {
                                    jx = jitterDistribution(randomEngine) * settings.recoilJitter;
                                    jy = jitterDistribution(randomEngine) * settings.recoilJitter;
                                }

                                moveX += recoilX + jx;
                                moveY += recoilY + jy + settings.downwardBias;
                                shouldMove = true;

                                lastPunch = ap;
                            }
                        }
                    }
                }
            } else if (!recoilActive || !shotsValid || shots <= 1) {
                ResetRecoil();
            }

            if (shouldMove)
                MoveMouse(moveX, moveY);
        }
    }

    namespace ESP {
        inline constexpr float UnitsPerMeter = 39.37f;
        inline constexpr float Pi = 3.14159265358979323846f;

        [[nodiscard]] inline bool ProjectPoint(const Vec3& world,
                                              const Matrix4x4& m,
                                              float halfWidth,
                                              float halfHeight,
                                              int& x, int& y) {
            const float w = m.m[3][0] * world.x + m.m[3][1] * world.y +
                            m.m[3][2] * world.z + m.m[3][3];
            if (!(w > 0.01f) || !std::isfinite(w)) return false;

            const float cx = m.m[0][0] * world.x + m.m[0][1] * world.y +
                             m.m[0][2] * world.z + m.m[0][3];
            const float cy = m.m[1][0] * world.x + m.m[1][1] * world.y +
                             m.m[1][2] * world.z + m.m[1][3];

            // One reciprocal is cheaper than two divisions and the half-size
            // terms are cached whenever the game client bitmap is resized.
            const float invW = 1.0f / w;
            const float px = halfWidth + cx * invW * halfWidth;
            const float py = halfHeight - cy * invW * halfHeight;
            if (!std::isfinite(px) || !std::isfinite(py)) return false;

            x = static_cast<int>(px);
            y = static_cast<int>(py);
            return true;
        }

        [[nodiscard]] inline bool WorldToScreen(const Vec3& world,
                                                const Matrix4x4& m,
                                                int& x, int& y) {
            if (Surface::halfWidth <= 0.0f || Surface::halfHeight <= 0.0f)
                return false;
            return ProjectPoint(world, m, Surface::halfWidth, Surface::halfHeight, x, y);
        }

        [[nodiscard]] inline float Meters(const Vec3& a, const Vec3& b) {
            return std::sqrt(Cache::DistanceSq(a, b)) / UnitsPerMeter;
        }

        using TinyGlyph = std::array<uint8_t, 5>;

        [[nodiscard]] inline TinyGlyph TinyNumberGlyph(char c) {
            switch (c) {
            case '0': return { 7, 5, 5, 5, 7 };
            case '1': return { 2, 6, 2, 2, 7 };
            case '2': return { 7, 1, 7, 4, 7 };
            case '3': return { 7, 1, 7, 1, 7 };
            case '4': return { 5, 5, 7, 1, 1 };
            case '5': return { 7, 4, 7, 1, 7 };
            case '6': return { 7, 4, 7, 5, 7 };
            case '7': return { 7, 1, 2, 2, 2 };
            case '8': return { 7, 5, 7, 5, 7 };
            case '9': return { 7, 5, 7, 1, 7 };
            default: return {};
            }
        }

        [[nodiscard]] inline int TinyTextWidth(const char* text) {
            int length = 0;
            while (text[length]) ++length;
            return length ? length * 4 - 1 : 0;
        }

        inline void DrawTinyText(int x, int y, const char* text, uint32_t color) {
            if (!Surface::pixels) return;
            const int textWidth = TinyTextWidth(text);
            Surface::MarkDirty(x, y, x + textWidth, y + 5);

            for (int index = 0; text[index]; ++index) {
                const auto glyph = TinyNumberGlyph(text[index]);
                for (int row = 0; row < 5; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        if (!(glyph[row] & (1u << (2 - column)))) continue;
                        const int px = x + index * 4 + column;
                        const int py = y + row;
                        if (px >= 0 && px < Surface::width &&
                            py >= 0 && py < Surface::height) {
                            Surface::pixels[static_cast<size_t>(py) *
                                            Surface::width + px] = color;
                        }
                    }
                }
            }
        }

        using SmallGlyph = std::array<uint8_t, 7>;

        [[nodiscard]] inline SmallGlyph SmallGlyphRows(char input) {
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
            switch (c) {
            case 'A': return {14,17,17,31,17,17,17};
            case 'B': return {30,17,17,30,17,17,30};
            case 'C': return {14,17,16,16,16,17,14};
            case 'D': return {30,17,17,17,17,17,30};
            case 'E': return {31,16,16,30,16,16,31};
            case 'F': return {31,16,16,30,16,16,16};
            case 'G': return {14,17,16,23,17,17,15};
            case 'H': return {17,17,17,31,17,17,17};
            case 'I': return {14,4,4,4,4,4,14};
            case 'J': return {7,2,2,2,2,18,12};
            case 'K': return {17,18,20,24,20,18,17};
            case 'L': return {16,16,16,16,16,16,31};
            case 'M': return {17,27,21,21,17,17,17};
            case 'N': return {17,25,21,19,17,17,17};
            case 'O': return {14,17,17,17,17,17,14};
            case 'P': return {30,17,17,30,16,16,16};
            case 'Q': return {14,17,17,17,21,18,13};
            case 'R': return {30,17,17,30,20,18,17};
            case 'S': return {15,16,16,14,1,1,30};
            case 'T': return {31,4,4,4,4,4,4};
            case 'U': return {17,17,17,17,17,17,14};
            case 'V': return {17,17,17,17,17,10,4};
            case 'W': return {17,17,17,21,21,21,10};
            case 'X': return {17,17,10,4,10,17,17};
            case 'Y': return {17,17,10,4,4,4,4};
            case 'Z': return {31,1,2,4,8,16,31};
            case '0': return {14,17,19,21,25,17,14};
            case '1': return {4,12,4,4,4,4,14};
            case '2': return {14,17,1,2,4,8,31};
            case '3': return {30,1,1,14,1,1,30};
            case '4': return {2,6,10,18,31,2,2};
            case '5': return {31,16,16,30,1,1,30};
            case '6': return {14,16,16,30,17,17,14};
            case '7': return {31,1,2,4,8,8,8};
            case '8': return {14,17,17,14,17,17,14};
            case '9': return {14,17,17,15,1,1,14};
            case '-': return {0,0,0,31,0,0,0};
            case '_': return {0,0,0,0,0,0,31};
            case '.': return {0,0,0,0,0,6,6};
            case '/': return {1,2,2,4,8,8,16};
            case '?': return {14,17,1,2,4,0,4};
            case ' ': return {};
            default:  return {31,17,5,2,4,0,4};
            }
        }

        [[nodiscard]] inline int SmallTextWidth(const char* text) {
            int length = 0;
            while (text && text[length]) ++length;
            return length ? length * 6 - 1 : 0;
        }

        inline void DrawSmallText(int x, int y, const char* text, uint32_t color) {
            if (!Surface::pixels || !text || !*text) return;
            const int textWidth = SmallTextWidth(text);
            Surface::MarkDirty(x, y, x + textWidth, y + 7);

            for (int index = 0; text[index]; ++index) {
                const auto glyph = SmallGlyphRows(text[index]);
                for (int row = 0; row < 7; ++row) {
                    for (int column = 0; column < 5; ++column) {
                        if (!(glyph[row] & (1u << (4 - column)))) continue;
                        const int px = x + index * 6 + column;
                        const int py = y + row;
                        if (px >= 0 && px < Surface::width &&
                            py >= 0 && py < Surface::height) {
                            Surface::pixels[static_cast<size_t>(py) *
                                            Surface::width + px] = color;
                        }
                    }
                }
            }
        }

        inline void DrawSmallTextCentered(int centerX, int y,
                                          const char* text, uint32_t color) {
            DrawSmallText(centerX - SmallTextWidth(text) / 2, y, text, color);
        }

        // Player info labels should stay proportional to the projected player.
        // A fixed 5x7 font looks enormous once the box is only a few pixels tall,
        // so use one of three small screen-space raster sizes instead. This never
        // scales text *up* with distance; it only steps down as the player gets
        // smaller on screen.
        struct InfoFontMetrics {
            int glyphW{};
            int glyphH{};
            int advance{};
        };

        [[nodiscard]] inline InfoFontMetrics InfoFontForBoxHeight(int boxH) {
            if (boxH >= 78) return { 5, 7, 6 }; // near
            if (boxH >= 44) return { 4, 6, 5 }; // medium
            return { 3, 5, 4 };                 // far
        }

        // Weapon labels are intentionally one step smaller than player names.
        // This keeps long weapon names compact without shrinking the name or
        // distance labels with them.
        [[nodiscard]] inline InfoFontMetrics WeaponFontForBoxHeight(int boxH) {
            if (boxH >= 78) return { 4, 6, 5 }; // near
            return { 3, 5, 4 };                 // medium / far
        }

        [[nodiscard]] inline int InfoTextWidth(const char* text,
                                                const InfoFontMetrics& font) {
            int length = 0;
            while (text && text[length]) ++length;
            return length ? (length - 1) * font.advance + font.glyphW : 0;
        }

        [[nodiscard]] inline bool DownsampledGlyphPixel(
            const SmallGlyph& glyph, int dstX, int dstY,
            const InfoFontMetrics& font) {
            // Map every destination cell to a source area and OR the covered
            // 5x7 source pixels. OR-downsampling keeps thin glyph strokes from
            // disappearing at 4x6 / 3x5.
            const int srcX0 = dstX * 5 / font.glyphW;
            const int srcX1 = (std::min)(4, ((dstX + 1) * 5 + font.glyphW - 1) / font.glyphW - 1);
            const int srcY0 = dstY * 7 / font.glyphH;
            const int srcY1 = (std::min)(6, ((dstY + 1) * 7 + font.glyphH - 1) / font.glyphH - 1);

            for (int sy = srcY0; sy <= srcY1; ++sy) {
                for (int sx = srcX0; sx <= srcX1; ++sx) {
                    if (glyph[sy] & (1u << (4 - sx)))
                        return true;
                }
            }
            return false;
        }

        inline void DrawInfoText(int x, int y, const char* text,
                                 uint32_t color,
                                 const InfoFontMetrics& font) {
            if (!Surface::pixels || !text || !*text) return;

            const int textWidth = InfoTextWidth(text, font);
            Surface::MarkDirty(x, y, x + textWidth, y + font.glyphH);

            for (int index = 0; text[index]; ++index) {
                const auto glyph = SmallGlyphRows(text[index]);
                const int glyphX = x + index * font.advance;

                for (int row = 0; row < font.glyphH; ++row) {
                    for (int column = 0; column < font.glyphW; ++column) {
                        if (!DownsampledGlyphPixel(glyph, column, row, font))
                            continue;

                        const int px = glyphX + column;
                        const int py = y + row;
                        if (px >= 0 && px < Surface::width &&
                            py >= 0 && py < Surface::height) {
                            Surface::pixels[static_cast<size_t>(py) *
                                            Surface::width + px] = color;
                        }
                    }
                }
            }
        }

        inline void DrawInfoTextCentered(int centerX, int y,
                                         const char* text, uint32_t color,
                                         const InfoFontMetrics& font) {
            DrawInfoText(centerX - InfoTextWidth(text, font) / 2,
                         y, text, color, font);
        }

        [[nodiscard]] inline const WeaponIcons::IconSet* WeaponIconForId(uint16_t id) {
            using namespace WeaponIcons;
            // CS2 item definition indices (guns + default knives + skin knives)
            switch (id) {
            case 1: return &deagle; case 2: return &elite;
            case 3: return &fiveseven; case 4: return &glock;
            case 7: return &ak47; case 8: return &aug;
            case 9: return &awp; case 10: return &famas;
            case 11: return &g3sg1; case 13: return &galilar;
            case 14: return &m249; case 16: return &m4a1;
            case 17: return &mac10; case 19: return &p90;
            case 23: return &mp5sd; case 24: return &ump45;
            case 25: return &xm1014; case 26: return &bizon;
            case 27: return &mag7; case 28: return &nova; // negev falls through text
            case 29: return &mag7; // sawed-off -> closest shotgun icon
            case 30: return &tec9;
            case 31: return &taser; case 32: return &p2000; // also hkp2000
            case 33: return &mp7; case 34: return &mp9;
            case 35: return &nova; case 36: return &p250;
            case 38: return &scar20; case 39: return &sg556;
            case 40: return &ssg08;
            case 41: return &knife; // knife_gg
            case 42: return &knife; // knife_ct
            case 43: case 44: case 45: case 46: case 47: case 48:
                return &knife; // flash/he/smoke/molotov/decoy/incgrenade - no icon
            case 49: return &c4;
            case 59: return &knife; // knife_t
            case 60: return &m4a1_silencer;
            case 61: return &usp_silencer;
            case 63: return &cz75a;
            case 64: return &revolver;
            // knife skins (common CS2 def indices)
            case 500: case 503: case 505: case 506: case 507: case 508:
            case 509: case 512: case 514: case 515: case 516: case 517:
            case 518: case 519: case 520: case 521: case 522: case 523:
            case 525: case 526:
                return &knife;
            default: return nullptr;
            }
        }

        [[nodiscard]] inline const char* WeaponLabelForId(uint16_t id) {
            switch (id) {
            case 1: return "DEAGLE"; case 2: return "DUALIES";
            case 3: return "FIVESEVEN"; case 4: return "GLOCK";
            case 7: return "AK-47"; case 8: return "AUG";
            case 9: return "AWP"; case 10: return "FAMAS";
            case 11: return "G3SG1"; case 13: return "GALIL";
            case 14: return "M249"; case 16: return "M4A4";
            case 17: return "MAC-10"; case 19: return "P90";
            case 23: return "MP5"; case 24: return "UMP-45";
            case 25: return "XM1014"; case 26: return "BIZON";
            case 27: return "MAG-7"; case 28: return "NEGEV";
            case 29: return "SAWED-OFF"; case 30: return "TEC-9";
            case 31: return "ZEUS"; case 32: return "P2000";
            case 33: return "MP7"; case 34: return "MP9";
            case 35: return "NOVA"; case 36: return "P250";
            case 38: return "SCAR-20"; case 39: return "SG553";
            case 40: return "SSG-08"; case 49: return "C4";
            case 60: return "M4A1-S"; case 61: return "USP-S";
            case 63: return "CZ75"; case 64: return "R8";
            default:
                if (id == 42 || id == 59 || (id >= 500 && id <= 530))
                    return "KNIFE";
                return nullptr;
            }
        }

        [[nodiscard]] inline const WeaponIcons::AlphaSprite& WeaponSpriteForBoxHeight(
            const WeaponIcons::IconSet& set, int boxH) {
            if (boxH >= 78) return set.nearIcon;
            if (boxH >= 44) return set.midIcon;
            return set.farIcon;
        }

        [[nodiscard]] inline uint32_t ModulatePremultipliedAlpha(uint32_t color,
                                                                 uint8_t coverage) {
            if (coverage == 255) return color;
            if (coverage == 0) return 0;
            const uint32_t a = ((color >> 24) & 0xFFu) * coverage / 255u;
            const uint32_t r = ((color >> 16) & 0xFFu) * coverage / 255u;
            const uint32_t g = ((color >> 8) & 0xFFu) * coverage / 255u;
            const uint32_t b = (color & 0xFFu) * coverage / 255u;
            return (a << 24) | (r << 16) | (g << 8) | b;
        }

        inline void DrawWeaponSpriteCentered(int centerX, int y,
                                             const WeaponIcons::AlphaSprite& sprite,
                                             uint32_t color) {
            if (!Surface::pixels || !sprite.alpha || sprite.width <= 0 || sprite.height <= 0)
                return;
            const int x = centerX - sprite.width / 2;
            Surface::MarkDirty(x, y, x + sprite.width, y + sprite.height);
            for (int row = 0; row < sprite.height; ++row) {
                const int py = y + row;
                if (py < 0 || py >= Surface::height) continue;
                for (int column = 0; column < sprite.width; ++column) {
                    const int px = x + column;
                    if (px < 0 || px >= Surface::width) continue;
                    const uint8_t coverage = sprite.alpha[row * sprite.width + column];
                    if (!coverage) continue;
                    Surface::pixels[static_cast<size_t>(py) * Surface::width + px] =
                        ModulatePremultipliedAlpha(color, coverage);
                }
            }
        }

        inline void NumberText(int value, char (&out)[4]) {
            value = std::clamp(value, 0, 100);
            if (value >= 100) {
                out[0] = '1'; out[1] = '0'; out[2] = '0'; out[3] = 0;
            } else if (value >= 10) {
                out[0] = static_cast<char>('0' + value / 10);
                out[1] = static_cast<char>('0' + value % 10);
                out[2] = 0; out[3] = 0;
            } else {
                out[0] = static_cast<char>('0' + value);
                out[1] = 0; out[2] = 0; out[3] = 0;
            }
        }

        [[nodiscard]] inline int ValueBarThickness(float normalizedWidth,
                                                     int boxH) {
            const int configuredWidth = 1 + static_cast<int>(std::lround(
                std::clamp(normalizedWidth, 0.0f, 1.0f) * 11.0f));
            const float projectedScale = std::clamp(
                static_cast<float>(boxH) / 180.0f, 0.20f, 1.0f);
            return (std::max)(1, static_cast<int>(std::lround(
                static_cast<float>(configuredWidth) * projectedScale)));
        }

        inline void DrawVerticalValueBar(
            int barX, int y, int boxH, int value,
            float normalizedWidth, uint32_t foreground, uint32_t secondary,
            int style, bool showText) {

            value = std::clamp(value, 0, 100);
            if (boxH <= 0) return;

            const int thickness = ValueBarThickness(normalizedWidth, boxH);
            const uint32_t outlineColor = Surface::Premultiply(16, 16, 16, 255);
            const uint32_t background = Surface::Premultiply(16, 16, 16, 230);

            Surface::FillRect(barX - 1, y - 1, thickness + 2, boxH + 2, outlineColor);
            Surface::FillRect(barX, y, thickness, boxH, background);

            const int filled = boxH * value / 100;
            if (filled > 0) {
                const int fillTop = y + boxH - filled;
                if (style == 1) {
                    const int segments = 10;
                    const int active = (value * segments + 99) / 100;
                    for (int i = 0; i < active; ++i) {
                        const int lower = y + boxH - i * boxH / segments;
                        const int upper = y + boxH - (i + 1) * boxH / segments;
                        const int segmentH = (std::max)(1, lower - upper - 1);
                        const uint32_t c = Surface::BlendColor(
                            static_cast<float>(i + 1) / segments, secondary, foreground);
                        Surface::FillRect(barX, upper, thickness, segmentH, c);
                    }
                } else if (style == 2) {
                    for (int i = 0; i < filled; ++i) {
                        const float level = filled > 1
                            ? static_cast<float>(i) / static_cast<float>(filled - 1)
                            : 1.0f;
                        Surface::FillRectPixels(barX, fillTop + i, thickness, 1,
                            Surface::BlendColor(1.0f - level, secondary, foreground));
                    }
                    Surface::MarkDirty(barX, fillTop, barX + thickness, y + boxH);
                } else {
                    Surface::FillRect(barX, fillTop, thickness, filled, foreground);
                }
            }

            if (showText && value < 100) {
                char text[4]{};
                NumberText(value, text);
                const int textWidth = TinyTextWidth(text);
                const int fillTop = y + boxH - filled;
                const int tx = barX + (thickness - textWidth) / 2;
                const int ty = std::clamp(fillTop - 2, y, y + boxH - 5);
                DrawTinyText(tx + 1, ty + 1, text,
                             Surface::Premultiply(0, 0, 0, 224));
                DrawTinyText(tx, ty, text, 0xFFFFFFFFu);
            }
        }

        inline void DrawHorizontalValueBar(
            int x, int barY, int boxW, int boxH, int value,
            float normalizedWidth, uint32_t foreground, uint32_t secondary,
            int style, bool showText, bool placeTextAbove) {

            value = std::clamp(value, 0, 100);
            if (boxW <= 0) return;

            const int thickness = ValueBarThickness(normalizedWidth, boxH);
            const uint32_t outlineColor = Surface::Premultiply(16, 16, 16, 255);
            const uint32_t background = Surface::Premultiply(16, 16, 16, 230);

            Surface::FillRect(x - 1, barY - 1, boxW + 2, thickness + 2, outlineColor);
            Surface::FillRect(x, barY, boxW, thickness, background);

            const int filled = boxW * value / 100;
            if (filled > 0) {
                if (style == 1) {
                    const int segments = 10;
                    const int active = (value * segments + 99) / 100;
                    for (int i = 0; i < active; ++i) {
                        const int left = x + i * boxW / segments;
                        const int right = x + (i + 1) * boxW / segments;
                        const int segmentW = (std::max)(1, right - left - 1);
                        const uint32_t c = Surface::BlendColor(
                            static_cast<float>(i + 1) / segments, secondary, foreground);
                        Surface::FillRect(left, barY, segmentW, thickness, c);
                    }
                } else if (style == 2) {
                    for (int i = 0; i < filled; ++i) {
                        const float level = filled > 1
                            ? static_cast<float>(i) / static_cast<float>(filled - 1)
                            : 1.0f;
                        Surface::FillRectPixels(x + i, barY, 1, thickness,
                            Surface::BlendColor(level, secondary, foreground));
                    }
                    Surface::MarkDirty(x, barY, x + filled, barY + thickness);
                } else {
                    Surface::FillRect(x, barY, filled, thickness, foreground);
                }
            }

            if (showText && value < 100) {
                char text[4]{};
                NumberText(value, text);
                const int textWidth = TinyTextWidth(text);
                const int edge = x + filled;
                const int tx = std::clamp(edge - textWidth / 2,
                                          x, x + boxW - textWidth);
                const int ty = placeTextAbove ? barY - 7 : barY + thickness + 2;
                DrawTinyText(tx + 1, ty + 1, text,
                             Surface::Premultiply(0, 0, 0, 224));
                DrawTinyText(tx, ty, text, 0xFFFFFFFFu);
            }
        }

        [[nodiscard]] inline uint32_t HealthStateColor(int health, uint32_t healthyColor) {
            // Keep the user's selected health color while healthy, then switch to
            // stronger warning colors at low HP so the bar is readable instantly.
            if (health < 25)
                return Surface::Premultiply(255, 52, 52, 255);
            if (health <= 45)
                return Surface::Premultiply(255, 153, 38, 255);
            return healthyColor;
        }

        inline void DrawHealth(int x, int y, int w, int h, int health,
                               const TeamVisuals& settings,
                               uint32_t foreground, uint32_t secondary) {
            health = std::clamp(health, 0, 100);
            // Normal keeps automatic warning thresholds. Segmented/Gradient use
            // the two user-selected colors across the bar instead.
            if (settings.healthStyle == 0)
                foreground = HealthStateColor(health, foreground);
            const float projectedScale = std::clamp(
                static_cast<float>(h) / 180.0f, 0.20f, 1.0f);
            const int gap = (std::max)(0, static_cast<int>(std::lround(
                4.0f * projectedScale)));
            const int thickness = ValueBarThickness(settings.healthWidth, h);

            switch (std::clamp(settings.healthSide, 0, 3)) {
            case 1: // Right
                DrawVerticalValueBar(x + w + gap + 1, y, h, health,
                    settings.healthWidth, foreground, secondary,
                    settings.healthStyle, true);
                break;
            case 2: // Top
                DrawHorizontalValueBar(x, y - gap - 1 - thickness, w, h, health,
                    settings.healthWidth, foreground, secondary,
                    settings.healthStyle, true, true);
                break;
            case 3: // Bottom
                DrawHorizontalValueBar(x, y + h + gap + 1, w, h, health,
                    settings.healthWidth, foreground, secondary,
                    settings.healthStyle, true, false);
                break;
            default: // Left
                DrawVerticalValueBar(x - gap - 1 - thickness, y, h, health,
                    settings.healthWidth, foreground, secondary,
                    settings.healthStyle, true);
                break;
            }
        }

        inline void DrawArmor(int x, int y, int w, int h, int armor,
                              const TeamVisuals& settings,
                              uint32_t color, uint32_t secondary) {
            armor = std::clamp(armor, 0, 100);
            if (armor <= 0) return;

            const float projectedScale = std::clamp(
                static_cast<float>(h) / 180.0f, 0.20f, 1.0f);
            const int gap = (std::max)(0, static_cast<int>(std::lround(
                4.0f * projectedScale)));
            const int armorThickness = ValueBarThickness(settings.armorWidth, h);
            const bool sameSide = settings.health.enabled &&
                settings.healthSide == settings.armorSide;
            const int extra = sameSide
                ? ValueBarThickness(settings.healthWidth, h) + 2 : 0;
            const int style = std::clamp(settings.armor.thickness, 1, 4) - 1;
            const int side = std::clamp(settings.armorSide, 0, 3);

            // Plates are five chunky armor cells, filled from the player's feet/left.
            if (style == 3) {
                constexpr int cells = 5;
                const int active = std::clamp((armor * cells + 99) / 100, 0, cells);
                const uint32_t outline = Surface::Premultiply(12, 12, 14, 235);
                if (side == 0 || side == 1) {
                    const int px = side == 0
                        ? x - gap - 4 - extra
                        : x + w + gap + 2 + extra;
                    const int cellH = (std::max)(2, (h - (cells - 1) * 2) / cells);
                    for (int i = 0; i < cells; ++i) {
                        const int cy = y + h - (i + 1) * cellH - i * 2;
                        Surface::FillRect(px - 1, cy - 1, 5, cellH + 2, outline);
                        if (i < active) Surface::FillRect(px, cy, 3, cellH, color);
                    }
                } else {
                    const int py = side == 2
                        ? y - gap - 4 - extra
                        : y + h + gap + 2 + extra;
                    const int cellW = (std::max)(3, (w - (cells - 1) * 2) / cells);
                    for (int i = 0; i < cells; ++i) {
                        const int cx = x + i * (cellW + 2);
                        Surface::FillRect(cx - 1, py - 1, cellW + 2, 5, outline);
                        if (i < active) Surface::FillRect(cx, py, cellW, 3, color);
                    }
                }
                return;
            }

            // Bar = continuous, Segments = cells, Gradient = smooth two-color fill.
            const int valueStyle = style == 1 ? 1 : (style == 2 ? 2 : 0);
            switch (side) {
            case 0: // Left
                DrawVerticalValueBar(x - gap - 1 - armorThickness - extra,
                    y, h, armor, settings.armorWidth, color, secondary, valueStyle, false);
                break;
            case 2: // Top
                DrawHorizontalValueBar(x, y - gap - 1 - armorThickness - extra,
                    w, h, armor, settings.armorWidth, color, secondary, valueStyle, false, true);
                break;
            case 3: // Bottom
                DrawHorizontalValueBar(x, y + h + gap + 1 + extra,
                    w, h, armor, settings.armorWidth, color, secondary, valueStyle, false, false);
                break;
            default: // Right
                DrawVerticalValueBar(x + w + gap + 1 + extra,
                    y, h, armor, settings.armorWidth, color, secondary, valueStyle, false);
                break;
            }
        }

        struct ScreenPoint {
            int x{};
            int y{};
            bool visible{};
        };

        struct PreparedPlayer {
            const Cache::Entity* entity = nullptr;
            const RuntimeTeamSnapshot* team = nullptr;
            bool valid = false;
            int feetX{};
            int feetY{};
            int headX{};
            int headY{};
            int boxW{};
            int boxH{};
            int left{};
            int barY{};
            int barH{};
            int displayHealth{};
            int distanceMeters{};
            std::array<ScreenPoint, Cache::BoneCount> bones{};
            std::array<ScreenPoint, 20> trail{};
            std::array<float, 20> trailAge{};
            int trailCount = 0;
        };

        inline std::vector<PreparedPlayer> preparedPlayers;

        inline void PreparePlayers() {
            const std::size_t count = Cache::entities.size();
            if (preparedPlayers.capacity() < static_cast<std::size_t>(Cache::MaxSlots))
                preparedPlayers.reserve(Cache::MaxSlots);
            preparedPlayers.resize(count);
            if (!count) return;

            // These values are immutable for the entire prepare pass. Workers share
            // this one snapshot and only write their own PreparedPlayer slots.
            const Matrix4x4 view = Cache::frame.view;
            const Vec3 localOrigin = Cache::frame.localOrigin;
            const int localTeam = Cache::frame.localTeam;
            const float halfWidth = Surface::halfWidth;
            const float halfHeight = Surface::halfHeight;
            const int screenWidth = Surface::width;
            const int screenHeight = Surface::height;
            const TimePoint trailNow = Clock::now();

            WorkerPool::RunRanges(count, 4,
                [&, view, localOrigin, localTeam, halfWidth, halfHeight,
                 screenWidth, screenHeight, trailNow](std::size_t begin, std::size_t end) {
                    for (std::size_t index = begin; index < end; ++index) {
                        auto& out = preparedPlayers[index];
                        out = {};

                        const auto& e = Cache::entities[index];
                        const auto& team = TeamSnapshot(e.team, localTeam);
                        const auto& settings = team.visuals;

                        if (!settings.enabled || !e.originValid ||
                            e.health <= 0 || e.health > 100)
                            continue;
                        if (settings.onlyVisible && !e.spotted) continue;
                        const float distanceSq = Cache::DistanceSq(e.origin, localOrigin);
                        if (distanceSq > team.maxDistanceSq)
                            continue;

                        Vec3 head = e.origin;
                        head.z += 72.0f;

                        int feetX{}, feetY{}, headX{}, headY{};
                        if (!ProjectPoint(e.origin, view, halfWidth, halfHeight,
                                          feetX, feetY) ||
                            !ProjectPoint(head, view, halfWidth, halfHeight,
                                          headX, headY))
                            continue;

                        const int boxH = feetY - headY;
                        if (boxH < 8 || boxH > screenHeight * 2) continue;
                        const int boxW = (std::max)(4, static_cast<int>(boxH * 0.54f));
                        const int left = headX - boxW / 2;
                        if (left >= screenWidth || left + boxW <= 0 ||
                            headY >= screenHeight || feetY <= 0)
                            continue;

                        out.entity = &e;
                        out.team = &team;
                        out.valid = true;
                        out.feetX = feetX;
                        out.feetY = feetY;
                        out.headX = headX;
                        out.headY = headY;
                        out.boxW = boxW;
                        out.boxH = boxH;
                        out.left = left;
                        out.barY = headY;
                        out.barH = boxH;
                        out.displayHealth = std::clamp(
                            static_cast<int>(std::lround(e.displayedHealth)), 0, 100);
                        if (settings.distance.enabled)
                            out.distanceMeters = (std::max)(0, static_cast<int>(
                                std::lround(std::sqrt(distanceSq) / UnitsPerMeter)));

                        if (!settings.box.enabled) {
                            const int trim = (std::max)(2, boxH / 36);
                            out.barY += trim;
                            out.barH = (std::max)(6, boxH - trim * 2);
                        }

                        // Project skeleton points in the worker phase. The final draw
                        // phase only rasterizes already-prepared screen coordinates.
                        if (settings.skeleton.enabled && e.bonesValid) {
                            const Vec3 follow{
                                e.origin.x - e.boneOrigin.x,
                                e.origin.y - e.boneOrigin.y,
                                e.origin.z - e.boneOrigin.z
                            };
                            for (const auto boneIndex : Cache::UsedBones) {
                                const Vec3 point{
                                    e.bones[boneIndex].x + follow.x,
                                    e.bones[boneIndex].y + follow.y,
                                    e.bones[boneIndex].z + follow.z
                                };
                                auto& dst = out.bones[boneIndex];
                                dst.visible = ProjectPoint(
                                    point, view, halfWidth, halfHeight, dst.x, dst.y);
                            }
                        }

                        if (settings.trail.enabled && e.trailCount >= 2) {
                            const int cap = static_cast<int>(e.trail.size());
                            const int start =
                                (e.trailHead - e.trailCount + cap) % cap;
                            out.trailCount = (std::min)(
                                e.trailCount, static_cast<int>(out.trail.size()));
                            for (int i = 0; i < out.trailCount; ++i) {
                                const int trailIndex = (start + i) % cap;
                                const Vec3& point = e.trail[trailIndex];
                                auto& dst = out.trail[i];
                                dst.visible = ProjectPoint(
                                    point, view, halfWidth, halfHeight, dst.x, dst.y);
                                const TimePoint stamp = e.trailTime[trailIndex];
                                out.trailAge[i] = stamp == TimePoint{}
                                    ? 1.25f
                                    : std::clamp(std::chrono::duration<float>(trailNow - stamp).count(),
                                                 0.0f, 1.25f);
                            }
                        }
                    }
                });
        }

        inline void DrawPreparedSkeleton(const PreparedPlayer& player) {
            if (!player.valid || !player.entity || !player.team) return;
            const auto& e = *player.entity;
            const auto& settings = player.team->visuals;
            if (!settings.skeleton.enabled || !e.bonesValid) return;

            using Link = std::array<std::size_t, 2>;
            static constexpr std::array<Link, 20> terrorist{{
                {{1, 2}}, {{2, 3}}, {{3, 4}}, {{4, 12}},
                {{12, 5}}, {{5, 6}}, {{6, 7}},
                {{12, 13}}, {{13, 14}}, {{14, 71}},
                {{12, 9}}, {{9, 50}}, {{50, 10}}, {{10, 11}},
                {{1, 17}}, {{17, 18}}, {{18, 19}},
                {{1, 20}}, {{20, 21}}, {{21, 22}}
            }};
            static constexpr std::array<Link, 20> counterTerrorist{{
                {{1, 2}}, {{2, 3}}, {{3, 4}}, {{4, 12}},
                {{12, 5}}, {{5, 6}}, {{6, 7}},
                {{12, 13}}, {{13, 14}}, {{14, 73}},
                {{12, 9}}, {{9, 50}}, {{50, 10}}, {{10, 11}},
                {{1, 17}}, {{17, 18}}, {{18, 19}},
                {{1, 20}}, {{20, 21}}, {{21, 22}}
            }};

            const int centerX = player.left + player.boxW / 2;
            const int allowance = (std::max)(
                player.boxW, static_cast<int>(player.boxH * 0.68f));
            const int minX = centerX - allowance;
            const int maxX = centerX + allowance;
            const int minY = player.headY - static_cast<int>(player.boxH * 0.18f);
            const int maxY = player.headY + player.boxH +
                             static_cast<int>(player.boxH * 0.12f);
            const float maxSegment = (std::max)(18.0f, player.boxH * 0.62f);
            const float maxSegmentSq = maxSegment * maxSegment;
            const int style = std::clamp(settings.skeletonStyle, 0, 3);
            const float lineThickness = std::clamp(settings.skeletonThickness, 1.0f, 4.0f);
            const float pulsePhase = std::chrono::duration<float>(
                Clock::now().time_since_epoch()).count() * 5.5f;
            const float pulse = 0.5f + 0.5f * std::sin(pulsePhase);

            auto sane = [&](const ScreenPoint& p) {
                return p.visible && p.x >= minX && p.x <= maxX &&
                       p.y >= minY && p.y <= maxY;
            };

            auto draw = [&](const auto& links) {
                for (const auto& link : links) {
                    const auto& a = player.bones[link[0]];
                    const auto& b = player.bones[link[1]];
                    if (!sane(a) || !sane(b)) continue;
                    if (Cache::DistanceSq(e.bones[link[0]], e.bones[link[1]]) >
                        55.0f * 55.0f)
                        continue;
                    const float dx = static_cast<float>(b.x - a.x);
                    const float dy = static_cast<float>(b.y - a.y);
                    if (dx * dx + dy * dy > maxSegmentSq) continue;

                    if (style == 2) { // Glow: keep the selected core width consistent.
                        Surface::LineSmooth(a.x, a.y, b.x, b.y,
                            Surface::FromHSV(settings.skeleton.color, 58),
                            std::clamp(lineThickness + 2.0f, 1.0f, 5.0f));
                        Surface::LineSmooth(a.x, a.y, b.x, b.y,
                            player.team->skeletonColor, lineThickness);
                    } else if (style == 3) { // Pulse changes intensity, never line thickness.
                        const uint8_t alpha = static_cast<uint8_t>(125.0f + pulse * 130.0f);
                        Surface::LineSmooth(a.x, a.y, b.x, b.y,
                            Surface::FromHSV(settings.skeleton.color, alpha),
                            lineThickness);
                    } else {
                        const uint32_t lineColor = style == 1
                            ? Surface::FromHSV(settings.skeleton.color, 215)
                            : player.team->skeletonColor;
                        Surface::LineSmooth(a.x, a.y, b.x, b.y, lineColor, lineThickness);
                    }
                }
            };

            if (e.team == 2) draw(terrorist);
            else draw(counterTerrorist);
        }

        inline void DrawPreparedTrail(const PreparedPlayer& player) {
            if (!player.valid || !player.team || player.trailCount < 2) return;
            const auto& settings = player.team->visuals;
            const int style = std::clamp(settings.trailPersonality, 0, 5);
            const int baseWidth = std::clamp(settings.trail.thickness, 1, 3);
            const float pulsePhase = std::chrono::duration<float>(
                Clock::now().time_since_epoch()).count() * 7.0f;

            auto ageAlpha = [&](int i, int minAlpha = 24, int maxAlpha = 220) {
                const float life = std::clamp(1.0f - player.trailAge[i] / 1.25f, 0.0f, 1.0f);
                return static_cast<uint8_t>(std::clamp(
                    minAlpha + static_cast<int>((maxAlpha - minAlpha) * life), 0, 255));
            };

            if (style == 2) { // Dots
                for (int i = 0; i < player.trailCount; ++i) {
                    const auto& point = player.trail[i];
                    if (!point.visible) continue;
                    const int radius = baseWidth + 1;
                    const uint32_t color = Surface::FromHSV(settings.trail.color, ageAlpha(i, 35, 230));
                    Surface::FillRoundedRectElliptic(point.x - radius, point.y - radius,
                                                     radius * 2 + 1, radius * 2 + 1,
                                                     radius, radius, color);
                }
                return;
            }

            int previousX{}, previousY{};
            bool previousVisible = false;
            for (int i = 0; i < player.trailCount; ++i) {
                const auto& point = player.trail[i];
                if (point.visible && previousVisible) {
                    const float dx = static_cast<float>(point.x - previousX);
                    const float dy = static_cast<float>(point.y - previousY);
                    const float segment = std::sqrt(dx * dx + dy * dy);
                    uint8_t alpha = 170;
                    int width = baseWidth;

                    if (style == 1) { // Fade
                        alpha = ageAlpha(i, 18, 225);
                    } else if (style == 3) { // Ribbon
                        const uint32_t glow = Surface::FromHSV(settings.trail.color, ageAlpha(i, 12, 72));
                        Surface::Line(previousX, previousY, point.x, point.y, glow,
                                      std::min(6, baseWidth + 3));
                        alpha = ageAlpha(i, 60, 225);
                        width = std::min(4, baseWidth + 1);
                    } else if (style == 4) { // Pulse
                        const float wave = 0.5f + 0.5f * std::sin(pulsePhase + i * 0.72f);
                        alpha = static_cast<uint8_t>(70.0f + wave * 170.0f);
                        width = std::clamp(baseWidth + (wave > 0.62f ? 1 : 0), 1, 4);
                    } else if (style == 5) { // Velocity
                        const float motion = std::clamp(segment / 24.0f, 0.0f, 1.0f);
                        alpha = static_cast<uint8_t>(80.0f + motion * 165.0f);
                        width = std::clamp(baseWidth + static_cast<int>(motion * 2.0f), 1, 5);
                    }

                    Surface::Line(previousX, previousY, point.x, point.y,
                                  Surface::FromHSV(settings.trail.color, alpha), width);
                }
                previousX = point.x;
                previousY = point.y;
                previousVisible = point.visible;
            }
        }

        inline void DrawRoundedOutlineBox(int x, int y, int w, int h,
                                          int radius, uint32_t color,
                                          int thickness) {
            if (w <= 0 || h <= 0) return;
            thickness = std::clamp(thickness, 1, 4);
            radius = std::clamp(radius, 0, (std::min)(w, h) / 2);
            if (radius <= 0) {
                Surface::OutlineBox(x, y, w, h, color, thickness);
                return;
            }

            const int left = x;
            const int top = y;
            const int right = x + w - 1;
            const int bottom = y + h - 1;
            const int segments = (std::max)(6, radius * 2);
            auto arc = [&](int cx, int cy, float start, float finish) {
                int previousX = 0;
                int previousY = 0;
                for (int i = 0; i <= segments; ++i) {
                    const float ratio = static_cast<float>(i) / segments;
                    const float angle = start + (finish - start) * ratio;
                    const int px = static_cast<int>(std::lround(cx + std::cos(angle) * radius));
                    const int py = static_cast<int>(std::lround(cy + std::sin(angle) * radius));
                    if (i > 0)
                        Surface::Line(previousX, previousY, px, py, color, thickness);
                    previousX = px;
                    previousY = py;
                }
            };

            Surface::Line(left + radius, top, right - radius, top, color, thickness);
            Surface::Line(left + radius, bottom, right - radius, bottom, color, thickness);
            Surface::Line(left, top + radius, left, bottom - radius, color, thickness);
            Surface::Line(right, top + radius, right, bottom - radius, color, thickness);
            arc(left + radius, top + radius, Pi, Pi * 1.5f);
            arc(right - radius, top + radius, Pi * 1.5f, Pi * 2.0f);
            arc(right - radius, bottom - radius, 0.0f, Pi * 0.5f);
            arc(left + radius, bottom - radius, Pi * 0.5f, Pi);
        }

        inline void DrawPreparedPlayer(const PreparedPlayer& player) {
            if (!player.valid || !player.entity || !player.team) return;
            const auto& e = *player.entity;
            const auto& team = *player.team;
            const auto& settings = team.visuals;

            if (settings.trail.enabled)
                DrawPreparedTrail(player);

            if (settings.box.enabled) {
                const uint32_t shadow = team.boxOutlineColor;
                const float boxThicknessValue = std::clamp(settings.boxThickness, 1.0f, 4.0f);
                const int boxThickness = std::clamp(
                    static_cast<int>(std::ceil(boxThicknessValue)), 1, 4);
                const int rounding = std::clamp(
                    static_cast<int>(std::lround(settings.boxRounding)), 0, 10);
                const int style = std::clamp(settings.boxStyle, 0, 5);
                const int l = player.left;
                const int t = player.headY;
                const int w = player.boxW;
                const int h = player.boxH;
                const bool filled = style == 1 || style == 3 || style == 5;
                const bool roundedStyle = style == 0 || style == 1;

                if (filled && w > 2 && h > 2) {
                    const int inset = (std::min)(boxThickness, (std::min)(w, h) / 2);
                    const int fillW = (std::max)(1, w - inset * 2);
                    const int fillH = (std::max)(1, h - inset * 2);
                    const int fillRadius = roundedStyle
                        ? (std::max)(0, rounding - inset) : 0;
                    if (fillRadius > 0) {
                        Surface::FillRoundedRectElliptic(
                            l + inset, t + inset, fillW, fillH,
                            fillRadius, fillRadius, team.boxFillColor);
                    } else {
                        Surface::FillRect(l + inset, t + inset, fillW, fillH,
                                          team.boxFillColor);
                    }
                }

                if (style == 0 || style == 1) {
                    // Normal / Normal + Filled. Width, outline and rounding
                    // are read directly from the ZIP menu's current values.
                    if (settings.boxOutlineEnabled) {
                        if (rounding > 0)
                            DrawRoundedOutlineBox(l - 1, t - 1, w + 2, h + 2,
                                                  rounding + 1, shadow, 1);
                        else
                            Surface::OutlineBox(l - 1, t - 1, w + 2, h + 2, shadow, 1);
                    }
                    if (rounding > 0)
                        DrawRoundedOutlineBox(l, t, w, h, rounding,
                                              team.boxColor, boxThickness);
                    else
                        Surface::OutlineBoxSmooth(l, t, w, h, team.boxColor, boxThicknessValue);
                }
                else if (style == 2 || style == 3) {
                    // Cornered / Cornered + Filled.
                    const int cx = (std::max)(5, w / 4);
                    const int cy = (std::max)(6, h / 5);
                    auto corners = [&](uint32_t color, float thick, int expand) {
                        const int x0 = l - expand, y0 = t - expand;
                        const int x1 = l + w - 1 + expand, y1 = t + h - 1 + expand;
                        Surface::LineSmooth(x0, y0, x0 + cx, y0, color, thick);
                        Surface::LineSmooth(x0, y0, x0, y0 + cy, color, thick);
                        Surface::LineSmooth(x1 - cx, y0, x1, y0, color, thick);
                        Surface::LineSmooth(x1, y0, x1, y0 + cy, color, thick);
                        Surface::LineSmooth(x0, y1, x0 + cx, y1, color, thick);
                        Surface::LineSmooth(x0, y1 - cy, x0, y1, color, thick);
                        Surface::LineSmooth(x1 - cx, y1, x1, y1, color, thick);
                        Surface::LineSmooth(x1, y1 - cy, x1, y1, color, thick);
                    };
                    if (settings.boxOutlineEnabled)
                        corners(shadow, boxThicknessValue, 1);
                    corners(team.boxColor, boxThicknessValue, 0);
                }
                else {
                    // Rainbow / Rainbow + Filled.
                    const float seconds = std::chrono::duration<float>(
                        Clock::now().time_since_epoch()).count();
                    float baseHue = std::fmod(seconds * 0.10f, 1.0f);
                    if (baseHue < 0.0f) baseHue += 1.0f;

                    if (settings.boxOutlineEnabled)
                        Surface::OutlineBox(l - 1, t - 1, w + 2, h + 2, shadow, 1);

                    const int perimeter = (std::max)(1, (w + h) * 2);
                    constexpr int segment = 4;
                    auto rainbowColor = [&](int distance) {
                        float hue = std::fmod(baseHue +
                            static_cast<float>(distance) / static_cast<float>(perimeter), 1.0f);
                        if (hue < 0.0f) hue += 1.0f;
                        return Surface::FromHSV(HSV{ hue, 0.92f, 1.0f }, 255);
                    };

                    int d = 0;
                    for (int px = 0; px < w; px += segment) {
                        const int len = (std::min)(segment, w - px);
                        Surface::FillRect(l + px, t, len, boxThickness, rainbowColor(d + px));
                    }
                    d += w;
                    for (int py = 0; py < h; py += segment) {
                        const int len = (std::min)(segment, h - py);
                        Surface::FillRect(l + w - boxThickness, t + py, boxThickness, len, rainbowColor(d + py));
                    }
                    d += h;
                    for (int px = 0; px < w; px += segment) {
                        const int len = (std::min)(segment, w - px);
                        Surface::FillRect(l + w - px - len, t + h - boxThickness, len, boxThickness, rainbowColor(d + px));
                    }
                    d += w;
                    for (int py = 0; py < h; py += segment) {
                        const int len = (std::min)(segment, h - py);
                        Surface::FillRect(l, t + h - py - len, boxThickness, len, rainbowColor(d + py));
                    }
                }
            }

            if (settings.health.enabled)
                DrawHealth(player.left, player.barY, player.boxW, player.barH,
                           player.displayHealth, settings, team.healthColor, team.healthSecondaryColor);
            if (settings.armor.enabled)
                DrawArmor(player.left, player.barY, player.boxW, player.barH,
                          e.armor, settings, team.armorColor, team.armorSecondaryColor);
            if (settings.snapline.enabled) {
                Surface::Line(
                    static_cast<int>(Surface::halfWidth), Surface::height - 2,
                    player.feetX, player.feetY,
                    team.snaplineColor,
                    std::clamp(settings.snapline.thickness, 1, 3));
            }
            if (settings.skeleton.enabled)
                DrawPreparedSkeleton(player);

            // Compact information labels use a smaller raster font as the
            // projected player gets smaller. This fixes far-away names / weapon /
            // distance text looking much larger than the player itself.
            const InfoFontMetrics infoFont = InfoFontForBoxHeight(player.boxH);
            if (settings.name.enabled && e.name[0]) {
                char label[19]{};
                // Keep far labels compact as well as physically smaller. Near
                // players can show more of the name; tiny boxes use fewer chars.
                const int maxChars = player.boxH >= 78 ? 18 : (player.boxH >= 44 ? 15 : 12);
                int n = 0;
                for (; n < maxChars && e.name[n]; ++n)
                    label[n] = static_cast<char>(std::toupper(
                        static_cast<unsigned char>(e.name[n])));
                label[n] = 0;

                // Center on the ESP box itself. headX can be a few pixels
                // off-center because it comes from a separate projected point.
                const int nameCenterX = player.left + player.boxW / 2;
                const int nameY = player.headY - infoFont.glyphH - 3;
                DrawInfoTextCentered(nameCenterX + 1, nameY + 1, label,
                                     Surface::Premultiply(0, 0, 0, 210), infoFont);
                DrawInfoTextCentered(nameCenterX, nameY, label,
                                     team.nameColor, infoFont);
            }

            // Anchor both info elements to the exact horizontal center of the
            // rendered box, not the separately projected feet point.
            const int infoCenterX = player.left + player.boxW / 2;
            int infoY = player.feetY + 3;
            if (settings.weapon.enabled && e.weaponId != 0) {
                if (const auto* icon = WeaponIconForId(e.weaponId)) {
                    const auto& sprite = WeaponSpriteForBoxHeight(*icon, player.boxH);
                    DrawWeaponSpriteCentered(infoCenterX + 1, infoY + 1, sprite,
                                             Surface::Premultiply(0, 0, 0, 210));
                    DrawWeaponSpriteCentered(infoCenterX, infoY, sprite,
                                             team.weaponColor);
                    infoY += sprite.height + 2;
                } else if (const char* label = WeaponLabelForId(e.weaponId)) {
                    DrawInfoTextCentered(infoCenterX + 1, infoY + 1, label,
                                         Surface::Premultiply(0, 0, 0, 210), infoFont);
                    DrawInfoTextCentered(infoCenterX, infoY, label,
                                         team.weaponColor, infoFont);
                    infoY += infoFont.glyphH + 2;
                }
            }

            if (settings.distance.enabled) {
                char distanceText[16]{};
                std::snprintf(distanceText, sizeof(distanceText), "%dM",
                              (std::max)(0, player.distanceMeters));
                DrawInfoTextCentered(infoCenterX + 1, infoY + 1, distanceText,
                                     Surface::Premultiply(0, 0, 0, 210), infoFont);
                DrawInfoTextCentered(infoCenterX, infoY, distanceText,
                                     team.distanceColor, infoFont);
            }
        }

        inline void DrawRadar() {
            if (!runtimeSettings.worldRadar || !Cache::frame.valid) return;

            const int size = 80 + static_cast<int>(runtimeSettings.worldRadarSize * 180.0f);
            const float rangeMeters = 20.0f + runtimeSettings.worldRadarRange * 480.0f;
            const float rangeSq = rangeMeters * rangeMeters;
            const int x = Surface::width - size - 24;
            const int y = 24;
            if (x < 0 || y + size > Surface::height) return;

            const uint32_t bg = Surface::Premultiply(8, 10, 10, 120);
            const uint32_t border = runtimeSettings.worldRadarPacked;
            Surface::FillRect(x, y, size, size, bg);
            Surface::OutlineBox(x, y, size, size, border, 1);
            Surface::Line(x + size / 2, y + 4, x + size / 2, y + size - 4,
                          Surface::Premultiply(150, 150, 150, 80), 1);
            Surface::Line(x + 4, y + size / 2, x + size - 4, y + size / 2,
                          Surface::Premultiply(150, 150, 150, 80), 1);

            const int cx = x + size / 2;
            const int cy = y + size / 2;
            Surface::FillRect(cx - 2, cy - 2, 5, 5, border);

            const float yaw = Cache::frame.anglesValid
                ? Cache::frame.viewAngles.y * Pi / 180.0f : 0.0f;
            const float cs = std::cos(-yaw);
            const float sn = std::sin(-yaw);
            const float scale = (size * 0.46f) / rangeMeters;

            for (const auto& e : Cache::entities) {
                if (!e.originValid || e.health <= 0 || e.health > 100) continue;

                const auto& team = TeamSnapshot(e.team, Cache::frame.localTeam);
                const auto& settings = team.visuals;
                if (!settings.enabled) continue;

                // "Only visible" is a screen-ESP filter only. Radar intentionally
                // shows cached players regardless of spotted/visibility state.
                const float dxM = (e.origin.x - Cache::frame.localOrigin.x) / UnitsPerMeter;
                const float dyM = (e.origin.y - Cache::frame.localOrigin.y) / UnitsPerMeter;
                if (dxM * dxM + dyM * dyM > rangeSq) continue;

                // View-relative radar:
                //   +forward = top of radar
                //   +right   = right side of radar
                // Source/CS yaw 0 faces +X, so derive forward/right directly from yaw
                // instead of drawing the rotated world X component on the horizontal axis.
                const float forward = dxM * cs - dyM * sn;
                const float right   = dxM * sn + dyM * cs;
                const int px = cx - static_cast<int>(right * scale);
                const int py = cy - static_cast<int>(forward * scale);
                Surface::FillRect(px - 2, py - 2, 5, 5, team.boxColor);
            }
        }

        inline void Render() {
            if (!Cache::frame.valid) return;

            // Phase 1: workers share one immutable frame/settings snapshot and
            // prepare/cull/project entities into independent slots.
            PreparePlayers();

            // Phase 2: rasterize everything together on the render thread, then
            // Tick() performs one layered-window Present() for the whole frame.
            DrawRadar();
            for (const auto& player : preparedPlayers)
                DrawPreparedPlayer(player);
        }
    }

    inline bool initialized = false;

    inline bool Initialize(HINSTANCE instance) {
        if (initialized) return true;
        if (!Surface::Initialize(instance)) return false;
        if (!OffsetFile::Load()) {
            MessageBoxW(nullptr,
                L"Missing output folder or offset files.\r\n\r\n"
                L"Place an output folder next to the exe with:\r\n"
                L"  offsets.hpp\r\n"
                L"  client_dll.hpp\r\n\r\n"
                L"The program will not run without them.",
                L"Maotaw - offsets required",
                MB_OK | MB_ICONERROR);
            return false;
        }
        WorkerPool::Start();
        Performance::Reset();
        FrameLimiter::Reset();
        Memory::EnsureAttached();
        initialized = true;
        return true;
    }

    inline void Tick() {
        if (!initialized) return;
        const auto started = Clock::now();
        if (!FrameLimiter::Ready(started)) return;

        if (!Memory::EnsureAttached()) {
            AimAssist::ResetRecoil();
            Surface::Hide();
            return;
        }

        // hide when cs2 is minimized / not around; stay up if our menu is focused
        if (!Surface::IsGameForeground()) {
            AimAssist::ResetRecoil();
            Surface::Hide();
            return;
        }

        const auto now = Clock::now();
        const bool structuralRefreshDue =
            Cache::nextSlot > Cache::MaxSlots && now >= Cache::nextRefresh;
        if (!Surface::SyncToGameWindow(structuralRefreshDue)) {
            AimAssist::ResetRecoil();
            Surface::Hide();
            return;
        }

        SnapshotRuntimeSettings();
        Cache::Update();
        AimAssist::Tick();
        Surface::BeginFrame();
        ESP::Render();
        Surface::Present();
        Performance::Record(started, Clock::now());
    }

    [[nodiscard]] inline bool HasGameWindow() {
        return Surface::targetWindow && IsWindow(Surface::targetWindow) &&
               IsWindowVisible(Surface::targetWindow);
    }

    [[nodiscard]] inline HWND FindGameWindowNow() {
        if (!Memory::EnsureAttached()) return nullptr;
        return Surface::FindTargetWindow();
    }

    inline bool FocusGameWindow() {
        HWND game = FindGameWindowNow();
        if (!game) return false;
        if (IsIconic(game)) ShowWindow(game, SW_RESTORE);
        BringWindowToTop(game);
        return SetForegroundWindow(game) != FALSE;
    }

    [[nodiscard]] inline int OverlayTargetFps() { return FrameLimiter::TargetFps(); }
    [[nodiscard]] inline int DisplayRefreshHz() { return FrameLimiter::DisplayRefreshHz(); }
    [[nodiscard]] inline float Fps() { return Performance::fps; }
    [[nodiscard]] inline float FrameMs() { return Performance::frameMs; }
    [[nodiscard]] inline float WorkMs() { return Performance::workMs; }
    [[nodiscard]] inline unsigned ThreadCount() { return WorkerPool::TotalThreads(); }

    inline void Shutdown() {
        if (!initialized) return;
        WorkerPool::Stop();
        Memory::Detach();
        Surface::Shutdown();
        Cache::entities.clear();
        Cache::pending.clear();
        ESP::preparedPlayers.clear();
        Cache::frame = {};
        Performance::Reset();
        FrameLimiter::Reset();
        initialized = false;
    }
}
