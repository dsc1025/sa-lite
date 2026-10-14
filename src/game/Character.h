#pragma once

#include <string>

namespace Game
{
    enum class LoginResult { Accepted, Rejected };
    struct Character
    {
        std::string name;
        int level = -1;
        int slot = -1;
    };
    struct NewCharacter
    {
        std::string name;
        int model = 0, color = 0, hometown = 0;
        int points[4] = {};
        int elements[4] = {};
    };
    void ValidateNewCharacter(const NewCharacter& character);
}
