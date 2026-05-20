#pragma once
#include <borealis.hpp>
#include <string>

#include "model/HostStorage.hpp"

class HostMenuTab : public brls::Box
{
  public:
    HostMenuTab(const HostInfo& hostToEdit);
    virtual ~HostMenuTab();
    static brls::View* create(const HostInfo& hostToEdit);

  private:
    HostInfo host;
};
