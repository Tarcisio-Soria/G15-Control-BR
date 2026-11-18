// G15 Control BR - WinPower.h
// Controla o "modo de energia" do Windows 11 (o slider Eficiencia/Equilibrado/
// Desempenho) via PowerSetActiveOverlayScheme. Nao exige administrador.
#pragma once

namespace WinPower {
    enum class Overlay { BestEfficiency, Balanced, BestPerformance, Unknown };
    bool    setOverlay(Overlay o);
    Overlay getOverlay();
    const char* name(Overlay o);
}
