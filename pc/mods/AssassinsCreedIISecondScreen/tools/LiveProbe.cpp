// Development-only, out-of-process inspection of the exact AC II executable.
// Not included in the downloadable payload. Never writes game memory.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

DWORD target{};
FILE* report{};
BOOL CALLBACK Windows(HWND window, LPARAM key) {
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    if (pid != target || !IsWindowVisible(window)) return TRUE;
    char title[256]{}, cls[256]{};
    GetWindowTextA(window, title, sizeof(title)); GetClassNameA(window, cls, sizeof(cls));
    std::fprintf(report, "window=%p class=%s title=%s iconic=%d foreground=%p\n", window, cls, title, int(IsIconic(window)), GetForegroundWindow());
    if (key == -1 && !std::strcmp(cls, "ScimitarEngineWindowClass")) {
        ShowWindow(window, SW_RESTORE);
        SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(window); return FALSE;
    }
    if (key && !std::strcmp(cls, "ScimitarEngineWindowClass")) {
        auto thread = GetWindowThreadProcessId(window, nullptr);
        AttachThreadInput(GetCurrentThreadId(), thread, TRUE);
        SetForegroundWindow(window);
        SetActiveWindow(window); SetFocus(window);
        Sleep(100);
        if (GetForegroundWindow() != window) {
            std::fprintf(report, "key skipped: target not foreground (foreground=%p focus=%p)\n", GetForegroundWindow(), GetFocus());
            AttachThreadInput(GetCurrentThreadId(), thread, FALSE); return TRUE;
        }
        auto scan = MapVirtualKeyA(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
        INPUT input{}; input.type = INPUT_KEYBOARD;
        input.ki.wScan = static_cast<WORD>(scan); input.ki.dwFlags = KEYEVENTF_SCANCODE;
        SendInput(1, &input, sizeof(input)); Sleep(150);
        input.ki.dwFlags |= KEYEVENTF_KEYUP; SendInput(1, &input, sizeof(input));
        AttachThreadInput(GetCurrentThreadId(), thread, FALSE);
        return FALSE;
    }
    return TRUE;
}
int main(int argc, char** argv) {
    report = std::fopen("F:\\Download\\ac2-probe.txt", "wb"); if (!report) return 1;
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (Process32First(list, &entry)) do {
        if (!_stricmp(entry.szExeFile, "AssassinsCreedIIGame.exe")) {
            if (target) { std::fprintf(report, "ambiguous target\n"); CloseHandle(list); return 2; }
            target = entry.th32ProcessID;
        }
    } while (Process32Next(list, &entry));
    CloseHandle(list);
    std::fprintf(report, "pid=%lu\n", target); if (!target) return 3;
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, target);
    if (!process) { std::fprintf(report, "OpenProcess failed=%lu\n", GetLastError()); return 4; }
    // PE shape is checked here; hook activation separately requires the full SHA256.
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS32 pe{}; SIZE_T got{};
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(0x400000), &dos, sizeof(dos), &got) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 0 || dos.e_lfanew > 4096 ||
        !ReadProcessMemory(process, reinterpret_cast<void*>(0x400000 + dos.e_lfanew), &pe, sizeof(pe), &got) ||
        pe.Signature != IMAGE_NT_SIGNATURE || pe.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe.FileHeader.TimeDateStamp != 0x4b8c2c75 || pe.OptionalHeader.SizeOfImage != 0x216e000) {
        std::fprintf(report, "unsupported PE image\n"); CloseHandle(process); return 5;
    }
    LPARAM command = argc == 3 && !std::strcmp(argv[1], "key") ? std::strtoul(argv[2], nullptr, 16) : 0;
    if (argc == 2 && !std::strcmp(argv[1], "front")) command = -1;
    EnumWindows(Windows, command);
    if (argc == 5 && !std::strcmp(argv[1], "read")) {
        auto address = std::strtoul(argv[2], nullptr, 16);
        auto count = std::strtoul(argv[3], nullptr, 16);
        if (!count || count > 0x100000 || address > 0xffffffffUL - count) return 6;
        std::vector<unsigned char> buffer(count);
        bool ok = ReadProcessMemory(process, reinterpret_cast<void*>(address), buffer.data(), count, &got);
        std::fprintf(report, "read=%08lx bytes=%lu got=%lu ok=%d error=%lu\n", address, count, static_cast<unsigned long>(got), int(ok), GetLastError());
        if (ok && got == count) {
            FILE* out = std::fopen(argv[4], "wb"); if (!out) return 7;
            std::fwrite(buffer.data(), 1, count, out); std::fclose(out);
        }
    } else {
        CreateDirectoryA("F:\\Download\\ac2-roots", nullptr);
        for (auto address : {0x2212490UL, 0x2212570UL, 0x2214d00UL, 0x223f81cUL, 0x223f814UL, 0x223f820UL, 0x223c590UL}) {
            unsigned long value{};
            bool ok = ReadProcessMemory(process, reinterpret_cast<void*>(address), &value, sizeof(value), &got);
            std::fprintf(report, "%08lx=%08lx ok=%d\n", address, value, int(ok));
            unsigned char bytes[0x600]{};
            if (ok && value && ReadProcessMemory(process, reinterpret_cast<void*>(value), bytes, sizeof(bytes), &got) && got == sizeof(bytes)) {
                char path[128]{}; std::snprintf(path, sizeof(path), "F:\\Download\\ac2-roots\\%08lx.bin", address);
                FILE* out = std::fopen(path, "wb");
                if (out) { std::fwrite(bytes, 1, sizeof(bytes), out); std::fclose(out); }
            }
        }
    }
    CloseHandle(process); std::fclose(report); return 0;
}
