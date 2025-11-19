#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace controller
{

struct SpecialInputOption
{
    std::uint32_t code;
    const char* name;
    bool selectable;
};

inline constexpr std::uint32_t INPUT_SPECIAL_KEY_PAUSE    = 0;
inline constexpr std::uint32_t INPUT_SPECIAL_KEY_KEYBOARD = 1;

// Devuelve la lista completa (incluye encabezados no seleccionables)
const std::vector<SpecialInputOption>& getAllSpecialInputOptions();

// Devuelve solo las entradas seleccionables en el mismo orden que el legacy
const std::vector<SpecialInputOption>& getSelectableSpecialInputOptions();

// Busca la opción por código. Devuelve nullptr si no existe.
const SpecialInputOption* findSpecialInputOption(std::uint32_t code);

// Obtiene el índice dentro de la lista seleccionable. Retorna 0 si no se encuentra.
std::size_t getSelectableIndexForCode(std::uint32_t code);

// Obtiene el código asociado a un índice de la lista seleccionable. Devuelve 0 si el índice es inválido.
std::uint32_t getCodeForSelectableIndex(std::size_t index);

// Devuelve un nombre listo para mostrar; si no existe, retorna una representación hexadecimal.
std::string getDisplayNameForCode(std::uint32_t code);

} // namespace controller
