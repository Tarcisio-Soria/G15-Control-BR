// G15 Control BR - main.cpp
// v2.0: monitor autossuficiente - CPU (AWCC) + GPU (NVML) + SSDs NVMe (SMART),
// controle de boost/modo, e diagnosticos (probe/rpmtest). Sem app externo.
#include "AwccWmi.h"
#include "GpuNvml.h"
#include "StorageTemp.h"
#include "FanCurve.h"
#include "KbdElc.h"
#include "Version.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const char* fanLabel(size_t idx) {
    switch (idx) { case 0: return "CPU"; case 1: return "GPU"; default: return "Fan"; }
}

static const char* modeName(uint32_t v) {
    switch (v) {
        case 0x97: case 0xa0: return "Balanced";
        case 0xa1: return "Performance";
        case 0xa3: return "Quiet";
        case 0xab: return "G-Mode";
        case 0x00: return "Custom/Manual";
        default:   return "?";
    }
}

struct FanInfo { uint32_t id; std::vector<uint32_t> sensors; };

static std::vector<FanInfo> mapFans(AwccWmi& a) {
    std::vector<FanInfo> out;
    for (uint32_t id : a.enumerateFans())
        out.push_back({ id, a.getFanRelatedSensorIds(id) });
    return out;
}

static void printFan(AwccWmi& a, size_t idx, const FanInfo& f) {
    auto boost = a.getFanBoost(f.id);
    auto mx    = a.getFanMaxRPM(f.id);
    auto rpm   = a.getFanRPM(f.id);
    printf("  %-4s (id 0x%02X)  RPM=%-5s boost=%-3s  maxRPM=%-5s\n",
        fanLabel(idx), f.id,
        rpm   ? std::to_string(*rpm).c_str()   : "?",
        boost ? std::to_string(*boost).c_str() : "?",
        mx    ? std::to_string(*mx).c_str()    : "?");
}

static std::string shorten(const std::string& s, size_t n) {
    return s.size() <= n ? s : s.substr(0, n);
}

static void printTemps(AwccWmi& a, GpuNvml& g, const std::vector<FanInfo>& fans) {
    printf("Temperaturas:\n");
    // CPU: sensor da AWCC associado a ventoinha da CPU (fallback 0x01)
    uint32_t cpuSensor = (!fans.empty() && !fans[0].sensors.empty()) ? fans[0].sensors[0] : 0x01;
    auto ct = a.getSensorTemperature(cpuSensor);
    printf("  CPU  (AWCC 0x%02X)  %s\n", cpuSensor, ct ? (std::to_string(*ct) + " C").c_str() : "n/d");
    // GPU: NVML (confiavel)
    if (g.available()) {
        auto gt = g.temperatureC(0);
        auto gu = g.utilizationPercent(0);
        auto gp = g.powerW(0);
        auto gc = g.clockMHz(0);
        printf("  GPU  (NVML)      %s   [uso %s, %s, %s]\n",
            gt ? (std::to_string(*gt) + " C").c_str() : "n/d",
            gu ? (std::to_string(*gu) + "%").c_str() : "?",
            gp ? (std::to_string(*gp) + "W").c_str() : "?",
            gc ? (std::to_string(*gc) + "MHz").c_str() : "?");
    } else {
        printf("  GPU  (NVML)      n/d (driver NVIDIA ausente)\n");
    }
    // SSDs NVMe (SMART)
    for (const auto& d : readNvmeTemps()) {
        printf("  SSD%d %-18s %s\n", d.index, shorten(d.model, 18).c_str(),
            d.tempC ? (std::to_string(*d.tempC) + " C").c_str() : "n/d");
    }
}

static void printStatus(AwccWmi& a, GpuNvml& g) {
    auto fans = mapFans(a);
    printf("\n==== G15 Control BR ====\n");
    printf("Ventoinhas:\n");
    if (fans.empty()) printf("  (nenhuma - rodou como admin?)\n");
    for (size_t i = 0; i < fans.size(); ++i) printFan(a, i, fans[i]);
    printTemps(a, g, fans);
    auto pm = a.getCurrentPowerMode();
    if (pm) printf("Modo atual: %s (0x%02X)\n", modeName(*pm), *pm);
    printf("========================\n\n");
}

static void monitor(AwccWmi& a, GpuNvml& g, int intervalSec) {
    if (intervalSec < 1) intervalSec = 2;
    auto fans = mapFans(a);
    printf("Monitor ao vivo - Ctrl+C para sair (intervalo %ds)\n", intervalSec);
    for (;;) {
        printf("\n");
        for (size_t i = 0; i < fans.size(); ++i) printFan(a, i, fans[i]);
        printTemps(a, g, fans);
        printf("--------------------------------\n");
        fflush(stdout);
        Sleep(static_cast<DWORD>(intervalSec) * 1000);
    }
}

