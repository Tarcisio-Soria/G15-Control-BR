// G15 Control BR - AutoStart.cpp
#include "AutoStart.h"
#define NOMINMAX
#define SECURITY_WIN32
#include <windows.h>
#include <security.h>
#include <cstdio>

#pragma comment(lib, "secur32.lib")

namespace {
    const wchar_t* TASK = L"G15ControlBR";

    // Roda um comando sem janela e devolve o codigo de saida (-1 se nao iniciou).
    int runHidden(std::wstring cmd) {
        STARTUPINFOW si{}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            return -1;
        WaitForSingleObject(pi.hProcess, 15000);
        DWORD code = (DWORD)-1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return (int)code;
    }

    std::wstring exePath() {
        wchar_t b[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, b, MAX_PATH);
        return b;
    }
    std::wstring envVar(const wchar_t* name) {
        wchar_t b[MAX_PATH] = L"";
        DWORD n = GetEnvironmentVariableW(name, b, MAX_PATH);
        return (n > 0 && n < MAX_PATH) ? std::wstring(b) : std::wstring();
    }
    bool isNetworkPath(const std::wstring& p) {
        if (p.size() >= 2 && p[0] == L'\\' && p[1] == L'\\') return true;        // UNC
        if (p.size() >= 3 && p[1] == L':') {                                         // letra mapeada
            wchar_t root[4] = { p[0], L':', L'\\', 0 };
            return GetDriveTypeW(root) == DRIVE_REMOTE;
        }
        return false;
    }
    std::wstring xmlEscape(const std::wstring& s) {
        std::wstring o;
        for (wchar_t c : s) {
            switch (c) {
                case L'&': o += L"&amp;"; break;
                case L'<': o += L"&lt;";  break;
                case L'>': o += L"&gt;";  break;
                case L'"': o += L"&quot;"; break;
                default:   o += c;
            }
        }
        return o;
    }
    std::wstring currentUser() {            // DOMINIO\usuario
        wchar_t b[512] = L""; ULONG n = 512;
        return GetUserNameExW(NameSamCompatible, b, &n) ? std::wstring(b) : std::wstring();
    }
}

bool AutoStart::isEnabled() {
    return runHidden(std::wstring(L"schtasks.exe /Query /TN \"") + TASK + L"\"") == 0;
}

bool AutoStart::disable() {
    return runHidden(std::wstring(L"schtasks.exe /Delete /TN \"") + TASK + L"\" /F") == 0;
}

bool AutoStart::canUseThisExe() { return !isNetworkPath(exePath()); }

bool AutoStart::enable(std::wstring& info) { return enable(exePath(), info); }

bool AutoStart::enable(const std::wstring& target, std::wstring& info) {
    // No logon um caminho de rede (ex.: \\servidor\pasta\...) pode ainda nao existir.
    // Nada e copiado em silencio: o usuario instala o programa (botao Instalar) e ativa de la.
    if (isNetworkPath(target)) {
        info = L"este exe esta em um caminho de rede; instale o programa para iniciar com o Windows";
        return false;
    }

    std::wstring user = xmlEscape(currentUser());
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
        L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
        L"  <RegistrationInfo><Description>G15 Control BR - inicia na bandeja ao entrar no Windows</Description></RegistrationInfo>\r\n"
        L"  <Triggers><LogonTrigger><Enabled>true</Enabled>";
    if (!user.empty()) xml += L"<UserId>" + user + L"</UserId>";
    xml +=
        L"<Delay>PT10S</Delay></LogonTrigger></Triggers>\r\n"   // espera a bandeja e o WMI subirem
        L"  <Principals><Principal id=\"Author\">";
    if (!user.empty()) xml += L"<UserId>" + user + L"</UserId>";
    xml +=
        L"<LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel></Principal></Principals>\r\n"
        L"  <Settings>\r\n"
        L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
        L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"   // notebook: roda tambem na bateria
        L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
        L"    <StartWhenAvailable>true</StartWhenAvailable>\r\n"
        L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"                   // sem limite (padrao seria matar em 3 dias)
        L"    <Enabled>true</Enabled>\r\n"
        L"  </Settings>\r\n"
        L"  <Actions Context=\"Author\"><Exec><Command>\"" + xmlEscape(target) + L"\"</Command>"
        L"<Arguments>--tray</Arguments></Exec></Actions>\r\n"
        L"</Task>\r\n";

    std::wstring tmp = envVar(L"TEMP");
    if (tmp.empty()) { info = L"variavel TEMP ausente"; return false; }
    tmp += L"\\g15controlbr_task.xml";
    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f) { info = L"nao consegui gravar o XML temporario"; return false; }
    const unsigned char bom[2] = { 0xFF, 0xFE };                // UTF-16 LE
    fwrite(bom, 1, 2, f);
    fwrite(xml.data(), sizeof(wchar_t), xml.size(), f);
    fclose(f);

    int rc = runHidden(std::wstring(L"schtasks.exe /Create /TN \"") + TASK + L"\" /XML \"" + tmp + L"\" /F");
    DeleteFileW(tmp.c_str());
    if (rc != 0) {
        wchar_t b[96]; swprintf(b, 96, L"schtasks falhou (codigo %d) - rode como Administrador", rc);
        info = b;
        return false;
    }
    info = L"ativado";
    return true;
}
