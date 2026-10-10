// ARM9 scene code still uses the old address-based name. Overlay2 symbols and
// the ROM locate this entry at Player::CanPause (020bd828). Keep the call on
// the real Player implementation, including water/state/control restrictions.
#include "Player.h"
extern "C" int func_020bd828(Player* player) {
    return player->Player::CanPause();
}
