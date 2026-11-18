// G15 Control BR - AutoStart.h
// "Iniciar com o Windows": tarefa agendada no logon do usuario, com privilegio
// elevado (o app precisa de Administrador, entao a chave Run do registro nao serve:
// ela abriria o UAC a cada boot). Mesmo metodo do tcc-g15.
#pragma once
#include <string>

namespace AutoStart {
    bool isEnabled();                      // a tarefa existe?
    bool canUseThisExe();                  // false se este exe esta em caminho de rede
    // Cria/atualiza a tarefa apontando para o exe (com --tray). Recusa caminho de rede
    // (ex.: \\servidor\pasta\...), que pode nao existir no logon: nesse caso o usuario deve
    // instalar o programa. info = texto para mostrar ao usuario.
    bool enable(std::wstring& info);                              // este exe
    bool enable(const std::wstring& exe, std::wstring& info);     // um exe especifico
    bool disable();
}
