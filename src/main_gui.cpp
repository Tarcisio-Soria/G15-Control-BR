// G15 Control BR - main_gui.cpp
// Fase C: janela ImGui + DX11, visual estilo MSI Afterburner:
//   - medidores grandes em cima
//   - graficos de historico empilhados (temp/uso/clock/potencia/fan) rolando no tempo
//   - controles em barra ao lado (sliders de boost, curva automatica, modos, energia)
// Reutiliza AwccWmi / GpuNvml / StorageTemp / FanCurve / WinPower.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <optional>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include "AwccWmi.h"
#include "GpuNvml.h"
#include "StorageTemp.h"
#include "FanCurve.h"
#include "WinPower.h"
#include "KbdElc.h"
#include "AutoStart.h"
#include "Installer.h"
#include "Version.h"

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRTV = nullptr;
static ImFont*                  g_bigFont = nullptr;   // Segoe UI Bold 30
static ImFont*                  g_numFont = nullptr;   // Segoe UI Bold 22 (valores dos graficos)
static float                    g_dpi = 1.0f;
static const ULONGLONG          READ_MS = 2000;        // intervalo de leitura dos sensores / ponto novo no grafico

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) { g_pd3dDevice->CreateRenderTargetView(back, nullptr, &g_mainRTV); back->Release(); }
}
static void CleanupRenderTarget() { if (g_mainRTV) { g_mainRTV->Release(); g_mainRTV = nullptr; } }

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext) != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static void restoreFans();
static void saveSettings(HWND hwnd);

// ---- bandeja (tray): minimizar esconde a janela e deixa so o icone ---------
static const UINT      WM_TRAYICON = WM_APP + 1;
static NOTIFYICONDATAW g_nid = {};
static bool            g_inTray = false;
static UINT            g_msgShowExisting = 0;   // enviada por uma 2a copia do app ao ser aberta
static UINT            g_msgTaskbarCreated = 0; // Explorer reiniciou: recoloca o icone

// Icone do app (recurso 1, res\g15.ico). small = tamanho da bandeja/barra de titulo.
static HICON appIcon(bool sm) {   // (nao usar o nome "small": e macro do Windows)
    int cx = GetSystemMetrics(sm ? SM_CXSMICON : SM_CXICON), cy = GetSystemMetrics(sm ? SM_CYSMICON : SM_CYICON);
    HICON h = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, cx, cy, LR_SHARED);
    return h ? h : LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // fallback: icone padrao do Windows
}

static void trayAdd(HWND h) {
    if (g_inTray) return;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = h; g_nid.uID = 1;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = appIcon(true);
    lstrcpynW(g_nid.szTip, L"G15 Control BR", ARRAYSIZE(g_nid.szTip));
    g_inTray = Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}
static void trayRemove() {
    if (!g_inTray) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_inTray = false;
}
static void traySetTip(const wchar_t* tip) {
    if (!g_inTray) return;
    lstrcpynW(g_nid.szTip, tip, ARRAYSIZE(g_nid.szTip));
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}
static bool g_startMax = false;   // iniciou escondido (--tray) e a janela salva estava maximizada
static void trayRestore(HWND h) {
    if (g_startMax) { g_startMax = false; ShowWindow(h, SW_SHOWMAXIMIZED); }
    else { ShowWindow(h, SW_SHOW); ShowWindow(h, SW_RESTORE); }
    SetForegroundWindow(h);
    trayRemove();
}

static bool g_toggleG = false;   // pedido de alternar o G-Mode (tecla G ou menu da bandeja)
static bool g_gModeOn = false;   // G-Mode ligado agora (atualizado a cada leitura)
static ULONGLONG g_saveAt = 0;         // != 0: gravar as configuracoes neste instante
static ULONGLONG g_kbdReapplyAt = 0;  // != 0: reenviar a cena do teclado neste instante (GetTickCount64)

// Estado de energia que faz o controlador do teclado trocar de animacao: tomada e "carregando".
static int powerKey() {
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return -1;
    return (ps.ACLineStatus << 8) | (ps.BatteryFlag != 255 ? (ps.BatteryFlag & 0x0C) : 0);   // 4 = critica, 8 = carregando
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    if (g_msgShowExisting && msg == g_msgShowExisting) { // outra copia foi aberta: mostra esta
        if (g_inTray) trayRestore(hWnd);
        else { if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE); SetForegroundWindow(hWnd); }
        return 0;
    }
    if (g_msgTaskbarCreated && msg == g_msgTaskbarCreated && g_inTray) {
        g_inTray = false; trayAdd(hWnd); return 0;
    }
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) trayRestore(hWnd);
        else if (lParam == WM_RBUTTONUP) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"Abrir");
            AppendMenuW(m, MF_STRING | (g_gModeOn ? MF_CHECKED : 0), 3, L"G-Mode (turbo)");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, 2, L"Sair (ventoinhas voltam ao automático)");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hWnd); // exigido p/ o menu fechar ao clicar fora
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(m);
            if (cmd == 1) trayRestore(hWnd);
            else if (cmd == 2) PostMessageW(hWnd, WM_CLOSE, 0, 0);
            else if (cmd == 3) g_toggleG = true;       // o laco principal faz a troca
        }
        return 0;
    case WM_POWERBROADCAST: {
        // Ao voltar da suspensao ou mudar tomada/carga, o teclado troca para a animacao gravada
        // daquele estado (ou apaga). Reenvia a cena do app pouco depois da troca.
        static int lastKey = powerKey();
        bool resume = wParam == 0x07 /*RESUMESUSPEND*/ || wParam == 0x12 /*RESUMEAUTOMATIC*/;
        int key = powerKey();
        if (resume || (wParam == 0x0A /*POWERSTATUSCHANGE*/ && key != lastKey))
            g_kbdReapplyAt = GetTickCount64() + 1500;
        lastKey = key;
        break;
    }
    case WM_HOTKEY:                                    // tecla G do notebook (F17)
        if (wParam == 1) g_toggleG = true;
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) { // minimizou: vai para a bandeja
            trayAdd(hWnd);
            if (g_inTray) ShowWindow(hWnd, SW_HIDE); // sem icone, fica na barra de tarefas
            return 0;
        }
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_ENDSESSION: // logoff/desligamento: o loop principal pode nao chegar ao fim
        if (wParam) { saveSettings(hWnd); restoreFans(); }
        return 0;
    case WM_CLOSE: saveSettings(hWnd); break; // grava antes de a janela ser destruida
    case WM_DESTROY: trayRemove(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam); // W: a classe e Unicode (senao o titulo vira "G")
}

static void applyModernStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 10; s.ChildRounding = 8; s.FrameRounding = 7; s.GrabRounding = 7;
    s.PopupRounding = 8; s.ScrollbarRounding = 9; s.TabRounding = 8;
    s.WindowPadding = ImVec2(16, 14); s.FramePadding = ImVec2(11, 7); s.CellPadding = ImVec2(7, 7);
    s.ItemSpacing = ImVec2(10, 9); s.ItemInnerSpacing = ImVec2(8, 6);
    s.ScrollbarSize = 12; s.GrabMinSize = 13;
    s.WindowBorderSize = 0; s.ChildBorderSize = 1; s.FrameBorderSize = 0;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]       = ImVec4(0.070f, 0.078f, 0.098f, 1.0f);
    c[ImGuiCol_ChildBg]        = ImVec4(0.104f, 0.115f, 0.140f, 1.0f);
    c[ImGuiCol_Border]         = ImVec4(0.20f, 0.22f, 0.27f, 0.7f);
    c[ImGuiCol_FrameBg]        = ImVec4(0.15f, 0.17f, 0.21f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.23f, 0.28f, 1.0f);
    c[ImGuiCol_FrameBgActive]  = ImVec4(0.24f, 0.27f, 0.33f, 1.0f);
    c[ImGuiCol_Text]           = ImVec4(0.92f, 0.93f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled]   = ImVec4(0.50f, 0.54f, 0.60f, 1.0f);
    c[ImGuiCol_Separator]      = ImVec4(0.20f, 0.22f, 0.27f, 0.8f);
    c[ImGuiCol_SliderGrab]     = ImVec4(0.33f, 0.74f, 0.96f, 1.0f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.45f, 0.82f, 1.0f, 1.0f);
    c[ImGuiCol_CheckMark]      = ImVec4(0.33f, 0.80f, 0.70f, 1.0f);
    c[ImGuiCol_Button]         = ImVec4(0.18f, 0.46f, 0.72f, 1.0f);
    c[ImGuiCol_ButtonHovered]  = ImVec4(0.24f, 0.56f, 0.84f, 1.0f);
    c[ImGuiCol_ButtonActive]   = ImVec4(0.14f, 0.38f, 0.60f, 1.0f);
}

// ---- historico (ring buffer) p/ cada metrica -----------------------------
struct Series {
    static const int CAP = 300;      // ~10 min @ 2 s (READ_MS)
    float v[CAP] = {0};
    int   head = 0, count = 0;
    bool  has  = false;              // alguma amostra valida ja entrou
    void push(float x) { v[head] = x; head = (head + 1) % CAP; if (count < CAP) count++; has = true; }
    float at(int i) const { int idx = (head - count + i + CAP * 8) % CAP; return v[idx]; } // i: 0=antigo
    float last() const { return count ? at(count - 1) : 0.0f; }
    void range(float& mn, float& mx) const {
        if (!count) { mn = mx = 0; return; }
        mn = mx = at(0);
        for (int i = 1; i < count; i++) { float x = at(i); if (x < mn) mn = x; if (x > mx) mx = x; }
    }
};
struct History {
    Series cpuT, gpuT, gpuUse, gpuClk, gpuW, boostC, boostG, rpmC, rpmG;
};

struct FanRow { uint32_t id; const char* label; std::optional<uint32_t> maxRpm; std::optional<uint32_t> relTemp; std::optional<uint32_t> rpm; };
struct Snapshot {
    std::optional<uint32_t> cpuTemp, mode;
    std::optional<unsigned> gpuTemp, gpuUtil, gpuW, gpuMHz;
    std::vector<NvmeDrive> ssds;
    std::vector<FanRow> fans;
    WinPower::Overlay winPerf = WinPower::Overlay::Unknown;
};
struct Controls {
    bool autoCurve = false;
    int  boost[4]  = {0,0,0,0};
    int  lastCurve[2] = {-1,-1};   // ultimo boost aplicado pela curva (-1 = ainda nao aplicou)
    bool initialized = false;
    bool gpuKeepOn = true;         // manter a ventoinha da GPU sempre girando (boost minimo)
    int  gpuFloorPct = 20;         // base (piso) do boost da GPU em %, 0..40: ~100 RPM por 1% (medido no 5515)
};

