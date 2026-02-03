#include "PCM_slider_stop.inc"
#include "PCM_slider_hole.inc"
#include "PCM_mouse_down.inc"
#include "PCM_mouse_up.inc"

#define SFX_CHANNEL            4

#define SFX_SLIDER_STOP        0
#define SFX_SLIDER_HOLE        1
#define SFX_MOUSE_DOWN         2
#define SFX_MOUSE_UP           3

#define SFX_SPEED_SLIDER_STOP  32
#define SFX_SPEED_SLIDER_HOLE  32
#define SFX_SPEED_MOUSE_DOWN   32
#define SFX_SPEED_MOUSE_UP     32

#define SFX_VOL_SLIDER_STOP    127
#define SFX_VOL_SLIDER_HOLE    127
#define SFX_VOL_MOUSE_DOWN     127
#define SFX_VOL_MOUSE_UP       127

const struct PatchStruct patches[] PROGMEM = {
// For non-looping PCM sounds, the last 2 bytes of the PCM arrays should be 0 (use hexedit on the .raw files)
{2,__data_PCM_slider_stop_raw,NULL,sizeof(__data_PCM_slider_stop_raw)-2,sizeof(__data_PCM_slider_stop_raw)-1},
{2,__data_PCM_slider_hole_raw,NULL,sizeof(__data_PCM_slider_hole_raw)-2,sizeof(__data_PCM_slider_hole_raw)-1},
{2,__data_PCM_mouse_down_raw,NULL,sizeof(__data_PCM_mouse_down_raw)-2,sizeof(__data_PCM_mouse_down_raw)-1},
{2,__data_PCM_mouse_up_raw,NULL,sizeof(__data_PCM_mouse_up_raw)-2,sizeof(__data_PCM_mouse_up_raw)-1},
};
