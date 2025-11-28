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
#include <string>

#include "ConfigManager.hpp"

namespace fs = std::filesystem;

std::string makeSafeHostId(const std::string& raw)
{
    std::string out = raw;
    for (char& c : out)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    // Limitar longitud excesiva para evitar problemas en filesystem
    if (out.size() > 60)
        out = out.substr(0, 60);
    if (out.empty())
        out = "host"; // fallback
    return out;
}

// Genera el archivo device.ini en la carpeta del host
bool HostStorage::writeDeviceIni(const std::string& hostDir, const std::string& safeHostName, const char* address, int port, bool paired, const char* mac)
{
    std::string deviceIniPath = hostDir + "/device.ini";
    FILE* f                   = fopen(deviceIniPath.c_str(), "w");
    if (!f)
        return false;

    fprintf(f, "[Device]\n");
    // Nota: no escribimos el campo `uuid` en device.ini.
    // La referencia Moonlight-Switch no persiste este campo en device.ini; en
    // su lugar el cliente usa `uniqueid` internamente al construir las URLs
    // (por ejemplo: /serverinfo?uniqueid=...). Para mantener el mismo
    // comportamiento y evitar inconsistencias, omitimos la escritura del
    // uuid aquí. Esto evita que device.ini sea la fuente de verdad para el
    // uniqueid y reduce riesgo de desincronización.
    // name: nombre del dispositivo (hostName local, o algo identificable)
    fprintf(f, "name=%s\n", safeHostName.c_str());
    // tipo de dispositivo
    fprintf(f, "type=psvita\n");
    // paired
    fprintf(f, "paired=%s\n", paired ? "true" : "false");
    // internal: IP del host
    fprintf(f, "internal=%s\n", address);
    // external: dejar vacío por ahora
    fprintf(f, "external=\n");
    // port: puerto externo usado
    fprintf(f, "port=%d\n", port);
    // prefer_external: por defecto false
    fprintf(f, "prefer_external=false\n");
    // microphone_port: puerto del micrófono (default MoonMic)
    fprintf(f, "microphone_port=48100\n");

    // mac: escribir solo si se pasó un válor válido
    if (mac != nullptr)
    {
        std::string macs(mac);
        // Evitar escribir placeholder conocido o cadenas vacías
        if (!macs.empty() && macs != "00:00:00:00:00:00")
        {
            // Normalizar: eliminar espacios iniciales/finales
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

    fclose(f);
    return true;
}
// Guarda un host tras pairing exitoso
bool HostStorage::savePairedHost(const std::string& name, const std::string& ip, int port, bool paired, const std::string& mac)
{
    HostInfo host;
    host.name   = name;
    host.ip     = ip;
    host.port   = port;
    host.paired = paired;
    host.mac    = mac;
    return HostStorage::addHost(host);
}

// Carga todos los hosts escaneando las carpetas y leyendo device.ini
std::vector<HostInfo> HostStorage::loadHosts()
{
    std::vector<HostInfo> hosts;
    ConfigManager config;
    std::string baseDir = config.getKeysDir();
    // Asegurar que el directorio base existe antes de intentar abrirlo
    if (!config.ensureKeysDirExists())
    {
        std::cout << "[DEBUG][HostStorage] No se pudo asegurar baseDir: '" << baseDir << "'\n";
        return hosts;
    }
    std::cout << "[DEBUG][HostStorage] baseDir usado para hosts: '" << baseDir << "'\n";
    DIR* dir = opendir(baseDir.c_str());
    if (!dir)
    {
        std::cout << "[DEBUG][HostStorage] No se pudo abrir el directorio: '" << baseDir << "'\n";
        return hosts;
    }
    struct dirent* entry;
    // Helper para sanitizar cadenas que provienen del filesystem (nombres de carpeta, valores)
    auto sanitize_display = [](const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            // Permitimos caracteres imprimibles ASCII y, para evitar problemas con
            // bytes de control o secuencias corruptas, reemplazamos otros por '?'
            if (c >= 32 && c != 127)
                out.push_back(c);
            else
                out.push_back('?');
        }
        // Trim básico
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
        std::cout << "[DEBUG][HostStorage] Carpeta encontrada: '" << folder << "'\n";
        if (folder.empty() || folder == "." || folder == ".." || folder.length() > 60)
            continue;
        std::string path = baseDir + "/" + folder;
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 || !(st.st_mode & S_IFDIR))
            continue;
        std::string iniPath = path + "/device.ini";
        std::ifstream ini(iniPath);
        if (!ini.is_open())
        {
            std::cout << "[DEBUG][HostStorage] No se pudo abrir: '" << iniPath << "'\n";
            continue;
        }
        HostInfo host;
        // Mostrar un nombre seguro al usuario evitando caracteres de control
        host.name   = sanitize_display(folder); // Por retrocompatibilidad: la carpeta era el nombre
        host.safeId = makeSafeHostId(folder);
        std::cout << "[DEBUG][HostStorage] Leyendo device.ini para host: '" << folder << "'\n";
        std::string line;
        while (std::getline(ini, line))
        {
            std::cout << "[DEBUG][HostStorage] device.ini: " << line << "\n";
            if (line.find("internal=") == 0)
            {
                std::string v = line.substr(strlen("internal="));
                host.ip       = sanitize_display(v);
            }
            else if (line.find("mac=") == 0)
            {
                std::string v = line.substr(strlen("mac="));
                host.mac      = sanitize_display(v);
            }
            else if (line.find("port=") == 0)
            {
                try
                {
                    host.port = std::stoi(line.substr(strlen("port=")));
                }
                catch (...)
                {
                    host.port = 0;
                }
            }
            else if (line.find("paired=") == 0)
            {
                std::string val = line.substr(strlen("paired="));
                host.paired     = (val == "true");
            }
            else if (line.find("microphone_port=") == 0)
            {
                try
                {
                    host.microphone_port = std::stoi(line.substr(strlen("microphone_port=")));
                }
                catch (...)
                {
                    host.microphone_port = 48100;
                }
            }
        }
        hosts.push_back(host);
    }
    closedir(dir);
    std::cout << "[DEBUG][HostStorage] Total hosts encontrados: " << hosts.size() << "\n";
    return hosts;
}

