// G15 Control BR - KbdElc.h
// Teclado RGB de 4 zonas (controlador "ELC", USB HID 187C:0550 / 187C:0551).
// Fala direto com o HID do Windows: sem driver, sem biblioteca externa e sem
// precisar de Administrador. Protocolo: relatorios de 33 bytes (+1 de report ID),
// o mesmo usado pelo AWCC, pelo alienfx-tools (API v4) e pelos projetos Linux.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class KbdElc {
public:
    static constexpr int ZONES = 4;          // da esquerda para a direita: esq, meio-esq, meio-dir, dir

    struct Rgb { uint8_t r = 0, g = 0, b = 0; };

    enum Effect : uint8_t { Color = 0, Pulse = 1, Morph = 2 };

    // Um passo da animacao de uma zona.
    struct Action {
        Effect   effect   = Color;
        uint16_t duration = 2000;            // duracao do passo (ms, aprox.)
        uint16_t tempo    = 0xFA;            // 0xFA = fixo (cor estatica)
        Rgb      color;
    };

    // O que cada zona faz. Um unico passo Color = cor fixa.
    struct Scene {
        std::vector<Action> zone[ZONES];
        static Scene solid(Rgb c);                           // tudo de uma cor
        static Scene zones(const Rgb z[ZONES]);              // uma cor por zona
        static Scene breathe(Rgb c, uint16_t ms);            // acende/apaga suave
        static Scene cycle(const std::vector<Rgb>& colors, uint16_t ms); // transicao entre cores
    };

    KbdElc() = default;
    ~KbdElc();
    KbdElc(const KbdElc&) = delete;
    KbdElc& operator=(const KbdElc&) = delete;

    bool open();                              // procura e abre o controlador
    void close();
    bool isOpen() const { return h_ != nullptr; }
    void setZoneIds(const uint8_t ids[ZONES]) { for (int i = 0; i < ZONES; ++i) ids_[i] = ids[i]; }
    uint16_t pid() const { return pid_; }
    const std::string& lastError() const { return err_; }

    // Aplica a cena. persist=false: so "ao vivo" (some ao reiniciar, nao grava nada).
    // persist=true: tambem grava na memoria do controlador como padrao de inicializacao.
    bool apply(const Scene& s, bool persist = false);
    bool setBrightness(int percent);          // 0..100
    bool off() { return apply(Scene::solid({0, 0, 0})); }

    // Diagnostico
    int  status();                            // 33 = pronto, 34 = ocupado, -1 = erro
    bool firmwareVersion(int& major, int& minor, int& rev);
    std::string lastReplyHex() const;

private:
    bool send(const uint8_t* payload, size_t n);  // payload comeca no comando (0x2x)
    bool readReply();
    bool waitReady();
    bool control(uint8_t sub, uint16_t animId);
    bool sendZone(int zone, const std::vector<Action>& acts, bool forceSeries);

    void*    h_      = nullptr;               // HANDLE
    uint8_t  ids_[ZONES] = { 8, 9, 10, 11 }; // ID de luz de cada zona. No G15 5515 sao 8-11 (confirmado);
                                              // outros modelos usam 0-3 (ver setZoneIds / --ids)
    uint16_t pid_    = 0;
    uint16_t outLen_ = 0, inLen_ = 0;
    uint8_t  reply_[64] = {0};
    std::string err_;
};

// "RRGGBB" ou "#RRGGBB" -> Rgb. false se invalido.
bool parseRgb(const std::wstring& s, KbdElc::Rgb& out);
