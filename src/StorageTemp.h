// G15 Control BR - StorageTemp.h
// Le temperatura de SSDs NVMe direto do Windows (DeviceIoControl / SMART),
// sem driver e sem aplicativo externo.
#pragma once
#include <optional>
#include <string>
#include <vector>

struct NvmeDrive {
    int index;              // PhysicalDriveN
    std::string model;      // nome do modelo (se disponivel)
    std::optional<int> tempC;
};

std::vector<NvmeDrive> readNvmeTemps(); // varre PhysicalDrive0..7
