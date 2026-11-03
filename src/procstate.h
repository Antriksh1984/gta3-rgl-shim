#pragma once
#include <windows.h>

// Logs the scheduling state a child process can inherit or be held back by:
// priority class, CPU affinity, job membership, I/O and memory priority, and
// explicit power-throttling (EcoQoS) settings.
void LogProcessState(HANDLE process, const wchar_t* label);

// Logs the limits of the job this process belongs to (e.g. the launcher's).
void LogOwnJobLimits();

// Undoes inherited handicaps on a (still suspended) child so it runs like a
// game started from Explorer: at least normal priority, all CPU cores, normal
// I/O and memory priority, and power throttling explicitly turned off.
void TuneChildProcess(HANDLE process);
