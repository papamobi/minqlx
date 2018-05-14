#define _GNU_SOURCE
#define __STDC_FORMAT_MACROS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#include <sys/mman.h>

#include "patterns.h"
#include "common.h"
#include "quake_common.h"
#include "simple_hook.h"
#include "patches.h"

#ifndef NOPY
#include "pyminqlx.h"
#endif

#define SVF_NOCLIENT 0x01
#define	CONTENTS_SOLID			0x00000001
#define	CONTENTS_PLAYERCLIP		0x10000

// qagame module.
void* qagame;
void* qagame_dllentry;

static void SetTag(void);
int16_t checkpoints;

typedef void (__cdecl *old_SP_trigger_multiple_ptr)(gentity_t *ent);
typedef void (__cdecl *old_SP_trigger_teleport_ptr)(gentity_t *ent);
typedef void (__cdecl *old_trigger_teleporter_touch_ptr)(gentity_t *a1, gentity_t *a2, trace_t *a3);
typedef void (__cdecl *old_SP_target_teleporter_ptr)(gentity_t *ent);
typedef void (__cdecl *old_target_teleporter_use_ptr)(gentity_t *a1, gentity_t *a2, gentity_t *a3);
typedef void (__cdecl *SP_trigger_push_ptr)(gentity_t *ent);
typedef gentity_t* (__cdecl *G_UseTargets_ptr)( gentity_t *ent, gentity_t *activator );
typedef void (__cdecl *race_point_touch_ptr)(gentity_t *a1, gentity_t *a2);
typedef gentity_t* (__cdecl *G_PickTarget_ptr)(char *str);
typedef void (__cdecl *old_SP_target_push_ptr)(gentity_t *ent);
typedef void (__cdecl *old_target_push_use_ptr)(gentity_t *a1, gentity_t *a2, gentity_t *a3);
typedef void (__cdecl *old_SP_race_point_ptr)(gentity_t *ent);

old_SP_trigger_multiple_ptr old_SP_trigger_multiple;
old_SP_trigger_teleport_ptr old_SP_trigger_teleport;
old_trigger_teleporter_touch_ptr old_trigger_teleporter_touch;
old_SP_target_teleporter_ptr old_SP_target_teleporter;
old_target_teleporter_use_ptr old_target_teleporter_use;
SP_trigger_push_ptr SP_trigger_push;
G_UseTargets_ptr G_UseTargets;
race_point_touch_ptr race_point_touch;
G_PickTarget_ptr G_PickTarget;
old_SP_target_push_ptr old_SP_target_push;
old_target_push_use_ptr old_target_push_use;
old_SP_race_point_ptr old_SP_race_point;

int wait_triggers[64][1024];
int p_scores[64];
int p_pings[64];
int p_starttimes[64];

gentity_t	*ent_global_fragsFilter;

void __cdecl target_init_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {	
	if (activator->client)
	{
		if ( !(ent->spawnflags & 1) )
			activator->client->ps.stats[STAT_ARMOR] = 0;
		if ( !(ent->spawnflags & 2) )
			activator->health = 100;
		if ( !(ent->spawnflags & 4) )
		{
			memset( activator->client->ps.ammo, 0, sizeof( activator->client->ps.ammo ) );
			activator->client->ps.stats[STAT_WEAPONS] = (1 << WP_MACHINEGUN);
			activator->client->ps.ammo[WP_MACHINEGUN] = 100;
			activator->client->ps.stats[STAT_WEAPONS] |= (1 << WP_GAUNTLET);
			activator->client->ps.ammo[WP_GAUNTLET] = -1;
			activator->client->ps.weapon = WP_MACHINEGUN;
		}	
		if ( !(ent->spawnflags & 8) )
			memset( activator->client->ps.powerups, 0, sizeof( activator->client->ps.powerups ) );
		if ( !(ent->spawnflags & 16) )
			activator->client->ps.stats[STAT_HOLDABLE_ITEM] = 0;
		if ( ent->spawnflags & 32 && activator->client->ps.stats[STAT_WEAPONS] & (1 << WP_MACHINEGUN)  )
		{
			activator->client->ps.stats[STAT_WEAPONS] = ( 1 << WP_GAUNTLET );
			activator->client->ps.ammo[WP_GAUNTLET] = -1;
			activator->client->ps.ammo[WP_MACHINEGUN] = 0;
		}
	}
	
}

void __cdecl SP_target_init( gentity_t *self ) {
	self->use = target_init_use;
}