// Sem boost, o firmware da Dell DESLIGA a ventoinha da GPU quando a GPU esta fria (0 RPM).
// Com este piso (~10%) ela fica em torno de 2300 RPM. Medido no 5515; a da CPU nunca para.
static inline int gpuFloorBoost(int pct) { return pct * 255 / 100; }
static const size_t GPU_FAN_INDEX = 1;
static const int SAFE_BOOST_NO_SENSOR = 150;   // ~59%: usado pela curva se NENHUM sensor de temperatura responder
// Boost que realmente vai para a ventoinha i, aplicando o piso da GPU se a opcao estiver ligada.
static int effBoost(const Controls& c, size_t i, int wanted) {
    if (c.gpuKeepOn && i == GPU_FAN_INDEX && wanted < gpuFloorBoost(c.gpuFloorPct)) return gpuFloorBoost(c.gpuFloorPct);
    return wanted;
}

static std::string g_awccErr;   // motivo de o AWCC nao ter conectado (mostrado no cabecalho)
static uint32_t    g_rpmPeak[2] = {0, 0};   // maior RPM ja visto por ventoinha nesta sessao

// Sensor da CPU: o associado a ventoinha da CPU (igual a CLI); fallback 0x01.
static uint32_t g_cpuSensor = 0x01;

// Enumeracao das ventoinhas: cara (varre ~50 IDs via WMI), entao roda UMA vez.
static void initFans(AwccWmi& a, Snapshot& s) {
    auto fanIds = a.enumerateFans();
    s.fans.clear();
    const char* labels[] = { "CPU", "GPU" };
    for (size_t i = 0; i < fanIds.size(); ++i)
        s.fans.push_back({ fanIds[i], i < 2 ? labels[i] : "Fan", a.getFanMaxRPM(fanIds[i]), std::nullopt });
    if (!fanIds.empty()) {
        auto rel = a.getFanRelatedSensorIds(fanIds[0]);
        if (!rel.empty()) g_cpuSensor = rel[0];
    }
}

// Leitura periodica (barata): 2 chamadas WMI + NVML. Roda mesmo sem AWCC,
// para a GPU continuar aparecendo quando nao for Administrador.
static void readSnapshot(AwccWmi& a, bool connected, GpuNvml& g, Snapshot& s) {
    if (connected) {
        s.cpuTemp = a.getSensorTemperature(g_cpuSensor);
        if (!s.cpuTemp && g_cpuSensor != 0x01) s.cpuTemp = a.getSensorTemperature(0x01);
        s.mode = a.getCurrentPowerMode();
        for (size_t i = 0; i < s.fans.size(); ++i) {   // RPM ao vivo (1 chamada WMI por ventoinha)
            s.fans[i].rpm = a.getFanRPM(s.fans[i].id);
            if (i < 2 && s.fans[i].rpm && *s.fans[i].rpm > g_rpmPeak[i] && *s.fans[i].rpm < 20000) g_rpmPeak[i] = *s.fans[i].rpm;
        }
    }
    if (g.available()) {
        s.gpuTemp = g.temperatureC(0); s.gpuUtil = g.utilizationPercent(0);
        s.gpuW = g.powerW(0); s.gpuMHz = g.clockMHz(0);
    }
    s.winPerf = WinPower::getOverlay();
}

// Restaura as ventoinhas ao automatico (saida normal, logoff e desligamento).
static AwccWmi*              g_awcc = nullptr;
static std::vector<uint32_t> g_fanIds;
// ---- modo termico: troca pelo usuario, tecla G e protecao termica -----------
// Protecao termica (mesma logica do tcc-g15): se CPU ou GPU ficar acima do limite por
// TRIP_DELAY_MS, liga o G-Mode (ventoinhas no maximo); depois de RESET_MS sem passar do
// limite, volta ao modo em que estava.
struct ThermalGuard {
    bool enabled  = true;
    int  cpuLimit = 95, gpuLimit = 85;     // graus C
    bool tripped  = false;                 // ventoinhas foram postas em 100% pela protecao
    int  prevBoost[4] = {0, 0, 0, 0};      // boost manual antes do disparo, para restaurar
    ULONGLONG hotSince = 0, lastHot = 0;
    bool restoreMode = true;               // ao sair, voltar ao modo termico em que o app encontrou a maquina
    bool haveStartMode = false;
    uint32_t startMode = 0x97;             // modo termico lido na abertura do app
    bool gKey = true, gKeyOk = false;      // tecla G (F17) alterna o G-Mode / registro deu certo
    uint32_t lastNonG = 0x97;              // ultimo modo que nao era G-Mode (para a tecla G voltar)
};
static ThermalGuard g_tg;
static const ULONGLONG TRIP_DELAY_MS = 8000, RESET_MS = 60000;
static const int       HOTKEY_G = 1;       // id do RegisterHotKey

static AwccWmi::ThermalMode modeFromValue(uint32_t v) {
    return v == 0xAB ? AwccWmi::ThermalMode::GMode : v == 0x00 ? AwccWmi::ThermalMode::Custom : AwccWmi::ThermalMode::Balanced;
}
// Troca de modo pedida pelo usuario (botao, bandeja, tecla G). Nao mexe na protecao
// termica: ela atua nas ventoinhas, nao no modo.
static bool userSetMode(AwccWmi& a, Snapshot& s, AwccWmi::ThermalMode m) {
    bool ok = a.applyThermalMode(m);
    if (ok) s.mode = (uint32_t)m;          // reflete na tela sem esperar a proxima leitura
    return ok;
}
static void toggleGMode(AwccWmi& a, Snapshot& s) {
    bool gOn = s.mode && *s.mode == 0xAB;
    userSetMode(a, s, gOn ? modeFromValue(g_tg.lastNonG) : AwccWmi::ThermalMode::GMode);
}
// Chamado a cada leitura de sensores.
// Ao disparar, poe as ventoinhas em 100% de boost e NAO troca o modo termico: medido no
// 5515, boost 100% gira ~5700/5400 RPM contra ~5000/4700 do G-Mode, e o G-Mode ainda
// libera mais potencia (mais calor). Depois de esfriar, devolve o que havia antes.
static void thermalGuardTick(AwccWmi& a, Snapshot& s, Controls& ctrl, ULONGLONG now) {
    ThermalGuard& t = g_tg;
    if (s.mode && *s.mode != 0xAB) t.lastNonG = *s.mode;
    g_gModeOn = s.mode && *s.mode == 0xAB;
    if (s.mode && !t.haveStartMode) { t.startMode = *s.mode; t.haveStartMode = true; }
    if (!t.enabled) { t.hotSince = 0; return; }

    // So leituras reais contam: sensor ausente nao dispara (evita falso alarme sem NVML).
    bool hot = (s.cpuTemp && (int)*s.cpuTemp >= t.cpuLimit) || (s.gpuTemp && (int)*s.gpuTemp >= t.gpuLimit);
    if (hot) { if (!t.hotSince) t.hotSince = now; t.lastHot = now; }
    else t.hotSince = 0;

    const size_t n = s.fans.size() < 4 ? s.fans.size() : 4;
    if (!t.tripped && hot && n > 0 && now - t.hotSince >= TRIP_DELAY_MS) {
        for (size_t i = 0; i < n; ++i) {
            t.prevBoost[i] = ctrl.boost[i];
            a.setFanBoost(s.fans[i].id, 255);
            ctrl.boost[i] = 255;
        }
        t.tripped = true;
    } else if (t.tripped && now - t.lastHot >= RESET_MS) {
        t.tripped = false;
        if (ctrl.autoCurve) {
            ctrl.lastCurve[0] = ctrl.lastCurve[1] = -1;   // a curva reassume no proximo ciclo
        } else {
            for (size_t i = 0; i < n; ++i) {
                const int b = effBoost(ctrl, i, t.prevBoost[i]);
                a.setFanBoost(s.fans[i].id, (uint32_t)b);
                ctrl.boost[i] = b;
            }
        }
    }
}
// O usuario mexeu nas ventoinhas durante um disparo: vale a escolha dele.
static void thermalGuardUserOverride() { g_tg.tripped = false; g_tg.hotSince = 0; }

static void restoreFans() {

    g_tg.tripped = false;
    // Devolve o modo termico em que a maquina estava quando o app abriu (ex.: sai do G-Mode).
    if (g_awcc && g_tg.restoreMode && g_tg.haveStartMode) {
        auto cur = g_awcc->getCurrentPowerMode();
        if (cur && *cur != g_tg.startMode) g_awcc->applyThermalMode(modeFromValue(g_tg.startMode));
    }
    if (!g_awcc) return;
    for (uint32_t id : g_fanIds) g_awcc->setFanBoost(id, 0);
    g_awcc = nullptr; // nao repete
}

static void pushHistory(History& h, const Snapshot& s, const Controls& ctrl) {
    if (s.cpuTemp) h.cpuT.push((float)*s.cpuTemp);
    if (s.gpuTemp) h.gpuT.push((float)*s.gpuTemp);
    if (s.gpuUtil) h.gpuUse.push((float)*s.gpuUtil);
    if (s.gpuMHz)  h.gpuClk.push((float)*s.gpuMHz);
    if (s.gpuW)    h.gpuW.push((float)*s.gpuW);
    h.boostC.push(ctrl.boost[0] * 100.0f / 255.0f);
    h.boostG.push(ctrl.boost[1] * 100.0f / 255.0f);
    if (s.fans.size() > 0 && s.fans[0].rpm) h.rpmC.push((float)*s.fans[0].rpm);
    if (s.fans.size() > 1 && s.fans[1].rpm) h.rpmG.push((float)*s.fans[1].rpm);
}

