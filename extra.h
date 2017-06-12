#ifndef EXTRA_H
#define EXTRA_H

#include "quake_common.h"

#define MAX_MAPNAMELENGTH 16

typedef struct {
  int gametype;
  char mapname[MAX_MAPNAMELENGTH];
  gentity_t entities[MAX_GENTITIES];
  int gametime;
  qboolean inuse;
} gamestate_t;

#endif /* EXTRA_H */