// saveHosts: no implementado, no se usa con device.ini
bool HostStorage::saveHosts(const std::vector<HostInfo>&) { return false; }

// addHost: usa la ruta configurable de dispositivos
bool HostStorage::addHost(const HostInfo& host)
{
    ConfigManager config;
    std::string baseDir = config.getKeysDir();
    // Asegurar directorio base
    if (!config.ensureKeysDirExists())
        return false;
    std::string safe          = host.safeId.empty() ? makeSafeHostId(host.name.empty() ? host.ip : host.name) : host.safeId;
    std::string keyDirStr     = baseDir + "/" + safe;
    std::string deviceIniPath = keyDirStr + "/device.ini";
    if (fs::exists(deviceIniPath))
        return false;
    // Crear directorio por-host (si no existe)
    if (!config.ensureKeyDirExists(safe))
        return false;
    return HostStorage::writeDeviceIni(keyDirStr, safe, host.ip.c_str(), host.port, host.paired, host.mac.empty() ? nullptr : host.mac.c_str());
}

std::optional<HostInfo> HostStorage::findHost(const std::string& name)
{
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
    ConfigManager config;
    std::string baseDir = config.getKeysDir();
    // name puede ser el nombre visible; intentar ambas rutas (name y safeId derivado)
    std::string keyDirStr = baseDir + "/" + name;
    if (!fs::exists(keyDirStr))
    {
        std::string safe = makeSafeHostId(name);
        std::string alt  = baseDir + "/" + safe;
        if (fs::exists(alt))
            keyDirStr = alt;
    }
    if (!fs::exists(keyDirStr))
        return false;
    // Limpiar cualquier certificado/clave cargada en memoria para este directorio
    try
    {
        std::cout << "[DEBUG][HostStorage] Eliminando archivos de certificados en: '" << keyDirStr << "'\n";
        std::error_code ec;
        fs::remove(keyDirStr + "/client.pem", ec);
        fs::remove(keyDirStr + "/key.pem", ec);
        fs::remove(keyDirStr + "/client.p12", ec);
        fs::remove(keyDirStr + "/uniqueid.dat", ec);
        // También eliminar archivos antiguos si existieran
        fs::remove(keyDirStr + "/client.pem.bak", ec);
        fs::remove(keyDirStr + "/key.pem.bak", ec);
    }
    catch (...)
    {
        std::cout << "[DEBUG][HostStorage] Error eliminando certificados (ignorado)\n";
    }

    // Luego eliminar la carpeta completa del host
    return fs::remove_all(keyDirStr) > 0;
}

bool HostStorage::updateHostIp(const std::string& name, const std::string& newIp)
{
    if (newIp.empty())
        return false;

    auto hostOpt = HostStorage::findHost(name);
    if (!hostOpt.has_value())
        return false;

    HostInfo host = *hostOpt;
    host.ip       = newIp;

    ConfigManager config;
    std::string baseDir = config.getKeysDir();
    std::string safe    = !host.safeId.empty() ? host.safeId : makeSafeHostId(name);
    std::string hostDir = baseDir + "/" + safe;

    if (!fs::exists(hostDir))
    {
        safe    = makeSafeHostId(name);
        hostDir = baseDir + "/" + safe;
        if (!fs::exists(hostDir))
            return false;
        host.safeId = safe;
    }

    return HostStorage::writeDeviceIni(hostDir, safe, host.ip.c_str(), host.port, host.paired, host.mac.empty() ? nullptr : host.mac.c_str());
}