// Cores de cada metrica: UMA definicao, usada nos graficos e nas caixas do topo,
// para a caixa e a linha do grafico da mesma grandeza terem sempre a mesma cor.
static const ImU32 COL_CPU  = IM_COL32(240, 96, 96, 255);    // temperatura da CPU
static const ImU32 COL_GPU  = IM_COL32(245, 170, 60, 255);   // temperatura da GPU
static const ImU32 COL_USE  = IM_COL32(90, 210, 140, 255);   // uso da GPU
static const ImU32 COL_CLK  = IM_COL32(90, 170, 245, 255);   // clock da GPU
static const ImU32 COL_PWR  = IM_COL32(190, 130, 245, 255);  // potencia da GPU
static const ImU32 COL_FANC = IM_COL32(70, 210, 230, 255);   // ventoinha da CPU
static const ImU32 COL_FANG = IM_COL32(120, 180, 255, 255);  // ventoinha da GPU
static ImVec4 colV(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

static ImVec4 tempColor(int c) {
    if (c >= 85) return ImVec4(0.93f, 0.33f, 0.33f, 1.0f);
    if (c >= 70) return ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
    return ImVec4(0.36f, 0.82f, 0.55f, 1.0f);
}
static const char* modeName(uint32_t v) {
    switch (v) { case 0x97: case 0xa0: return "Balanced"; case 0xa1: return "Performance";
        case 0xa3: return "Quiet"; case 0xab: return "G-Mode"; case 0x00: return "Custom"; default: return "?"; }
}

// ---- medidor compacto no topo (numero grande) -----------------------------
static void statTile(const char* name, const char* big, ImVec4 col, const char* sub) {
    ImGui::TableNextColumn();
    // Altura automatica pelo conteudo (escala com fonte/DPI) e sem barra de rolagem.
    ImGui::BeginChild(name, ImVec2(0, 0),
        ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextDisabled("%s", name);
    ImGui::PushFont(g_bigFont);
    ImGui::TextColored(col, "%s", big);
    ImGui::PopFont();
    if (sub && *sub) ImGui::TextDisabled("%s", sub);
    ImGui::EndChild();
}

// ---- grafico de historico estilo Afterburner ------------------------------
struct Plot { const Series* s; ImU32 col; };
static void graphCard(const char* title, const char* unit, const char* valfmt,
                      const Plot* plots, int nplots, float vmin, float vmax, bool autoMax) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 86.0f * g_dpi;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    // escala automatica (clock/potencia): usa o maior visto + folga
    if (autoMax) {
        float mx = vmin;
        for (int k = 0; k < nplots; k++) { float a, b; plots[k].s->range(a, b); if (b > mx) mx = b; }
        vmax = mx * 1.15f + 1.0f;
    }

    dl->AddRectFilled(p0, p1, IM_COL32(16, 18, 24, 255), 6.0f);
    dl->AddRect(p0, p1, IM_COL32(38, 42, 52, 255), 6.0f);
    for (int i = 1; i < 4; i++) { float y = p0.y + h * i / 4; dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(44, 48, 58, 140)); }

    float range = vmax - vmin; if (range < 1e-3f) range = 1.0f;
    auto Y = [&](float v) { float t = (v - vmin) / range; if (t < 0) t = 0; if (t > 1) t = 1; return p1.y - 2.0f - t * (h - 4.0f); };

    for (int k = 0; k < nplots; k++) {
        const Series& s = *plots[k].s;
        int n = s.count; if (n < 2) continue;
        float step = w / (float)(Series::CAP - 1);
        static std::vector<ImVec2> pts; pts.clear(); pts.reserve(n);
        for (int i = 0; i < n; i++) { float x = p1.x - (n - 1 - i) * step; pts.push_back(ImVec2(x, Y(s.at(i)))); }
        ImU32 fill = (plots[k].col & 0x00FFFFFF) | 0x33000000;
        for (int i = 0; i + 1 < n; i++)
            dl->AddQuadFilled(ImVec2(pts[i].x, p1.y - 1), pts[i], pts[i + 1], ImVec2(pts[i + 1].x, p1.y - 1), fill);
        dl->AddPolyline(pts.data(), n, plots[k].col, 0, 2.0f * g_dpi);
    }

    // titulo
    dl->AddText(ImVec2(p0.x + 9, p0.y + 6), IM_COL32(165, 172, 188, 255), title);
    // valores atuais (grande, a direita, cor de cada serie)
    float vx = p1.x - 10;
    float fs = 21.0f * g_dpi;
    for (int k = nplots - 1; k >= 0; k--) {
        char b[24]; snprintf(b, sizeof(b), valfmt, plots[k].s->last());
        ImVec2 ts = g_numFont->CalcTextSizeA(fs, FLT_MAX, 0.0f, b);
        vx -= ts.x;
        dl->AddText(g_numFont, fs, ImVec2(vx, p0.y + 4), plots[k].col, b);
        vx -= 12 * g_dpi;
    }
    // min/max (pequeno, embaixo)
    float gmn = 1e9f, gmx = -1e9f;
    for (int k = 0; k < nplots; k++) { float a, b; plots[k].s->range(a, b); if (plots[k].s->count) { if (a < gmn) gmn = a; if (b > gmx) gmx = b; } }
    if (gmx >= gmn) {
        char mm[48]; snprintf(mm, sizeof(mm), "min %.0f  max %.0f %s", gmn, gmx, unit);
        dl->AddText(ImVec2(p0.x + 9, p1.y - 17), IM_COL32(120, 126, 140, 255), mm);
    }
    ImGui::Dummy(ImVec2(w, h + 6 * g_dpi));
}

static void drawGraphs(History& h, const Snapshot& s) {
    const ImU32 CPU  = COL_CPU;
    const ImU32 GPU  = COL_GPU;
    const ImU32 USE  = COL_USE;
    const ImU32 CLK  = COL_CLK;
    const ImU32 PWR  = COL_PWR;
    const ImU32 FANC = COL_FANC;
    const ImU32 FANG = COL_FANG;

    ImGui::TextColored(ImVec4(0.70f, 0.74f, 0.82f, 1.0f), "MONITORAMENTO");
    ImGui::SameLine(); ImGui::TextDisabled("(historico ~10 min   -  ");
    ImGui::SameLine(0, 0); ImGui::TextColored(colV(COL_CPU), "CPU");
    ImGui::SameLine(); ImGui::TextColored(colV(COL_GPU), "GPU");
    ImGui::SameLine(0, 0); ImGui::TextDisabled(")");

    Plot t[2] = { { &h.cpuT, CPU }, { &h.gpuT, GPU } };
    graphCard("Temperatura  (CPU / GPU)", "C", "%.0f", t, 2, 30.0f, 100.0f, false);

    Plot u[1] = { { &h.gpuUse, USE } };
    graphCard("GPU  -  uso", "%", "%.0f%%", u, 1, 0.0f, 100.0f, false);

    Plot c[1] = { { &h.gpuClk, CLK } };
    graphCard("GPU  -  clock", "MHz", "%.0f", c, 1, 0.0f, 100.0f, true);

    Plot p[1] = { { &h.gpuW, PWR } };
    graphCard("GPU  -  potencia", "W", "%.0fW", p, 1, 0.0f, 100.0f, true);

    Plot f[2] = { { &h.boostC, FANC }, { &h.boostG, FANG } };
    graphCard("Ventoinhas  -  boost (CPU / GPU)", "%", "%.0f%%", f, 2, 0.0f, 100.0f, false);

    // RPM real. A BIOS informa um "maximo" (4200/3900 no 5515) que vale so para o modo normal:
    // em G-Mode/boost alto as ventoinhas passam disso, entao a escala segue o maior valor ja visto.
    float rpmMax = 4500.0f;
    for (uint32_t pk : g_rpmPeak) if ((float)pk * 1.08f > rpmMax) rpmMax = (float)pk * 1.08f;
    Plot r[2] = { { &h.rpmC, FANC }, { &h.rpmG, FANG } };
    graphCard("Ventoinhas  -  RPM (CPU / GPU)", "RPM", "%.0f", r, 2, 0.0f, rpmMax, false);
}

// ---- Teclado RGB ------------------------------------------------------------
static KbdElc g_kbd;
static bool   g_wantRedraw = false;   // pede mais um quadro (envio pendente ao teclado)
static ULONGLONG g_redrawAt = 0;      // pede um quadro neste instante (animacao do preview); 0 = nenhum
struct KbdUi {
    int   mode = 0;                   // 0 fixa, 1 por zona, 2 respiracao, 3 ciclo de 2 cores
    float col[KbdElc::ZONES][3] = { {0,0.6f,1}, {0,0.6f,1}, {0,0.6f,1}, {0,0.6f,1} };
    float col2[3] = { 1, 0, 0.5f };
    int   bright = 100, speedMs = 2000;
    bool  dirty = false, dirtyBright = false;
    bool  used = false;               // o usuario ja mexeu no teclado pelo app (reaplica ao abrir)
    bool  off  = false;               // ultimo comando foi "Apagar"
    int   pickZone = 0;               // zona cujo seletor de cor esta aberto no preview
    ULONGLONG lastSend = 0;
    std::string msg;
};
static KbdUi g_kui;

static KbdElc::Rgb toRgb(const float* c) {
    auto ch = [](float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (uint8_t)(v * 255.0f + 0.5f); };
    return { ch(c[0]), ch(c[1]), ch(c[2]) };
}
static KbdElc::Scene kbdScene(const KbdUi& k) {
    switch (k.mode) {
    case 1: { KbdElc::Rgb z[KbdElc::ZONES]; for (int i = 0; i < KbdElc::ZONES; ++i) z[i] = toRgb(k.col[i]); return KbdElc::Scene::zones(z); }
    case 2: return KbdElc::Scene::breathe(toRgb(k.col[0]), (uint16_t)k.speedMs);
    case 3: return KbdElc::Scene::cycle({ toRgb(k.col[0]), toRgb(k.col2) }, (uint16_t)k.speedMs);
    default: return KbdElc::Scene::solid(toRgb(k.col[0]));
    }
}
// ---- preview do teclado (estilo retroiluminado) -----------------------------
// Teclas escuras com contorno e legenda na cor da zona. Simula na tela os efeitos
// (respiracao/ciclo) e o brilho; o teclado real segue o relogio do proprio controlador,
// entao a animacao daqui e so ilustrativa, nao sincronizada.
static void kbdPreviewColors(const KbdUi& k, ImVec4 out[KbdElc::ZONES]) {
    const double t   = GetTickCount64() / 1000.0;
    const double per = (k.speedMs < 100 ? 100 : k.speedMs) / 1000.0;
    double ph = fmod(t, 2.0 * per) / per;                 // 0..2
    const float tri = (float)(ph < 1.0 ? ph : 2.0 - ph);  // 0 -> 1 -> 0
    const float br  = k.bright / 100.0f;
    for (int z = 0; z < KbdElc::ZONES; ++z) {
        const float* b = (k.mode == 1) ? k.col[z] : k.col[0];
        float c[3] = { b[0], b[1], b[2] };
        if (k.mode == 2) { float f = 1.0f - tri; c[0] *= f; c[1] *= f; c[2] *= f; }
        if (k.mode == 3) for (int i = 0; i < 3; ++i) c[i] = k.col[0][i] + (k.col2[i] - k.col[0][i]) * tri;
        if (k.off) c[0] = c[1] = c[2] = 0;
        // piso cinza-escuro: tecla apagada continua visivel no desenho
        const float dim[3] = { 0.17f, 0.19f, 0.24f };
        for (int i = 0; i < 3; ++i) { c[i] *= br; if (c[i] < dim[i]) c[i] = dim[i]; }
        out[z] = ImVec4(c[0], c[1], c[2], 1.0f);
    }
}