void multi_trigger( gentity_t *self, gentity_t *other) {
	int clientNum = other->client->ps.clientNum;
	if ( wait_triggers[clientNum][self->s.number] <= level->time)
	{
		G_UseTargets (self, other);
		wait_triggers[clientNum][self->s.number] = level->time + (self->wait * 1000);
	}
	else
		return;
}

void Use_Multi( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	multi_trigger( ent, activator );
}

void Touch_Multi( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if( !other->client )
		return;
	multi_trigger( self, other );
}

void __cdecl SP_trigger_multiple( gentity_t *ent ) {
	old_SP_trigger_multiple(ent);
	if (ent->wait == -1.0)
		ent->wait = 60*60*24;
	ent->touch = Touch_Multi;
	ent->use = Use_Multi;
}

#define	VectorScale(v, s, o) ((o)[0]=(v)[0]*(s),(o)[1]=(v)[1]*(s),(o)[2]=(v)[2]*(s))
void trigger_teleporter_touch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	float x1 = other->client->ps.velocity[0];
	float y1 = other->client->ps.velocity[1];
	float cur_speed = sqrt(x1*x1+y1*y1);
	old_trigger_teleporter_touch(self, other, trace);
	if ( self->spawnflags & 2 )
		VectorScale( other->client->ps.velocity, cur_speed / 400.0, other->client->ps.velocity );
}

void __cdecl SP_trigger_teleport( gentity_t *ent ) {
	old_SP_trigger_teleport(ent);
	old_trigger_teleporter_touch = ent->touch;
	ent->touch = trigger_teleporter_touch;
}

void target_teleporter_use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	float x1 = activator->client->ps.velocity[0];
	float y1 = activator->client->ps.velocity[1];
	float cur_speed = sqrt(x1*x1+y1*y1);
	old_target_teleporter_use(self, other, activator);
	if ( self->spawnflags & 1 )		//KEEP_SPEED
		VectorScale( activator->client->ps.velocity, cur_speed / 400.0, activator->client->ps.velocity );		
}

void __cdecl SP_target_teleporter( gentity_t *ent ) {
	old_SP_target_teleporter(ent);
	old_target_teleporter_use = ent->use;
	ent->use = target_teleporter_use;
}

void __cdecl target_startTimer_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	int clientnum = activator->client->ps.clientNum;
	if ( (svs->time - p_pings[clientnum]) < 3000 )
		return;
	int test = activator->client->race.startTime;
	for (int i=0; i<1024; i++)
		wait_triggers[clientnum][i] = 0;
	p_scores[clientnum] = 0;
	char *temp = other->targetname;
	other->targetname = 0;
	race_point_touch(other, activator);
	other->targetname = temp;
	if (test != activator->client->race.startTime)
		p_starttimes[clientnum] = svs->time;
	G_UseTargets (ent, activator);
	return;
}

void __cdecl SP_target_startTimer( gentity_t *ent ) {
	ent->use = target_startTimer_use;
}

void __cdecl target_stopTimer_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	if ( activator->client->race.startTime )
	{
		activator->client->pers.teamState.flagruntime = svs->time - p_starttimes[activator->client->ps.clientNum];
		if (!activator->client->pers.teamState.flagruntime)
			activator->client->pers.teamState.flagruntime = 4;
		activator->client->race.nextRacePoint = ent;
		char *temp = ent->target;
		ent->target = 0;
		race_point_touch(ent, activator);
		ent->target = temp;
		G_UseTargets (ent, activator);
	}
	return;
}

void __cdecl SP_target_stopTimer( gentity_t *ent ) {
	ent->use = target_stopTimer_use;
}

void __cdecl target_checkpoint_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	if ( !activator->client->race.startTime )
		return;
	if ( wait_triggers[activator->client->ps.clientNum][other->s.number] )
		return;
	wait_triggers[activator->client->ps.clientNum][other->s.number] = 60 * 60 * 24;
	activator->client->pers.teamState.flagruntime = level->time - activator->client->race.startTime;
	activator->client->race.nextRacePoint = ent;
	char *temp_target = ent->target;
	char *temp_targetname = ent->targetname;
	ent->target = ent->targetname;
	race_point_touch(ent, activator);
	ent->target = temp_target;
	ent->targetname = temp_targetname;
	return;
}

void __cdecl SP_target_checkpoint( gentity_t *ent ) {
	ent->use = target_checkpoint_use;
}

