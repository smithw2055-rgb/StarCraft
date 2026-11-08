#include "Core/Platform/Platform.h"
#include "Main/GameRuntimeState.h"
#include "Render/Draw2D.h"

// Keep this translation unit tied to the complete GameController runtime layout.

#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#endif

namespace
{
    Game::Client::GameLoop::GameController gController;
    std::string gMapPathOverride;
    std::string gScreenshotPath;
    int gScreenshotFrame = 30;
    bool gDirectTestMap = false;
    bool gHasCameraOverride = false;
    float gCameraX = 0.0f;
    float gCameraY = 0.0f;
    bool gEnableGuiHud = false;
    bool gHighDpi = true;
    bool gInteractionDemo = false;
    int gExitAfterFrame = -1;
    int gFrameCount = 0;

    void SetRuntimeEnvFlag(const char* Name)
    {
#if defined(_WIN32)
        _putenv_s(Name, "1");
#else
        setenv(Name, "1", 1);
#endif
    }

#if defined(_WIN32)
    LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo)
    {
        CreateDirectoryA("artifacts", nullptr);
        HANDLE hLogFile = CreateFileA("artifacts\\crash_log.txt", GENERIC_WRITE, 0, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        auto LogBoth = [&](const char* Text) {
            if (!Text) return;
            DWORD Len = (DWORD)strlen(Text);
            DWORD Written = 0;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), Text, Len, &Written, nullptr);
            if (hLogFile != INVALID_HANDLE_VALUE)
                WriteFile(hLogFile, Text, Len, &Written, nullptr);
        };

        LogBoth("\n=======================================================\n");
        LogBoth("[CRASH DETECTED] Unhandled exception occurred!\n");

        DWORD Code = ExceptionInfo && ExceptionInfo->ExceptionRecord ? ExceptionInfo->ExceptionRecord->ExceptionCode : 0;
        void* Addr = ExceptionInfo && ExceptionInfo->ExceptionRecord ? ExceptionInfo->ExceptionRecord->ExceptionAddress : nullptr;

        char HeaderBuf[256];
        snprintf(HeaderBuf, sizeof(HeaderBuf), "Exception Code: 0x%08lX\nException Address: 0x%p\n", Code, Addr);
        LogBoth(HeaderBuf);

        if (ExceptionInfo && ExceptionInfo->ExceptionRecord &&
            ExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
            ExceptionInfo->ExceptionRecord->NumberParameters >= 2)
        {
            ULONG_PTR AccessType = ExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR FaultAddr = ExceptionInfo->ExceptionRecord->ExceptionInformation[1];
            char AvBuf[256];
            snprintf(AvBuf, sizeof(AvBuf), "Access Violation: Attempted to %s address 0x%p\n",
                     (AccessType == 0 ? "read from" : (AccessType == 1 ? "write to" : "execute")),
                     (void*)FaultAddr);
            LogBoth(AvBuf);
        }

        // Minidump
        HANDLE hDumpFile = CreateFileA("artifacts\\crash.dmp", GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDumpFile != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION Mei{};
            Mei.ThreadId = GetCurrentThreadId();
            Mei.ExceptionPointers = ExceptionInfo;
            Mei.ClientPointers = FALSE;

            BOOL Success = MiniDumpWriteDump(
                GetCurrentProcess(),
                GetCurrentProcessId(),
                hDumpFile,
                MiniDumpNormal,
                &Mei,
                nullptr,
                nullptr);
            CloseHandle(hDumpFile);
            if (Success)
                LogBoth("Minidump successfully written to artifacts\\crash.dmp\n");
            else
            {
                char ErrBuf[128];
                snprintf(ErrBuf, sizeof(ErrBuf), "MiniDumpWriteDump failed with error: %lu\n", GetLastError());
                LogBoth(ErrBuf);
            }
        }

        // Stack trace
        LogBoth("\n--- STACK TRACE ---\n");
        HANDLE Process = GetCurrentProcess();
        SymInitialize(Process, nullptr, TRUE);

        CONTEXT ContextRecord{};
        if (ExceptionInfo && ExceptionInfo->ContextRecord)
            ContextRecord = *ExceptionInfo->ContextRecord;
        else
            RtlCaptureContext(&ContextRecord);

        STACKFRAME64 StackFrame{};
        StackFrame.AddrPC.Offset = ContextRecord.Rip;
        StackFrame.AddrPC.Mode = AddrModeFlat;
        StackFrame.AddrFrame.Offset = ContextRecord.Rbp;
        StackFrame.AddrFrame.Mode = AddrModeFlat;
        StackFrame.AddrStack.Offset = ContextRecord.Rsp;
        StackFrame.AddrStack.Mode = AddrModeFlat;

        for (int Frame = 0; Frame < 64; ++Frame)
        {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, Process, GetCurrentThread(),
                             &StackFrame, &ContextRecord, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            {
                break;
            }

            if (StackFrame.AddrPC.Offset == 0)
                break;

            DWORD64 Address = StackFrame.AddrPC.Offset;
            char SymbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)]{};
            PSYMBOL_INFO Symbol = (PSYMBOL_INFO)SymbolBuffer;
            Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            Symbol->MaxNameLen = MAX_SYM_NAME;

            DWORD64 Displacement = 0;
            char FrameBuf[512];
            if (SymFromAddr(Process, Address, &Displacement, Symbol))
            {
                IMAGEHLP_LINE64 Line{};
                Line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD LineDisplacement = 0;
                if (SymGetLineFromAddr64(Process, Address, &LineDisplacement, &Line))
                {
                    snprintf(FrameBuf, sizeof(FrameBuf), "  [%02d] 0x%016llX %s (%s:%lu)\n",
                             Frame, Address, Symbol->Name, Line.FileName, Line.LineNumber);
                }
                else
                {
                    snprintf(FrameBuf, sizeof(FrameBuf), "  [%02d] 0x%016llX %s + 0x%llX\n",
                             Frame, Address, Symbol->Name, Displacement);
                }
            }
            else
            {
                snprintf(FrameBuf, sizeof(FrameBuf), "  [%02d] 0x%016llX (Unknown Symbol)\n",
                         Frame, Address);
            }
            LogBoth(FrameBuf);
        }

        LogBoth("=======================================================\n");
        if (hLogFile != INVALID_HANDLE_VALUE)
            CloseHandle(hLogFile);

        return EXCEPTION_CONTINUE_SEARCH;
    }
