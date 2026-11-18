// G15 Control BR - StorageTemp.cpp
#include "StorageTemp.h"
#include <windows.h>
#include <winioctl.h>
#include <cstdio>
#include <cstring>

// Le o log de saude NVMe (log page 0x02) e extrai a temperatura composta.
static std::optional<int> nvmeTemp(HANDLE h) {
    const DWORD LOG_LEN = 512;
    DWORD bufLen = FIELD_OFFSET(STORAGE_PROPERTY_QUERY, AdditionalParameters)
                 + sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA) + LOG_LEN;
    std::vector<BYTE> buf(bufLen, 0);

    auto query = reinterpret_cast<STORAGE_PROPERTY_QUERY*>(buf.data());
    query->PropertyId = StorageDeviceProtocolSpecificProperty;
    query->QueryType  = PropertyStandardQuery;

    auto proto = reinterpret_cast<STORAGE_PROTOCOL_SPECIFIC_DATA*>(query->AdditionalParameters);
    proto->ProtocolType              = ProtocolTypeNvme;
    proto->DataType                  = NVMeDataTypeLogPage;
    proto->ProtocolDataRequestValue  = 0x02; // SMART / Health Information
    proto->ProtocolDataRequestSubValue = 0;
    proto->ProtocolDataOffset        = sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA);
    proto->ProtocolDataLength        = LOG_LEN;

    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                         buf.data(), bufLen, buf.data(), bufLen, &ret, nullptr))
        return std::nullopt;

    auto desc = reinterpret_cast<STORAGE_PROTOCOL_DATA_DESCRIPTOR*>(buf.data());
    auto pd   = &desc->ProtocolSpecificData;
    if (pd->ProtocolDataOffset < sizeof(STORAGE_PROTOCOL_SPECIFIC_DATA)) return std::nullopt;

    BYTE* log = reinterpret_cast<BYTE*>(pd) + pd->ProtocolDataOffset;
    int kelvin = log[1] | (log[2] << 8); // bytes 1-2 = temperatura composta (Kelvin)
    if (kelvin <= 0) return std::nullopt;
    return kelvin - 273;
}

static std::string deviceModel(HANDLE h) {
    BYTE buf[1024] = {0};
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType  = PropertyStandardQuery;
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf, sizeof(buf), &ret, nullptr))
        return "";
    auto d = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buf);
    std::string s;
    if (d->ProductIdOffset && d->ProductIdOffset < sizeof(buf))
        s = reinterpret_cast<char*>(buf + d->ProductIdOffset);
    // trim de espacos nas pontas
    size_t a = s.find_first_not_of(' ');
    size_t b = s.find_last_not_of(' ');
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

std::vector<NvmeDrive> readNvmeTemps() {
    std::vector<NvmeDrive> out;
    for (int i = 0; i < 8; ++i) {
        wchar_t path[64];
        wsprintfW(path, L"\\\\.\\PhysicalDrive%d", i);
        HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        auto t = nvmeTemp(h);
        std::string model = deviceModel(h);
        CloseHandle(h);
        if (t) out.push_back({ i, model, t });
    }
    return out;
}