void __cdecl target_fragsFilter_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	if ( !activator->client->race.startTime )
		return;
	int clientNum = activator->client->ps.clientNum;
	if ( p_scores[clientNum] >= ent->count )
		G_UseTargets (ent, activator);
	return;
}

void __cdecl SP_target_fragsFilter( gentity_t *ent ) {
	ent->use = target_fragsFilter_use;
}

void __cdecl target_score_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	if ( !activator->client->race.startTime )
		return;
	int clientNum = activator->client->ps.clientNum;
	p_scores[clientNum] += ent->count;
	if (ent_global_fragsFilter)
	{
		SV_SendServerCommand( svs->clients + activator->client->ps.clientNum, "cp \"^2%d / %d\n\"", p_scores[clientNum], ent_global_fragsFilter->count );
		target_fragsFilter_use(ent_global_fragsFilter, other, activator);
	}
	return;
}

void __cdecl SP_target_score( gentity_t *ent ) {
	ent->use = target_score_use;
	if ( !ent->count )
		ent->count = 1;
}

void trigger_push_velocity_touch( gentity_t *ent, gentity_t *other, trace_t *trace ) {
	if (!other->client)
		return;
	int clientNum = other->client->ps.clientNum;
	if ( wait_triggers[clientNum][ent->s.number] <= level->time)
	{
		wait_triggers[clientNum][ent->s.number] = level->time + (ent->wait * 1000);
		if ( other->client->ps.pm_type != PM_NORMAL )
			return;
		if (ent->spawnflags == 3)
			other->client->ps.velocity[2] = ent->s.origin2[2];
		else if (ent->spawnflags == 11)
			other->client->ps.velocity[2] += ent->s.origin2[2];
	}
}

void __cdecl SP_trigger_push_velocity( gentity_t *ent ) {
	SP_trigger_push(ent);
	ent->r.svFlags |= SVF_NOCLIENT;
	if (ent->wait == -1.0)
		ent->wait = 60*60*24;
	ent->touch = trigger_push_velocity_touch;
}

void __cdecl target_push_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	activator->fly_sound_debounce_time = level->time + 5000;
	old_target_push_use( ent, other, activator );
	return;
}

void __cdecl SP_target_push( gentity_t *ent ) {
	old_SP_target_push(ent);
	if (ent->wait == -1.0)
		ent->wait = 60*60*24;
	old_target_push_use = ent->use;
	ent->use = target_push_use;
}

void new_race_point_touch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if( !other->client )
		return;
	other->client->pers.teamState.flagruntime = level->time - other->client->race.startTime;
	race_point_touch(self, other);
	return;
}

void __cdecl SP_race_point( gentity_t *ent ) {
	old_SP_race_point(ent);
	ent->touch = new_race_point_touch;
}

void __cdecl target_speed_use( gentity_t *ent, gentity_t *other, gentity_t *activator ) {
	if (!activator->client)
		return;
	float x1 = activator->client->ps.velocity[0];
	float y1 = activator->client->ps.velocity[1];
	float z1 = activator->client->ps.velocity[2];
	float x1_abs = abs(x1);
	float y1_abs = abs(y1);
	float z1_abs = abs(z1);
	float cur_speed = sqrt(x1*x1+y1*y1);
	float speed = ent->speed;
	if (speed == 0)
		speed = 100;
	if ( ent->spawnflags == 64 )
		activator->client->ps.velocity[2] = speed;
	else
	{
		if ( ent->spawnflags & 1 )
		{
			speed = cur_speed * speed / 100;
			ent->spawnflags -= 1;
		}
		if ( ent->spawnflags & 256 )
			ent->spawnflags -= 256;
		if ( ent->spawnflags == 64 )
		{
			activator->client->ps.velocity[0] = 0;
			activator->client->ps.velocity[1] = 0;
			activator->client->ps.velocity[2] = speed;
		}	
		if ( ent->spawnflags == 4+2 )
			activator->client->ps.velocity[0] += speed;
		if ( ent->spawnflags == 8+2 )
			activator->client->ps.velocity[0] -= speed;
		if ( ent->spawnflags == 16+2 )
			activator->client->ps.velocity[1] += speed;
		if ( ent->spawnflags == 32+2 )
			activator->client->ps.velocity[1] -= speed;
		if ( ent->spawnflags == 64+2 )
			activator->client->ps.velocity[2] += speed;
		if ( ent->spawnflags == 128+2 )
			activator->client->ps.velocity[2] -= speed;
		if ( ent->spawnflags == 168 )
		{
			activator->client->ps.velocity[0] = -speed;
			activator->client->ps.velocity[1] = -speed;
			activator->client->ps.velocity[2] = -speed;
		}
		if ( ent->spawnflags == 128 )
		{
			activator->client->ps.velocity[0] = 0;
			activator->client->ps.velocity[1] = 0;
			activator->client->ps.velocity[2] = -speed;
		}
		if ( ent->spawnflags == 64+32+2 )
		{
			activator->client->ps.velocity[1] -= speed;
			activator->client->ps.velocity[2] += speed;
		}
		if (cur_speed > 0)
		{
			if ( ent->spawnflags == 2+4+16 )
			{
				float x1_v = sqrt(speed*speed/(x1_abs+y1_abs)*x1_abs);
				float y1_v = sqrt(speed*speed/(x1_abs+y1_abs)*y1_abs);
				activator->client->ps.velocity[0] += (x1 >= 0) ? x1_v : -x1_v;
				activator->client->ps.velocity[1] += (y1 >= 0) ? y1_v : -y1_v;
			}
			if ( ent->spawnflags == 2+8+32 )
			{
				float x1_v = sqrt(speed*speed/(x1_abs+y1_abs)*x1_abs);
				float y1_v = sqrt(speed*speed/(x1_abs+y1_abs)*y1_abs);
				activator->client->ps.velocity[0] -= (x1 >= 0) ? x1_v : -x1_v;
				activator->client->ps.velocity[1] -= (y1 >= 0) ? y1_v : -y1_v;
			}
		}
	}
	return;
}
	
