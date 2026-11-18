// G15 Control BR - KbdElc.cpp
#include "KbdElc.h"
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
}
#include <cstdio>
#include <cstring>
#include <cwctype>

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace {
    const uint16_t VID = 0x187C;
    const uint16_t PIDS[] = { 0x0550, 0x0551 };

    // Comandos principais
    const uint8_t CMD_QUERY = 0x20, CMD_USER_ANIM = 0x21, CMD_SERIES = 0x23,
                  CMD_ACTION = 0x24, CMD_DIM = 0x26, CMD_SET_COLOR = 0x27;
    // Subcomandos de animacao
    const uint8_t AN_START = 1, AN_SAVE = 2, AN_PLAY = 3, AN_REMOVE = 4, AN_SET_DEFAULT = 6;
    // IDs de animacao
    const uint16_t ANIM_LIVE    = 0xFFFF;   // "em execucao": nao grava na memoria
    const uint16_t ANIM_DEFAULT = 0x0061;   // padrao apos o boot (gravado)

    const int ST_READY = 33, ST_BUSY = 34;
}

// ---- cenas ----------------------------------------------------------------
KbdElc::Scene KbdElc::Scene::solid(Rgb c) {
    Scene s;
    for (auto& z : s.zone) z = { Action{ Color, 2000, 0xFA, c } };
    return s;
}
KbdElc::Scene KbdElc::Scene::zones(const Rgb z[ZONES]) {
    Scene s;
    for (int i = 0; i < ZONES; ++i) s.zone[i] = { Action{ Color, 2000, 0xFA, z[i] } };
    return s;
}
KbdElc::Scene KbdElc::Scene::breathe(Rgb c, uint16_t ms) {
    Scene s;
    for (auto& z : s.zone) z = { Action{ Morph, ms, 0x64, c }, Action{ Morph, ms, 0x64, Rgb{} } };
    return s;
}
KbdElc::Scene KbdElc::Scene::cycle(const std::vector<Rgb>& colors, uint16_t ms) {
    Scene s;
    for (auto& z : s.zone)
        for (const Rgb& c : colors) z.push_back(Action{ Morph, ms, 0x64, c });
    return s;
}

bool parseRgb(const std::wstring& in, KbdElc::Rgb& out) {
    std::wstring s = in;
    if (!s.empty() && s[0] == L'#') s.erase(0, 1);
    if (s.size() != 6) return false;
    for (wchar_t c : s) if (!iswxdigit(c)) return false;
    unsigned long v = wcstoul(s.c_str(), nullptr, 16);
    out = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    return true;
}

// ---- dispositivo ----------------------------------------------------------
KbdElc::~KbdElc() { close(); }

void KbdElc::close() {
    if (h_) { CloseHandle((HANDLE)h_); h_ = nullptr; }
}

