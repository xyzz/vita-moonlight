#include "utils/dialog_utils.h"

#include <borealis.hpp>

using namespace brls;

Dialog* createCustomDialog(brls::Box* content, const DialogOptions& options)
{
    Style style = Application::getStyle();

    if (!content)
        return nullptr;

    if (options.contentPadding > 0)
    {
        float padding = options.contentPadding;
        content->setPadding(padding, padding, padding, padding);
    }

    if (options.contentWidth > 0)
    {
        content->setWidth(options.contentWidth);
    }

    if (options.alignItems != brls::AlignItems::AUTO)
    {
        content->setAlignItems(options.alignItems);
    }
    if (options.justifyContent != brls::JustifyContent::FLEX_START)
    {
        content->setJustifyContent(options.justifyContent);
    }

    auto* dialog = new brls::Dialog(content);
    dialog->setCancelable(options.cancelable);
    dialog->setFocusable(options.focusable);
    if (options.hideHighlight)
    {
        dialog->setHideHighlight(true);
    }

    return dialog;
}

Label* createLabel(const std::string& text, float fontSize,
    brls::HorizontalAlign align, float marginBottom)
{
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(fontSize);
    label->setHorizontalAlign(align);
    label->setSingleLine(false);
    if (marginBottom > 0)
    {
        label->setMarginBottom(marginBottom);
    }
    return label;
}

Box* createInfoBox(const std::vector<std::pair<std::string, std::string>>& rows,
    float fontSize, float rowSpacing)
{
    Style style = Application::getStyle();
    auto* box   = new brls::Box(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::FLEX_START);
    box->setJustifyContent(brls::JustifyContent::FLEX_START);

    bool isFirst = true;
    for (const auto& row : rows)
    {
        auto* label              = new brls::Label();
        std::string displayValue = row.second.empty() ? "-" : row.second;
        label->setText(row.first + ": " + displayValue);
        label->setHorizontalAlign(brls::HorizontalAlign::LEFT);
        label->setSingleLine(false);
        label->setFontSize(fontSize);

        if (!isFirst && rowSpacing > 0)
        {
            label->setMarginTop(rowSpacing);
        }
        isFirst = false;
        box->addView(label);
    }

    return box;
}

Dialog* createLoadingDialog(const std::string& message)
{
    Style style       = Application::getStyle();
    brls::Box* holder = new brls::Box(brls::Axis::COLUMN);

    auto* label = new brls::Label();
    label->setText(message);
    label->setFontSize(style["brls/dialog/fontSize"]);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMarginBottom(21);

    auto* spinner = new brls::ProgressSpinner(brls::ProgressSpinnerSize::LARGE);
    spinner->View::setSize(brls::Size(92, 92));

    holder->addView(label);
    holder->addView(spinner);
    holder->setAlignItems(brls::AlignItems::CENTER);
    holder->setJustifyContent(brls::JustifyContent::CENTER);
    holder->setPadding(style["brls/dialog/paddingTopBottom"],
        style["brls/dialog/paddingLeftRight"], 28,
        style["brls/dialog/paddingLeftRight"]);

    auto* dialog = new brls::Dialog(holder);
    dialog->setCancelable(false);
    dialog->setFocusable(true);
    dialog->setHideHighlight(true);
    dialog->open();
    return dialog;
}

bool updateLoadingDialogText(brls::Dialog* dialog, const std::string& message)
{
    if (!dialog)
        return false;
    // Buscar recursivamente la primera Label dentro del diálogo
    std::function<brls::View*(brls::View*)> findLabel;
    findLabel = [&findLabel](brls::View* v) -> brls::View*
    {
        if (!v)
            return nullptr;
        brls::Label* label = dynamic_cast<brls::Label*>(v);
        if (label)
            return v;
        brls::Box* box = dynamic_cast<brls::Box*>(v);
        if (!box)
            return nullptr;
        auto ch = box->getChildren();
        for (auto* c : ch)
        {
            brls::View* found = findLabel(c);
            if (found)
                return found;
        }
        return nullptr;
    };

    auto children = dialog->getChildren();
    for (auto* c : children)
    {
        brls::View* found = findLabel(c);
        if (found)
        {
            brls::Label* lbl = dynamic_cast<brls::Label*>(found);
            if (lbl)
            {
                lbl->setText(message);
                return true;
            }
        }
    }
    return false;
}