static void drawKbdPreview(KbdUi& k) {
    // Linhas do teclado do G15 (com numpad); largura de cada tecla em "unidades".
    static const float rows[6][19] = {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1.5f, 1,1,1,1, 0},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1.5f, 1,1,1,1, 0},
        {1.5f,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1, 0},
        {1.8f,1,1,1,1,1,1,1,1,1,1,1,1.7f, 1,1,1,1, 0, 0},
        {2.3f,1,1,1,1,1,1,1,1,1,1,2.2f, 1,1,1,1, 0, 0, 0},
        {1.3f,1,1,1,5.2f,1,1,1,1,1, 1,1,1,1, 0, 0, 0, 0, 0},
    };
    static const char* labels[6][15] = {
        {"","F1","F2","F3","F4","F5","F6","F7","F8","F9","","","","",""},
        {"'","1","2","3","4","5","6","7","8","9","0","-","=","",""},
        {"","Q","W","E","R","T","Y","U","I","O","P","[","]","\\",""},
        {"","A","S","D","F","G","H","J","K","L",";","'","","",""},
        {"","Z","X","C","V","B","N","M",",",".","/","","","",""},
        {"","","","","","","","<","v",">","","","","",""},
    };
    static const float zoneEnd[KbdElc::ZONES] = { 0.27f, 0.52f, 0.76f, 1.01f };  // fracao da largura (estimada)

    const float W = ImGui::GetContentRegionAvail().x;
    const float s = W / 388.0f;                       // desenho pensado em 388 x 150
    const float H = 150.0f * s;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##kbprev", ImVec2(W, H));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec4 zc[KbdElc::ZONES];
    kbdPreviewColors(k, zc);
    auto zoneAt = [&](float fx) { for (int z = 0; z < KbdElc::ZONES; ++z) if (fx < zoneEnd[z]) return z; return KbdElc::ZONES - 1; };
    const float X0 = 4.0f, KW = 380.0f, U = 19.4f, G = 2.4f, KH = 20.0f, Y0 = 6.0f;
    int hoverZone = -1;
    if (hovered) hoverZone = zoneAt(((ImGui::GetMousePos().x - p0.x) / s - X0) / KW);
    const bool perZone = k.mode == 1;

    dl->AddRectFilled(p0, ImVec2(p0.x + W, p0.y + H), IM_COL32(27, 30, 38, 255), 8.0f * s);
    dl->AddRect(p0, ImVec2(p0.x + W, p0.y + H), IM_COL32(44, 49, 60, 255), 8.0f * s);

    ImFont* font = ImGui::GetFont();
    const float fs = 9.5f * s < 9.0f ? 9.0f : 9.5f * s;
    for (int r = 0; r < 6; ++r) {
        int n = 0; while (n < 19 && rows[r][n] > 0) ++n;
        float x = X0;
        for (int c = 0; c < n; ++c) {
            const bool numpad = c >= n - 4;
            if (c == n - 4) x = X0 + KW - 4 * U + G / 2 + 3;          // numpad encostado a direita
            float kw = rows[r][c] * U - G;
            if (!numpad && c == n - 5) kw = (X0 + KW - 4 * U - 3) - x - G / 2;   // ultima tecla fecha a linha
            const float y = Y0 + r * (KH + G);
            const int z = zoneAt((x + kw / 2 - X0) / KW);
            ImVec4 col = zc[z];
            if (hoverZone >= 0 && (!perZone || hoverZone == z)) {      // realce da zona sob o mouse
                col.x = col.x + (1 - col.x) * 0.35f; col.y = col.y + (1 - col.y) * 0.35f; col.z = col.z + (1 - col.z) * 0.35f;
            }
            const ImU32 cu = ImGui::ColorConvertFloat4ToU32(col);
            const ImVec2 a(p0.x + x * s, p0.y + y * s), b(p0.x + (x + kw) * s, p0.y + (y + KH) * s);
            dl->AddRectFilled(a, b, IM_COL32(15, 17, 22, 255), 3.0f * s);
            dl->AddRect(a, b, cu, 3.0f * s, 0, 1.2f * s < 1.0f ? 1.0f : 1.2f * s);
            const char* lb = (!numpad && c < 15) ? labels[r][c] : "";
            if (lb[0]) {
                ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, lb);
                dl->AddText(font, fs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), cu, lb);
            } else {                                                   // tecla sem letra: tracinho aceso
                float bw = (kw - 8 < 12 ? kw - 8 : 12) * s;
                float cy = (a.y + b.y) * 0.5f;
                dl->AddRectFilled(ImVec2(a.x + 4 * s, cy - s), ImVec2(a.x + 4 * s + bw, cy + s), cu, s);
            }
            x += kw + G;
        }
    }

    // clique: abre o seletor de cor da zona (ou da cor unica, nos outros modos)
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) { k.pickZone = perZone && hoverZone >= 0 ? hoverZone : 0; ImGui::OpenPopup("##kbpick"); }
    if (ImGui::BeginPopup("##kbpick")) {
        static const char* zn[] = { "Zona 1 - esquerda", "Zona 2 - meio-esquerda", "Zona 3 - meio-direita", "Zona 4 - direita" };
        ImGui::TextDisabled("%s", perZone ? zn[k.pickZone] : (k.mode == 3 ? "Primeira cor" : "Cor do teclado"));
        if (ImGui::ColorPicker3("##kbpc", k.col[k.pickZone], ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) k.dirty = true;
        ImGui::EndPopup();
    }
    ImGui::TextDisabled(perZone ? "Clique numa zona do teclado para mudar a cor." : "Clique no teclado para mudar a cor.");

    // efeitos animados: pede novo quadro a ~20 por segundo (em vez de a cada vsync)
    if (k.mode >= 2 && !k.off) { ULONGLONG at = GetTickCount64() + 50; if (!g_redrawAt || at < g_redrawAt) g_redrawAt = at; }
}

static void drawKeyboard(KbdUi& k) {
    if (!g_kbd.isOpen()) { ImGui::TextDisabled("Teclado RGB nao encontrado."); return; }
    drawKbdPreview(k);
    ImGui::Dummy(ImVec2(0, 2));

    const char* modes[] = { "Cor fixa", "Uma cor por zona", "Respiracao", "Ciclo de 2 cores" };
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##kmode", &k.mode, modes, 4)) k.dirty = true;

    const ImGuiColorEditFlags cf = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha;
    if (k.mode == 1) {
        const char* zl[] = { "Esq", "M-esq", "M-dir", "Dir" };
        for (int i = 0; i < KbdElc::ZONES; ++i) {
            if (i) ImGui::SameLine();
            if (ImGui::ColorEdit3(zl[i], k.col[i], cf)) k.dirty = true;
        }
    } else {
        if (ImGui::ColorEdit3("Cor", k.col[0], cf)) k.dirty = true;
        if (k.mode == 3) { ImGui::SameLine(); if (ImGui::ColorEdit3("Segunda cor", k.col2, cf)) k.dirty = true; }
    }
    if (k.mode >= 2) {
        ImGui::SetNextItemWidth(-90 * g_dpi);
        if (ImGui::SliderInt("Velocidade", &k.speedMs, 500, 8000, "%d ms")) k.dirty = true;
    }
    ImGui::SetNextItemWidth(-90 * g_dpi);
    if (ImGui::SliderInt("Brilho", &k.bright, 0, 100, "%d%%")) k.dirtyBright = true;

    bool ok = true, sent = false;
    if (ImGui::Button("Apagar")) { ok = g_kbd.off(); sent = true; k.dirty = false; k.used = true; k.off = true; k.msg = ok ? "apagado" : ""; }
    ImGui::SameLine();
    if (ImGui::Button("Gravar como padrao")) {
        ok = g_kbd.apply(kbdScene(k), true); sent = true; k.dirty = false; k.used = true; k.off = false;
        k.msg = ok ? "gravado: sera a cor ao ligar o notebook" : "";
    }

    // Envio "ao vivo" enquanto mexe, limitado a ~12 por segundo (cada envio sao varios relatorios HID).
    ULONGLONG now = GetTickCount64();
    if (k.dirty || k.dirtyBright) {
        if (now - k.lastSend >= 80) {
            if (k.dirty)       { ok = g_kbd.apply(kbdScene(k)) && ok; k.dirty = false; k.off = false; }
            k.used = true;
            if (k.dirtyBright) { ok = g_kbd.setBrightness(k.bright) && ok; k.dirtyBright = false; }
            k.lastSend = now; sent = true; k.msg.clear();
        } else g_wantRedraw = true;
    }
    if (sent && !ok) k.msg = "falhou: " + g_kbd.lastError();
    if (!k.msg.empty()) ImGui::TextDisabled("%s", k.msg.c_str());
    else ImGui::TextDisabled("Muda na hora; so fica apos reiniciar se gravar.");
}

static bool        g_autoStart = false;   // tarefa "iniciar com o Windows" existe (consultado ao abrir)
static bool        g_autoStartOk = true;  // este exe pode ser usado na tarefa (nao esta em caminho de rede)
static bool        g_isInstalled = false; // este processo e a copia instalada
static bool        g_hasInstall  = false; // existe uma instalacao no computador
static bool        g_relaunchInstall = false; // ao fechar, reabrir com --install
static HWND        g_hwnd = nullptr;
static const int   TAB_COUNT = 8;
static int         g_tab = 0;                 // aba aberta no painel direito (7 = Sobre)
static void        drawAboutContent();        // definida mais abaixo
static std::wstring settingsPath();           // definida mais abaixo
static std::string g_autoStartMsg;

