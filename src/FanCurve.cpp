// G15 Control BR - FanCurve.cpp
#include "FanCurve.h"
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>

FanCurves defaultCurves() {
    FanCurves c;
    c.cpu = { {50,0}, {60,60}, {70,120}, {80,190}, {90,255} };
    // RPM ~ 100 x boost%, e satura em ~58% (boost ~150): acima disso so faz barulho.
    c.gpu = { {40,50}, {50,90}, {60,140}, {65,150} };
    c.intervalSec = 2;
    c.hysteresis  = 8;
    return c;
}

int interpolateBoost(const std::vector<CurvePoint>& c, int temp) {
    if (c.empty()) return 0;
    if (temp <= c.front().temp) return c.front().boost;
    if (temp >= c.back().temp)  return c.back().boost;
    for (size_t i = 1; i < c.size(); ++i) {
        if (temp <= c[i].temp) {
            const auto& a = c[i-1];
            const auto& b = c[i];
            if (b.temp == a.temp) return b.boost;
            double f = double(temp - a.temp) / double(b.temp - a.temp);
            int v = int(a.boost + f * (b.boost - a.boost) + 0.5);
            return std::max(0, std::min(255, v));
        }
    }
    return c.back().boost;
}

std::wstring iniPathNextToExe() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t slash = p.find_last_of(L'\\');
    if (slash != std::wstring::npos) p = p.substr(0, slash + 1);
    return p + L"g15ctl.ini";
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool loadConfig(const std::wstring& path, FanCurves& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.cpu.clear(); out.gpu.clear();
    std::string line, section;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') { section = trim(line.substr(1, line.find(']') - 1)); continue; }
        // chave=valor ou temp,boost
        char sep = (line.find('=') != std::string::npos) ? '=' : ',';
        size_t pos = line.find(sep);
        if (pos == std::string::npos) continue;
        std::string a = trim(line.substr(0, pos));
        std::string b = trim(line.substr(pos + 1));
        if (section == "options") {
            if (a == "interval")   out.intervalSec = std::max(1, atoi(b.c_str()));
            if (a == "hysteresis") out.hysteresis  = std::max(1, atoi(b.c_str()));
        } else if (section == "cpu" || section == "gpu") {
            CurvePoint p{ atoi(a.c_str()), std::max(0, std::min(255, atoi(b.c_str()))) };
            (section == "cpu" ? out.cpu : out.gpu).push_back(p);
        }
    }
    auto byTemp = [](const CurvePoint& x, const CurvePoint& y){ return x.temp < y.temp; };
    std::sort(out.cpu.begin(), out.cpu.end(), byTemp);
    std::sort(out.gpu.begin(), out.gpu.end(), byTemp);
    if (out.cpu.empty() || out.gpu.empty()) { out = defaultCurves(); return true; }
    return true;
}

void saveConfig(const std::wstring& path, const FanCurves& c) {
    std::ofstream f(path);
    if (!f) return;
    f << "# G15 Control BR - curva de ventoinha (temp C, boost 0-255)\n";
    f << "# Edite a vontade; boost 0 = deixa no automatico do firmware.\n\n";
    f << "[options]\ninterval=" << c.intervalSec << "\nhysteresis=" << c.hysteresis << "\n\n";
    f << "[cpu]\n";
    for (auto& p : c.cpu) f << p.temp << "," << p.boost << "\n";
    f << "\n[gpu]\n";
    for (auto& p : c.gpu) f << p.temp << "," << p.boost << "\n";
}

void printCurves(const FanCurves& c) {
    printf("Intervalo=%ds  histerese=%d\n", c.intervalSec, c.hysteresis);
    printf("Curva CPU (temp->boost):");
    for (auto& p : c.cpu) printf("  %dC=%d", p.temp, p.boost);
    printf("\nCurva GPU (temp->boost):");
    for (auto& p : c.gpu) printf("  %dC=%d", p.temp, p.boost);
    printf("\n");
}
