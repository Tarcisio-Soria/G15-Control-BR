// G15 Control BR - GpuNvml.cpp
#include "GpuNvml.h"
#include <windows.h>

// Tipos minimos da NVML (sem depender do header do SDK)
typedef int   nvmlReturn_t;            // 0 = NVML_SUCCESS
typedef void* nvmlDevice_t;
static const int NVML_SUCCESS = 0;
static const int NVML_TEMPERATURE_GPU = 0;

typedef nvmlReturn_t (*PFN_init)();
typedef nvmlReturn_t (*PFN_shutdown)();
typedef nvmlReturn_t (*PFN_getCount)(unsigned*);
typedef nvmlReturn_t (*PFN_getHandle)(unsigned, nvmlDevice_t*);
typedef nvmlReturn_t (*PFN_getName)(nvmlDevice_t, char*, unsigned);
typedef nvmlReturn_t (*PFN_getTemp)(nvmlDevice_t, int, unsigned*);
typedef nvmlReturn_t (*PFN_getFan)(nvmlDevice_t, unsigned*);
typedef nvmlReturn_t (*PFN_getUtil)(nvmlDevice_t, unsigned*); // usa struct; ver abaixo

// nvmlUtilization_t = { unsigned gpu; unsigned memory; }
struct nvmlUtilization { unsigned gpu; unsigned memory; };
typedef nvmlReturn_t (*PFN_getUtilS)(nvmlDevice_t, nvmlUtilization*);
typedef nvmlReturn_t (*PFN_getPower)(nvmlDevice_t, unsigned*);        // milliwatts
typedef nvmlReturn_t (*PFN_getClock)(nvmlDevice_t, int, unsigned*);   // tipo, MHz
static const int NVML_CLOCK_GRAPHICS = 0;

bool GpuNvml::init() {
    HMODULE h = LoadLibraryA("nvml.dll");
    if (!h) {
        // caminho alternativo (algumas instalacoes)
        h = LoadLibraryA("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }
    if (!h) return false;
    dll_ = h;

    pInit_      = (void*)GetProcAddress(h, "nvmlInit_v2");
    if (!pInit_) pInit_ = (void*)GetProcAddress(h, "nvmlInit");
    pShutdown_  = (void*)GetProcAddress(h, "nvmlShutdown");
    pGetCount_  = (void*)GetProcAddress(h, "nvmlDeviceGetCount_v2");
    if (!pGetCount_) pGetCount_ = (void*)GetProcAddress(h, "nvmlDeviceGetCount");
    pGetHandle_ = (void*)GetProcAddress(h, "nvmlDeviceGetHandleByIndex_v2");
    if (!pGetHandle_) pGetHandle_ = (void*)GetProcAddress(h, "nvmlDeviceGetHandleByIndex");
    pGetName_   = (void*)GetProcAddress(h, "nvmlDeviceGetName");
    pGetTemp_   = (void*)GetProcAddress(h, "nvmlDeviceGetTemperature");
    pGetFan_    = (void*)GetProcAddress(h, "nvmlDeviceGetFanSpeed");
    pGetUtil_   = (void*)GetProcAddress(h, "nvmlDeviceGetUtilizationRates");
    pGetPower_  = (void*)GetProcAddress(h, "nvmlDeviceGetPowerUsage");
    pGetClock_  = (void*)GetProcAddress(h, "nvmlDeviceGetClockInfo");

    if (!pInit_ || !pShutdown_ || !pGetCount_ || !pGetHandle_ || !pGetTemp_) return false;

    if (((PFN_init)pInit_)() != NVML_SUCCESS) return false;

    unsigned c = 0;
    if (((PFN_getCount)pGetCount_)(&c) != NVML_SUCCESS) { ((PFN_shutdown)pShutdown_)(); return false; }
    count_ = c;
    ok_ = true;
    return true;
}

GpuNvml::~GpuNvml() {
    if (ok_ && pShutdown_) ((PFN_shutdown)pShutdown_)();
    if (dll_) FreeLibrary((HMODULE)dll_);
}

bool GpuNvml::handle(unsigned index, void** devOut) {
    if (!ok_ || index >= count_) return false;
    nvmlDevice_t dev = nullptr;
    if (((PFN_getHandle)pGetHandle_)(index, &dev) != NVML_SUCCESS) return false;
    *devOut = dev;
    return true;
}

std::string GpuNvml::name(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev) || !pGetName_) return "";
    char buf[96] = {0};
    if (((PFN_getName)pGetName_)(dev, buf, sizeof(buf)) != NVML_SUCCESS) return "";
    return std::string(buf);
}

std::optional<unsigned> GpuNvml::temperatureC(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev)) return std::nullopt;
    unsigned t = 0;
    if (((PFN_getTemp)pGetTemp_)(dev, NVML_TEMPERATURE_GPU, &t) != NVML_SUCCESS) return std::nullopt;
    return t;
}

std::optional<unsigned> GpuNvml::fanPercent(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev) || !pGetFan_) return std::nullopt;
    unsigned f = 0;
    if (((PFN_getFan)pGetFan_)(dev, &f) != NVML_SUCCESS) return std::nullopt;
    return f;
}

std::optional<unsigned> GpuNvml::utilizationPercent(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev) || !pGetUtil_) return std::nullopt;
    nvmlUtilization u{0, 0};
    if (((PFN_getUtilS)pGetUtil_)(dev, &u) != NVML_SUCCESS) return std::nullopt;
    return u.gpu;
}

std::optional<unsigned> GpuNvml::powerW(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev) || !pGetPower_) return std::nullopt;
    unsigned mw = 0;
    if (((PFN_getPower)pGetPower_)(dev, &mw) != NVML_SUCCESS) return std::nullopt;
    return (mw + 500) / 1000; // mW -> W arredondado
}

std::optional<unsigned> GpuNvml::clockMHz(unsigned index) {
    void* dev = nullptr;
    if (!handle(index, &dev) || !pGetClock_) return std::nullopt;
    unsigned mhz = 0;
    if (((PFN_getClock)pGetClock_)(dev, NVML_CLOCK_GRAPHICS, &mhz) != NVML_SUCCESS) return std::nullopt;
    return mhz;
}
