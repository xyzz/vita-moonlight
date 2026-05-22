#include "view/grid_view.hpp"

#include <borealis/views/button.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>

#include "debug.hpp"
#include "view/pccard.hpp"

GridView::GridView()
{
    vita_log::info("[GridView] Constructor llamado");
    this->setAxis(brls::Axis::COLUMN);
    this->columns = 4; // 4 columns by default for PS Vita
    this->itemNames.clear();
    this->itemIcons.clear();
    this->onItemSelect = nullptr;
}

void GridView::setItems(const std::vector<std::string>& names, const std::vector<std::string>& icons)
{
    vita_log::info("[GridView] setItems llamado, names.size()=%zu", names.size());
    this->itemNames = names;
    this->itemIcons = icons;
    reload();
}

void GridView::setOnItemSelect(ItemSelectCallback cb)
{
    this->onItemSelect = cb;
}

void GridView::setItemIcon(int index, const std::string& iconPath)
{
    if (index >= 0 && index < (int)itemViews.size())
    {
        PCCard* card = dynamic_cast<PCCard*>(itemViews[index]);
        if (card)
        {
            card->setPCImage(iconPath);
        }
    }
}

void GridView::reload()
{
    vita_log::info("[GridView] reload llamado, itemNames.size()=%zu", itemNames.size());

    this->clearViews();
    itemViews.clear();

    if (itemNames.empty())
    {
        vita_log::info("[GridView] reload: lista vacía, no se añaden elementos");
        return;
    }

    size_t currentRow        = 0;
    brls::Box* currentRowBox = nullptr;

    for (size_t i = 0; i < itemNames.size(); ++i)
    {
        if (i % columns == 0)
        {
            currentRowBox = new brls::Box(brls::Axis::ROW);
            currentRowBox->setAlignItems(brls::AlignItems::STRETCH);
            this->addView(currentRowBox);
            currentRow++;
        }

        auto* card = new PCCard(itemNames[i], itemIcons.empty() ? "img/moonlight/pc.png" : itemIcons[i]);

        card->setClickAction([this, i]()
            {
            vita_log::info("[GridView] Elemento seleccionado idx=%d", i);
            if (onItemSelect) onItemSelect(i); });

        card->setFocusable(true);

        if (i % columns != 0)
        {
            card->setMarginLeft(12);
        }

        currentRowBox->addView(card);
        itemViews.push_back(card);

        vita_log::info("[GridView] Elemento añadido: '%s' en fila %d, columna %d",
            itemNames[i].c_str(), currentRow, (i % columns) + 1);
    }

    vita_log::info("[GridView] reload finalizado, %d filas creadas", currentRow);
}

brls::View* GridView::getNextFocus(brls::FocusDirection direction, brls::View* currentView)
{
    if (!currentView || itemViews.empty())
        return nullptr;

    auto it = std::find(itemViews.begin(), itemViews.end(), currentView);
    if (it == itemViews.end())
        return nullptr;

    size_t currentIndex = std::distance(itemViews.begin(), it);
    size_t totalItems   = itemViews.size();
    int currentRow      = currentIndex / columns;
    int currentCol      = currentIndex % columns;
    int totalRows       = (totalItems + columns - 1) / columns;

    brls::View* nextView = nullptr;

    switch (direction)
    {
        case brls::FocusDirection::RIGHT:
            if (currentCol < columns - 1 && currentIndex + 1 < totalItems)
            {
                nextView = itemViews[currentIndex + 1];
            }
            break;

        case brls::FocusDirection::LEFT:
            if (currentCol > 0)
            {
                nextView = itemViews[currentIndex - 1];
            }
            break;

        case brls::FocusDirection::DOWN:
            if (currentRow < totalRows - 1)
            {
                int nextRowIndex = (currentRow + 1) * columns + currentCol;
                if (nextRowIndex < (int)totalItems)
                {
                    nextView = itemViews[nextRowIndex];
                }
                else
                {
                    // If there is no element in this column, go to the last in the row
                    int lastInRow = std::min((int)totalItems - 1, (currentRow + 1) * columns + columns - 1);
                    nextView      = itemViews[lastInRow];
                }
            }
            break;

        case brls::FocusDirection::UP:
            if (currentRow > 0)
            {
                int prevRowIndex = (currentRow - 1) * columns + currentCol;
                if (prevRowIndex < (int)totalItems)
                {
                    nextView = itemViews[prevRowIndex];
                }
            }
            break;

        default:
            break;
    }

    return nextView;
}
