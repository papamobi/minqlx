#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sacrifice.h"

gentity_t* n_flag;
const float SCORE_PERIOD = 2;

qboolean Sacrifice_IsEnabled(void) {
  return n_flag != NULL;
}


void __cdecl Sacrifice_ScoreThink( gentity_t* ent ) {
  int team, cs;
  char buffer[16];

  // ToDo: add period constatnt

  ent->nextthink = level->time + SCORE_PERIOD * 1000 ;
  ent->think = Sacrifice_ScoreThink;

  switch( ent->s.generic1 ) {
    case 1 << 1:
      team = 1;
      cs = CS_SCORES1;
      break;

    case 1 << 2:
      team = 2;
      cs = CS_SCORES2;
      break;

    default:
      return;
  }

  level->teamScores[ team ]++;
  sprintf( buffer, "%d", level->teamScores[ team ] );
  SV_SetConfigstring( cs, buffer);
}


int Sacrifice_TouchObelisk( gentity_t *ent, gentity_t *other, int team ) {
  gclient_t* cl = other->client;

  if (!cl->ps.powerups[PW_NEUTRALFLAG])
    return 0;

  VectorCopy( ent->s.pos.trBase, n_flag->s.pos.trBase );
  VectorCopy( n_flag->s.pos.trBase, n_flag->s.origin );
  VectorCopy( n_flag->s.pos.trBase, n_flag->r.currentOrigin );

  RespawnItem( n_flag );

  cl->ps.powerups[PW_NEUTRALFLAG] = 0;

  n_flag->nextthink = level->time + SCORE_PERIOD * 1000;
  n_flag->think = Sacrifice_ScoreThink;
  n_flag->s.generic1 = 1 << team;

  return 0;
}


int Sacrifice_TouchFlag( gentity_t *ent, gentity_t *other, int team ) {
  if ( n_flag->s.generic1 & ( 1 << team ) ) return 0;
  n_flag->s.generic1 = 0;
  n_flag->nextthink = 0;

  return Team_TouchEnemyFlag(ent, other, team);
}


void Sacrifice_ResetFlag( gentity_t* ent) {
  VectorCopy( n_flag->s.origin2, n_flag->s.origin );
  VectorCopy( n_flag->s.origin2, n_flag->s.pos.trBase );
  VectorCopy( n_flag->s.origin2, n_flag->r.currentOrigin );
  Team_DroppedFlagThink(ent);
}


void Sacrifice_Init(void) {
  n_flag = NULL;
  cvar_t* g_factory = Cvar_FindVar("g_factory");

  if ( strcmp( g_factory->string, "sacrifice" ) != 0 ) return;

  for(int i=0; i<MAX_GENTITIES; i++) {
    gentity_t* ent = &g_entities[i];

    if (!ent->inuse)
      continue;

    if (strcmp(ent->classname, "team_CTF_neutralflag") == 0) {
      n_flag = ent;
      VectorCopy( ent->s.origin, ent->s.origin2 );
      n_flag->r.svFlags |= SVF_NOCLIENT;
      n_flag->s.eFlags |= EF_NODRAW;
      n_flag->r.contents = 0;
      n_flag->nextthink = level->time + 15000;
      n_flag->think = RespawnItem;
      return;
    }
  }
}
