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
        this->ipField->init(brls::getStr("host_dialog/edit_host_ip_hint"), originalHost.ip, [](std::string) { }, "", "", 22);
    }
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
    std::string newIp   = this->ipField ? this->ipField->getValue() : "";
    std::string newName = this->nameField ? this->nameField->getValue() : "";

    if (newIp.empty())
    {
        brls::Application::notify(brls::getStr("host_dialog/edit_host_ip_empty_error"));
        return;
    }

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
            newHost.safeId   = makeSafeHostId(newName);
            if (HostStorage::addHost(newHost))
            {
                brls::Application::notify(brls::getStr("host_dialog/edit_host_success"));
                // Solicitar recarga global para que el cambio sea visible inmediatamente
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
    else if (newIp != originalHost.ip)
    {
        // Only IP changed
        if (HostStorage::updateHostIp(targetName, newIp))
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
