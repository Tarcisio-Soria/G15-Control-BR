// G15 Control BR - AwccWmi.h
// Acesso as funcoes termicas da BIOS Alienware/Dell via WMI (root\WMI,
// classe AWCCWmiMethodFunction). Mesma interface usada pelo AWCC.
#pragma once

#include <windows.h>
#include <wbemidl.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class AwccWmi {
public:
    enum class ThermalMode : uint32_t {
        Custom   = 0x00,
        Balanced = 0x97,
        GMode    = 0xAB,
    };

    // Faixas de IDs (iguais no AWCC/tcc-g15)
    static constexpr uint32_t SENSOR_ID_FIRST = 0x01;
    static constexpr uint32_t SENSOR_ID_LAST  = 0x30;
    static constexpr uint32_t FAN_ID_FIRST    = 0x31;
    static constexpr uint32_t FAN_ID_LAST     = 0x63;

    AwccWmi() = default;
    ~AwccWmi();

    bool connect();                      // inicializa COM + WMI + localiza a instancia
    const std::wstring& lastError() const { return err_; }

    // Leitura (seguro)
    std::optional<uint32_t> getSensorTemperature(uint32_t sensorId); // graus C
    std::optional<uint32_t> getFanRPM(uint32_t fanId);
    std::optional<uint32_t> getFanRPMPercent(uint32_t fanId);
    std::optional<uint32_t> getFanBoost(uint32_t fanId);
    std::optional<uint32_t> getFanMaxRPM(uint32_t fanId);           // sub 9
    std::optional<uint32_t> getDeviceByIndex(uint32_t index);       // enumeracao via getPowerID (sub 3)
    std::optional<uint32_t> getCurrentPowerMode();                  // sub 0x0b (modo termico atual)
    std::optional<uint32_t> getFanRelatedSensorsCount(uint32_t fanId);
    std::vector<uint32_t> getFanRelatedSensorIds(uint32_t fanId); // sensores ligados a essa ventoinha

    std::vector<uint32_t> enumerateFans();    // 0x31..0x63 com sensores > 0
    std::vector<uint32_t> enumerateSensors(); // 0x01..0x30 com leitura valida

    // Controle (escreve no hardware - exige admin)
    bool setFanBoost(uint32_t fanId, uint32_t value); // 0..255 (0 = automatico)
    bool applyThermalMode(ThermalMode mode);

private:
    std::optional<uint32_t> call(const wchar_t* method, uint32_t arg);
    std::optional<uint32_t> thermalInformation(uint32_t arg) { return call(L"Thermal_Information", arg); }
    std::optional<uint32_t> thermalControl(uint32_t arg)     { return call(L"Thermal_Control", arg); }
    std::optional<uint32_t> getFanSensors(uint32_t arg)      { return call(L"GetFanSensors", arg); }

    bool           comInited_ = false;
    IWbemLocator*  loc_  = nullptr;
    IWbemServices* svc_  = nullptr;
    IWbemClassObject* cls_ = nullptr; // definicao da classe (cache p/ nao refazer GetObject a cada chamada)
    BSTR          path_ = nullptr; // __PATH da instancia AWCCWmiMethodFunction
    std::wstring   err_;
};
