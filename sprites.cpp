#define NANO_RESOURCE_IMPLEMENTATION
#include "config.h"
#include "sprites.h"

static_assert(sizeof(bmp_font)==144,"font atlas must be 24 bytes x 6 rows");

#if DOOM_INCLUDE_MONSTERS
static_assert(sizeof(bmp_imp_bits)==(NANO_FULL_ANIMATION?640:192),"Imp frame stride");
static_assert(sizeof(bmp_imp_mask)==sizeof(bmp_imp_bits),"Imp mask length");
#endif

#if DOOM_INCLUDE_DOOR
static_assert(sizeof(bmp_door_bits)==128,"32x32 row-major door");
#endif
