# Notas técnicas — Dell G15 5515 (AMD)

Referência para o nosso controlador (inspirado no alienfx-tools). Extraído da análise
dos projetos Linux em `~/git/G15` (Dell-G-Series-Controller, Dell_G15_Fan_Cli,
dell-g15-fan-control).

## Identificação do modelo

- O 5515 é **AMD** → namespace ACPI `\_SB.AMW3.WMAX` (Intel usa `\_SB.AMWW.WMAX`).
- **ID do modelo: `0xc80`** (lido pelo método `0x1a` / args `0x02, 0x02`).

## Tabela de métodos WMAX

Formato: `\_SB.AMW3.WMAX 0 <método> {<arg0>, <arg1>, <arg2>, 0x00}`

| Função                 | Método | Args                | Observação                 |
|------------------------|--------|---------------------|----------------------------|
| Ler modelo             | 0x1a   | 0x02, 0x02          | retorna 0xc80 no 5515      |
| Ler modo de energia    | 0x14   | 0x0b                |                            |
| Definir modo           | 0x15   | 0x01, <modo>        |                            |
| Toggle G-Mode          | 0x25   | 0x01, <0/1>         |                            |
| Ler G-Mode             | 0x25   | 0x02                | 0xab = ligado              |
| Boost ventoinha 1      | 0x15   | 0x02, 0x32, <0-255> | fan1 = id 0x32             |
| Ler RPM ventoinha 1    | 0x14   | 0x05, 0x32          |                            |
| Ler boost ventoinha 1  | 0x14   | 0x0c, 0x32          |                            |
| Boost ventoinha 2      | 0x15   | 0x02, 0x33, <0-255> | fan2 = id 0x33             |
| Ler RPM ventoinha 2    | 0x14   | 0x05, 0x33          |                            |
| Ler boost ventoinha 2  | 0x14   | 0x0c, 0x33          |                            |

Valores de modo: Balanced `0xa0`, Performance `0xa1`, Quiet `0xa3`, **G-Mode `0xab`**.

## Macete do 5515 (importante)

No 5515 os modos de energia nomeados (Balanced/Performance/Quiet/FullSpeed/BatterySaver)
NÃO funcionam de forma confiável — o `Dell-G-Series-Controller` remove todos eles ao
detectar o 5515. O que funciona de verdade:

1. **Toggle do G-Mode** (0x25)
2. **Boost direto por ventoinha** (0x32 / 0x33; 0 = automático/firmware, 255 = máximo)

## Teclado RGB (USB HID, não é ACPI)

- Dispositivo: **VID 0x187C, PID 0x0550 ou 0x0551**.
- Protocolo "ELC": relatórios HID de 33 bytes; comando `SET_COLOR 0x27`; animações.
- **Zonas no 5515: IDs 8, 9, 10, 11** (esquerda -> direita), confirmado no Windows. Os IDs 0-3
  dos projetos Linux sao ignorados em silencio por este controlador (firmware 1.1.7).
- Portável para Windows via HIDAPI/libusb.

## Duas gerações de interface da ventoinha (Linux)

- **Antiga** (`acpi_call`): escreve a string crua em `/proc/acpi/call`. Precisa de módulo
  fora da árvore do kernel.
- **Nova** (`alienware_wmi`): driver do kernel expõe `hwmon` com `fan1_input`, `fan2_input`
  (RPM), `fan1_boost`, `fan2_boost` (0-255). Mais limpa.

## Como isso vale no Windows

Os métodos acima são da BIOS/ACPI-WMI, independentes do SO. No Windows, o alienfx-tools
já faz o "encanamento" de chamar a WMI (ACPI\\PNP0C14). Os projetos Linux fornecem o mapa
legível desses métodos e os macetes do 5515. O teclado (ELC/HID) é portável direto.

---

# Análise das versões Windows (alienfx-tools e tcc-g15)

As duas chamam os MESMOS métodos da BIOS Alienware que mapeamos no Linux — mudam só o
caminho até lá.

## alienfx-tools (C++ / Visual Studio) — suíte completa

