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
    int port    = 47989; // Sunshine Default Port
    bool paired = false;
    // Host MAC address (optional, can be empty)
    std::string mac;
    // Secure identifier (folder) derived from name or IP; used as keyDir
    std::string safeId;
    // Microphone port (per device, saved in device.ini)
    int microphone_port = 48100; // MoonMic default port
};

// Generate a "safe" identifier to use as a folder name
std::string makeSafeHostId(const std::string& raw);

class HostStorage
{
  public:
    // Load all hosts by reading device.ini from each folder
    static std::vector<HostInfo> loadHosts();
    // Not implemented (not used with device.ini)
    static bool saveHosts(const std::vector<HostInfo>& hosts);
    // Add a host (create device.ini if ​​it doesn't exist)
    static bool addHost(const HostInfo& host);
    // Search for a host by name
    static std::optional<HostInfo> findHost(const std::string& name);
    // Delete a host by name (delete folder)
    static bool removeHost(const std::string& name);
    // Update the IP address of an existing host
    static bool updateHostIp(const std::string& name, const std::string& newIp);
    // Save a host after successful pairing (create device.ini)
    // mac: optional value with the MAC address of the host (ex: "AA:BB:CC:DD:EE:FF").
    static bool savePairedHost(const std::string& name, const std::string& ip, int port, bool paired, const std::string& mac = "");
    // Generate device.ini file in host folder (call after successful pairing)
    // mac: optional pointer; if nullptr or empty string, the mac= line is not written
    static bool writeDeviceIni(const std::string& hostDir, const std::string& safeHostName, const char* address, int port, bool paired, const char* mac = nullptr);
};