// Diagnostico: enumeracao de dispositivos (metodo do alienfx-tools) + leituras cruas.
static void probe(AwccWmi& a) {
    printf("\n==== PROBE (getPowerID / sub 3) ====\n");
    std::vector<uint32_t> fans;
    for (uint32_t i = 0; i < 64; ++i) {
        auto v = a.getDeviceByIndex(i);
        if (!v || *v == 0) { printf("[idx %2u] fim\n", i); break; }
        uint32_t id = *v & 0xFF;
        if (*v >= 0x100 && *v <= 0x10F)      printf("[idx %2u] SENSOR    funcID=0x%04X id=0x%02X\n", i, *v, id);
        else if (*v > 0x8F)                  printf("[idx %2u] MODO      funcID=0x%04X val=0x%02X\n", i, *v, id);
        else { printf("[idx %2u] VENTOINHA funcID=0x%04X id=0x%02X\n", i, *v, id); fans.push_back(id); }
    }
    printf("--- RPM/maxRPM/%%/boost (AWCC) ---\n");
    for (uint32_t id : fans) {
        auto rpm = a.getFanRPM(id); auto mx = a.getFanMaxRPM(id);
        auto pc = a.getFanRPMPercent(id); auto bo = a.getFanBoost(id);
        printf("Fan 0x%02X: RPM=%-5s maxRPM=%-5s %%=%-4s boost=%-4s\n", id,
            rpm ? std::to_string(*rpm).c_str() : "?", mx ? std::to_string(*mx).c_str() : "?",
            pc ? std::to_string(*pc).c_str() : "?", bo ? std::to_string(*bo).c_str() : "?");
    }
    printf("====================================\n\n");
}

static void rpmtest(AwccWmi& a) {
    auto fans = a.enumerateFans();
    printf("\n==== RPM TEST (boost 100 temporario, restaura) ====\n");
    for (uint32_t id : fans) {
        auto r0 = a.getFanRPM(id);
        bool ok = a.setFanBoost(id, 100);
        Sleep(3000);
        auto r1 = a.getFanRPM(id); auto p1 = a.getFanRPMPercent(id); auto b1 = a.getFanBoost(id);
        a.setFanBoost(id, 0);
        printf("Fan 0x%02X: set=%s RPM antes=%s c/boost=%s %%=%s boostLido=%s\n", id,
            ok ? "OK" : "FALHOU",
            r0 ? std::to_string(*r0).c_str() : "?", r1 ? std::to_string(*r1).c_str() : "?",
            p1 ? std::to_string(*p1).c_str() : "?", b1 ? std::to_string(*b1).c_str() : "?");
    }
    printf("===================================================\n\n");
}

// ---- Motor de curva (Fase B) ----
static AwccWmi* g_awcc = nullptr;
static std::vector<uint32_t> g_fanIds;
static volatile bool g_stop = false;

static BOOL WINAPI ctrlHandler(DWORD t) {
    if (t == CTRL_C_EVENT || t == CTRL_BREAK_EVENT || t == CTRL_CLOSE_EVENT) {
        g_stop = true;
        if (g_awcc) for (uint32_t id : g_fanIds) g_awcc->setFanBoost(id, 0);
        printf("\n[ventoinhas restauradas ao automatico]\n");
        return TRUE;
    }
    return FALSE;
}

static void curveRun(AwccWmi& a, GpuNvml& g, const FanCurves& cur) {
    auto fans = mapFans(a);
    if (fans.empty()) { printf("Nenhuma ventoinha (rode como Administrador).\n"); return; }
    uint32_t cpuFan = fans[0].id;
    uint32_t gpuFan = fans.size() > 1 ? fans[1].id : fans[0].id;
    uint32_t cpuSensor = !fans[0].sensors.empty() ? fans[0].sensors[0] : 0x01;

    g_awcc = &a;
    g_fanIds.clear();
    for (auto& f : fans) g_fanIds.push_back(f.id);
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    printf("\nMotor de curva ATIVO - Ctrl+C para sair e restaurar o automatico.\n");
    printCurves(cur);
    printf("--------------------------------\n");

    int lastCpu = -1, lastGpu = -1;
    while (!g_stop) {
        auto ctO = a.getSensorTemperature(cpuSensor);
        auto gtO = g.available() ? g.temperatureC(0) : a.getSensorTemperature(0x06);
        int ct = ctO ? (int)*ctO : 0;
        int gt = gtO ? (int)*gtO : 0;
        int tCpu = interpolateBoost(cur.cpu, ct);
        int tGpu = interpolateBoost(cur.gpu, gt);

        bool appCpu = (lastCpu < 0) || abs(tCpu - lastCpu) >= cur.hysteresis
                      || (tCpu == 0 && lastCpu != 0) || (tCpu == 255 && lastCpu != 255);
        bool appGpu = (lastGpu < 0) || abs(tGpu - lastGpu) >= cur.hysteresis
                      || (tGpu == 0 && lastGpu != 0) || (tGpu == 255 && lastGpu != 255);
        if (appCpu) { a.setFanBoost(cpuFan, tCpu); lastCpu = tCpu; }
        if (appGpu) { a.setFanBoost(gpuFan, tGpu); lastGpu = tGpu; }

        printf("CPU %2dC -> boost %3d%s    GPU %2dC -> boost %3d%s\n",
            ct, lastCpu, appCpu ? " *" : "  ", gt, lastGpu, appGpu ? " *" : "  ");
        fflush(stdout);

        for (int i = 0; i < cur.intervalSec * 2 && !g_stop; ++i) Sleep(500);
    }
    for (uint32_t id : g_fanIds) a.setFanBoost(id, 0); // restauracao final
}