#endif

}

static void InitCb()
{
    gController.Initialize(sapp_width(), sapp_height());
}

static void FrameCb()
{
    float Dt = static_cast<float>(sapp_frame_duration());
    int W = sapp_width();
    int H = sapp_height();

    gController.Update(Dt, W, H);
    gController.Render(W, H);

    ++gFrameCount;
    if (!gScreenshotPath.empty() && gFrameCount == gScreenshotFrame)
    {
        if (Render::CaptureScreenshot(gScreenshotPath))
        {
            std::cout << "StarCraft: screenshot saved to " << gScreenshotPath << std::endl;
        }
        else
        {
            std::cerr << "StarCraft: failed to capture screenshot to " << gScreenshotPath << std::endl;
        }
    }

    if (gExitAfterFrame > 0)
    {
        if (gFrameCount >= gExitAfterFrame)
            Core::Platform::RequestQuit();
    }
}

static void EventCb(const sapp_event* Event)
{
    gController.OnEvent(Event);
}

static void CleanupCb()
{
    gController.Shutdown();
}

sapp_desc sokol_main(int argc, char* argv[])
{
#if defined(_WIN32)
    SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);
#endif

    for (int I = 1; I < argc; ++I)
    {
        if (std::strcmp(argv[I], "--map") == 0 && I + 1 < argc)
            gMapPathOverride = argv[++I];
        else if (std::strcmp(argv[I], "--test-map") == 0 && I + 1 < argc)
        {
            gMapPathOverride = argv[++I];
            gDirectTestMap = true;
        }
        else if (std::strcmp(argv[I], "--camera") == 0 && I + 2 < argc)
        {
            gHasCameraOverride = true;
            gCameraX = std::strtof(argv[++I], nullptr);
            gCameraY = std::strtof(argv[++I], nullptr);
        }
        else if (std::strcmp(argv[I], "--gui-hud") == 0)
        {
            gEnableGuiHud = true;
        }
        else if (std::strcmp(argv[I], "--low-dpi") == 0)
        {
            gHighDpi = false;
        }
        else if (std::strcmp(argv[I], "--exit-after-frame") == 0 && I + 1 < argc)
        {
            gExitAfterFrame = std::atoi(argv[++I]);
        }
        else if (std::strcmp(argv[I], "--screenshot") == 0 && I + 1 < argc)
        {
            gScreenshotPath = argv[++I];
        }
        else if (std::strcmp(argv[I], "--screenshot-frame") == 0 && I + 1 < argc)
        {
            gScreenshotFrame = std::atoi(argv[++I]);
        }
        else if (std::strcmp(argv[I], "--interaction-demo") == 0)
        {
            gInteractionDemo = true;
            SetRuntimeEnvFlag("STARCRAFT_INTERACTION_DEMO");
        }
        else if (std::strcmp(argv[I], "--debug-harvest") == 0)
        {
            SetRuntimeEnvFlag("STARCRAFT_DEBUG_HARVEST");
        }
        else if (std::strcmp(argv[I], "--debug-construction") == 0)
        {
            SetRuntimeEnvFlag("STARCRAFT_DEBUG_CONSTRUCTION");
        }
        else if (std::strcmp(argv[I], "--debug-iscript") == 0)
        {
            SetRuntimeEnvFlag("STARCRAFT_DEBUG_ISCRIPT_RUNTIME");
        }
        else if (gMapPathOverride.empty())
            gMapPathOverride = argv[I];
    }
    if (gExitAfterFrame <= 0)
    {
        if (const char* EnvExitFrame = std::getenv("STARCRAFT_EXIT_AFTER_FRAME"))
            gExitAfterFrame = std::atoi(EnvExitFrame);
    }
    if (gScreenshotPath.empty())
    {
        if (const char* EnvScreenshot = std::getenv("STARCRAFT_SCREENSHOT_PATH"))
            gScreenshotPath = EnvScreenshot;
    }

    if (!gMapPathOverride.empty())
    {
        if (gDirectTestMap)
        {
            std::cout << "StarCraft: direct test map requested: " << gMapPathOverride << std::endl;
            gController.SetCampaignRuntimeEnabled(false);
            gController.SetDirectStartEnabled(true);
        }
        gController.SetMapPathOverride(gMapPathOverride);
    }
    if (gHasCameraOverride)
        gController.SetInitialCameraOverride(gCameraX, gCameraY);
    if (gEnableGuiHud)
        gController.SetInitialGuiHudEnabled(true);
    if (gInteractionDemo)
    {
        std::cout << "StarCraft: interaction demo requested" << std::endl;
        gController.SetInteractionDemoEnabled(true);
    }

    sapp_desc Desc = {};
    Desc.init_cb = InitCb;
    Desc.frame_cb = FrameCb;
    Desc.event_cb = EventCb;
    Desc.cleanup_cb = CleanupCb;
    Desc.width = 1280;
    Desc.height = 720;
    Desc.high_dpi = gHighDpi;
    Desc.sample_count = 1;
    Desc.window_title = "StarCraft";
    Desc.icon.sokol_default = true;
    Desc.logger.func = slog_func;
    return Desc;
}
