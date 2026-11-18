// G15 Control BR - WinPower.cpp
#include "WinPower.h"
#define NOMINMAX
#include <windows.h>
#include <string>

namespace {
    // GUIDs dos overlays de energia do Windows
    const GUID G_PERF = { 0xded574b5, 0x45a0, 0x4f42, { 0x87,0x37,0x46,0x34,0x5c,0x09,0xc2,0x38 } };
    const GUID G_EFF  = { 0x961cc777, 0x2547, 0x4f9d, { 0x81,0x74,0x7d,0x86,0x18,0x1b,0x8a,0x7a } };
    const GUID G_BAL  = { 0, 0, 0, { 0,0,0,0,0,0,0,0 } }; // GUID_NULL = Equilibrado (padrao)

    typedef DWORD (WINAPI *PFN_Set)(GUID);
    typedef DWORD (WINAPI *PFN_Get)(GUID*);
    PFN_Set pSet = nullptr;
    PFN_Get pGet = nullptr;
    bool loaded = false;

    void load() {
        if (loaded) return;
        loaded = true;
        HMODULE h = LoadLibraryA("powrprof.dll");
        if (h) {
            pSet = (PFN_Set)GetProcAddress(h, "PowerSetActiveOverlayScheme");
            pGet = (PFN_Get)GetProcAddress(h, "PowerGetActiveOverlayScheme");
        }
    }

    // Fallback confiavel: le o overlay ativo direto do registro do Windows.
    // (A API PowerGetActiveOverlayScheme as vezes nao retorna neste sistema.)
    WinPower::Overlay regOverlay() {
        char buf[80] = {0}; DWORD sz = sizeof(buf);
        LONG r = RegGetValueA(HKEY_LOCAL_MACHINE,
            "SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes",
            "ActiveOverlayAcPowerScheme", RRF_RT_REG_SZ, nullptr, buf, &sz);
        if (r != ERROR_SUCCESS) return WinPower::Overlay::Balanced; // ausente = equilibrado
        std::string s(buf);
        if (s.find("ded574b5") != std::string::npos) return WinPower::Overlay::BestPerformance;
        if (s.find("961cc777") != std::string::npos) return WinPower::Overlay::BestEfficiency;
        return WinPower::Overlay::Balanced;
    }
}

bool WinPower::setOverlay(Overlay o) {
    load();
    if (!pSet) return false;
    GUID g = (o == Overlay::BestPerformance) ? G_PERF
           : (o == Overlay::BestEfficiency)  ? G_EFF
           : G_BAL;
    return pSet(g) == 0;
}

WinPower::Overlay WinPower::getOverlay() {
    load();
    if (pGet) {
        GUID g;
        if (pGet(&g) == 0) {
            if (IsEqualGUID(g, G_PERF)) return Overlay::BestPerformance;
            if (IsEqualGUID(g, G_EFF))  return Overlay::BestEfficiency;
            return Overlay::Balanced;
        }
    }
    // API falhou: usa o registro.
    return regOverlay();
}

const char* WinPower::name(Overlay o) {
    switch (o) {
        case Overlay::BestPerformance: return "Desempenho maximo";
        case Overlay::BestEfficiency:  return "Eficiencia";
        case Overlay::Balanced:        return "Equilibrado";
        default:                       return "?";
    }
}