static void usage() {
    printf("G15 Control BR v" G15_VERSION "\n");
    printf("Uso: g15ctl [comando]\n");
    printf("  status               CPU/GPU/SSD + ventoinhas (padrao)\n");
    printf("  monitor [segundos]   Monitor ao vivo (padrao 2s; Ctrl+C sai)\n");
    printf("  probe                Diagnostico (enumeracao e leituras cruas)\n");
    printf("  rpmtest              Testa leitura de RPM sob boost\n");
    printf("  curve show           Mostra a curva ativa (arquivo g15ctl.ini)\n");
    printf("  curve run            Controle automatico por curva (Ctrl+C restaura)\n");
    printf("  mode balanced|gmode|custom   Aplica modo termico\n");
    printf("  boost <id-hex>|all <0-255>   Define boost\n");
    printf("  kbd ...                      Teclado RGB (g15ctl kbd para ver os comandos)\n");
    printf("\nControle e leitura AWCC exigem Administrador (teclado nao).\n");
}

// ---- Teclado RGB (nao precisa de Administrador nem do AWCC) ----
static int kbdUsage() {
    printf("Uso: g15ctl kbd <comando>   (cores em hexadecimal RRGGBB)\n");
    printf("  info                          Controlador, firmware e estado\n");
    printf("  color <cor>                   Teclado inteiro de uma cor\n");
    printf("  zones <c0> <c1> <c2> <c3>     Uma cor por zona, da esquerda p/ a direita\n");
    printf("  breathe <cor> [ms]            Respiracao (padrao 2000 ms)\n");
    printf("  cycle <cor> <cor> [...] [ms]  Transicao entre 2 a 6 cores\n");
    printf("  bright <0-100>                Brilho\n");
    printf("  off                           Apaga\n");
    printf("  ... --save                    Grava tambem como padrao ao ligar o notebook\n");
    return 2;
}

