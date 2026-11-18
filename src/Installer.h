// G15 Control BR - Installer.h
// Instalador embutido no proprio exe (sem setup separado).
//   Instalar   : copia para "Arquivos de Programas\G15 Control BR", cria atalho no
//                Menu Iniciar e a entrada em "Aplicativos instalados" do Windows.
//   Portatil   : roda de onde estiver, sem copiar nada.
// A pasta de instalacao fica em Arquivos de Programas de proposito: o app roda como
// Administrador (e pode iniciar com o Windows elevado), entao o exe nao deve ficar numa
// pasta que qualquer programa sem privilegio consiga sobrescrever.
#pragma once
#include <string>

namespace Installer {
    std::wstring installDir();          // ...\Program Files\G15 Control BR
    std::wstring installedExe();        // ...\g15gui.exe dentro dela
    bool isInstalled();                 // existe uma instalacao?
    bool isRunningInstalled();          // este processo E a copia instalada?

    enum class Choice { Install, Portable, Quit };
    // Aviso de abertura. update = ja existe instalacao (oferece atualizar).
    Choice ask(bool update, bool& dontAskAgain);

    bool install(std::wstring& err);    // copia arquivos + atalho + registro
    // Fluxo completo de "--uninstall": confirma, fecha o app, remove tudo. Retorna codigo de saida.
    // quiet = sem perguntas nem avisos (usado por scripts e pelo QuietUninstallString).
    int  runUninstall(bool quiet = false);
    bool launchInstalled(const wchar_t* args);
    bool relaunchSelf(const wchar_t* args);   // reabre este mesmo exe com argumentos
}
