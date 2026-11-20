// G15 Control BR - SysStats.cpp
#include "SysStats.h"
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <powrprof.h>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "powrprof.lib")

namespace {

// Mesmo layout de PROCESSOR_POWER_INFORMATION (ntpoapi.h, so no WDK).
struct ProcPowerInfo { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; };

// ---- CPU: uso -------------------------------------------------------------
ULONGLONG ft(const FILETIME& f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }

void readCpuUse(SysSnapshot& s) {
    static ULONGLONG pIdle = 0, pKernel = 0, pUser = 0;
    static bool have = false;
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return;
    ULONGLONG idle = ft(i), kernel = ft(k), user = ft(u);     // "kernel" ja inclui o tempo ocioso
    if (have) {
        ULONGLONG dIdle = idle - pIdle, dTotal = (kernel - pKernel) + (user - pUser);
        if (dTotal > 0) {
            double use = 100.0 * (double)(dTotal - dIdle) / (double)dTotal;
            s.cpuUse = (float)(use < 0 ? 0 : use > 100 ? 100 : use);
        }
    }
    pIdle = idle; pKernel = kernel; pUser = user; have = true;
}

// ---- CPU: clock efetivo ---------------------------------------------------
// "Processor Frequency" (base, MHz) x "% Processor Performance" (/100) = clock real, com boost.
PDH_HQUERY   g_q = nullptr;
PDH_HCOUNTER g_cPerf = nullptr, g_cFreq = nullptr;
int          g_pdhState = 0;     // 0 = nao tentou, 1 = ok, -1 = indisponivel

void pdhInit() {
    g_pdhState = -1;
    if (PdhOpenQueryW(nullptr, 0, &g_q) != ERROR_SUCCESS) return;
    bool a = PdhAddEnglishCounterW(g_q, L"\\Processor Information(_Total)\\% Processor Performance", 0, &g_cPerf) == ERROR_SUCCESS;
    bool b = PdhAddEnglishCounterW(g_q, L"\\Processor Information(_Total)\\Processor Frequency", 0, &g_cFreq) == ERROR_SUCCESS;
    if (!(a && b)) { PdhCloseQuery(g_q); g_q = nullptr; return; }
    PdhCollectQueryData(g_q);
    g_pdhState = 1;
}

void readCpuClock(SysSnapshot& s) {
    if (g_pdhState == 0) pdhInit();
    if (g_pdhState == 1 && PdhCollectQueryData(g_q) == ERROR_SUCCESS) {
        PDH_FMT_COUNTERVALUE perf{}, freq{};
        if (PdhGetFormattedCounterValue(g_cPerf, PDH_FMT_DOUBLE, nullptr, &perf) == ERROR_SUCCESS && perf.CStatus == ERROR_SUCCESS &&
            PdhGetFormattedCounterValue(g_cFreq, PDH_FMT_DOUBLE, nullptr, &freq) == ERROR_SUCCESS && freq.CStatus == ERROR_SUCCESS &&
            freq.doubleValue > 0.0) {
            double mhz = freq.doubleValue * perf.doubleValue / 100.0;
            if (mhz > 100.0 && mhz < 10000.0) { s.cpuMHz = (unsigned)(mhz + 0.5); return; }
        }
    }
    // Plano B: valor que o Windows informa por processador (menos fiel ao boost).
    SYSTEM_INFO si; GetSystemInfo(&si);
    std::vector<ProcPowerInfo> pi(si.dwNumberOfProcessors);
    if (CallNtPowerInformation(ProcessorInformation, nullptr, 0, pi.data(),
                               (ULONG)(pi.size() * sizeof(ProcPowerInfo))) == 0 && !pi.empty()) {
        double sum = 0; for (auto& p : pi) sum += p.CurrentMhz;
        if (sum > 0) s.cpuMHz = (unsigned)(sum / pi.size() + 0.5);
    }
}

// ---- Memoria --------------------------------------------------------------
void readRam(SysSnapshot& s) {
    MEMORYSTATUSEX m{}; m.dwLength = sizeof(m);
    if (!GlobalMemoryStatusEx(&m)) return;
    s.ramTotalMB = (unsigned)(m.ullTotalPhys / (1024 * 1024));
    s.ramUsedMB  = (unsigned)((m.ullTotalPhys - m.ullAvailPhys) / (1024 * 1024));
    s.ramPct     = (float)m.dwMemoryLoad;
}

// ---- Bateria (so a carga) -------------------------------------------------
void readBattery(SysSnapshot& s) {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return;
    s.batPresent = !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255;
    if (s.batPresent) s.batPct = ps.BatteryLifePercent <= 100 ? ps.BatteryLifePercent : -1;
}

} // namespace

void SysStats::read(SysSnapshot& s) {
    s = SysSnapshot{};                // campos sem leitura nesta rodada ficam vazios (nada de valor velho)
    readCpuUse(s);
    readCpuClock(s);
    readRam(s);
    readBattery(s);
}
