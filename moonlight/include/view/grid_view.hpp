#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

class GridView : public brls::Box
{
  public:
    using ItemSelectCallback = std::function<void(int)>;

    GridView();

    // Factory para XMLView
    static brls::View* create() { return new GridView(); }

    void setItems(const std::vector<std::string>& names, const std::vector<std::string>& icons = {});

    void setOnItemSelect(ItemSelectCallback cb);

    void setColumns(int cols) { this->columns = cols; }

    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

  private:
    int columns;
    std::vector<std::string> itemNames;
    std::vector<std::string> itemIcons;
    ItemSelectCallback onItemSelect;
    std::vector<brls::View*> itemViews;

    void reload();
};