static void drawControls(Snapshot& s, bool connected, AwccWmi& a, Controls& ctrl) {
    // ---- abas por tema: 8 botoes em 2 fileiras de 4 (todas visiveis, sem rolagem) ----
    static const char* kTabs[]   = { "Ventoinhas", "Modo Dell", "Protecao", "Windows", "SSDs", "Teclado", "Aplicativo", "Sobre" };
    static const char* kTitles[] = { "Ventoinhas", "Modo termico (Dell)", "Protecao termica", "Desempenho (Windows)",
                                     "SSDs (NVMe)", "Teclado RGB", "Aplicativo", "Sobre" };
    {
        const float gap = 6.0f * g_dpi;
        const float bw  = (ImGui::GetContentRegionAvail().x - gap * 3) / 4.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
        for (int i = 0; i < TAB_COUNT; ++i) {
            if (i % 4) ImGui::SameLine();
            const bool sel   = g_tab == i;
            const bool alert = i == 2 && g_tg.tripped;      // protecao disparada: aba em vermelho
            ImGui::PushStyleColor(ImGuiCol_Button,        sel ? ImVec4(0.18f, 0.46f, 0.72f, 1.0f) : ImVec4(0.15f, 0.17f, 0.21f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? ImVec4(0.24f, 0.56f, 0.84f, 1.0f) : ImVec4(0.20f, 0.23f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, alert ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                               : sel   ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.70f, 0.74f, 0.82f, 1.0f));
            if (ImGui::Button(kTabs[i], ImVec2(bw, 0))) g_tab = i;
            ImGui::PopStyleColor(3);
        }
        ImGui::PopStyleVar();
    }
    ImGui::SeparatorText(kTitles[g_tab]);

    if (g_tab == 0) {
    ImGui::BeginDisabled(!connected);
    if (ImGui::Checkbox("Controle automatico pela curva", &ctrl.autoCurve))
        ctrl.lastCurve[0] = ctrl.lastCurve[1] = -1; // forca reaplicar ao ligar
    ImGui::EndDisabled();
    ImGui::TextDisabled(ctrl.autoCurve ? "(a curva ajusta sozinha)" : "(arraste as barras p/ definir o boost)");
    ImGui::BeginDisabled(!connected || s.fans.size() <= GPU_FAN_INDEX);
    if (ImGui::Checkbox("Ventoinha da GPU sempre girando", &ctrl.gpuKeepOn) && s.fans.size() > GPU_FAN_INDEX) {
        if (ctrl.autoCurve) ctrl.lastCurve[GPU_FAN_INDEX] = -1;          // a curva reaplica com/sem o piso
        else {
            int cur = ctrl.boost[GPU_FAN_INDEX];
            if (!ctrl.gpuKeepOn && cur == gpuFloorBoost(ctrl.gpuFloorPct)) cur = 0;   // desligou: devolve ao firmware
            ctrl.boost[GPU_FAN_INDEX] = effBoost(ctrl, GPU_FAN_INDEX, cur);
            a.setFanBoost(s.fans[GPU_FAN_INDEX].id, (uint32_t)ctrl.boost[GPU_FAN_INDEX]);
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Sem isso, o firmware da Dell desliga a ventoinha da GPU\nquando a GPU esta fria (0 RPM). Ligado, ela fica com um\nboost minimo (a base abaixo; cerca de 100 RPM por 1%%).");
    ImGui::BeginDisabled(!connected || !ctrl.gpuKeepOn || s.fans.size() <= GPU_FAN_INDEX);
    ImGui::SetNextItemWidth(-90 * g_dpi);
    if (ImGui::SliderInt("Base da GPU", &ctrl.gpuFloorPct, 0, 40, "%d%%") && s.fans.size() > GPU_FAN_INDEX) {
        if (ctrl.autoCurve) ctrl.lastCurve[GPU_FAN_INDEX] = -1;          // a curva reaplica com a base nova
        else {
            ctrl.boost[GPU_FAN_INDEX] = effBoost(ctrl, GPU_FAN_INDEX, ctrl.boost[GPU_FAN_INDEX]);
            a.setFanBoost(s.fans[GPU_FAN_INDEX].id, (uint32_t)ctrl.boost[GPU_FAN_INDEX]);
        }
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 2));

    ImGui::BeginDisabled(ctrl.autoCurve || !connected);
    for (size_t i = 0; i < s.fans.size() && i < 4; ++i) {
        ImGui::PushID((int)i);
        ImGui::Text("%s", s.fans[i].label);
        if (i == 0 && s.cpuTemp) { ImGui::SameLine(44 * g_dpi); ImGui::TextColored(tempColor((int)*s.cpuTemp), "%d\u00b0", (int)*s.cpuTemp); }
        if (i == 1 && s.gpuTemp) { ImGui::SameLine(44 * g_dpi); ImGui::TextColored(tempColor((int)*s.gpuTemp), "%d\u00b0", (int)*s.gpuTemp); }
        ImGui::SameLine(92 * g_dpi); ImGui::SetNextItemWidth(-62 * g_dpi);
        int pct = ctrl.boost[i] * 100 / 255;
        char lbl[40];
        if (s.fans[i].rpm) snprintf(lbl, sizeof(lbl), "%d%%%%   %u RPM", pct, *s.fans[i].rpm);
        else               snprintf(lbl, sizeof(lbl), "%d%%%%", pct);
        if (ImGui::SliderInt("##b", &ctrl.boost[i], 0, 255, lbl)) {
            thermalGuardUserOverride();
            ctrl.boost[i] = effBoost(ctrl, i, ctrl.boost[i]);
            a.setFanBoost(s.fans[i].id, (uint32_t)ctrl.boost[i]);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Auto")) {
            thermalGuardUserOverride();
            ctrl.boost[i] = effBoost(ctrl, i, 0);
            a.setFanBoost(s.fans[i].id, (uint32_t)ctrl.boost[i]);
        }
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Ao fechar, as ventoinhas voltam ao automatico.");
    }

    // botao que fica destacado (verde) quando aquele modo e o ativo
    auto activeBtn = [](const char* lbl, bool active) -> bool {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.62f, 0.45f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.70f, 0.52f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.54f, 0.40f, 1.0f));
        }
        bool r = ImGui::Button(lbl);
        if (active) ImGui::PopStyleColor(3);
        return r;
    };

    if (g_tab == 1) {
    bool balOn = s.mode && (*s.mode == 0x97 || *s.mode == 0xa0);
    bool gmOn  = s.mode && (*s.mode == 0xab);
    ImGui::BeginDisabled(!connected);
    if (activeBtn("Balanced", balOn)) userSetMode(a, s, AwccWmi::ThermalMode::Balanced);
    ImGui::SameLine(); if (activeBtn("G-Mode (turbo)", gmOn)) userSetMode(a, s, AwccWmi::ThermalMode::GMode);
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::AlignTextToFramePadding();
    if (s.mode) ImGui::TextDisabled("atual: %s", modeName(*s.mode));
    ImGui::TextDisabled("O 5515 so tem Balanced e G-Mode.");
    if (ImGui::Checkbox("Tecla G do notebook alterna o G-Mode", &g_tg.gKey)) {
        if (g_tg.gKey) g_tg.gKeyOk = RegisterHotKey(g_hwnd, HOTKEY_G, MOD_NOREPEAT, VK_F17) != FALSE;
        else { UnregisterHotKey(g_hwnd, HOTKEY_G); g_tg.gKeyOk = false; }
    }
    ImGui::Checkbox("Ao sair, voltar ao modo termico inicial", &g_tg.restoreMode);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ao fechar o app (ou desligar o notebook), devolve o modo\nem que a maquina estava quando o app abriu.\nEx.: se voce ligou o G-Mode, ele e desligado ao sair.");
    if (g_tg.gKey && !g_tg.gKeyOk) ImGui::TextColored(ImVec4(1, 0.75f, 0.35f, 1), "Tecla G em uso por outro programa (AWCC?).");

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.72f, 0.28f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.34f, 0.26f, 1.0f));
    if (ImGui::Button("TURBO MAXIMO\n(G-Mode + Desempenho Windows)", ImVec2(-FLT_MIN, 48 * g_dpi))) {
        if (connected) userSetMode(a, s, AwccWmi::ThermalMode::GMode);
        WinPower::setOverlay(WinPower::Overlay::BestPerformance);
    }
    ImGui::PopStyleColor(2);
    }

    if (g_tab == 2) {
    if (ImGui::Checkbox("Ventoinhas em 100% se superaquecer", &g_tg.enabled) && !g_tg.enabled) {
        g_tg.tripped = false; g_tg.hotSince = 0;   // desligou: nao desfaz mais nada sozinho
    }
    ImGui::BeginDisabled(!g_tg.enabled);
    ImGui::SetNextItemWidth(-90 * g_dpi); ImGui::SliderInt("Limite CPU", &g_tg.cpuLimit, 80, 100, "%d C");
    ImGui::SetNextItemWidth(-90 * g_dpi); ImGui::SliderInt("Limite GPU", &g_tg.gpuLimit, 70, 95, "%d C");
    ImGui::EndDisabled();
    if (g_tg.tripped)
        ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "DISPARADA: ventoinhas em 100%.\nVolta sozinho 60 s depois de esfriar.");
    else if (g_tg.enabled && g_tg.hotSince)
        ImGui::TextColored(ImVec4(1, 0.75f, 0.35f, 1), "Acima do limite: dispara em instantes...");
    else
        ImGui::TextDisabled("Dispara apos 8 s acima do limite; volta 60 s\ndepois de esfriar.");

    }

    if (g_tab == 3) {
    using WO = WinPower::Overlay;
    if (activeBtn("Eficiencia", s.winPerf == WO::BestEfficiency))  WinPower::setOverlay(WO::BestEfficiency);
    ImGui::SameLine(); if (activeBtn("Equilibrado", s.winPerf == WO::Balanced)) WinPower::setOverlay(WO::Balanced);
    ImGui::SameLine(); if (activeBtn("Desempenho max", s.winPerf == WO::BestPerformance)) WinPower::setOverlay(WO::BestPerformance);
    ImGui::TextDisabled("atual: %s", WinPower::name(s.winPerf));

    ImGui::TextDisabled("E o modo de energia do Windows (o mesmo\ndas Configuracoes > Energia).");
    }

    if (g_tab == 4) {
        if (s.ssds.empty()) ImGui::TextDisabled("Nenhum SSD NVMe encontrado.");
        else ImGui::TextDisabled("Temperatura lida do proprio SSD, a cada 5 s.");
        for (const auto& d : s.ssds) {
            ImGui::Text("%.22s", d.model.empty() ? "SSD" : d.model.c_str());
            // SSD esquenta menos que CPU/GPU: +15 desloca as faixas de cor para 55/70 C.
            if (d.tempC) { ImGui::SameLine(290 * g_dpi);
                           ImGui::TextColored(tempColor(*d.tempC + 15), "%d°", *d.tempC); }
        }
    }

    if (g_tab == 5) drawKeyboard(g_kui);
    if (g_tab == 7) drawAboutContent();

    if (g_tab == 6) {
    if (g_isInstalled) {
        ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Instalado");
        ImGui::SameLine();
        if (ImGui::SmallButton("Desinstalar...")) Installer::relaunchSelf(L"--uninstall"); // ele confirma e fecha este app
    } else {
        ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Modo portatil");
        ImGui::SameLine();
        if (ImGui::SmallButton(g_hasInstall ? "Atualizar instalacao..." : "Instalar...")) {
            g_relaunchInstall = true;                 // fecha normalmente e reabre com --install
            PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        }
    }
    // Em caminho de rede a tarefa nao pode apontar para este exe: so libera depois de instalar.
    const bool blockAuto = !g_autoStartOk && !g_autoStart;
    if (blockAuto) ImGui::TextDisabled("Instale para poder iniciar com o Windows.");
    ImGui::BeginDisabled(blockAuto);
    bool as = g_autoStart;
    if (ImGui::Checkbox("Iniciar com o Windows (na bandeja)", &as)) {
        std::wstring info;
        bool ok = as ? AutoStart::enable(info) : AutoStart::disable();
        g_autoStart = AutoStart::isEnabled();
        if (!as && ok) info = L"desativado";
        if (!as && !ok) info = L"nao consegui remover a tarefa";
        g_autoStartMsg.clear();
        for (wchar_t c : info) g_autoStartMsg += (c < 128) ? (char)c : '?';
    }
    ImGui::EndDisabled();
    if (!g_autoStartMsg.empty()) ImGui::TextWrapped("%s", g_autoStartMsg.c_str());
    }
}

