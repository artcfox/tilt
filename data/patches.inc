// FX: Jump
const char jump[] PROGMEM = {
0,PC_WAVE,4,
0,PC_ENV_SPEED,-88,
0,PC_PITCH,59,
1,PC_NOTE_UP,1,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Clop
const char clop[] PROGMEM = {
0,PC_ENV_SPEED,-80,
0,PC_NOISE_PARAMS,2,
1,PC_ENV_VOL,0,
3,PC_ENV_VOL,0x3f,
0,PC_NOISE_PARAMS,3,
0,PATCH_END
};

// FX: Fart
const char fart[] PROGMEM = {
0,PC_ENV_VOL,127,
0,PC_WAVE,1,
0,PC_ENV_SPEED,-1,
0,PC_PITCH,24,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_UP,1,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Gulp
const char gulp[] PROGMEM = {
0,PC_WAVE,0,
0,PC_ENV_SPEED,-16,
0,PC_ENV_VOL,96,
1,PC_PITCH,68,
1,PC_NOTE_UP,2,
1,PC_NOTE_UP,2,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Death
const char death[] PROGMEM = {
0,PC_WAVE,1,
0,PC_ENV_SPEED,-16,
1,PC_PITCH,54,
1,PC_NOTE_DOWN,2,
1,PC_NOTE_DOWN,3,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Falling
const char falling[] PROGMEM = {
0,PC_WAVE,2,
0,PC_ENV_SPEED,-16,
0,PC_SLIDE_SPEED,12,
0,PC_SLIDE,-24,
8,PC_ENV_SPEED,-32,
0,PATCH_END
};

// FX: Echo Alert
const char alert[] PROGMEM = {
0,PC_WAVE,1,
0,PC_PITCH,90,
0,PC_ENV_SPEED,-32,
0,PC_NOTE_DOWN,26,
1,PC_NOTE_UP,3,
1,PC_NOTE_UP,3,
1,PC_NOTE_UP,3,
1,PC_NOTE_UP,4,
1,PC_NOTE_UP,4,
1,PC_NOTE_DOWN,16,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Shake
const char shake[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,80,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,26,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// FX: Thud
const char thud[] PROGMEM = {
0,PC_WAVE,0,
0,PC_PITCH,82,
0,PC_ENV_SPEED,-64,
0,PC_NOTE_DOWN,30,
1,PC_NOTE_CUT,0,
0,PATCH_END
};

// MIDI: Instrument
const char flute[] PROGMEM = {
0,PC_WAVE,8,
0,PC_ENV_VOL,10,
1,PC_ENV_SPEED,12,
5,PC_ENV_SPEED,-9,
0,PATCH_END
};

#define FX_JUMP      3
#define FX_CLOP      4
#define FX_MENU      4
#define FX_FART      5
#define FX_GULP      6
#define FX_DEATH     7
#define FX_FALLING   8
#define FX_ALERT     9
#define FX_SHAKE     10
#define FX_THUD      11

const struct PatchStruct patches[] PROGMEM = {
{0,NULL,flute,0,0},
{0,NULL,flute,0,0},
{0,NULL,flute,0,0},

{0,NULL,jump,0,0},
{1,NULL,clop,0,0},
{0,NULL,fart,0,0},
{0,NULL,gulp,0,0},
{0,NULL,death,0,0},
{0,NULL,falling,0,0},
{0,NULL,alert,0,0},
{0,NULL,shake,0,0},
{0,NULL,thud,0,0},
};
