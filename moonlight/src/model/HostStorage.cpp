/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "model/HostStorage.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

#include "ConfigManager.hpp"

namespace fs = std::filesystem;

namespace
{

std::recursive_mutex hostStorageMutex;

std::string trim(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseDecimal(const std::string& value, int maximum, int& result)
{
    if (value.empty())
        return false;

    int parsed = 0;
    for (char character : value)
    {
        if (character < '0' || character > '9')
            return false;
        const int digit = character - '0';
        if (parsed > (maximum - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    result = parsed;
    return true;
}

bool isIpv4Address(const std::string& address)
{
    size_t start = 0;
    for (int octet = 0; octet < 4; ++octet)
    {
        const size_t separator = address.find('.', start);
        const bool lastOctet   = octet == 3;
        if ((separator == std::string::npos) != lastOctet)
            return false;

        const size_t end = lastOctet ? address.size() : separator;
        int value        = 0;
        if (end == start || end - start > 3 || !parseDecimal(address.substr(start, end - start), 255, value))
            return false;
        start = end + 1;
    }
    return true;
}

int parsePort(const std::string& value, int fallback)
{
    try
    {
        int port = std::stoi(value);
        return port >= 1 && port <= 65535 ? port : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

}

bool parseHostEndpoint(const std::string& input, std::string& address, int& port)
{
    const std::string endpoint = trim(input);
    if (endpoint.empty())
        return false;

    std::string parsedAddress = endpoint;
    int parsedPort            = DEFAULT_GAMESTREAM_HTTP_PORT;
    const size_t separator    = endpoint.find(':');
    if (separator != std::string::npos)
    {
        if (separator == 0 || separator != endpoint.rfind(':'))
            return false;
        parsedAddress              = endpoint.substr(0, separator);
        const std::string portText = endpoint.substr(separator + 1);
        if (!parseDecimal(portText, 65535, parsedPort) || parsedPort == 0)
            return false;
    }

    if (!isIpv4Address(parsedAddress))
        return false;

    address = std::move(parsedAddress);
    port    = parsedPort;
    return true;
}

std::string formatHostEndpoint(const std::string& address, int port)
{
    if (port <= 0 || port == DEFAULT_GAMESTREAM_HTTP_PORT || address.find(':') != std::string::npos)
        return address;
    return address + ":" + std::to_string(port);
}

std::string makeSafeHostId(const std::string& raw)
{
    std::string out = raw;
    for (char& c : out)
    {
        const unsigned char value = static_cast<unsigned char>(c);
        if (value < 32 || value == 127 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    while (!out.empty() && (out.front() == ' ' || out.front() == '.'))
        out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    if (out.size() > 60)
        out = out.substr(0, 60);
    if (out.empty())
        out = "host";
    return out;
}

bool HostStorage::writeDeviceIni(const std::string& hostDir, const std::string& safeHostName, const char* address, int port, bool paired, const char* mac, int microphonePort)
{
    if (address == nullptr)
        return false;

    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    std::string deviceIniPath = hostDir + "/device.ini";
    FILE* f                   = fopen(deviceIniPath.c_str(), "w");
    if (!f)
        return false;

    fprintf(f, "[Device]\n");
    // The client identity lives in uniqueid.dat; device.ini only stores host data.
    fprintf(f, "name=%s\n", safeHostName.c_str());
    fprintf(f, "type=psvita\n");
    fprintf(f, "paired=%s\n", paired ? "true" : "false");
    fprintf(f, "internal=%s\n", address);
    fprintf(f, "external=\n");
    fprintf(f, "port=%d\n", port);
    fprintf(f, "prefer_external=false\n");
    fprintf(f, "microphone_port=%d\n", microphonePort);

    if (mac != nullptr)
    {
        std::string macs(mac);
        if (!macs.empty() && macs != "00:00:00:00:00:00")
        {
            size_t b = 0;
            while (b < macs.size() && isspace((unsigned char)macs[b]))
                ++b;
            size_t e = macs.size();
            while (e > b && isspace((unsigned char)macs[e - 1]))
                --e;
            std::string macTrim = macs.substr(b, e - b);
            if (!macTrim.empty())
                fprintf(f, "mac=%s\n", macTrim.c_str());
        }
    }

    return fclose(f) == 0;
}
bool HostStorage::savePairedHost(const std::string& name, const std::string& ip, int port, bool paired, const std::string& mac, int microphonePort)
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    ConfigManager config;
    config.load();
    const std::string safe = makeSafeHostId(name.empty() ? ip : name);
    if (!config.ensureKeyDirExists(safe))
        return false;
    return writeDeviceIni(config.getKeysDir() + "/" + safe, safe, ip.c_str(), port,
        paired, mac.empty() ? nullptr : mac.c_str(), microphonePort);
}

std::vector<HostInfo> HostStorage::loadHosts()
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    std::vector<HostInfo> hosts;
    ConfigManager config;
    config.load();
    std::string baseDir = config.getKeysDir();
    if (!config.ensureKeysDirExists())
    {
        return hosts;
    }
    DIR* dir = opendir(baseDir.c_str());
    if (!dir)
    {
        return hosts;
    }
    struct dirent* entry;
    auto sanitize_display = [](const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            if (c >= 32 && c != 127)
                out.push_back(c);
            else
                out.push_back('?');
        }
        size_t b = 0;
        while (b < out.size() && std::isspace((unsigned char)out[b]))
            ++b;
        size_t e = out.size();
        while (e > b && std::isspace((unsigned char)out[e - 1]))
            --e;
        return out.substr(b, e - b);
    };

    while ((entry = readdir(dir)) != nullptr)
    {
        std::string folder = entry->d_name;
        if (folder.empty() || folder == "." || folder == ".." || folder.length() > 60)
            continue;
        std::string path = baseDir + "/" + folder;
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        std::string iniPath = path + "/device.ini";
        std::ifstream ini(iniPath);
        if (!ini.is_open())
        {
            continue;
        }
        HostInfo host;
        host.name   = sanitize_display(folder);
        host.safeId = makeSafeHostId(folder);
        std::string line;
        while (std::getline(ini, line))
        {
            if (line.find("internal=") == 0)
            {
                std::string v = line.substr(strlen("internal="));
                std::string endpointAddress;
                int endpointPort = host.port;
                v                = sanitize_display(v);
                if (parseHostEndpoint(v, endpointAddress, endpointPort))
                {
                    host.ip   = std::move(endpointAddress);
                    host.port = endpointPort;
                }
                else
                {
                    host.ip = std::move(v);
                }
            }
            else if (line.find("mac=") == 0)
            {
                std::string v = line.substr(strlen("mac="));
                host.mac      = sanitize_display(v);
            }
            else if (line.find("port=") == 0)
            {
                host.port = parsePort(line.substr(strlen("port=")), host.port);
            }
            else if (line.find("paired=") == 0)
            {
                std::string val = line.substr(strlen("paired="));
                host.paired     = (val == "true");
            }
            else if (line.find("microphone_port=") == 0)
            {
                host.microphone_port = parsePort(
                    line.substr(strlen("microphone_port=")), host.microphone_port);
            }
        }
        if (host.ip.empty())
            continue;
        hosts.push_back(host);
    }
    closedir(dir);
    return hosts;
}

bool HostStorage::addHost(const HostInfo& host)
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    ConfigManager config;
    config.load();
    std::string baseDir = config.getKeysDir();
    if (!config.ensureKeysDirExists())
        return false;
    std::string safe          = makeSafeHostId(host.safeId.empty() ? (host.name.empty() ? host.ip : host.name) : host.safeId);
    std::string keyDirStr     = baseDir + "/" + safe;
    std::string deviceIniPath = keyDirStr + "/device.ini";
    if (fs::exists(deviceIniPath))
        return false;
    if (!config.ensureKeyDirExists(safe))
        return false;
    return HostStorage::writeDeviceIni(keyDirStr, safe, host.ip.c_str(), host.port, host.paired, host.mac.empty() ? nullptr : host.mac.c_str(), host.microphone_port);
}

std::optional<HostInfo> HostStorage::findHost(const std::string& name)
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    auto hosts = loadHosts();
    for (const auto& h : hosts)
    {
        if (h.name == name)
            return h;
    }
    return std::nullopt;
}

bool HostStorage::removeHost(const std::string& name)
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    ConfigManager config;
    config.load();
    std::string baseDir          = config.getKeysDir();
    auto host                    = findHost(name);
    const std::string identifier = host && !host->safeId.empty() ? host->safeId : name;
    std::string keyDirStr        = baseDir + "/" + makeSafeHostId(identifier);
    if (!fs::exists(keyDirStr))
        return false;
    try
    {
        std::cout << "[DEBUG][HostStorage] Eliminando archivos de certificados en: '" << keyDirStr << "'\n";
        std::error_code ec;
        fs::remove(keyDirStr + "/client.pem", ec);
        fs::remove(keyDirStr + "/key.pem", ec);
        fs::remove(keyDirStr + "/client.p12", ec);
        fs::remove(keyDirStr + "/uniqueid.dat", ec);
        fs::remove(keyDirStr + "/client.pem.bak", ec);
        fs::remove(keyDirStr + "/key.pem.bak", ec);
    }
    catch (...)
    {
        std::cout << "[DEBUG][HostStorage] Error eliminando certificados (ignorado)\n";
    }

    return fs::remove_all(keyDirStr) > 0;
}

bool HostStorage::updateHostEndpoint(const std::string& name, const std::string& newIp, int newPort)
{
    std::lock_guard<std::recursive_mutex> lock(hostStorageMutex);
    if (newIp.empty() || newPort < 1 || newPort > 65535)
        return false;

    auto hostOpt = HostStorage::findHost(name);
    if (!hostOpt.has_value())
        return false;

    HostInfo host = *hostOpt;
    host.ip       = newIp;
    host.port     = newPort;

    ConfigManager config;
    config.load();
    std::string baseDir = config.getKeysDir();
    std::string safe    = makeSafeHostId(!host.safeId.empty() ? host.safeId : name);
    std::string hostDir = baseDir + "/" + safe;

    if (!fs::exists(hostDir))
    {
        safe    = makeSafeHostId(name);
        hostDir = baseDir + "/" + safe;
        if (!fs::exists(hostDir))
            return false;
        host.safeId = safe;
    }

    return HostStorage::writeDeviceIni(hostDir, safe, host.ip.c_str(), host.port, host.paired, host.mac.empty() ? nullptr : host.mac.c_str(), host.microphone_port);
}
