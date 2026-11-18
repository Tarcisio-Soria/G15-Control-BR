# G15 Control BR

Controlador leve, em C++, para notebooks Dell G-series (desenvolvido e testado no
**Dell G15 5515**, AMD). Alternativa ao AWCC (Alienware Command Center), sem serviços
pesados: ventoinhas, modos térmicos, teclado RGB e monitoramento em um único `.exe`.

> **Projeto não oficial, sem relação com a Dell ou a Alienware.** Mexe em controle térmico
> via BIOS: use com atenção à temperatura, sobretudo nos primeiros testes.

## Download (sem compilar)

Os executáveis prontos estão na pasta [`dist/`](dist/):

| Arquivo | O que é |
|---|---|
| [`g15gui.exe`](dist/g15gui.exe) | Interface gráfica (a que você quer usar). Instala-se sozinha se você permitir. |
| [`g15ctl.exe`](dist/g15ctl.exe) | Linha de comando (opcional). |
| [`SHA256SUMS.txt`](dist/SHA256SUMS.txt) | Somas de verificação dos dois arquivos. |

Para conferir o download no PowerShell: `Get-FileHash g15gui.exe` e compare com o `SHA256SUMS.txt`.

**Avisos sobre o executável**

- Ele **não é assinado digitalmente**, então o Windows SmartScreen ou o antivírus podem alertar.
  O código-fonte completo está neste repositório e você pode compilar o seu (veja "Build").
- Ele **pede Administrador**: ler e controlar as ventoinhas pela BIOS exige isso.
- Ao abrir uma cópia não instalada, o app pergunta se deve instalar em
  `%ProgramFiles%\G15 Control BR` ou rodar em modo portátil. Nada é copiado sem você escolher.

## Hardware-alvo

- Testado: **Dell G15 5515** (AMD, ID de modelo `0xc80`, namespace `\_SB.AMW3.WMAX`).
- Em outros G15 (5511/5520/5525/5530/5535/5590) ou Alienware pode funcionar com ajustes,
  mas **só o 5515 foi validado**. Em especial, os IDs das zonas do teclado mudam por modelo.

## O que ele faz

**Interface (`g15gui.exe`, Dear ImGui + DX11)** com gráficos de histórico (atualização a cada 2 s)
e controles em abas:

- **Ventoinhas:** RPM ao vivo, boost por ventoinha (0–255, 0 = automático do firmware),
  **controle automático pela curva** (ligado por padrão), ventoinha da GPU sempre girando
  com **base ajustável** (0–40%).
- **Modo Dell:** Balanceado, G-Mode ou Custom.
- **Proteção térmica:** se CPU ou GPU passar do limite (padrão 95 °C / 85 °C) por 8 s, põe as
  ventoinhas em 100% de boost e devolve o que havia 60 s depois de esfriar.
- **Windows:** modo de energia (Eficiência / Equilibrado / Desempenho).
- **SSDs:** temperatura NVMe via SMART.
- **Teclado:** cor fixa, por zona (4 zonas), respiração e ciclo de cores, brilho, e uma
  prévia do teclado. Pode gravar como padrão de inicialização.
- **Aplicativo:** iniciar com o Windows (tarefa agendada elevada, `--tray`), instalar/desinstalar.
- **Sobre:** versão, autor e links.

Outros recursos: minimizar para a bandeja; **tecla G** do notebook (F17) alterna o G-Mode;
ao sair, as ventoinhas e o modo térmico voltam ao que estavam; as configurações são gravadas
em `%APPDATA%\G15ControlBR\g15gui.ini` pouco depois de cada alteração e ao fechar; só uma
instância roda por vez; um **vigia** reenvia o boost se o firmware passar a ignorá-lo.

**Linha de comando (`g15ctl.exe`):** `status`, `monitor`, `probe`, `rpmtest`, `curve show|run`,
`mode balanced|gmode|custom`, `boost <id-hex>|all <0-255>` e
`kbd info|color|zones|breathe|cycle|bright|off`.

## Como as ventoinhas funcionam neste modelo

Medido no 5515 (detalhes em [`docs/NOTAS-5515.md`](docs/NOTAS-5515.md)):

- O boost é um acréscimo sobre o que o firmware decidiria; o RPM não é definido diretamente.
- Cerca de **100 RPM por 1%** de boost, e o RPM **satura em ~5800/5500** a partir de ~58%.
- O firmware **para a ventoinha da GPU** (0 RPM) quando a placa está fria e o boost é 0;
  por isso existe a opção "sempre girando".
- As ventoinhas mudam ~100 RPM por segundo.

## Build

- Toolchain: MSVC (Build Tools for Visual Studio 2022) + Windows SDK 10.
- Compila no Windows (não no WSL): `powershell -ExecutionPolicy Bypass -File build.ps1`.
- Clone com o submódulo do Dear ImGui: `git clone --recurse-submodules <url>`
  (ou, num clone existente, `git submodule update --init`). A CLI não tem dependências;
  sem o ImGui o script compila só a CLI e avisa.
- Para atualizar os executáveis da pasta `dist/` depois de compilar:
  `powershell -ExecutionPolicy Bypass -File tools/make-dist.ps1`.

## Configuração da curva

A curva (pontos temperatura → boost, intervalo e histerese) usa valores padrão. Para
personalizar, crie `g15ctl.ini` ao lado do `.exe`; `g15ctl curve show` mostra o formato.

## Licença e créditos

- Licença: **GPL-3.0** (veja [`LICENSE`](LICENSE)).
- [Dear ImGui](https://github.com/ocornut/imgui) (MIT), em `third_party/imgui`.
- O funcionamento do protocolo da BIOS e do teclado foi estudado em projetos abertos:
  [alienfx-tools](https://github.com/t-troll/alienfx-tools) (MIT) e outros controladores
  livres para o G15. Veja `docs/NOTAS-5515.md`.

Autor: Tarcisio Soria — <https://www.linkedin.com/in/kknd/>
