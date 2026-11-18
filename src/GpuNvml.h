// G15 Control BR - GpuNvml.h
// Leitura confiavel da GPU NVIDIA via NVML (nvml.dll carregada dinamicamente).
// Nao exige SDK nem instalacao: a dll vem com o driver (System32).
#pragma once
#include <optional>
#include <string>

class GpuNvml {
public:
    bool init();   // carrega nvml.dll e inicializa; false se indisponivel
    ~GpuNvml();

    bool     available() const { return ok_; }
    unsigned count() const { return count_; }
    std::string name(unsigned index = 0);
    std::optional<unsigned> temperatureC(unsigned index = 0); // graus C
    std::optional<unsigned> fanPercent(unsigned index = 0);   // % (pode nao existir em laptop)
    std::optional<unsigned> utilizationPercent(unsigned index = 0);
    std::optional<unsigned> powerW(unsigned index = 0);       // potencia em Watts
    std::optional<unsigned> clockMHz(unsigned index = 0);     // clock do nucleo grafico (MHz)

private:
    void*    dll_   = nullptr; // HMODULE
    bool     ok_    = false;
    unsigned count_ = 0;

    // ponteiros para funcoes da NVML
    void* pInit_        = nullptr;
    void* pShutdown_    = nullptr;
    void* pGetCount_    = nullptr;
    void* pGetHandle_   = nullptr;
    void* pGetName_     = nullptr;
    void* pGetTemp_     = nullptr;
    void* pGetFan_      = nullptr;
    void* pGetUtil_     = nullptr;
    void* pGetPower_    = nullptr;
    void* pGetClock_    = nullptr;

    bool handle(unsigned index, void** devOut);
};