// Abre um endereco no navegador padrao. Passa pelo explorer.exe de proposito: este app
// roda como Administrador, e abrir direto faria o navegador herdar esse privilegio.
// So aceita http/https (o endereco vem de Version.h, mas nao custa conferir).
static void openUrl(const char* url) {
    if (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0) return;
    std::wstring w; for (const char* p = url; *p; ++p) w += (wchar_t)(unsigned char)*p;
    w = L"\"" + w + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", w.c_str(), nullptr, SW_SHOWNORMAL);
}

// ---- aba "Sobre" ------------------------------------------------------------
static void drawAboutContent() {
    auto row = [](const char* k, const char* v) {
        ImGui::TextDisabled("%s", k); ImGui::SameLine(110 * g_dpi); ImGui::TextWrapped("%s", v);
    };
    auto bullet = [](const char* t) { ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%s", t); };
    ImGui::PushFont(g_bigFont);
    ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "G15 Control BR");
    ImGui::PopFont();
    ImGui::TextWrapped("Controle de ventoinhas, modo termico e teclado RGB para notebooks Dell G15. "
                       "Alternativa leve ao Alienware Command Center (AWCC).");
    ImGui::Dummy(ImVec2(0, 4));

    row("Versao", "v" G15_VERSION "   (compilado em " __DATE__ ")");
    row("Autor", G15_AUTHOR);
    if (G15_AUTHOR_URL[0]) {   // link do perfil, na mesma linha do nome
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.0f, 1.0f), "LinkedIn");
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", G15_AUTHOR_URL);
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), b, IM_COL32(115, 184, 255, 255)); // sublinhado
        }
        if (ImGui::IsItemClicked()) openUrl(G15_AUTHOR_URL);
    }
    if (G15_HOMEPAGE[0]) row("Site", G15_HOMEPAGE);
    row("Testado em", "Dell G15 5515 (AMD)");
    row("Execucao", g_isInstalled ? "instalado" : "modo portatil");
    {
        std::string cfg; for (wchar_t c : settingsPath()) cfg += (c < 128) ? (char)c : '?';
        row("Configuracoes", cfg.c_str());
    }

    ImGui::SeparatorText("Origem");
    ImGui::TextWrapped("Escrito do zero em C++, a partir do estudo de projetos abertos que documentam as "
                       "funcoes da BIOS Dell/Alienware e o protocolo do teclado:");
    bullet("alienfx-tools (T-Troll) - ventoinhas, curvas e iluminacao");
    bullet("tcc-g15 (AlexIII) - interface WMI do AWCC");
    bullet("Dell-G-Series-Controller (cemkaya-mpi) - protocolo ELC do teclado");
    bullet("Dell_G15_Fan_Cli e dell-g15-fan-control - metodos ACPI");
    ImGui::TextWrapped("Interface: Dear ImGui (Omar Cornut).");

    ImGui::SeparatorText("Aviso");
    ImGui::TextWrapped("Projeto nao oficial, sem relacao com a Dell ou a Alienware. Atua no controle termico pela "
                       "BIOS: o firmware continua protegendo o hardware, mas use por sua conta e risco.");
}

static void drawUI(Snapshot& s, bool connected, AwccWmi& a, Controls& ctrl, History& h) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("G15 Control BR", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // cabecalho
    ImGui::PushFont(g_bigFont);
    ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "G15 Control BR");
    ImGui::PopFont();
    ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Dell G15 5515");
    if (!connected) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "  AWCC indisponivel (%s) - rode como Administrador", g_awccErr.c_str()); }
    else if (s.fans.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.75f, 0.35f, 1), "  AWCC conectado, mas nenhuma ventoinha encontrada (tentando de novo...)"); }
    if (g_tg.tripped) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "  PROTECAO TERMICA ATIVA"); }
    // versao, alinhada a direita (so se sobrar espaco na linha do cabecalho)
    {
        const char* ver = "v" G15_VERSION;
        float vw = ImGui::CalcTextSize(ver).x;
        float x  = ImGui::GetWindowContentRegionMax().x - vw;
        ImGui::SameLine();
        if (x > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", ver);
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Sobre o G15 Control BR");
            }
            if (ImGui::IsItemClicked()) g_tab = 7;   // abre a aba Sobre
        } else ImGui::NewLine();
    }

    // medidores (topo, largura total)
    // Responsivo: 7 caixas por linha se couber; senao quebra em 4, 2 ou 1 coluna.
    float tileMin = 130.0f * g_dpi;
    int cols = (int)(ImGui::GetContentRegionAvail().x / tileMin);
    cols = cols >= 7 ? 7 : cols >= 4 ? 4 : cols >= 2 ? 2 : 1;
    if (ImGui::BeginTable("stats", cols, ImGuiTableFlags_SizingStretchSame)) {
        char b[24], sub[48];
        snprintf(b, sizeof(b), s.cpuTemp ? "%d\u00b0" : "--", s.cpuTemp.value_or(0));
        statTile("CPU", b, s.cpuTemp ? colV(COL_CPU) : ImVec4(.5f,.5f,.5f,1), "Ryzen + iGPU");
        snprintf(b, sizeof(b), s.gpuTemp ? "%u\u00b0" : "--", s.gpuTemp.value_or(0));
        statTile("GPU", b, s.gpuTemp ? colV(COL_GPU) : ImVec4(.5f,.5f,.5f,1), "NVIDIA RTX");
        snprintf(b, sizeof(b), s.gpuUtil ? "%u%%" : "--", s.gpuUtil.value_or(0));
        statTile("GPU uso", b, colV(COL_USE), "utilizacao");
        snprintf(b, sizeof(b), s.gpuMHz ? "%u" : "--", s.gpuMHz.value_or(0));
        snprintf(sub, sizeof(sub), "MHz");
        statTile("GPU clock", b, colV(COL_CLK), sub);
        snprintf(b, sizeof(b), s.gpuW ? "%uW" : "--", s.gpuW.value_or(0));
        statTile("GPU potencia", b, colV(COL_PWR), "consumo");
        for (size_t i = 0; i < s.fans.size() && i < 2; ++i) {   // RPM das ventoinhas
            const FanRow& f = s.fans[i];
            snprintf(b, sizeof(b), f.rpm ? "%u" : "--", f.rpm.value_or(0));
            if (i < 2 && g_rpmPeak[i]) snprintf(sub, sizeof(sub), "RPM   pico %u", g_rpmPeak[i]); else snprintf(sub, sizeof(sub), "RPM");
            statTile(i == 0 ? "Fan CPU" : "Fan GPU", b, colV(i == 0 ? COL_FANC : COL_FANG), sub);
        }
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 2));

    // corpo: graficos (esq) + controles (dir)
    float ctrlW = 410 * g_dpi;
    float graphsW = ImGui::GetContentRegionAvail().x - ctrlW - ImGui::GetStyle().ItemSpacing.x;
    if (graphsW < 320 * g_dpi) graphsW = ImGui::GetContentRegionAvail().x; // janela estreita: empilha
    float bodyH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginChild("graphs", ImVec2(graphsW, bodyH), ImGuiChildFlags_Borders);
    drawGraphs(h, s);
    ImGui::EndChild();

    if (graphsW < ImGui::GetContentRegionAvail().x) {
        ImGui::SameLine();
        ImGui::BeginChild("controls", ImVec2(ctrlW, bodyH), ImGuiChildFlags_Borders);
        drawControls(s, connected, a, ctrl);
        ImGui::EndChild();
    }

    ImGui::End();
}

// ---- configuracoes persistentes: g15gui.ini ao lado do exe ------------------
// Guarda posicao/tamanho da janela, curva automatica ligada e o teclado (modo,
// cores, brilho, velocidade). Gravado ao fechar e em logoff/desligamento.
static Controls* g_ctrl = nullptr;

// %APPDATA%\G15ControlBR\g15gui.ini. Fica em disco local de proposito: ao lado do exe
// falhava quando o app roda de um caminho de rede (\\servidor\pasta\...), onde a API de
// .ini do Windows cria o arquivo mas nao consegue gravar nele (saia com 0 bytes).
static std::wstring settingsPath() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t base[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", base, MAX_PATH);
    std::wstring dir;
    if (n > 0 && n < MAX_PATH) {
        dir = std::wstring(base) + L"\\G15ControlBR";
        CreateDirectoryW(dir.c_str(), nullptr);       // ok se ja existir
        dir += L"\\";
    } else {                                          // sem APPDATA: volta para a pasta do exe
        std::wstring p = iniPathNextToExe();
        size_t s = p.find_last_of(L'\\');
        if (s != std::wstring::npos) dir = p.substr(0, s + 1);
    }
    cached = dir + L"g15gui.ini";
    return cached;
}
static void iniSetInt(const wchar_t* sec, const wchar_t* key, int v, const std::wstring& f) {
    wchar_t b[32]; swprintf(b, 32, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, f.c_str());
}
static int iniGetInt(const wchar_t* sec, const wchar_t* key, int def, const std::wstring& f) {
    return (int)GetPrivateProfileIntW(sec, key, def, f.c_str());
}
static void iniSetCol(const wchar_t* key, const float* c, const std::wstring& f) {
    KbdElc::Rgb r = toRgb(c);
    wchar_t b[16]; swprintf(b, 16, L"%02X%02X%02X", r.r, r.g, r.b);
    WritePrivateProfileStringW(L"keyboard", key, b, f.c_str());
}
static void iniGetCol(const wchar_t* key, float* c, const std::wstring& f) {
    wchar_t b[16] = L"";
    GetPrivateProfileStringW(L"keyboard", key, L"", b, 16, f.c_str());
    KbdElc::Rgb r;
    if (parseRgb(b, r)) { c[0] = r.r / 255.0f; c[1] = r.g / 255.0f; c[2] = r.b / 255.0f; }
}

