/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#include "utils/host_search.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <borealis/core/logger.hpp>
#include <cstring>
#include <fstream>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#endif

#ifdef __PSV__
#include <psp2/kernel/clib.h>
#define VITALOG sceClibPrintf
#else
#define VITALOG(...) ((void)0)
#endif

#ifdef __PSV__
// Vita implementation: loadHostsVita() with struct HostInfoVita (char[])
std::vector<HostInfoVita> loadHostsVita()
{
    std::vector<HostInfoVita> hosts;
    VITALOG("[loadHostsVita] Dirección de hosts vector: %p\n", (void*)&hosts);
    std::string baseDir = "ux0:data/moonlight/devices/";
    DIR* dir            = opendir(baseDir.c_str());
    if (!dir)
        return hosts;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        char folder[64] = { 0 };
        strncpy(folder, entry->d_name, sizeof(folder) - 1);
        VITALOG("[loadHostsVita] Leyendo carpeta: '%s', longitud: %d\n", folder, (int)strlen(folder));
        if (strlen(folder) == 0 || strcmp(folder, ".") == 0 || strcmp(folder, "..") == 0 || strlen(folder) > 60)
        {
            VITALOG("[loadHostsVita] Carpeta ignorada: '%s'\n", folder);
            continue;
        }
        std::string path = baseDir + folder;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
        {
            VITALOG("[loadHostsVita] '%s' no es un directorio válido\n", path.c_str());
            continue;
        }
        std::string iniPath = path + "/device.ini";
        std::ifstream ini(iniPath);
        if (!ini.is_open())
        {
            VITALOG("[loadHostsVita] No se pudo abrir INI: %s\n", iniPath.c_str());
            continue;
        }
        HostInfoVita host = { 0 };
        strncpy(host.name, folder, sizeof(host.name) - 1);
        std::string line;
        while (std::getline(ini, line))
        {
            VITALOG("[loadHostsVita] Línea INI: '%s'\n", line.c_str());
            // Allow both 'internal = ...' and 'internal=...'
            std::string key, value;
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos)
            {
                key   = line.substr(0, eqPos);
                value = line.substr(eqPos + 1);
                // Remove spaces around
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                if (key == "internal")
                {
                    strncpy(host.ip, value.c_str(), sizeof(host.ip) - 1);
                }
                else if (key == "prefer_external")
                {
                    host.preferExternal = (value == "true");
                }
            }
        }
        VITALOG("[loadHostsVita] Host cargado: name='%s', ip='%s', preferExternal=%d\n", host.name, host.ip, host.preferExternal);
        VITALOG("[loadHostsVita] push_back: dirección hosts=%p, size=%lu, último host='%s'\n", (void*)&hosts, (unsigned long)hosts.size(), host.name);
        hosts.push_back(host);
        VITALOG("[loadHostsVita] tras push_back: dirección hosts=%p, size=%lu\n", (void*)&hosts, (unsigned long)hosts.size());
    }
    unsigned long safeSize = (unsigned long)hosts.size();
    if (safeSize > 1000)
    {
        VITALOG("[loadHostsVita] ERROR: Tamaño de hosts.size() corrupto: %lu\n", safeSize);
        return std::vector<HostInfoVita>();
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "[loadHostsVita] Total hosts cargados: %lu", safeSize);
    VITALOG("%s\n", buf);
    closedir(dir);
    return hosts;
}
#else
// Cross-platform implementation (non-Vita): loadHosts() with std::string
std::vector<SearchHostInfo> loadHosts()
{
    std::vector<SearchHostInfo> hosts;
    std::string baseDir = "devices/";
    DIR* dir            = opendir(baseDir.c_str());
    if (!dir)
        return hosts;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string folder = entry->d_name;
        if (folder.empty() || folder == "." || folder == ".." || folder.length() > 60)
            continue;
        std::string path = baseDir + folder;
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        std::string iniPath = path + "/device.ini";
        std::ifstream ini(iniPath);
        if (!ini.is_open())
            continue;
        SearchHostInfo host;
        host.name = folder;
        std::string line;
        while (std::getline(ini, line))
        {
            if (line.find("internal = ") == 0)
            {
                size_t prefixLen = strlen("internal = ");
                host.ip          = line.substr(prefixLen);
            }
            else if (line.find("prefer_external = ") == 0)
            {
                size_t prefixLen    = strlen("prefer_external = ");
                std::string val     = line.substr(prefixLen);
                host.preferExternal = (val == "true");
            }
        }
        hosts.push_back(host);
    }
    return hosts;
}
#endif