void __cdecl SP_target_speed( gentity_t *ent ) {
	if (ent->wait == -1.0)
		ent->wait = 60*60*24;
	ent->use = target_speed_use;
}

void __cdecl My_Cmd_AddCommand(char* cmd, void* func) {
    if (!common_initialized) InitializeStatic();
    
    Cmd_AddCommand(cmd, func);
}

void __cdecl My_Sys_SetModuleOffset(char* moduleName, void* offset) {
    // We should be getting qagame, but check just in case.
    if (!strcmp(moduleName, "qagame")) {
        // Despite the name, it's not the actual module, but vmMain.
        // We use dlinfo to get the base of the module so we can properly
        // initialize all the pointers relative to the base.
    	qagame_dllentry = offset;

        Dl_info dlinfo;
        int res = dladdr(offset, &dlinfo);
        if (!res) {
            DebugError("dladdr() failed.\n", __FILE__, __LINE__, __func__);
            qagame = NULL;
        }
        else {
            qagame = dlinfo.dli_fbase;
        }
        DebugPrint("Got qagame: %#010x\n", qagame);
    }
    else
        DebugPrint("Unknown module: %s\n", moduleName);
    
    Sys_SetModuleOffset(moduleName, offset);
    if (common_initialized) {
    	SearchVmFunctions();
    	HookVm();
    	InitializeVm();
	
		int page_size = sysconf(_SC_PAGESIZE);
		G_UseTargets = (G_UseTargets_ptr)qagame_dllentry + 0x189D0;
		race_point_touch = (race_point_touch_ptr)qagame_dllentry + 0xEE60;
		G_PickTarget = (G_PickTarget_ptr)qagame_dllentry + 0x18930;
		mprotect((void*)((uint64_t)(offset + 0x254878) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE);
		mprotect((void*)((uint64_t)(offset + 0xF1FC + 2) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset + 0x24E748) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE);
		mprotect((void*)((uint64_t)(offset + 0x24F1C0) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE);
		mprotect((void*)((uint64_t)(offset - 0xCCCB) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0xCCD0) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0xCCD8) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0xCE49) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0x2A0A6) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0x2A226) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset + 0x6B69) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0x271A5+1) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		mprotect((void*)((uint64_t)(offset - 0x27183+2) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
		*(int64_t*)(offset + 0x254878) = "125";
		old_SP_trigger_multiple = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 18);
		old_SP_trigger_teleport = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 20);
		old_SP_target_teleporter = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 31);
		SP_trigger_push = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 19);
		old_SP_race_point = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 58);
		old_SP_target_push = *(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 36);
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 18) = (int64_t*)SP_trigger_multiple;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 20) = (int64_t*)SP_trigger_teleport;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 31) = (int64_t*)SP_target_teleporter;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 46) = "target_init";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 46) = (int64_t*)SP_target_init;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 47) = "target_stopTimer";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 47) = (int64_t*)SP_target_stopTimer;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 48) = "target_startTimer";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 48) = (int64_t*)SP_target_startTimer;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 50) = "target_checkpoint";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 50) = (int64_t*)SP_target_checkpoint;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 30) = (int64_t*)SP_target_score;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 49) = "target_fragsFilter";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 49) = (int64_t*)SP_target_fragsFilter;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 51) = "trigger_push_velocity";
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 51) = (int64_t*)SP_trigger_push_velocity;
		*(int64_t*)(offset + 0x24EE10 + 0x10 * 52) = "target_speed";
		*(int64_t*)(offset + 0x24E748 + 24) = "frags";
		*(int32_t*)(offset + 0x24E748 + 24 + 8) = 0x3B8;
		*(int32_t*)(offset + 0x24E748 + 24 + 12) = 0x00;
		*(int32_t*)(offset + 0x24E748 + 24 + 16) = 0x00;
		*(int8_t*)(offset - 0xCE49+1) = 0x6C;
		*(int8_t*)(offset - 0xCE49+2) = 0x01;
		*(int8_t*)(offset - 0xCE49+3) = 0x00;
		*(int8_t*)(offset - 0xCE49+4) = 0x00;
		*(int8_t*)(offset - 0xCCD8) = 0xF3;
		*(int8_t*)(offset - 0xCCD0) = 0xF3;
		*(int8_t*)(offset - 0xCCCB) = 0xE9;
		*(int8_t*)(offset - 0xCE49) = 0xE9;
		*(int8_t*)(offset + 0x6B69) = 0x83;
		*(int8_t*)(offset + 0x6B69+1) = 0x8E;
		*(char*)(offset + 0xF1FC + 2) = 0;
		*(int8_t*)(offset - 0xCCD8+1) = 0x0F;
		*(int8_t*)(offset - 0xCCD8+2) = 0x5E;
		*(int8_t*)(offset - 0xCCD8+3) = 0x05;
		*(int8_t*)(offset - 0xCCD8+4) = 0xD8;
		*(int8_t*)(offset - 0xCCD8+5) = 0x30;
		*(int8_t*)(offset - 0xCCD8+6) = 0x7E;
		*(int8_t*)(offset - 0xCCD8+7) = 0x00;
		*(int8_t*)(offset - 0xCCD0+1) = 0x0F;
		*(int8_t*)(offset - 0xCCD0+2) = 0x58;
		*(int8_t*)(offset - 0xCCD0+3) = 0x45;
		*(int8_t*)(offset - 0xCCD0+4) = 0x28;
		*(int8_t*)(offset - 0xCCCB+1) = 0x82;
		*(int8_t*)(offset - 0xCCCB+2) = 0xFE;
		*(int8_t*)(offset - 0xCCCB+3) = 0xFF;
		*(int8_t*)(offset - 0xCCCB+4) = 0xFF;
		*(int8_t*)(offset - 0x2A0A6) = 0xEB;
		*(int8_t*)(offset - 0x2A226) = 0xEB;
		*(int8_t*)(offset - 0x271A5+1) = 0x08;
		*(int8_t*)(offset - 0x27183+2) = 0x08;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 52) = (int64_t*)SP_target_speed;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 58) = (int64_t*)SP_race_point;
		*(int64_t*)(offset + 0x24EE10 + 8 + 0x10 * 36) = (int64_t*)SP_target_push;
		
    }
}