Monorepo. Módulos principais:

| Módulo | Função |
|--------|--------|
| AlienFX-SDK | iluminação (USB HID) |
| alienfan-tools/alienfan-SDK (v1 e v2) | ventoinha + energia |
| alienfan-tools/alienfan-low | driver de baixo nível p/ avaliar ACPI direto |
| alienfan-curve | curvas de ventoinha (velocidade x temperatura) |
| alienfan-gui / -cli / -mon | apps de usuário |
| LightFX | emulador da API LightFX da Dell (jogos) |

- **Plumbing:** driver de kernel próprio (IOCTL_GPD_OPEN_ACPI / EVAL_ACPI_WITH_PARAMETER);
  também lê porta/PCI/MSR/memória. Exige instalar driver.
- **Recursos (alienfan-controls.h):** modo de energia, RPM, %, boost (get/set), temp,
  G-Mode (get/set), ID do sistema, sensores por ventoinha, RPM máx., TCC (limite térmico),
  offset de voltagem (undervolt), XMP/OC de memória.

## tcc-g15 (Python / PySide) — limpo e simples

- `src/Backend/`: AWCCThermal.py, AWCCWmiWrapper.py, DetectHardware.py
- `src/GUI/`: medidores, bandeja, atalho de tecla
- **Plumbing:** WMI (`root\WMI` → classe `AWCCWmiMethodFunction`), SEM driver. Métodos:
  `Thermal_Information`, `Thermal_Control`, `GetFanSensors`, com args empacotados em bits.
- **Recursos:** ler temp, ler RPM, definir % da ventoinha (SetAddonSpeedPercent), aplicar
  modo (Balanced 0x97/0xA0, G-Mode 0xAB, Custom 0). Fan IDs 0x31-0x63, sensores 0x01-0x30.
- Suporta o 5515 explicitamente. (as pastas -the0cp e -alexiii são idênticas)

## Mapa WMI (Windows) -> WMAX (Linux/BIOS)

| Nome WMI            | Código WMAX |
|---------------------|-------------|
| Thermal_Information | 0x14        |
| Thermal_Control     | 0x15        |
| GameShiftStatus     | 0x25        |
| GetFanSensors       | 0x13        |

Empacotamento de argumentos (tcc-g15):
- GetSensorTemperature(sensorId): arg = (sensorId << 8) | 4  -> Thermal_Information
- GetFanRPM(fanId):               arg = (fanId << 8) | 5     -> Thermal_Information
- GetFanRPMPercent(fanId):        arg = (fanId << 8) | 6     -> Thermal_Information
- SetAddonSpeedPercent(fanId,s):  arg = (s << 16)|(fanId<<8)|2 -> Thermal_Control
- ApplyThermalMode(modo):         arg = (modo << 8) | 1      -> Thermal_Control

## Decisão: nosso projeto em C++ (base alienfx-tools)

- Núcleo de ventoinha/energia: avaliar começar via WMI (mais seguro) e evoluir p/ via driver.
- Teclado RGB: AlienFX-SDK (HID) ou protocolo ELC (VID 187c:0550/0551).
- Extras (undervolt/OC): só via driver, fase posterior.
- Build no Windows (MSVC/Visual Studio), não no WSL.

---

# Medições no 5515 real (Windows, G15 Control BR)

## RPM das ventoinhas

| Situação                         | CPU (0x32) | GPU (0x33) |
|----------------------------------|------------|------------|
| "RPM máximo" informado pela BIOS | 4200       | 3900       |
| G-Mode (tecla G)                 | ~5000      | ~4700      |
| Boost manual em 100% (255)       | ~5700      | ~5400      |

- O RPM é lido ao vivo (Thermal_Information, sub 5) e acompanha a carga.
- O "máximo" da BIOS (sub 9) vale só para o controle automático no modo normal; não é teto.
- **G-Mode não é a velocidade máxima:** boost manual em 255 gira ~700 RPM a mais.

## Tecla G

- Chega ao Windows como **F17** (VK 0x80). `RegisterHotKey` funciona.