static int kbdMain(int argc, wchar_t** argv) {
    std::vector<std::wstring> a;
    bool save = false;
    uint8_t ids[KbdElc::ZONES]; bool haveIds = false;   // --ids a,b,c,d : IDs de zona p/ outros modelos (5515 = 8,9,10,11)
    for (int i = 2; i < argc; ++i) {
        std::wstring s = argv[i];
        if (s == L"--save") save = true;
        else if (s == L"--ids" && i + 1 < argc) { unsigned v[4]; haveIds = swscanf(argv[++i], L"%u,%u,%u,%u", &v[0], &v[1], &v[2], &v[3]) == 4; for (int k = 0; k < 4; ++k) ids[k] = (uint8_t)v[k]; }
        else a.push_back(s);
    }
    if (a.empty()) return kbdUsage();
    const std::wstring& sub = a[0];

    KbdElc kb;
    if (!kb.open()) { printf("Teclado: %s\n", kb.lastError().c_str()); return 1; }
    if (haveIds) kb.setZoneIds(ids);

    // ultimo argumento numerico (sem cara de cor) = duracao em ms
    auto takeMs = [&](size_t minArgs) -> uint16_t {
        if (a.size() > minArgs && a.back().size() < 6) {
            unsigned long v = wcstoul(a.back().c_str(), nullptr, 10);
            a.pop_back();
            if (v < 100) v = 100; if (v > 60000) v = 60000;
            return (uint16_t)v;
        }
        return 2000;
    };
    auto done = [&](bool ok) -> int {
        if (ok) printf("Teclado: OK%s\n", save ? " (gravado como padrao)" : "");
        else    printf("Teclado: FALHOU - %s\n", kb.lastError().c_str());
        return ok ? 0 : 1;
    };

    if (sub == L"info") {
        int ma = 0, mi = 0, re = 0;
        printf("Controlador: USB 187C:%04X\n", kb.pid());
        // (o "estado" ecoa o ultimo comando; depois de uma consulta mostra 0x20)
        if (kb.firmwareVersion(ma, mi, re)) printf("Firmware:    %d.%d.%d   (resposta: %s)\n", ma, mi, re, kb.lastReplyHex().c_str());
        else printf("Firmware:    sem resposta\n");
        int st = kb.status();
        printf("Estado:      %d (%s)   (resposta: %s)\n", st, st == 33 ? "pronto" : st == 34 ? "ocupado" : "?", kb.lastReplyHex().c_str());
        return 0;
    }
    if (sub == L"off") return done(kb.apply(KbdElc::Scene::solid({0, 0, 0}), save));
    if (sub == L"bright" && a.size() == 2)
        return done(kb.setBrightness((int)wcstol(a[1].c_str(), nullptr, 10)));
    if (sub == L"color" && a.size() == 2) {
        KbdElc::Rgb c;
        if (!parseRgb(a[1], c)) return kbdUsage();
        return done(kb.apply(KbdElc::Scene::solid(c), save));
    }
    if (sub == L"zones" && a.size() == 5) {
        KbdElc::Rgb z[KbdElc::ZONES];
        for (int i = 0; i < KbdElc::ZONES; ++i) if (!parseRgb(a[1 + i], z[i])) return kbdUsage();
        return done(kb.apply(KbdElc::Scene::zones(z), save));
    }
    if (sub == L"breathe" && a.size() >= 2) {
        uint16_t ms = takeMs(2);
        KbdElc::Rgb c;
        if (a.size() != 2 || !parseRgb(a[1], c)) return kbdUsage();
        return done(kb.apply(KbdElc::Scene::breathe(c, ms), save));
    }
    if (sub == L"cycle" && a.size() >= 3) {
        uint16_t ms = takeMs(3);
        std::vector<KbdElc::Rgb> cs;
        for (size_t i = 1; i < a.size(); ++i) { KbdElc::Rgb c; if (!parseRgb(a[i], c)) return kbdUsage(); cs.push_back(c); }
        if (cs.size() < 2 || cs.size() > 6) return kbdUsage();
        return done(kb.apply(KbdElc::Scene::cycle(cs, ms), save));
    }
    return kbdUsage();
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && std::wstring(argv[1]) == L"kbd") return kbdMain(argc, argv);

    AwccWmi awcc;
    bool connected = awcc.connect();
    GpuNvml gpu; gpu.init();

    std::wstring cmd = (argc >= 2) ? argv[1] : L"status";

    if (!connected && cmd != L"help")
        wprintf(L"Aviso AWCC: %ls (rode como Administrador)\n", awcc.lastError().c_str());

    if (cmd == L"status" || cmd == L"map") { printStatus(awcc, gpu); return 0; }
    if (cmd == L"monitor") {
        int sec = (argc >= 3) ? static_cast<int>(wcstol(argv[2], nullptr, 10)) : 2;
        monitor(awcc, gpu, sec); return 0;
    }
    if (cmd == L"probe")   { probe(awcc);   return 0; }
    if (cmd == L"rpmtest") { rpmtest(awcc); return 0; }
    if (cmd == L"curve") {
        FanCurves cur;
        std::wstring ini = iniPathNextToExe();
        if (!loadConfig(ini, cur)) { cur = defaultCurves(); saveConfig(ini, cur); }
        std::wstring sub = (argc >= 3) ? argv[2] : L"run";
        if (sub == L"show") { printCurves(cur); wprintf(L"Config: %ls\n", ini.c_str()); return 0; }
        if (sub == L"run")  { curveRun(awcc, gpu, cur); return 0; }
        usage(); return 2;
    }
    if (cmd == L"mode" && argc >= 3) {
        std::wstring m = argv[2];
        AwccWmi::ThermalMode mode;
        if      (m == L"balanced") mode = AwccWmi::ThermalMode::Balanced;
        else if (m == L"gmode")    mode = AwccWmi::ThermalMode::GMode;
        else if (m == L"custom")   mode = AwccWmi::ThermalMode::Custom;
        else { usage(); return 2; }
        bool ok = awcc.applyThermalMode(mode);
        printf("Aplicar modo: %s\n", ok ? "OK" : "FALHOU (admin?)");
        return ok ? 0 : 1;
    }
    if (cmd == L"boost" && argc >= 4) {
        uint32_t value = static_cast<uint32_t>(wcstoul(argv[3], nullptr, 10));
        std::wstring who = argv[2];
        bool ok = true;
        if (who == L"all") { for (uint32_t id : awcc.enumerateFans()) ok = awcc.setFanBoost(id, value) && ok; }
        else { ok = awcc.setFanBoost(static_cast<uint32_t>(wcstoul(argv[2], nullptr, 16)), value); }
        printf("Definir boost: %s\n", ok ? "OK" : "FALHOU (admin?)");
        return ok ? 0 : 1;
    }

    usage();
    return 0;
}