void __cdecl My_G_InitGame(int levelTime, int randomSeed, int restart) {
    G_InitGame(levelTime, randomSeed, restart);

    if (!cvars_initialized) { // Only called once.
        SetTag();
    }
    InitializeCvars();
	
	int isthereracepoint = 0;
	checkpoints = 0;
	ent_global_fragsFilter = 0;
	gentity_t	*z1;
	int	j1;
	for ( j1=1, z1=g_entities+j1 ; j1 < level->num_entities ; j1++,z1++ )
	{
		if (z1->inuse && !ent_global_fragsFilter && !strncmp(z1->classname, "target_fragsFilter", 18) && z1->target && !z1->targetname)
			ent_global_fragsFilter = z1;
		if (z1->inuse && !strncmp(z1->classname, "race_point", 10) )
			isthereracepoint = 1;
		if (z1->inuse && (!strncmp(z1->classname, "target_checkpoint", 17) || !strncmp(z1->classname, "target_startTimer", 17) || !strncmp(z1->classname, "target_stopTimer", 16) ))
			checkpoints++;
	}
	cvar_t* temp1 = Cvar_FindVar("g_factoryTitle");
	if (isthereracepoint)
	{
		for ( j1=1, z1=g_entities+j1 ; j1 < level->num_entities ; j1++,z1++ )
		{
			if (z1->inuse && !strncmp(z1->classname, "target_fragsFilter", 18))
				G_FreeEntity(z1);
			else if (z1->inuse && !strncmp(z1->classname, "target_init", 11))
				G_FreeEntity(z1);
			else if (z1->inuse && !strncmp(z1->classname, "target_stopTimer", 16))
				G_FreeEntity(z1);
			else if (z1->inuse && !strncmp(z1->classname, "target_startTimer", 17))
				G_FreeEntity(z1);
			else if (z1->inuse && !strncmp(z1->classname, "target_checkpoint", 17))
				G_FreeEntity(z1);
			else if (z1->inuse && !strncmp(z1->classname, "trigger_push_velocity", 21))
				G_FreeEntity(z1);
			checkpoints = 0;
		}
	}
	
	int page_size = sysconf(_SC_PAGESIZE);
	mprotect((void*)((uint64_t)(qagame_dllentry - 0x2573D) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC );
	mprotect((void*)((uint64_t)(qagame_dllentry - 0x258B9) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
	if ( !strncmp(temp1->string, "T", 1) )	{
		*(int32_t*)(qagame_dllentry - 0x258B9) = 0xFFFD2B51;
		*(int32_t*)(qagame_dllentry - 0x2573D) = 0x00051B61;
	}
	else
	{
		*(int32_t*)(qagame_dllentry - 0x258B9) = 0x0004D939;
		*(int32_t*)(qagame_dllentry - 0x2573D) = 0x0004D7BD;
	}

#ifndef NOPY
    if (restart)
	   NewGameDispatcher(restart);
#endif
}

// USED FOR PYTHON

#ifndef NOPY
void __cdecl My_SV_ExecuteClientCommand(client_t *cl, char *s, qboolean clientOK) {
    char* res = s;
    if (clientOK && cl->gentity) {
        res = ClientCommandDispatcher(cl - svs->clients, s);
        if (!res)
            return;
    }

    SV_ExecuteClientCommand(cl, res, clientOK);
}

void __cdecl My_SV_SendServerCommand(client_t* cl, char* fmt, ...) {
	va_list	argptr;
	char buffer[MAX_MSGLEN];

	va_start(argptr, fmt);
	vsnprintf((char *)buffer, sizeof(buffer), fmt, argptr);
	va_end(argptr);

    char* res = buffer;
	if (cl && cl->gentity)
		res = ServerCommandDispatcher(cl - svs->clients, buffer);
	else if (cl == NULL)
		res = ServerCommandDispatcher(-1, buffer);

	if (!res)
		return;

    SV_SendServerCommand(cl, res);
}

void __cdecl My_SV_ClientEnterWorld(client_t* client, usercmd_t* cmd) {
	clientState_t state = client->state; // State before we call real one.
	SV_ClientEnterWorld(client, cmd);

	// gentity is NULL if map changed.
	// state is CS_PRIMED only if it's the first time they connect to the server,
	// otherwise the dispatcher would also go off when a game starts and such.
	if (client->gentity != NULL && state == CS_PRIMED) {
		ClientLoadedDispatcher(client - svs->clients);
	}
}

void __cdecl My_SV_SetConfigstring(int index, char* value) {
    // Indices 16 and 66X are spammed a ton every frame for some reason,
    // so we add some exceptions for those. I don't think we should have any
    // use for those particular ones anyway. If we don't do this, we get
    // like a 25% increase in CPU usage on an empty server.
    if (index == 16 || (index >= 662 && index < 670)) {
        SV_SetConfigstring(index, value);
        return;
    }

    if (!value) value = "";
    char* res = SetConfigstringDispatcher(index, value);
    // NULL means stop the event.
    if (res)
        SV_SetConfigstring(index, res);
}

void __cdecl My_SV_DropClient(client_t* drop, const char* reason) {
    ClientDisconnectDispatcher(drop - svs->clients, reason);

    SV_DropClient(drop, reason);
}

void __cdecl My_Com_Printf(char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    char* res = ConsolePrintDispatcher(buf);
    // NULL means stop the event.
    if (res)
        Com_Printf(buf);
}

void __cdecl My_SV_SpawnServer(char* server, qboolean killBots) {
    SV_SpawnServer(server, killBots);

    // We call NewGameDispatcher here instead of G_InitGame when it's not just a map_restart,
    // otherwise configstring 0 and such won't be initialized and we can't instantiate minqlx.Game.
    NewGameDispatcher(qfalse);
}

void  __cdecl My_G_RunFrame(int time) {
    // Dropping frames is probably not a good idea, so we don't allow cancelling.
    FrameDispatcher();
	playerState_t	*ps;	
	for (int i=0; i<32; i++)
	{
		ps = (playerState_t*)(&g_entities[i].client->ps);
		if (ps->ping == 999)
			p_pings[i] = svs->time;
	}
    G_RunFrame(time);
}

char* __cdecl My_ClientConnect(int clientNum, qboolean firstTime, qboolean isBot) {
	if (firstTime) {
		char* res = ClientConnectDispatcher(clientNum, isBot);
		if (res && !isBot) {
			return res;
		}
	}

	return ClientConnect(clientNum, firstTime, isBot);
}

void __cdecl My_ClientSpawn(gentity_t* ent) {
    ClientSpawn(ent);
    
    // Since we won't ever stop the real function from being called,
    // we trigger the event after calling the real one. This will allow
    // us to set weapons and such without it getting overriden later.
    ClientSpawnDispatcher(ent - g_entities);
	
	int clientNum = ent->client->ps.clientNum;
	for (int i=0; i<1024; i++)
		wait_triggers[clientNum][i] = 0;
	p_scores[clientNum] = 0;
	p_pings[clientNum] = 0;
	
	gentity_t *z1;
	int	j1;
	for ( j1=1, z1=g_entities+j1 ; j1 < level->num_entities ; j1++,z1++ )
	{
		if (z1->inuse && z1->r.ownerNum == clientNum && z1->parent == ent)
			G_FreeEntity(z1);
	}
	if (checkpoints)
		SV_SendServerCommand( svs->clients + clientNum, "cs 710 \"%d\"\n", checkpoints);
	
}

void __cdecl My_G_StartKamikaze(gentity_t* ent) {
    int client_id, is_used_on_demand;

    if (ent->client) {
        // player activated kamikaze item
        ent->client->ps.eFlags &= ~EF_KAMIKAZE;
        client_id = ent->client->ps.clientNum;
        is_used_on_demand = 1;
    } else if (ent->activator) {
        // dead player's body blast
        client_id = ent->activator->r.ownerNum;
        is_used_on_demand = 0;
    } else {
        // I don't know
        client_id = -1;
        is_used_on_demand = 0;
    }

    if (is_used_on_demand)
       KamikazeUseDispatcher(client_id);

    G_StartKamikaze(ent);

    if (client_id != -1)
        KamikazeExplodeDispatcher(client_id, is_used_on_demand);
}
#endif

// Hook static functions. Can be done before program even runs.
void HookStatic(void) {
	int res, failed = 0;
    DebugPrint("Hooking...\n");
    res = Hook((void*)Cmd_AddCommand, My_Cmd_AddCommand, (void*)&Cmd_AddCommand);
	if (res) {
		DebugPrint("ERROR: Failed to hook Cmd_AddCommand: %d\n", res);
		failed = 1;
	}

    res = Hook((void*)Sys_SetModuleOffset, My_Sys_SetModuleOffset, (void*)&Sys_SetModuleOffset);
    if (res) {
		DebugPrint("ERROR: Failed to hook Sys_SetModuleOffset: %d\n", res);
		failed = 1;
	}

    // ==============================
    //    ONLY NEEDED FOR PYTHON
    // ==============================
#ifndef NOPY
    res = Hook((void*)SV_ExecuteClientCommand, My_SV_ExecuteClientCommand, (void*)&SV_ExecuteClientCommand);
    if (res) {
		DebugPrint("ERROR: Failed to hook SV_ExecuteClientCommand: %d\n", res);
		failed = 1;
    }

    res = Hook((void*)SV_ClientEnterWorld, My_SV_ClientEnterWorld, (void*)&SV_ClientEnterWorld);
	if (res) {
		DebugPrint("ERROR: Failed to hook SV_ClientEnterWorld: %d\n", res);
		failed = 1;
	}

	res = Hook((void*)SV_SendServerCommand, My_SV_SendServerCommand, (void*)&SV_SendServerCommand);
	if (res) {
		DebugPrint("ERROR: Failed to hook SV_SendServerCommand: %d\n", res);
		failed = 1;
	}

    res = Hook((void*)SV_SetConfigstring, My_SV_SetConfigstring, (void*)&SV_SetConfigstring);
    if (res) {
        DebugPrint("ERROR: Failed to hook SV_SetConfigstring: %d\n", res);
        failed = 1;
    }

    res = Hook((void*)SV_DropClient, My_SV_DropClient, (void*)&SV_DropClient);
    if (res) {
        DebugPrint("ERROR: Failed to hook SV_DropClient: %d\n", res);
        failed = 1;
    }

    res = Hook((void*)Com_Printf, My_Com_Printf, (void*)&Com_Printf);
    if (res) {
        DebugPrint("ERROR: Failed to hook Com_Printf: %d\n", res);
        failed = 1;
    }

    res = Hook((void*)SV_SpawnServer, My_SV_SpawnServer, (void*)&SV_SpawnServer);
    if (res) {
        DebugPrint("ERROR: Failed to hook SV_SpawnServer: %d\n", res);
        failed = 1;
    }

#endif

    if (failed) {
		DebugPrint("Exiting.\n");
		exit(1);
	}
}

/* 
 * Hooks VM calls. Not all use Hook, since the VM calls are stored in a table of
 * pointers. We simply set our function pointer to the current pointer in the table and
 * then replace the it with our replacement function. Just like hooking a VMT.
 * 
 * This must be called AFTER Sys_SetModuleOffset, since Sys_SetModuleOffset is called after
 * the VM DLL has been loaded, meaning the pointer we use has been set.
 *
 * PROTIP: If you can, ALWAYS use VM_Call table hooks instead of using Hook().
*/
void HookVm(void) {
    DebugPrint("Hooking VM functions...\n");

#if defined(__x86_64__) || defined(_M_X64)
    pint vm_call_table = *(int32_t*)OFFSET_RELP_VM_CALL_TABLE + OFFSET_RELP_VM_CALL_TABLE + 4;
#elif defined(__i386) || defined(_M_IX86)
    pint vm_call_table = *(int32_t*)OFFSET_RELP_VM_CALL_TABLE + 0xCEFF4 + (pint)qagame;
#endif

	G_InitGame = *(G_InitGame_ptr*)(vm_call_table + RELOFFSET_VM_CALL_INITGAME);
	*(void**)(vm_call_table + RELOFFSET_VM_CALL_INITGAME) = My_G_InitGame;

	G_RunFrame = *(G_RunFrame_ptr*)(vm_call_table + RELOFFSET_VM_CALL_RUNFRAME);

#ifndef NOPY
	*(void**)(vm_call_table + RELOFFSET_VM_CALL_RUNFRAME) = My_G_RunFrame;

	int res, failed = 0, count = 0;
	res = Hook((void*)ClientConnect, My_ClientConnect, (void*)&ClientConnect);
	if (res) {
		DebugPrint("ERROR: Failed to hook ClientConnect: %d\n", res);
		failed = 1;
	}
  count++;

    res = Hook((void*)G_StartKamikaze, My_G_StartKamikaze, (void*)&G_StartKamikaze);
    if (res) {
        DebugPrint("ERROR: Failed to hook G_StartKamikaze: %d\n", res);
        failed = 1;
    }
    count++;

    res = Hook((void*)ClientSpawn, My_ClientSpawn, (void*)&ClientSpawn);
    if (res) {
        DebugPrint("ERROR: Failed to hook ClientSpawn: %d\n", res);
        failed = 1;
    }
    count++;

	if (failed) {
		DebugPrint("Exiting.\n");
		exit(1);
	}

    if ( !seek_hook_slot( -count ) ) {
        DebugPrint("ERROR: Failed to rewind hook slot\nExiting.\n");
        exit(1);
    }
#endif
}


/////////////
// HELPERS //
/////////////

static void SetTag(void) {
    // Add minqlx tag.
    char tags[1024]; // Surely 1024 is enough?
    cvar_t* sv_tags = Cvar_FindVar("sv_tags");
    if (strlen(sv_tags->string) > 2) { // Does it already have tags?
        snprintf(tags, sizeof(tags), "sv_tags \"" SV_TAGS_PREFIX ",%s\"", sv_tags->string);
        Cbuf_ExecuteText(EXEC_INSERT, tags);
    }
    else {
        Cbuf_ExecuteText(EXEC_INSERT, "sv_tags \"" SV_TAGS_PREFIX "\"");
    }
}
