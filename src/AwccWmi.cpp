// G15 Control BR - AwccWmi.cpp
#include "AwccWmi.h"
#include <comdef.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

static const wchar_t* kClass = L"AWCCWmiMethodFunction";

AwccWmi::~AwccWmi() {
    if (path_) SysFreeString(path_);
    if (cls_)  cls_->Release();
    if (svc_)  svc_->Release();
    if (loc_)  loc_->Release();
    if (comInited_) CoUninitialize();
}

bool AwccWmi::connect() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) { err_ = L"CoInitializeEx falhou"; return false; }
    comInited_ = true;

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    // RPC_E_TOO_LATE pode ocorrer se ja inicializado - nao e fatal
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { err_ = L"CoInitializeSecurity falhou"; return false; }

    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&loc_));
    if (FAILED(hr)) { err_ = L"CoCreateInstance(WbemLocator) falhou"; return false; }

    hr = loc_->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
        0, nullptr, nullptr, &svc_);
    if (FAILED(hr)) { err_ = L"ConnectServer(ROOT\\WMI) falhou"; return false; }

    hr = CoSetProxyBlanket(svc_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) { err_ = L"CoSetProxyBlanket falhou"; return false; }

    // Localiza a (unica) instancia e guarda seu __PATH
    IEnumWbemClassObject* en = nullptr;
    hr = svc_->CreateInstanceEnum(_bstr_t(kClass), WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY, nullptr, &en);
    if (FAILED(hr) || !en) { err_ = L"Classe AWCCWmiMethodFunction nao encontrada"; return false; }

    IWbemClassObject* obj = nullptr;
    ULONG got = 0;
    hr = en->Next(WBEM_INFINITE, 1, &obj, &got);
    en->Release();
    if (got == 0 || !obj) { err_ = L"Nenhuma instancia AWCC encontrada"; return false; }

    VARIANT vp; VariantInit(&vp);
    hr = obj->Get(L"__PATH", 0, &vp, nullptr, nullptr);
    if (SUCCEEDED(hr) && vp.vt == VT_BSTR) path_ = SysAllocString(vp.bstrVal);
    VariantClear(&vp);
    obj->Release();

    if (!path_) { err_ = L"Nao foi possivel obter __PATH da instancia"; return false; }
    return true;
}

// Chama um metodo (in: arg2, out: argr). Retorna nullopt em erro/valor invalido.
std::optional<uint32_t> AwccWmi::call(const wchar_t* method, uint32_t arg) {
    if (!svc_ || !path_) return std::nullopt;

    HRESULT hr = S_OK;
    if (!cls_) {
        hr = svc_->GetObject(_bstr_t(kClass), 0, nullptr, &cls_, nullptr);
        if (FAILED(hr) || !cls_) { cls_ = nullptr; return std::nullopt; }
    }

    IWbemClassObject* inDef = nullptr;
    hr = cls_->GetMethod(method, 0, &inDef, nullptr);
    if (FAILED(hr) || !inDef) return std::nullopt;

    IWbemClassObject* inInst = nullptr;
    hr = inDef->SpawnInstance(0, &inInst);
    inDef->Release();
    if (FAILED(hr) || !inInst) return std::nullopt;

    VARIANT v; VariantInit(&v);
    v.vt = VT_I4; v.lVal = static_cast<LONG>(arg);
    inInst->Put(L"arg2", 0, &v, 0);
    VariantClear(&v);

    IWbemClassObject* out = nullptr;
    hr = svc_->ExecMethod(path_, _bstr_t(method), 0, nullptr, inInst, &out, nullptr);
    inInst->Release();
    if (FAILED(hr) || !out) return std::nullopt;

    VARIANT r; VariantInit(&r);
    hr = out->Get(L"argr", 0, &r, nullptr, nullptr);
    out->Release();
    if (FAILED(hr)) { VariantClear(&r); return std::nullopt; }
    // argr e uint32 (chega como VT_I4); qualquer outro tipo e convertido ou rejeitado.
    if (r.vt != VT_I4 && r.vt != VT_UI4 && FAILED(VariantChangeType(&r, &r, 0, VT_I4))) {
        VariantClear(&r); return std::nullopt;
    }
    uint32_t res = static_cast<uint32_t>(r.lVal);
    VariantClear(&r);
    if (res == 0xFFFFFFFF) return std::nullopt; // -1 => nao suportado
    return res;
}

