/*
    Copyright 2025 AorsiniYT

    SPDX-License-Identifier: GPL-3.0-only
*/
#pragma once
#include <optional>
#include <string>
#include <vector>

struct HostInfo
{
    std::string name;
    std::string ip;
    int port    = 47989; // Puerto por defecto Sunshine
    bool paired = false;
    // Dirección MAC del host (opcional, puede estar vacía)
    std::string mac;
    // Identificador seguro (carpeta) derivado del nombre o IP; se usa como keyDir
    std::string safeId;
};

// Genera un identificador "seguro" para usar como nombre de carpeta
std::string makeSafeHostId(const std::string& raw);

class HostStorage
{
  public:
    // Carga todos los hosts leyendo device.ini de cada carpeta
    static std::vector<HostInfo> loadHosts();
    // No implementado (no se usa con device.ini)
    static bool saveHosts(const std::vector<HostInfo>& hosts);
    // Añade un host (crea device.ini si no existe)
    static bool addHost(const HostInfo& host);
    // Busca un host por nombre
    static std::optional<HostInfo> findHost(const std::string& name);
    // Elimina un host por nombre (borra la carpeta)
    static bool removeHost(const std::string& name);
    // Actualiza la dirección IP de un host existente
    static bool updateHostIp(const std::string& name, const std::string& newIp);
    // Guarda un host tras pairing exitoso (crea device.ini)
    // Guarda un host tras pairing exitoso (crea device.ini)
    // mac: valor opcional con la dirección MAC del host (ej: "AA:BB:CC:DD:EE:FF").
    static bool savePairedHost(const std::string& name, const std::string& ip, int port, bool paired, const std::string& mac = "");
    // Genera el archivo device.ini en la carpeta del host (llamar tras pairing exitoso)
    // mac: puntero opcional; si es nullptr o cadena vacía, no se escribe la línea mac=
    static bool writeDeviceIni(const std::string& hostDir, const std::string& safeHostName, const char* address, int port, bool paired, const char* mac = nullptr);
    // NOTE: la actualización de la línea mac= se gestiona ahora durante el
    // flujo normal de emparejamiento y mediante la reescritura segura de
    // `device.ini` desde los puntos de persistencia (writeDeviceIni / savePairedHost).
    // La función auxiliar para actualizar solo la línea mac ya no existe.
};
