#include "UiText.h"
#include "../text/TextEncoding.h"
#include <stdexcept>

namespace Client
{
    int PetStateIndex(const Game::PlayerStatus& player, int slot)
    {
        if (player.ridePet == slot) return static_cast<int>(Game::PetState::Ride);
        if (player.battlePet == slot) return static_cast<int>(Game::PetState::Battle);
        return static_cast<int>(Game::PetState::Standby);
    }

    CString LegacyText(const std::string& text)
    {
        auto wide = TextEncoding::LegacyText(text);
        return CString(wide.data(), static_cast<int>(wide.size()));
    }

    CString ErrorText(const char* message)
    {
        auto wide = TextEncoding::ErrorText(message);
        return CString(wide.data(), static_cast<int>(wide.size()));
    }

    std::string AsciiText(CEdit& edit)
    {
        CString text;
        edit.GetWindowText(text);
        std::string result;
        for (int i = 0; i < text.GetLength(); ++i)
        {
            if (text[i] < 33 || text[i] > 126)
                throw std::invalid_argument("Please use ASCII characters without spaces");
            result += static_cast<char>(text[i]);
        }
        return result;
    }

    CString Number(int value)
    {
        CString text;
        text.Format(L"%d", value);
        return text;
    }

    CString Ratio(int value, int maximum)
    {
        CString text;
        text.Format(L"%d / %d", value, maximum);
        return text;
    }
}