static void saveSettings(HWND hwnd) {
    const std::wstring f = settingsPath();
    WINDOWPLACEMENT wp; wp.length = sizeof(wp);
    if (hwnd && GetWindowPlacement(hwnd, &wp)) {
        const RECT& r = wp.rcNormalPosition;
        iniSetInt(L"window", L"x", r.left, f);  iniSetInt(L"window", L"y", r.top, f);
        iniSetInt(L"window", L"w", r.right - r.left, f); iniSetInt(L"window", L"h", r.bottom - r.top, f);
        bool maxed = wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED);
        iniSetInt(L"window", L"maximized", maxed ? 1 : 0, f);
    }
    if (g_ctrl) iniSetInt(L"fans", L"autoCurve", g_ctrl->autoCurve ? 1 : 0, f);
    if (g_ctrl) iniSetInt(L"fans", L"gpuKeepOn", g_ctrl->gpuKeepOn ? 1 : 0, f);
    if (g_ctrl) iniSetInt(L"fans", L"gpuFloorPct", g_ctrl->gpuFloorPct, f);
    iniSetInt(L"thermal", L"guard", g_tg.enabled ? 1 : 0, f);
    iniSetInt(L"thermal", L"cpuLimit", g_tg.cpuLimit, f); iniSetInt(L"thermal", L"gpuLimit", g_tg.gpuLimit, f);
    iniSetInt(L"thermal", L"gKey", g_tg.gKey ? 1 : 0, f);
    iniSetInt(L"thermal", L"restoreMode", g_tg.restoreMode ? 1 : 0, f);
    iniSetInt(L"app", L"tab", g_tab, f);
    const KbdUi& k = g_kui;
    iniSetInt(L"keyboard", L"used", k.used ? 1 : 0, f);   iniSetInt(L"keyboard", L"off", k.off ? 1 : 0, f);
    iniSetInt(L"keyboard", L"mode", k.mode, f);           iniSetInt(L"keyboard", L"bright", k.bright, f);
    iniSetInt(L"keyboard", L"speedMs", k.speedMs, f);
    iniSetCol(L"zone0", k.col[0], f); iniSetCol(L"zone1", k.col[1], f);
    iniSetCol(L"zone2", k.col[2], f); iniSetCol(L"zone3", k.col[3], f);
    iniSetCol(L"color2", k.col2, f);
}

static void loadKeyboardSettings(KbdUi& k) {
    const std::wstring f = settingsPath();
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
    k.used    = iniGetInt(L"keyboard", L"used", 0, f) != 0;
    k.off     = iniGetInt(L"keyboard", L"off", 0, f) != 0;
    k.mode    = clampi(iniGetInt(L"keyboard", L"mode", k.mode, f), 0, 3);
    k.bright  = clampi(iniGetInt(L"keyboard", L"bright", k.bright, f), 0, 100);
    k.speedMs = clampi(iniGetInt(L"keyboard", L"speedMs", k.speedMs, f), 500, 8000);
    iniGetCol(L"zone0", k.col[0], f); iniGetCol(L"zone1", k.col[1], f);
    iniGetCol(L"zone2", k.col[2], f); iniGetCol(L"zone3", k.col[3], f);
    iniGetCol(L"color2", k.col2, f);
}