bool KbdElc::open() {
    close();
    err_.clear();
    GUID hidGuid; HidD_GetHidGuid(&hidGuid);
    HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) { err_ = "SetupDiGetClassDevs falhou"; return false; }

    bool sawDevice = false;
    SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; !h_ && SetupDiEnumDeviceInterfaces(info, nullptr, &hidGuid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(info, &ifd, det, need, nullptr, nullptr)) continue;

        HANDLE h = CreateFileW(det->DevicePath, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        HIDD_ATTRIBUTES at; at.Size = sizeof(at);
        bool match = false;
        if (HidD_GetAttributes(h, &at) && at.VendorID == VID)
            for (uint16_t p : PIDS) if (at.ProductID == p) match = true;
        if (match) {
            sawDevice = true;
            PHIDP_PREPARSED_DATA pp = nullptr;
            HIDP_CAPS caps{};
            if (HidD_GetPreparsedData(h, &pp)) {
                HidP_GetCaps(pp, &caps);
                HidD_FreePreparsedData(pp);
            }
            // Protocolo ELC = relatorio de saida de 34 bytes (1 de ID + 33 de dados).
            if (caps.OutputReportByteLength == 34 && caps.InputReportByteLength <= sizeof(reply_)) {
                h_ = h; pid_ = at.ProductID;
                outLen_ = caps.OutputReportByteLength;
                inLen_  = caps.InputReportByteLength ? caps.InputReportByteLength : 34;
                continue;
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(info);

    if (!h_) {
        err_ = sawDevice ? "controlador 187C encontrado, mas com formato de relatorio inesperado"
                         : "teclado RGB (USB 187C:0550/0551) nao encontrado";
        return false;
    }
    return true;
}

bool KbdElc::send(const uint8_t* payload, size_t n) {
    if (!h_) return false;
    uint8_t buf[64] = {0};
    if (n + 2 > outLen_) return false;
    buf[0] = 0x00;            // report ID
    buf[1] = 0x03;            // prefixo ELC
    memcpy(buf + 2, payload, n);
    if (!HidD_SetOutputReport((HANDLE)h_, buf, outLen_)) {
        char e[64]; snprintf(e, sizeof(e), "envio HID falhou (erro %lu)", GetLastError());
        err_ = e;
        return false;
    }
    return true;
}

bool KbdElc::readReply() {
    if (!h_) return false;
    memset(reply_, 0, sizeof(reply_));
    return HidD_GetInputReport((HANDLE)h_, reply_, inLen_) != FALSE;
}

int KbdElc::status() {
    return readReply() ? reply_[2] : -1;
}

// Espera o controlador sair de "ocupado" (no maximo ~2 s, para nunca travar o app).
bool KbdElc::waitReady() {
    for (int i = 0; i < 100; ++i) {
        int st = status();
        if (st >= 0 && st != ST_BUSY) return true;
        Sleep(20);
    }
    err_ = "controlador do teclado nao ficou pronto";
    return false;
}

bool KbdElc::control(uint8_t sub, uint16_t animId) {
    const uint8_t p[] = { CMD_USER_ANIM, 0x00, sub, (uint8_t)(animId >> 8), (uint8_t)animId };
    return send(p, sizeof(p));
}

// Envia o que uma zona faz. Cor fixa "ao vivo" usa SET_COLOR (mais simples);
// animacao ou gravacao usam serie + acoes (ate 3 acoes por relatorio).
bool KbdElc::sendZone(int zone, const std::vector<Action>& acts, bool forceSeries) {
    if (acts.empty()) return true;
    if (!forceSeries && acts.size() == 1 && acts[0].effect == Color) {
        const Rgb& c = acts[0].color;
        const uint8_t p[] = { CMD_SET_COLOR, c.r, c.g, c.b, 0x00, 0x01, ids_[zone] };
        return send(p, sizeof(p));
    }
    const uint8_t sel[] = { CMD_SERIES, 0x01 /*loop*/, 0x00, 0x01 /*n zonas*/, ids_[zone] };
    if (!send(sel, sizeof(sel))) return false;
    for (size_t i = 0; i < acts.size(); i += 3) {
        uint8_t p[1 + 3 * 8] = { CMD_ACTION };
        size_t n = 1;
        for (size_t k = i; k < acts.size() && k < i + 3; ++k) {
            const Action& a = acts[k];
            p[n++] = a.effect;
            p[n++] = (uint8_t)(a.duration >> 8); p[n++] = (uint8_t)a.duration;
            p[n++] = (uint8_t)(a.tempo >> 8);    p[n++] = (uint8_t)a.tempo;
            p[n++] = a.color.r; p[n++] = a.color.g; p[n++] = a.color.b;
        }
        if (!send(p, n)) return false;
    }
    return true;
}

bool KbdElc::apply(const Scene& s, bool persist) {
    if (!h_) { err_ = "teclado nao aberto"; return false; }

    if (persist) {
        // Grava como animacao padrao (0x61): e o que o teclado mostra apos ligar.
        // O controlador tem um slot por estado de energia e troca sozinho quando o estado
        // muda (tomada, carregando, carregado, bateria). Slot vazio = teclado apagado, entao
        // a cena vai para todos os estados "ligado"; nos de suspensao fica apagado.
        // O padrao (0x61) e gravado por ultimo para continuar sendo o de inicializacao.
        const Scene dark = Scene::solid({0, 0, 0});
        const struct { uint16_t id; const Scene* sc; } slots[] = {
            { 0x5B, &dark }, { 0x5E, &dark },                    // suspenso na tomada / na bateria
            { 0x5C, &s }, { 0x5D, &s }, { 0x5F, &s }, { 0x60, &s }, // carregado, carregando, bateria, bateria fraca
            { ANIM_DEFAULT, &s },
        };
        for (const auto& sl : slots) {
            if (!waitReady()) return false;
            if (!control(AN_REMOVE, sl.id) || !control(AN_START, sl.id)) return false;
            for (int z = 0; z < ZONES; ++z) if (!sendZone(z, sl.sc->zone[z], true)) return false;
            if (!control(AN_SAVE, sl.id) || !control(AN_SET_DEFAULT, sl.id)) return false;
        }
    }

    // Ao vivo: animacao "em execucao", nada e gravado.
    if (!waitReady()) return false;
    if (!control(AN_REMOVE, ANIM_LIVE) || !control(AN_START, ANIM_LIVE)) return false;
    for (int z = 0; z < ZONES; ++z) if (!sendZone(z, s.zone[z], false)) return false;
    return control(AN_PLAY, ANIM_LIVE);
}

bool KbdElc::setBrightness(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    // O comando e de "escurecimento": 0 = brilho total, 100 = apagado.
    const uint8_t p[] = { CMD_DIM, (uint8_t)(100 - percent), 0x00, (uint8_t)ZONES, ids_[0], ids_[1], ids_[2], ids_[3] };
    return waitReady() && send(p, sizeof(p));
}

bool KbdElc::firmwareVersion(int& major, int& minor, int& rev) {
    const uint8_t p[] = { CMD_QUERY, 0x00 };
    if (!send(p, sizeof(p)) || !readReply()) return false;
    major = reply_[4]; minor = reply_[5]; rev = reply_[6];
    return true;
}

std::string KbdElc::lastReplyHex() const {
    std::string s;
    char b[4];
    for (int i = 0; i < 12; ++i) { snprintf(b, sizeof(b), "%02X ", reply_[i]); s += b; }
    return s;
}
