# G15 Control BR - build.ps1
# Compila com MSVC (Build Tools for VS 2022). Rode no Windows (PowerShell).
#   bin\g15ctl.exe  - CLI (sempre)
#   bin\g15gui.exe  - GUI (so se o Dear ImGui estiver disponivel)
# ImGui: procurado em $env:IMGUI_DIR ou em third_party\imgui. Para obter:
#   git clone --depth 1 https://github.com/ocornut/imgui third_party/imgui
$ErrorActionPreference = 'Stop'
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat nao encontrado. Instale o Build Tools for VS 2022 com a carga C++." }

$src = Join-Path $PSScriptRoot 'src'
$bin = Join-Path $PSScriptRoot 'bin'
$obj = Join-Path $PSScriptRoot 'obj'
New-Item -ItemType Directory -Force -Path $bin, $obj | Out-Null

# pushd (e nao cd) para funcionar tambem em caminho UNC, ex.: \\servidor\pasta\...
function Invoke-Cl([string]$clArgs) {
    cmd /c "`"$vcvars`" >nul && pushd `"$PSScriptRoot`" && cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Fo:obj\ $clArgs"
    if ($LASTEXITCODE -ne 0) { throw "Falha na compilacao (cl.exe retornou $LASTEXITCODE)" }
}

$core = 'src\AwccWmi.cpp src\GpuNvml.cpp src\StorageTemp.cpp src\FanCurve.cpp src\KbdElc.cpp'
$libs = 'wbemuuid.lib ole32.lib oleaut32.lib'

# Recursos (icone do app). res\g15.ico e gerado por tools\make-icon.ps1.
cmd /c "`"$vcvars`" >nul && pushd `"$PSScriptRoot`" && rc /nologo /fo obj\app.res src\app.rc"
if ($LASTEXITCODE -ne 0) { throw "Falha ao compilar recursos (rc.exe retornou $LASTEXITCODE)" }

Invoke-Cl "/Fe:bin\g15ctl.exe src\main.cpp $core obj\app.res $libs"
Write-Host "OK: $bin\g15ctl.exe"

$imgui = if ($env:IMGUI_DIR) { $env:IMGUI_DIR } else { Join-Path $PSScriptRoot 'third_party\imgui' }
if (Test-Path (Join-Path $imgui 'imgui.h')) {
    $ig = "`"$imgui\imgui.cpp`" `"$imgui\imgui_draw.cpp`" `"$imgui\imgui_tables.cpp`" `"$imgui\imgui_widgets.cpp`" " +
          "`"$imgui\backends\imgui_impl_win32.cpp`" `"$imgui\backends\imgui_impl_dx11.cpp`""
    Invoke-Cl "/I`"$imgui`" /Fe:bin\g15gui.exe src\main_gui.cpp src\WinPower.cpp src\AutoStart.cpp src\Installer.cpp $core $ig obj\app.res $libs d3d11.lib dxgi.lib user32.lib shell32.lib gdi32.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED `"/MANIFESTUAC:level='requireAdministrator' uiAccess='false'`""
    Write-Host "OK: $bin\g15gui.exe"
} else {
    Write-Warning "GUI nao compilada: Dear ImGui nao encontrado em '$imgui' (veja o topo deste script)."
}
