#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#include "quake_common.h"
#include "race.h"

// types
typedef void (__cdecl *spawn_ptr)(gentity_t *ent);

spawn_ptr SP_race_point;

spawn_t* spawn_table;

void __cdecl My_rp_touch( gentity_t *self, gentity_t *other, trace_t *trace ) {
    if (!other->client) return;
    other->client->pers.teamState.flagruntime = level->time - other->client->race.startTime;
    rp_touch(self, other, trace);
}

void __cdecl My_SP_race_point( gentity_t *ent ) {
    SP_race_point(ent);
    ent->touch = My_rp_touch;
}

int replace_spawn(int index, spawn_ptr new_method, spawn_ptr* old_method, char* new_name) {
    // em92: to be able to write something to address
    int page_size = sysconf(_SC_PAGESIZE);
    if (page_size == -1) return errno;
    int res = mprotect((void*)((pint)(&spawn_table[index]) & ~(page_size-1)), page_size, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (res) return errno;

    *old_method = spawn_table[index].spawn;
    spawn_table[index].spawn = new_method;
    if (new_name) spawn_table[index].name = new_name;

    return 0;
}


int hook_race_methods(void) {

    spawn_table = qagame + 0x2CDFA0;

    // em92: uncomment to get original spawn table items
    //*
    for(int i=0; spawn_table[i].name; i++) {
        Com_Printf("%d\t%s:\t%p\n", i, spawn_table[i].name, spawn_table[i].spawn);
    }
    // */

    replace_spawn(57, My_SP_race_point, &SP_race_point, NULL);

    return 0;
}
