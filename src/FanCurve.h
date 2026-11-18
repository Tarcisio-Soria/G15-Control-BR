// G15 Control BR - FanCurve.h
// Curva temperatura->boost por ventoinha, com interpolacao e config em arquivo.
#pragma once
#include <string>
#include <vector>

struct CurvePoint { int temp; int boost; }; // boost 0-255

struct FanCurves {
    std::vector<CurvePoint> cpu;
    std::vector<CurvePoint> gpu;
    int intervalSec = 2;
    int hysteresis  = 8;   // so reaplica se o alvo mudar >= isto
};

FanCurves defaultCurves();

// Boost interpolado (linear, com clamp) para uma temperatura.
int interpolateBoost(const std::vector<CurvePoint>& c, int temp);

// Config (INI simples). loadConfig retorna false se o arquivo nao existir.
bool loadConfig(const std::wstring& path, FanCurves& out);
void saveConfig(const std::wstring& path, const FanCurves& c);
std::wstring iniPathNextToExe();   // <pasta do exe>\g15ctl.ini

void printCurves(const FanCurves& c);