// Restaura posicao/tamanho. false = nada salvo (ou invalido): usa o padrao.
// hidden = inicio na bandeja: posiciona sem mostrar.
static bool restoreWindow(HWND hwnd, bool hidden) {
    const std::wstring f = settingsPath();
    int w = iniGetInt(L"window", L"w", 0, f), h = iniGetInt(L"window", L"h", 0, f);
    if (w < 400 || h < 300) return false;
    int x = iniGetInt(L"window", L"x", 100, f), y = iniGetInt(L"window", L"y", 80, f);
    RECT r = { x, y, x + w, y + h };
    if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return false; // monitor que nao existe mais
    WINDOWPLACEMENT wp; ZeroMemory(&wp, sizeof(wp)); wp.length = sizeof(wp);
    wp.rcNormalPosition = r;
    bool maxed = iniGetInt(L"window", L"maximized", 0, f) != 0;
    wp.showCmd = hidden ? SW_HIDE : maxed ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    if (hidden) g_startMax = maxed;
    return SetWindowPlacement(hwnd, &wp) != FALSE;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int) {
    // --tray: inicia escondido na bandeja (usado pelo "Iniciar com o Windows").
    const bool startTray = cmdLine && wcsstr(cmdLine, L"--tray") != nullptr;
    // --uninstall: chamado por "Aplicativos instalados" do Windows ou pelo botao da GUI.
    //   --quiet: sem perguntas (QuietUninstallString / scripts).
    if (cmdLine && wcsstr(cmdLine, L"--uninstall"))
        return Installer::runUninstall(wcsstr(cmdLine, L"--quiet") != nullptr);
    // --autostart-on / --autostart-off: liga/desliga "iniciar com o Windows" sem abrir a janela.
    // Codigo de saida 0 = ok. (Mesma acao da caixa de selecao da GUI.)
    if (cmdLine && wcsstr(cmdLine, L"--autostart-on"))  { std::wstring info; return AutoStart::enable(info) ? 0 : 1; }
    if (cmdLine && wcsstr(cmdLine, L"--autostart-off")) { return AutoStart::disable() ? 0 : 1; }
    // --install: instala direto, sem perguntar (botao "Instalar" da GUI).
    const bool wantInstall = cmdLine && wcsstr(cmdLine, L"--install") != nullptr;
    // Instancia unica: duas copias brigariam pelo controle das ventoinhas e do teclado.
    // Se ja houver uma rodando, pede para ela aparecer (mesmo estando na bandeja) e sai.
    g_msgShowExisting = RegisterWindowMessageW(L"G15ControlBR_ShowExisting");
    HANDLE instMutex = CreateMutexW(nullptr, FALSE, L"Local\\G15ControlBR_SingleInstance");
    // --install vem de uma copia que esta fechando: espera ela sair (ate ~8 s).
    for (int i = 0; wantInstall && instMutex && GetLastError() == ERROR_ALREADY_EXISTS && i < 80; ++i) {
        CloseHandle(instMutex); Sleep(100);
        instMutex = CreateMutexW(nullptr, FALSE, L"Local\\G15ControlBR_SingleInstance");
    }
    if (instMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"G15ControlBR", nullptr);
        if (other) {
            DWORD pid = 0; GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, g_msgShowExisting, 0, 0);
        }
        CloseHandle(instMutex);
        return 0;
    }
    ImGui_ImplWin32_EnableDpiAwareness();

    // Instalador embutido: se esta copia nao e a instalada, pergunta o que fazer.
    // Nao pergunta no inicio automatico (--tray) nem se o usuario ja escolheu "portatil".
    if (!Installer::isRunningInstalled() && !startTray) {
        const std::wstring sf = settingsPath();
        const bool portable = iniGetInt(L"app", L"portable", 0, sf) != 0;
        if (wantInstall || !portable) {
            bool dontAsk = false;
            Installer::Choice ch = wantInstall ? Installer::Choice::Install
                                               : Installer::ask(Installer::isInstalled(), dontAsk);
            if (ch == Installer::Choice::Quit) { if (instMutex) CloseHandle(instMutex); return 0; }
            if (ch == Installer::Choice::Install) {
                std::wstring err;
                if (Installer::install(err)) {
                    if (!err.empty()) MessageBoxW(nullptr, err.c_str(), L"G15 Control BR", MB_OK | MB_ICONWARNING);
                    iniSetInt(L"app", L"portable", 0, sf);
                    if (instMutex) CloseHandle(instMutex);   // libera para a copia instalada abrir
                    Installer::launchInstalled(nullptr);
                    return 0;
                }
                MessageBoxW(nullptr, (L"A instalação falhou: " + err + L"\n\nO programa vai abrir em modo portátil.").c_str(),
                            L"G15 Control BR", MB_OK | MB_ICONWARNING);
            } else if (dontAsk) {
                iniSetInt(L"app", L"portable", 1, sf);
            }
        }
    }
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr, nullptr, nullptr, nullptr, L"G15ControlBR", nullptr };
    wc.hIcon = appIcon(false); wc.hIconSm = appIcon(true);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"G15 Control BR", WS_OVERLAPPEDWINDOW, 100, 80, 1060, 760, nullptr, nullptr, wc.hInstance, nullptr);
    g_msgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }
    if (startTray) {
        restoreWindow(hwnd, true);
        trayAdd(hwnd);
        // Bandeja ainda nao disponivel (logo apos o logon): cai para minimizado, que tenta de novo.
        if (!g_inTray) ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    } else if (!restoreWindow(hwnd, false)) ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // nao gerar imgui.ini: a janela e unica e o layout e fixo
    g_dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f * g_dpi);
    g_numFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f * g_dpi);
    g_bigFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 28.0f * g_dpi);
    if (!g_bigFont) g_bigFont = io.Fonts->AddFontDefault();
    if (!g_numFont) g_numFont = g_bigFont;
    applyModernStyle();
    ImGui::GetStyle().ScaleAllSizes(g_dpi);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    AwccWmi awcc; bool connected = awcc.connect();
    GpuNvml gpu; gpu.init();
    g_kbd.open();   // teclado RGB (opcional)
    g_autoStart    = AutoStart::isEnabled();
    g_autoStartOk  = AutoStart::canUseThisExe();
    g_isInstalled  = Installer::isRunningInstalled();
    g_hasInstall   = Installer::isInstalled();
    g_hwnd         = hwnd;
    loadKeyboardSettings(g_kui);
    // Reaplica o ultimo estado so se o usuario ja usou o teclado pelo app; senao nao toca nele.
    if (g_kbd.isOpen() && g_kui.used) {
        if (g_kui.off) g_kbd.off(); else g_kbd.apply(kbdScene(g_kui));
        g_kbd.setBrightness(g_kui.bright);
    }
    FanCurves curves; if (!loadConfig(iniPathNextToExe(), curves)) curves = defaultCurves();

    Snapshot snap; Controls ctrl; History hist;
    g_ctrl = &ctrl;
    // Curva ligada por padrao: sem ela so o firmware decide, e em Balanced ele deixa a CPU
    // passar de 80 C com a ventoinha em ~2000 RPM (medido no 5515).
    ctrl.autoCurve = connected && iniGetInt(L"fans", L"autoCurve", 1, settingsPath()) != 0;
    ctrl.gpuKeepOn = iniGetInt(L"fans", L"gpuKeepOn", 1, settingsPath()) != 0;
    ctrl.gpuFloorPct = iniGetInt(L"fans", L"gpuFloorPct", 20, settingsPath());
    if (ctrl.gpuFloorPct < 0) ctrl.gpuFloorPct = 0; else if (ctrl.gpuFloorPct > 40) ctrl.gpuFloorPct = 40;
    {   // protecao termica e tecla G (padrao: ambas ligadas, limites 95/85 C)
        const std::wstring sf = settingsPath();
        auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
        g_tg.enabled  = iniGetInt(L"thermal", L"guard", 1, sf) != 0;
        g_tg.cpuLimit = clampi(iniGetInt(L"thermal", L"cpuLimit", 95, sf), 80, 100);
        g_tg.gpuLimit = clampi(iniGetInt(L"thermal", L"gpuLimit", 85, sf), 70, 95);
        g_tg.gKey     = iniGetInt(L"thermal", L"gKey", 1, sf) != 0;
        g_tg.restoreMode = iniGetInt(L"thermal", L"restoreMode", 1, sf) != 0;
        g_tab = clampi(iniGetInt(L"app", L"tab", 0, sf), 0, TAB_COUNT - 1);
        if (g_tg.gKey) g_tg.gKeyOk = RegisterHotKey(hwnd, HOTKEY_G, MOD_NOREPEAT, VK_F17) != FALSE;
    }
    // Enumera ventoinhas e le o boost atual. Se vier vazio (WMI ainda subindo),
    // o laco principal tenta de novo a cada 3 s em vez de ficar sem controles.
    auto loadFans = [&]() {
        initFans(awcc, snap);
        for (size_t i = 0; i < snap.fans.size() && i < 4; ++i) ctrl.boost[i] = (int)awcc.getFanBoost(snap.fans[i].id).value_or(0);
        g_fanIds.clear();
        for (auto& f : snap.fans) g_fanIds.push_back(f.id);
        // piso da ventoinha da GPU ja na abertura (vale mesmo com a curva desligada)
        if (snap.fans.size() > GPU_FAN_INDEX) {
            int b = effBoost(ctrl, GPU_FAN_INDEX, ctrl.boost[GPU_FAN_INDEX]);
            if (b != ctrl.boost[GPU_FAN_INDEX]) { awcc.setFanBoost(snap.fans[GPU_FAN_INDEX].id, (uint32_t)b); ctrl.boost[GPU_FAN_INDEX] = b; }
        }
    };
    ULONGLONG lastFanTry = GetTickCount64();
    if (connected) {
        loadFans();
        g_awcc = &awcc;
    } else {
        for (wchar_t c : awcc.lastError()) g_awccErr += (c < 128) ? (char)c : '?';
    }
    ctrl.initialized = true;
    readSnapshot(awcc, connected, gpu, snap);
    snap.ssds = readNvmeTemps();
    pushHistory(hist, snap, ctrl);
    ULONGLONG lastRead = GetTickCount64(), lastSsd = lastRead, lastCurveTick = 0;
    const ULONGLONG curveEveryMs = (ULONGLONG)curves.intervalSec * 1000;

    // Desenha sob demanda: so quando ha entrada do usuario ou dado novo (a cada READ_MS).
    // Antes redesenhava a tela inteira a cada vsync, mesmo parada, ocupando a GPU a toa.
    bool running = true;
    int  redraw = 3;                 // quadros pendentes (o ImGui precisa de alguns p/ assentar)
    while (running) {
        if (redraw <= 0) {           // nada a desenhar: dorme ate a proxima leitura ou mensagem
            ULONGLONG el = GetTickCount64() - lastRead;
            ULONGLONG waitMs = el >= READ_MS ? 0 : READ_MS - el;
            if (g_redrawAt) {                 // animacao pendente: acorda antes
                ULONGLONG nowW = GetTickCount64();
                ULONGLONG toAnim = g_redrawAt > nowW ? g_redrawAt - nowW : 0;
                if (toAnim < waitMs) waitMs = toAnim;
            }
            MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)waitMs, QS_ALLINPUT);
            if (g_redrawAt && GetTickCount64() >= g_redrawAt) { g_redrawAt = 0; redraw = 1; }
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
            redraw = 3;
        }
        if (!running) break;

        // Tecla G ou "G-Mode" no menu da bandeja: alterna e atualiza a tela na hora.
        if (g_toggleG) {
            g_toggleG = false;
            if (connected) { toggleGMode(awcc, snap); g_gModeOn = snap.mode && *snap.mode == 0xAB; redraw = 3; }
        }

        ULONGLONG now = GetTickCount64();
        if (g_saveAt && now >= g_saveAt) { g_saveAt = 0; saveSettings(hwnd); }
        if (g_kbdReapplyAt && now >= g_kbdReapplyAt) {
            g_kbdReapplyAt = 0;
            if (g_kui.used) {
                if (!g_kbd.isOpen()) g_kbd.open();
                auto put = [&] { return g_kui.off ? g_kbd.off() : g_kbd.apply(kbdScene(g_kui)); };
                if (!put() && g_kbd.open()) put();          // apos suspensao o handle pode ter morrido
                g_kbd.setBrightness(g_kui.bright);
            }
        }
        if (now - lastRead >= READ_MS) {
            readSnapshot(awcc, connected, gpu, snap); lastRead = now;
            if (connected) thermalGuardTick(awcc, snap, ctrl, now);
            if (connected && snap.fans.empty() && now - lastFanTry >= 3000) { loadFans(); lastFanTry = now; }
            // SSDs mudam devagar e a leitura abre os discos: a cada 5 s basta.
            if (now - lastSsd >= 5000) { snap.ssds = readNvmeTemps(); lastSsd = now; }
            // Curva: mesmo criterio da CLI (intervalo e histerese do g15ctl.ini).
            if (connected && ctrl.autoCurve && !g_tg.tripped && now - lastCurveTick >= curveEveryMs) {   // protecao disparada tem prioridade
                lastCurveTick = now;
                const int tempOf[2] = { snap.cpuTemp ? (int)*snap.cpuTemp : -1, snap.gpuTemp ? (int)*snap.gpuTemp : -1 };
                const std::vector<CurvePoint>* cv[2] = { &curves.cpu, &curves.gpu };
                static int noTemp[2] = {0, 0};   // ciclos seguidos sem leitura do sensor de cada ventoinha
                for (size_t i = 0; i < 2 && i < snap.fans.size(); ++i) {
                    int temp = tempOf[i];
                    if (temp < 0) {
                        // Sem leitura nao e 0 C: mantem o ultimo boost por 3 ciclos; depois usa o outro
                        // sensor (processador e GPU esquentam juntos) e, sem nenhum, um boost de seguranca.
                        if (++noTemp[i] < 3) continue;
                        temp = tempOf[1 - i];
                        if (temp < 0) { temp = -1; }
                    } else noTemp[i] = 0;
                    int wanted = temp >= 0 ? interpolateBoost(*cv[i], temp) : SAFE_BOOST_NO_SENSOR;
                    int t = effBoost(ctrl, i, wanted);
                    int last = ctrl.lastCurve[i];
                    // O firmware pode zerar o boost sozinho (troca de modo, suspensao): confere o valor
                    // real na BIOS e reenvia se divergir, em vez de confiar so no ultimo envio.
                    if (last >= 0) {
                        auto real = awcc.getFanBoost(snap.fans[i].id);
                        if (real && (int)*real != last) last = -1;
                    }
                    // Vigia: boost alto mas RPM baixo por ~16 s = o firmware esta ignorando os comandos
                    // (visto no 5515: boost 63% e a ventoinha parada em 600 RPM ate reiniciar o app).
                    // Fechar o app manda boost 0 e destrava; faz o mesmo aqui e reenvia o valor.
                    static int stuck[2] = {0, 0};
                    if (ctrl.lastCurve[i] >= 90 && snap.fans[i].rpm && *snap.fans[i].rpm < 1500) {
                        if (++stuck[i] >= 8) {
                            stuck[i] = 0;
                            awcc.setFanBoost(snap.fans[i].id, 0);
                            Sleep(300);
                            last = -1;
                        }
                    } else stuck[i] = 0;
                    bool apply = last < 0 || abs(t - last) >= curves.hysteresis
                                 || (t == 0 && last != 0) || (t == 255 && last != 255);
                    if (apply) { awcc.setFanBoost(snap.fans[i].id, (uint32_t)t); ctrl.lastCurve[i] = t; ctrl.boost[i] = t; }
                }
            }
            pushHistory(hist, snap, ctrl);
            if (g_inTray) { // dica do icone: temperaturas ao passar o mouse
                wchar_t c[16] = L"--", g[16] = L"--", tip[96];
                if (snap.cpuTemp) swprintf(c, 16, L"%u°C", *snap.cpuTemp);
                if (snap.gpuTemp) swprintf(g, 16, L"%u°C", *snap.gpuTemp);
                const wchar_t NL = L'\n';
                swprintf(tip, 96, L"G15 Control BR%lcCPU %ls   GPU %ls%lc%ls%ls", NL, c, g, NL,
                         g_tg.tripped ? L"PROTECAO: ventoinhas 100%  " : g_gModeOn ? L"G-Mode ligado  " : L"",
                         ctrl.autoCurve ? L"Curva automatica" : L"");
                traySetTip(tip);
            }
            if (redraw < 1) redraw = 1; // dado novo, sem interacao: um quadro basta
        }

        // Minimizada: continua lendo e controlando a curva, mas nao desenha nada.
        if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) { redraw = 0; continue; }
        if (redraw <= 0) continue;
        --redraw;

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        drawUI(snap, connected, awcc, ctrl, hist);
        // Qualquer interacao agenda uma gravacao das configuracoes 3 s depois de o usuario soltar
        // o controle, para nao depender de o app fechar normalmente.
        if (ImGui::IsAnyItemActive() || ImGui::IsMouseReleased(0)) g_saveAt = GetTickCount64() + 3000;
        ImGui::Render();
        const float clear[4] = { 0.05f, 0.055f, 0.07f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRTV, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
        if (g_wantRedraw) { g_wantRedraw = false; if (redraw < 1) redraw = 1; }
    }

    // restaura ventoinhas ao automatico ao sair
    restoreFans();

    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); DestroyWindow(hwnd); UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (instMutex) CloseHandle(instMutex);
    // Botao "Instalar": este app ja fechou tudo; reabre a si mesmo so para instalar.
    if (g_relaunchInstall) Installer::relaunchSelf(L"--install");
    return 0;
}
