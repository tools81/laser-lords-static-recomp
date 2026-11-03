/* Machine state snapshots: run-ahead, quicksave slots. */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* One function per module walks its state with st_io: the same code saves
   (copies out of the module) and loads (copies back in). */
typedef struct {
    int load;
    uint8_t *buf;
    size_t n, cap, pos;
    int err;
} St;

void st_io(St *s, void *p, size_t n);
#define ST(s, v) st_io((s), &(v), sizeof(v))
/* a malloc'd array whose length is part of the state */
void st_array(St *s, void **p, uint32_t *count, size_t elem);

void os9_state(St *s);
void cdfm_state(St *s);
void audio_state(St *s);
void video_state(St *s);
int  os9_can_snapshot(void);        /* the kernel is running */
extern uint32_t g_ram_hwm;          /* highest heap address handed out */

/* whole machine */
int  state_save(St *s);             /* fills s (s->buf reused between calls) */
int  state_load(St *s);             /* returns 0 on success */
void st_free(St *s);

/* quicksave slots on disk (saves/quick_NN.state) */
int  state_save_slot(int slot);
int  state_load_slot(int slot);
int  state_slot_exists(int slot, char *when, size_t n);
