// G15 Control BR - Installer.cpp
#include "Installer.h"
#include "AutoStart.h"
#include "Version.h"
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cwchar>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")
// Controles visuais modernos (necessario para o TaskDialog)
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
    const wchar_t* APP_NAME   = L"G15 Control BR";
    const wchar_t* APP_VER    = G15_VERSION_W;
    const wchar_t* UNINST_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\G15ControlBR";
    const wchar_t* WND_CLASS  = L"G15ControlBR";
    const wchar_t* MUTEX_NAME = L"Local\\G15ControlBR_SingleInstance";

    std::wstring exePath() {
        wchar_t b[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, b, MAX_PATH);
        return b;
    }
    std::wstring dirOf(const std::wstring& p) {
        size_t s = p.find_last_of(L'\\');
        return s == std::wstring::npos ? std::wstring() : p.substr(0, s);
    }
    bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
    bool sameFile(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

    std::wstring knownFolder(int csidl) {
        wchar_t b[MAX_PATH] = L"";
        return SUCCEEDED(SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, b)) ? std::wstring(b) : std::wstring();
    }
    std::wstring shortcutPath() {   // Menu Iniciar de todos os usuarios
        std::wstring p = knownFolder(CSIDL_COMMON_PROGRAMS);
        return p.empty() ? p : p + L"\\" + APP_NAME + L".lnk";
    }

    bool createShortcut(const std::wstring& lnk, const std::wstring& target) {
        HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool ok = false;
        IShellLinkW* sl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl)) && sl) {
            sl->SetPath(target.c_str());
            sl->SetWorkingDirectory(dirOf(target).c_str());
            sl->SetDescription(L"Controle de ventoinhas, modo termico e teclado RGB do Dell G15");
            sl->SetIconLocation(target.c_str(), 0);
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && pf) {
                ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
                pf->Release();
            }
            sl->Release();
        }
        if (SUCCEEDED(hrInit)) CoUninitialize();
        return ok;
    }

    void regSetStr(HKEY k, const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    }
    void regSetDword(HKEY k, const wchar_t* name, DWORD v) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    }
    bool writeUninstallEntry(const std::wstring& exe) {
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINST_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        regSetStr(k, L"DisplayName", APP_NAME);
        regSetStr(k, L"DisplayVersion", APP_VER);
        regSetStr(k, L"Publisher", L"G15 Control BR (projeto nao oficial)");
        regSetStr(k, L"InstallLocation", dirOf(exe));
        regSetStr(k, L"DisplayIcon", exe);
        regSetStr(k, L"UninstallString", L"\"" + exe + L"\" --uninstall");
        regSetStr(k, L"QuietUninstallString", L"\"" + exe + L"\" --uninstall --quiet");
        regSetDword(k, L"NoModify", 1);
        regSetDword(k, L"NoRepair", 1);
        RegCloseKey(k);
        return true;
    }

    bool launch(const std::wstring& exe, const wchar_t* args) {
        HINSTANCE r = ShellExecuteW(nullptr, L"open", exe.c_str(), args, dirOf(exe).c_str(), SW_SHOWNORMAL);
        return (INT_PTR)r > 32;
    }

    // Pede para a instancia em execucao fechar e espera ela sair (ate ~8 s).
    bool closeRunningInstance() {
        HWND w = FindWindowW(WND_CLASS, nullptr);
        if (w) PostMessageW(w, WM_CLOSE, 0, 0);
        for (int i = 0; i < 80; ++i) {
            HANDLE m = OpenMutexW(SYNCHRONIZE, FALSE, MUTEX_NAME);
            if (!m) return true;
            CloseHandle(m);
            Sleep(100);
        }
        return false;
    }
}

std::wstring Installer::installDir() {
    std::wstring pf = knownFolder(CSIDL_PROGRAM_FILES);
    return pf.empty() ? pf : pf + L"\\" + APP_NAME;
}
std::wstring Installer::installedExe() {
    std::wstring d = installDir();
    return d.empty() ? d : d + L"\\g15gui.exe";
}
bool Installer::isInstalled() {
    std::wstring e = installedExe();
    return !e.empty() && exists(e);
}
bool Installer::isRunningInstalled() {
    std::wstring e = installedExe();
    return !e.empty() && sameFile(e, exePath());
}

