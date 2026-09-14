#include "tab/edit_host_tab.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/views/cells/cell_input.hpp>

#include "model/HostStorage.hpp"
#include "tab/hosts_tab.hpp"

using namespace brls::literals;

EditHostTab::EditHostTab(const HostInfo& hostToEdit)
    : originalHost(hostToEdit)
{
    this->inflateFromXMLRes("xml/tabs/edit_host.xml");

    if (this->ipField)
    {
        this->ipField->init(brls::getStr("host_dialog/edit_host_ip_hint"), originalHost.ip, [this](const std::string& input)
            {
            if (input.empty())
                return;
            std::string address;
            int port = DEFAULT_GAMESTREAM_HTTP_PORT;
            if (!parseHostEndpoint(input, address, port))
            {
                brls::Application::notify(brls::getStr("host_dialog/edit_host_ip_invalid_error"));
                return;
            }
            this->openPortKeyboard(); }, "", "", 15, brls::KEYBOARD_USE_EXTENDED_NUMBER);
    }
    if (this->portField)
        this->portField->init(brls::getStr("host_dialog/edit_host_port"), originalHost.port, [](long) { }, brls::getStr("host_dialog/edit_host_port_hint"), 5);
    if (this->nameField)
    {
        this->nameField->init(brls::getStr("host_dialog/edit_host_name_hint"), originalHost.name, [](std::string) { }, "", "", 50);
    }

    if (this->saveButton)
    {
        this->saveButton->setText(brls::getStr("host_dialog/edit_host_save"));
        this->saveButton->registerClickAction([this](brls::View*)
            {
            this->saveChanges();
            return true; });
    }
}

EditHostTab::~EditHostTab()
{
}

brls::View* EditHostTab::create(const HostInfo& hostToEdit)
{
    return new EditHostTab(hostToEdit);
}

void EditHostTab::saveChanges()
{
    std::string ipInput = this->ipField ? this->ipField->getValue() : "";
    std::string newName = this->nameField ? this->nameField->getValue() : "";

    if (ipInput.empty())
    {
        brls::Application::notify(brls::getStr("host_dialog/edit_host_ip_empty_error"));
        return;
    }

    std::string newIp;
    int ignoredPort = DEFAULT_GAMESTREAM_HTTP_PORT;
    if (!parseHostEndpoint(ipInput, newIp, ignoredPort))
    {
        brls::Application::notify(brls::getStr("host_dialog/edit_host_ip_invalid_error"));
        return;
    }

    const long portInput = this->portField ? this->portField->getValue() : DEFAULT_GAMESTREAM_HTTP_PORT;
    if (portInput < 1 || portInput > 65535)
    {
        brls::Application::notify(brls::getStr("host_dialog/edit_host_port_invalid_error"));
        return;
    }
    const int newPort = static_cast<int>(portInput);

    if (newName.empty())
    {
        brls::Application::notify(brls::getStr("host_dialog/edit_host_name_empty_error"));
        return;
    }

    std::string targetName = !originalHost.safeId.empty() ? originalHost.safeId : originalHost.name;

    if (newName != originalHost.name)
    {
        // Name changed, need to remove old and add new
        if (HostStorage::removeHost(originalHost.name))
        {
            HostInfo newHost = originalHost;
            newHost.name     = newName;
            newHost.ip       = newIp;
            newHost.port     = newPort;
            newHost.safeId   = makeSafeHostId(newName);
            if (HostStorage::addHost(newHost))
            {
                brls::Application::notify(brls::getStr("host_dialog/edit_host_success"));
                // Request global reload so that the change is visible immediately
                HostsTab::requestGlobalRefresh();
            }
            else
            {
                brls::Application::notify(brls::getStr("host_dialog/edit_host_failure"));
            }
        }
        else
        {
            brls::Application::notify(brls::getStr("host_dialog/edit_host_failure"));
        }
    }
    else if (newIp != originalHost.ip || newPort != originalHost.port)
    {
        if (HostStorage::updateHostEndpoint(targetName, newIp, newPort))
        {
            brls::Application::notify(brls::getStr("host_dialog/edit_host_success"));
            // Refresh hosts list globally so UI cache updates
            HostsTab::requestGlobalRefresh();
        }
        else
        {
            brls::Application::notify(brls::getStr("host_dialog/edit_host_failure"));
        }
    }
    else
    {
        // No changes
        brls::Application::notify("No changes made");
    }

    brls::Application::popActivity();

    // Request a global refresh so the Hosts list (cached in the UI) reflects the
    // saved changes immediately. Use brls::sync to ensure this runs on the UI thread.
    brls::sync([]()
        { HostsTab::requestGlobalRefresh(); });
}

void EditHostTab::openPortKeyboard()
{
    brls::sync([this]()
        {
        if (!this->portField)
            return;
        brls::Application::getImeManager()->openForNumber([this](long port)
            {
            if (!this->portField)
                return;
            if (port < 1 || port > 65535)
            {
                brls::Application::notify(brls::getStr("host_dialog/edit_host_port_invalid_error"));
                return;
            }
            this->portField->setValue(port); },
            brls::getStr("host_dialog/edit_host_port"), brls::getStr("host_dialog/edit_host_port_hint"), 5, std::to_string(this->portField->getValue())); });
}
