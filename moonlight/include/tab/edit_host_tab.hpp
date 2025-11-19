#pragma once
#include <borealis.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <string>

#include "model/HostStorage.hpp"

class EditHostTab : public brls::Box
{
  public:
    EditHostTab(const HostInfo& hostToEdit);
    virtual ~EditHostTab();
    static brls::View* create(const HostInfo& hostToEdit);
    void saveChanges();

  private:
    HostInfo originalHost;
    BRLS_BIND(brls::InputCell, ipField, "ip_field");
    BRLS_BIND(brls::InputCell, nameField, "name_field");
    BRLS_BIND(brls::Button, saveButton, "save_button");
};
