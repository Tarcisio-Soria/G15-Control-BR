// G15 Control BR - SysStats.h
// Sensores do sistema so para os graficos, lidos com APIs do Windows (sem driver, sem
// Administrador): uso e clock da CPU, memoria RAM e carga da bateria.
#pragma once
#include <optional>

struct SysSnapshot {
    // CPU
    std::optional<float>    cpuUse;        // uso medio, 0..100 %
    std::optional<unsigned> cpuMHz;        // clock efetivo medio (inclui boost), MHz
    // Memoria
    unsigned ramUsedMB = 0, ramTotalMB = 0;
    float    ramPct = 0.0f;
    // Bateria (so a carga)
    bool     batPresent = false;
    int      batPct = -1;                  // 0..100, -1 = desconhecido
};

namespace SysStats {
    // Atualiza tudo. Barato; chamar a cada leitura (2 s).
    // O uso da CPU e a diferenca desde a chamada anterior, entao a 1a chamada nao tem cpuUse.
    void read(SysSnapshot& s);
}