std::optional<uint32_t> AwccWmi::getSensorTemperature(uint32_t id) {
    if (id < SENSOR_ID_FIRST || id > SENSOR_ID_LAST) return std::nullopt;
    return thermalInformation(((id & 0xFF) << 8) | 0x04);
}
std::optional<uint32_t> AwccWmi::getFanRPM(uint32_t id) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return std::nullopt;
    return thermalInformation(((id & 0xFF) << 8) | 0x05);
}
std::optional<uint32_t> AwccWmi::getFanRPMPercent(uint32_t id) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return std::nullopt;
    return thermalInformation(((id & 0xFF) << 8) | 0x06);
}
std::optional<uint32_t> AwccWmi::getFanBoost(uint32_t id) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return std::nullopt;
    return thermalInformation(((id & 0xFF) << 8) | 0x0C);
}
std::optional<uint32_t> AwccWmi::getFanMaxRPM(uint32_t id) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return std::nullopt;
    return thermalInformation(((id & 0xFF) << 8) | 0x09);
}
// Enumeracao de dispositivos (getPowerID, sub 3). Retorna funcID: >0x100 sensor,
// >0x8f modo de energia, caso contrario ventoinha. 0/nullopt = fim da lista.
std::optional<uint32_t> AwccWmi::getDeviceByIndex(uint32_t index) {
    return thermalInformation(((index & 0xFF) << 8) | 0x03);
}
std::optional<uint32_t> AwccWmi::getCurrentPowerMode() {
    return thermalInformation(0x0B); // sub 0x0b, sem argumento
}
std::optional<uint32_t> AwccWmi::getFanRelatedSensorsCount(uint32_t id) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return std::nullopt;
    return getFanSensors(((id & 0xFF) << 8) | 0x01);
}
std::vector<uint32_t> AwccWmi::getFanRelatedSensorIds(uint32_t id) {
    std::vector<uint32_t> ids;
    auto cnt = getFanRelatedSensorsCount(id);
    if (!cnt) return ids;
    uint32_t n = (*cnt < 0x20) ? *cnt : 0x20;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t arg = ((i & 0xFF) << 16) | ((id & 0xFF) << 8) | 0x02;
        auto sid = getFanSensors(arg);
        if (sid && *sid >= SENSOR_ID_FIRST && *sid <= SENSOR_ID_LAST) ids.push_back(*sid);
    }
    return ids;
}

std::vector<uint32_t> AwccWmi::enumerateFans() {
    std::vector<uint32_t> fans;
    for (uint32_t id = FAN_ID_FIRST; id <= FAN_ID_LAST; ++id) {
        auto c = getFanRelatedSensorsCount(id);
        if (c && *c > 0 && *c < 0x100) fans.push_back(id);
    }
    return fans;
}
std::vector<uint32_t> AwccWmi::enumerateSensors() {
    std::vector<uint32_t> sensors;
    for (uint32_t id = SENSOR_ID_FIRST; id <= SENSOR_ID_LAST; ++id) {
        auto t = getSensorTemperature(id);
        if (t && *t > 0 && *t < 150) sensors.push_back(id); // temp plausivel em C
    }
    return sensors;
}

bool AwccWmi::setFanBoost(uint32_t id, uint32_t value) {
    if (id < FAN_ID_FIRST || id > FAN_ID_LAST) return false;
    if (value > 0xFF) value = 0xFF;
    uint32_t arg = ((value & 0xFF) << 16) | ((id & 0xFF) << 8) | 0x02;
    auto r = thermalControl(arg);
    return r && *r == 0;
}
bool AwccWmi::applyThermalMode(ThermalMode mode) {
    uint32_t arg = ((static_cast<uint32_t>(mode) & 0xFF) << 8) | 0x01;
    auto r = thermalControl(arg);
    return r && *r == 0;
}
