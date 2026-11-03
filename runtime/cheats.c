/* Cheats: invincibility (F5). */
#include "recomp.h"
#include "runtime.h"

int g_invincible;

/* game.json hook on cdi_tribes' damage routine (0xDBD0C): d0 = amount,
   d1 = 0 to take it from the player's energy (-22336(a6)), 1 to lower the
   maximum.  Combat damage is the only caller (0xD9A38); when the player is
   invincible the routine returns at once, so energy stays where it was and
   no death follows.  The enemies' strength is kept elsewhere and is not
   touched. */
int hook_player_damage(void) {
    return g_invincible && D(1) == 0;
}