Installer::Choice Installer::ask(bool update, bool& dontAskAgain) {
    dontAskAgain = false;
    std::wstring dir = installDir();
    std::wstring installTxt = update
        ? L"Atualizar a instalação\nSubstitui a versão instalada por esta e abre a instalada.\n" + dir
        : L"Instalar\nCopia o programa para a pasta abaixo, cria atalho no Menu Iniciar e permite iniciar com o Windows.\n" + dir;
    std::wstring portableTxt =
        L"Rodar em modo portátil\nNão copia nada: roda desta pasta mesmo.\n" + dirOf(exePath());

    TASKDIALOG_BUTTON btns[] = {
        { 100, installTxt.c_str() },
        { 101, portableTxt.c_str() },
    };
    TASKDIALOGCONFIG c = {};
    c.cbSize = sizeof(c);
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = APP_NAME;
    c.pszMainIcon = MAKEINTRESOURCEW(1);          // icone do app (recurso 1)
    c.hInstance = GetModuleHandleW(nullptr);
    c.pszMainInstruction = update ? L"Esta cópia não é a instalada"
                                  : L"Este programa não está instalado";
    c.pszContent = update
        ? L"Já existe uma instalação do G15 Control BR neste computador. O que deseja fazer com esta cópia?"
        : L"Você pode instalá-lo ou apenas rodá-lo daqui, em modo portátil.";
    c.cButtons = 2; c.pButtons = btns; c.nDefaultButton = 100;
    c.pszVerificationText = L"Não perguntar de novo (vale para o modo portátil)";
    c.pszFooter = L"As configurações ficam em %APPDATA%\\G15ControlBR nos dois casos.";

    int pressed = 0; BOOL verify = FALSE;
    if (FAILED(TaskDialogIndirect(&c, &pressed, nullptr, &verify))) {
        // Sem TaskDialog: pergunta simples.
        int r = MessageBoxW(nullptr,
            (std::wstring(L"Este programa não está instalado.\n\nSim = instalar em ") + dir +
             L"\nNão = rodar em modo portátil").c_str(), APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
        return r == IDYES ? Choice::Install : r == IDNO ? Choice::Portable : Choice::Quit;
    }
    dontAskAgain = verify != FALSE;
    if (pressed == 100) return Choice::Install;
    if (pressed == 101) return Choice::Portable;
    return Choice::Quit;
}

bool Installer::install(std::wstring& err) {
    std::wstring dir = installDir(), self = exePath(), dst = installedExe();
    if (dir.empty()) { err = L"pasta Arquivos de Programas não encontrada"; return false; }
    if (sameFile(self, dst)) return true;   // ja e a instalada

    CreateDirectoryW(dir.c_str(), nullptr);
    if (!CopyFileW(self.c_str(), dst.c_str(), FALSE)) {
        wchar_t b[160];
        swprintf(b, 160, L"não consegui copiar o programa (erro %lu). Feche o G15 Control BR instalado e tente de novo.", GetLastError());
        err = b; return false;
    }
    // CLI, se estiver ao lado deste exe
    std::wstring srcDir = dirOf(self);
    std::wstring cli = srcDir + L"\\g15ctl.exe";
    if (exists(cli)) CopyFileW(cli.c_str(), (dir + L"\\g15ctl.exe").c_str(), FALSE);
    // Curva de ventoinha: so copia se a instalacao ainda nao tiver a sua (nao sobrescreve ajuste do usuario)
    std::wstring ini = srcDir + L"\\g15ctl.ini";
    if (exists(ini)) CopyFileW(ini.c_str(), (dir + L"\\g15ctl.ini").c_str(), TRUE);

    std::wstring lnk = shortcutPath();
    if (!lnk.empty()) createShortcut(lnk, dst);     // falha no atalho nao invalida a instalacao
    if (!writeUninstallEntry(dst)) { err = L"instalado, mas não consegui registrar em Aplicativos instalados"; }

    // Se "iniciar com o Windows" ja estava ativo, passa a apontar para a copia instalada.
    if (AutoStart::isEnabled()) { std::wstring info; AutoStart::enable(dst, info); }
    return true;
}

bool Installer::launchInstalled(const wchar_t* args) { return launch(installedExe(), args); }
bool Installer::relaunchSelf(const wchar_t* args)   { return launch(exePath(), args); }

int Installer::runUninstall(bool quiet) {
    std::wstring dir = installDir(), exe = installedExe();
    if (!isInstalled()) {
        if (!quiet) MessageBoxW(nullptr, L"O G15 Control BR não está instalado.", APP_NAME, MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    std::wstring q = L"Desinstalar o G15 Control BR?\n\nSerão removidos:\n  • a pasta " + dir +
                     L"\n  • o atalho do Menu Iniciar\n  • o início com o Windows\n\n"
                     L"As ventoinhas voltam ao automático. Suas configurações em %APPDATA%\\G15ControlBR são mantidas.";
    if (!quiet && MessageBoxW(nullptr, q.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;

    if (!closeRunningInstance()) {
        if (!quiet) MessageBoxW(nullptr, L"O G15 Control BR ainda está aberto. Feche-o (Sair no ícone da bandeja) e tente de novo.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
        return 1;
    }
    AutoStart::disable();
    std::wstring lnk = shortcutPath();
    if (!lnk.empty()) DeleteFileW(lnk.c_str());
    RegDeleteKeyW(HKEY_LOCAL_MACHINE, UNINST_KEY);

    // Apaga os arquivos. Este exe pode ser o proprio instalado (em uso), entao a pasta e
    // removida por um cmd que espera este processo terminar. Confere o caminho antes:
    // so apaga se for exatamente a pasta do app e contiver o exe.
    bool safe = dir.size() > wcslen(APP_NAME) + 4 &&
                _wcsicmp(dir.c_str() + dir.size() - wcslen(APP_NAME), APP_NAME) == 0 && exists(exe);
    if (safe) {
        std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & rmdir /s /q \"" + dir + L"\"";
        STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        wchar_t tmp[MAX_PATH] = L""; GetTempPathW(MAX_PATH, tmp);
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, tmp, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    if (!quiet) MessageBoxW(nullptr, L"G15 Control BR desinstalado.", APP_NAME, MB_OK | MB_ICONINFORMATION);
    return 0;
}
