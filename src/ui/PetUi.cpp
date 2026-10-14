#include "PetUi.h"
#include "../game/PlayerStatus.h"
#include "../game/PetTypes.h"

namespace Client
{
    int PetStateIndex(const Game::PlayerStatus& player, int slot)
    {
        if (player.ridePet == slot) return static_cast<int>(Game::PetState::Ride);
        if (player.battlePet == slot) return static_cast<int>(Game::PetState::Battle);
        return static_cast<int>(Game::PetState::Standby);
    }
}
