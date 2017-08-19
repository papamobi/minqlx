#define _GNU_SOURCE
#define __STDC_FORMAT_MACROS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#include "patterns.h"
#include "common.h"
#include "quake_common.h"
#include "simple_hook.h"

#ifndef NOPY
#include "pyminqlx.h"
#endif

// qagame module.
void* qagame;
void* qagame_dllentry;

vmCvar_t* g_gametype;

static void SetTag(void);

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
    }
}

void __cdecl My_G_InitGame(int levelTime, int randomSeed, int restart) {
    G_InitGame(levelTime, randomSeed, restart);

    if (!cvars_initialized) { // Only called once.
        SetTag();
    }
    InitializeCvars();

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
}

void __cdecl My_G_MissileImpact( gentity_t *ent, trace_t *trace ) {
    G_MissileImpact(ent, trace);

    if ( (ent->s.eType != ET_MISSILE) && !strcmp(ent->classname, "translocator")) {
        trace_t tr;
        vec3_t xs = {0, -15, 15};
        vec3_t ys = {0, -15, 15};
        vec3_t zs = {0, -24, 32};
        gentity_t* player = ent->parent;
        for (int i=0; i<3; i++)
        for (int j=0; j<3; j++)
        for (int k=0; k<3; k++) {
            vec3_t start = {ent->r.currentOrigin[0] - xs[i], ent->r.currentOrigin[1] - ys[j], ent->r.currentOrigin[2] - zs[k]};

            SV_Trace(&tr, start, player->r.mins, player->r.maxs, start, player->r.ownerNum, player->clipmask, qfalse);

            if (tr.entityNum >= 0 && tr.entityNum < sv_maxclients->integer) {
              // telefrag victim
              int victim_id = tr.entityNum;
              TeleportPlayer( player, g_entities[ victim_id ].r.currentOrigin, ent->s.apos.trBase);
              i = j = k = 3; break;
            } else if (!tr.startsolid) {
              TeleportPlayer( player, start, ent->s.apos.trBase);
              i = j = k = 3; break;
            }
        }
        G_FreeEntity(ent);
    }
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

void __cdecl My_CA_RoundStateTransition(void) {
    if (level->roundState.eCurrent == ROUND_OVER) {
        BeforeRoundEndDispatcher();
    }
    CA_RoundStateTransition();
}

void __cdecl My_Touch_Item(gentity_t *ent, gentity_t *other, trace_t *trace) {
    if (ent->s.modelindex == MODELINDEX_NEUTRALFLAG) {
        int gt = g_gametype->integer;
        g_gametype->integer = GT_1FCTF;
        Touch_Item(ent, other, trace);
        g_gametype->integer = gt;
    } else {
        Touch_Item(ent, other, trace);
    }
}

void __cdecl My_player_die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath ){
    if (self->client && self->client->ps.powerups[PW_NEUTRALFLAG]) {
        int gt = g_gametype->integer;
        g_gametype->integer = GT_1FCTF;
        player_die(self, inflictor, attacker, damage, meansOfDeath );
        g_gametype->integer = gt;
    } else {
        player_die(self, inflictor, attacker, damage, meansOfDeath );
    }
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
    pint vm_call_table = *(int32_t*)OFFSET_RELP_VM_CALL_TABLE + 0xCAFF4 + (pint)qagame;
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

    res = Hook((void*)Touch_Item, My_Touch_Item, (void*)&Touch_Item);
    if (res) {
        DebugPrint("ERROR: Failed to hook Touch_Item: %d\n", res);
        failed = 1;
    }
    count++;

    res = Hook((void*)player_die, My_player_die, (void*)&player_die);
    if (res) {
        DebugPrint("ERROR: Failed to hook player_die: %d\n", res);
        failed = 1;
    }
    count++;

    res = Hook((void*)CA_RoundStateTransition, My_CA_RoundStateTransition, (void*)&CA_RoundStateTransition);
    if (res) {
        DebugPrint("ERROR: Failed to hook CA_RoundStateTransition: %d\n", res);
        failed = 1;
    }
    count++;

    res = Hook((void*)G_StartKamikaze, My_G_StartKamikaze, (void*)&G_StartKamikaze);
    if (res) {
        DebugPrint("ERROR: Failed to hook G_StartKamikaze: %d\n", res);
        failed = 1;
    }
    count++;

    res = Hook((void*)G_MissileImpact, My_G_MissileImpact, (void*)&G_MissileImpact);
    if (res) {
        DebugPrint("ERROR: Failed to hook G_MissileImpact: %d\n", res);
        failed = 1;
    }

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

    cvarTable_t* cvar_table_item = vm_call_table + 20*sizeof(void*);
    while(1) {
        if (!cvar_table_item->cvarName) {
            DebugPrint("ERROR: Failed to find g_gametype vmcvar\nExiting.\n");
            exit(1);
        }

        if ( strcmp(cvar_table_item->cvarName, "g_gametype") == 0 ) {
            g_gametype = cvar_table_item->vmCvar;
            break;
        }

        cvar_table_item++;
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
