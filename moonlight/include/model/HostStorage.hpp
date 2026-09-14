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
    int port    = 47989;
    bool paired = false;
    std::string mac;
    std::string safeId;
    int microphone_port = 48100;
};

constexpr int DEFAULT_GAMESTREAM_HTTP_PORT = 47989;

bool parseHostEndpoint(const std::string& input, std::string& address, int& port);
std::string formatHostEndpoint(const std::string& address, int port);
std::string makeSafeHostId(const std::string& raw);

class HostStorage
{
  public:
    static std::vector<HostInfo> loadHosts();
    static bool addHost(const HostInfo& host);
    static std::optional<HostInfo> findHost(const std::string& name);
    static bool removeHost(const std::string& name);
    static bool updateHostEndpoint(const std::string& name, const std::string& newIp, int newPort);
    static bool savePairedHost(const std::string& name, const std::string& ip, int port, bool paired, const std::string& mac = "", int microphonePort = 48100);
    static bool writeDeviceIni(const std::string& hostDir, const std::string& safeHostName, const char* address, int port, bool paired, const char* mac = nullptr, int microphonePort = 48100);
};
