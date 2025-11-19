#pragma once
#include <borealis.hpp>
#include <functional>
#include <string>
#include <vector>

// GridView mejorado para Moonlight PSVita/Windows
// Inspirado en Moonlight-Switch, con navegación por foco y columnas limitadas

class GridView : public brls::Box
{
  public:
    using ItemSelectCallback = std::function<void(int)>;

    GridView();

    // Factory para XMLView
    static brls::View* create() { return new GridView(); }

    // Configura los elementos a mostrar (nombre, icono opcional)
    void setItems(const std::vector<std::string>& names, const std::vector<std::string>& icons = {});

    // Callback cuando se selecciona un elemento
    void setOnItemSelect(ItemSelectCallback cb);

    // Configurar número de columnas
    void setColumns(int cols) { this->columns = cols; }

    // Navegación por foco mejorada
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;

  private:
    int columns;
    std::vector<std::string> itemNames;
    std::vector<std::string> itemIcons;
    ItemSelectCallback onItemSelect;
    std::vector<brls::View*> itemViews;

    void reload();
};
