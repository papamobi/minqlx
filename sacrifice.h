#include "quake_common.h"

qboolean Sacrifice_IsEnabled(void);
int Sacrifice_TouchObelisk( gentity_t *ent, gentity_t *other, int team );
int Sacrifice_TouchFlag( gentity_t *ent, gentity_t *other, int team );
void Sacrifice_ResetFlag( gentity_t* ent );
void Sacrifice_Init(void);
