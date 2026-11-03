#include "procstate.h"
#include "log.h"

// Older SDKs lack this flag (Windows 11): 1 = Windows may ignore the process's
// timer-resolution requests, which breaks frame limiters built on timeBeginPeriod.
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

namespace {

// ProcessIoPriority (33) is only reachable through the Nt* calls.
const ULONG kProcessIoPriority = 33;
using NtQueryInfoFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
using NtSetInfoFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);

template <typename Fn>
Fn NtProc(const char* name)
{
    return reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), name));
}

// 0 very low, 1 low, 2 normal, 3 high; -1 if unknown.
int GetIoPriority(HANDLE process)
{
    ULONG prio = 0;
    auto query = NtProc<NtQueryInfoFn>("NtQueryInformationProcess");
    if (query && query(process, kProcessIoPriority, &prio, sizeof(prio), nullptr) >= 0)
        return int(prio);
    return -1;
}

const wchar_t* PriorityName(DWORD cls)
{
    switch (cls) {
    case IDLE_PRIORITY_CLASS:         return L"idle";
    case BELOW_NORMAL_PRIORITY_CLASS: return L"below normal";
    case NORMAL_PRIORITY_CLASS:       return L"normal";
    case ABOVE_NORMAL_PRIORITY_CLASS: return L"above normal";
    case HIGH_PRIORITY_CLASS:         return L"high";
    case REALTIME_PRIORITY_CLASS:     return L"realtime";
    default:                          return L"?";
    }
}

} // namespace

void LogProcessState(HANDLE process, const wchar_t* label)
{
    DWORD_PTR procMask = 0, sysMask = 0;
    GetProcessAffinityMask(process, &procMask, &sysMask);
    BOOL inJob = FALSE;
    IsProcessInJob(process, nullptr, &inJob);

    MEMORY_PRIORITY_INFORMATION mem = {};
    GetProcessInformation(process, ProcessMemoryPriority, &mem, sizeof(mem));

    // Only reports settings made explicitly (by the process or someone else);
    // Windows' own automatic EcoQoS decisions aren't visible here.
    PROCESS_POWER_THROTTLING_STATE pt = {PROCESS_POWER_THROTTLING_CURRENT_VERSION};
    GetProcessInformation(process, ProcessPowerThrottling, &pt, sizeof(pt));

    Log(L"%s: priority %s, affinity 0x%IX of 0x%IX%s, in job: %s, I/O priority %d, "
        L"memory priority %lu, power throttling control 0x%lX state 0x%lX",
        label, PriorityName(GetPriorityClass(process)), procMask, sysMask,
        procMask != sysMask ? L" (RESTRICTED)" : L"", inJob ? L"yes" : L"no",
        GetIoPriority(process), mem.MemoryPriority, pt.ControlMask, pt.StateMask);
}

void LogOwnJobLimits()
{
    BOOL inJob = FALSE;
    if (!IsProcessInJob(GetCurrentProcess(), nullptr, &inJob) || !inJob)
        return;

    // nullptr = the job this process is in.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION ext = {};
    if (QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &ext, sizeof(ext), nullptr)) {
        const auto& b = ext.BasicLimitInformation;
        Log(L"Launcher job: limit flags 0x%lX, priority class 0x%lX, affinity 0x%IX, "
            L"breakaway %s", b.LimitFlags, b.PriorityClass, b.Affinity,
            (b.LimitFlags & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK))
                ? L"allowed" : L"not allowed");
    } else {
        Log(L"Launcher job: limits not readable (%lu)", GetLastError());
    }

    JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpu = {};
    if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &cpu, sizeof(cpu), nullptr) &&
        cpu.ControlFlags != 0)
        Log(L"Launcher job: CPU rate control flags 0x%lX, rate %lu", cpu.ControlFlags, cpu.CpuRate);
}

void TuneChildProcess(HANDLE process)
{
    // Children inherit idle / below-normal priority from their parent.
    DWORD cls = GetPriorityClass(process);
    if (cls == IDLE_PRIORITY_CLASS || cls == BELOW_NORMAL_PRIORITY_CLASS) {
        if (SetPriorityClass(process, NORMAL_PRIORITY_CLASS))
            Log(L"Raised game priority from %s to normal", PriorityName(cls));
    }

    // Children inherit the parent's CPU affinity; give the game every core.
    DWORD_PTR procMask = 0, sysMask = 0;
    if (GetProcessAffinityMask(process, &procMask, &sysMask) && procMask != sysMask) {
        if (SetProcessAffinityMask(process, sysMask))
            Log(L"Widened game CPU affinity from 0x%IX to 0x%IX", procMask, sysMask);
        else
            Log(L"WARN: could not widen game CPU affinity (%lu)", GetLastError());
    }

    // Opt out of EcoQoS: full CPU speed and honoured timer-resolution requests,
    // whatever Windows decides about the launcher's process tree.
    PROCESS_POWER_THROTTLING_STATE pt = {PROCESS_POWER_THROTTLING_CURRENT_VERSION};
    pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    pt.StateMask = 0;
    if (!SetProcessInformation(process, ProcessPowerThrottling, &pt, sizeof(pt))) {
        // Older Windows rejects the timer flag; retry with execution speed only.
        pt.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        if (!SetProcessInformation(process, ProcessPowerThrottling, &pt, sizeof(pt)))
            Log(L"WARN: could not turn off power throttling (%lu)", GetLastError());
    }

    MEMORY_PRIORITY_INFORMATION mem = {MEMORY_PRIORITY_NORMAL};
    SetProcessInformation(process, ProcessMemoryPriority, &mem, sizeof(mem));

    int io = GetIoPriority(process);
    if (io >= 0 && io < 2) {
        ULONG normal = 2;
        auto set = NtProc<NtSetInfoFn>("NtSetInformationProcess");
        if (set && set(process, kProcessIoPriority, &normal, sizeof(normal)) >= 0)
            Log(L"Raised game I/O priority from %d to normal", io);
    }
}
