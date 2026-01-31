#include "PCM_slider_stop.inc"
#include "PCM_slider_hole.inc"
#include "PCM_mouse_down.inc"
#include "PCM_mouse_up.inc"

const char patch04[] PROGMEM = {
0,PC_TREMOLO_LEVEL,70,
0,PC_TREMOLO_RATE,80,
0,PC_WAVE,0,
0,PC_ENV_VOL,48,  // was 48
0,PC_ENV_SPEED,4, // was 4
1,PC_NOTE_HOLD,0,
0,PC_ENV_SPEED,-8, // was -8
8,PC_NOTE_CUT,0,
0,PATCH_END
};
#if 0
// FX: Thud
const char thud[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,82,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,30,
1,PC_NOTE_CUT,0,
0,PATCH_END
};
#endif

// FX: Thud v2
const char thud[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,82,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,30,
0,PC_ENV_SPEED,-128,
0,PATCH_END
};

#if 0
// FX: Hole
const char hole[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,90,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,29,
0,PC_ENV_SPEED,-128,
0,PATCH_END
};
#endif

// FX: Hole v2
const char hole[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,86,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,25,
2,PC_NOTE_CUT,0,
0,PATCH_END
};

#define SFX_CHANNEL            4

#define SFX_SLIDER_STOP        3
#define SFX_SLIDER_HOLE        4
#define SFX_MOUSE_DOWN         5
#define SFX_MOUSE_UP           6
#define FX_THUD                7

#define SFX_SPEED_SLIDER_STOP  32
#define SFX_SPEED_SLIDER_HOLE  32
#define SFX_SPEED_MOUSE_DOWN   32
#define SFX_SPEED_MOUSE_UP     32

#define SFX_VOL_SLIDER_STOP    127
#define SFX_VOL_SLIDER_HOLE    127
#define SFX_VOL_MOUSE_DOWN     255
#define SFX_VOL_MOUSE_UP       255

extern const char waves[];
const struct PatchStruct patches[] PROGMEM = {
{0,NULL,patch04,0,0},
{0,NULL,patch04,0,0},
{0,NULL,patch04,0,0},
// For the below to work, the last 2 bytes of the PCM_Data arrays need to be 0
{2,__data_PCM_slider_stop_raw,NULL,sizeof(__data_PCM_slider_stop_raw)-2,sizeof(__data_PCM_slider_stop_raw)-1},
{2,__data_PCM_slider_hole_raw,NULL,sizeof(__data_PCM_slider_hole_raw)-2,sizeof(__data_PCM_slider_hole_raw)-1},
{2,PCM_mouse_down,NULL,sizeof(PCM_mouse_down)-2,sizeof(PCM_mouse_down)-1},
{2,PCM_mouse_up,NULL,sizeof(PCM_mouse_up)-2,sizeof(PCM_mouse_up)-1},
{0,NULL,thud,0,0},
};
