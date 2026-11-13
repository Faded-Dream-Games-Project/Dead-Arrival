// ============================================================================
//  DEAD ARRIVAL - assets.c
//  Loads textures from ./textures/*.png and sounds from ./sfx/*.wav,
//  both relative to the executable (assets sit next to the binary).
//  Every asset has a procedural fallback so the game still runs if a file
//  is missing. Music is generated as an ambient WAV loop per map.
// ============================================================================
#include "game.h"

static Texture2D sTextures[TEX_COUNT];
static bool      sTexValid[TEX_COUNT];

static Sound  sSnd[SND_COUNT];
static bool   sSndValid[SND_COUNT];
static Music  sMusic;
static bool   sMusicValid = false;
static char   sAssetBase[512] = ".";

static const char *TEX_FILES[TEX_COUNT] = {
    [TEX_BRICK]     = "textures/brick.png",
    [TEX_WOOD]      = "textures/wood.png",
    [TEX_PLANK]     = "textures/plank.png",
    [TEX_STONE]     = "textures/stone.png",
    [TEX_MARBLE]    = "textures/marble.png",
    [TEX_VELVET]    = "textures/velvet.png",
    [TEX_CURTAIN]   = "textures/curtain.png",
    [TEX_CONCRETE]  = "textures/concrete.png",
    [TEX_METAL]     = "textures/metal.png",
    [TEX_RUST]      = "textures/rust.png",
    [TEX_SANDBAG]   = "textures/sandbag.png",
    [TEX_GRASS]     = "textures/grass.png",
    [TEX_DIRT]      = "textures/dirt.png",
    [TEX_GRAVE]     = "textures/grave.png",
    [TEX_GLASS]     = "textures/glass.png",
    [TEX_ROOF]      = "textures/roof.png",
    [TEX_STUCCO]    = "textures/stucco.png",
    [TEX_TILE]      = "textures/tile.png",
    [TEX_ASPHALT]   = "textures/asphalt.png",
    [TEX_ZOMBIE]    = "textures/zombie.png",
    [TEX_BOX]       = "textures/box.png",
    [TEX_PARTICLE]  = "textures/particle.png",
    [TEX_FLASH]     = "textures/flash.png",
    [TEX_BLOODVIGN] = "textures/bloodvign.png",
    [TEX_LOGO]      = "textures/logo.png",
    [TEX_ICON]      = "textures/icon.png",
};

static const char *SND_FILES[SND_COUNT] = {
    [SND_SHOT_LIGHT]  = "sfx/shot_light.wav",
    [SND_SHOT_HEAVY]  = "sfx/shot_heavy.wav",
    [SND_SHOT_SHOTGUN]= "sfx/shot_shotgun.wav",
    [SND_SHOT_SNIPER] = "sfx/shot_sniper.wav",
    [ SND_KNIFE]      = "sfx/knife.wav",
    [SND_HIT]         = "sfx/hit.wav",
    [SND_SQUISH]      = "sfx/squish.wav",
    [SND_GROAN1]      = "sfx/groan1.wav",
    [SND_GROAN2]      = "sfx/groan2.wav",
    [SND_GROAN3]      = "sfx/groan3.wav",
    [SND_ZATTACK]     = "sfx/zattack.wav",
    [SND_HURT]        = "sfx/hurt.wav",
    [SND_DOWN]        = "sfx/down.wav",
    [SND_DOOR]        = "sfx/door.wav",
    [SND_BUY]         = "sfx/buy.wav",
    [SND_DENIED]      = "sfx/denied.wav",
    [SND_BOX_OPEN]    = "sfx/box_open.wav",
    [SND_BOX_SPIN]    = "sfx/box_spin.wav",
    [SND_BOX_WIN]     = "sfx/box_win.wav",
    [SND_ROUND_START] = "sfx/round_start.wav",
    [SND_ROUND_END]   = "sfx/round_end.wav",
    [SND_RELOAD]      = "sfx/reload.wav",
    [SND_DRY]         = "sfx/dry.wav",
    [SND_STEP1]       = "sfx/step1.wav",
    [SND_STEP2]       = "sfx/step2.wav",
    [SND_HEARTBEAT]   = "sfx/heartbeat.wav",
    [SND_ZRISE]       = "sfx/zrise.wav",
    [SND_JUMP_PAD]    = "sfx/jump_pad.wav",
    [SND_ZIPLINE]     = "sfx/zipline.wav",
    [SND_PERK]        = "sfx/perk.wav",
};

static const char *MUS_FILES[MUS_COUNT] = {
    [MUS_AMBIENT] = "sfx/ambient.wav",
};

// ---------------------------------------------------------------------------
// Procedural texture fallbacks
// ---------------------------------------------------------------------------
static Texture2D MakeFallbackTex(int id) {
    Color *px = malloc(64*64*sizeof(Color));
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        Color c = { 128, 128, 128, 255 };
        int ck = ((x/8 + y/8) & 1);
        switch (id) {
            case TEX_BRICK:    c = (Color){ 120+ck*14,  72+ck*10,  58+ck*8, 255}; break;
            case TEX_WOOD:     c = (Color){ 110+ck*20,  80+ck*14,  50+ck*10, 255}; break;
            case TEX_PLANK:    c = (Color){ 130+ck*24,  98+ck*18,  62+ck*12, 255}; break;
            case TEX_STONE:    c = (Color){ 105+ck*22, 103+ck*22,  98+ck*20, 255}; break;
            case TEX_MARBLE:   c = (Color){ 200+ck*20, 196+ck*18, 186+ck*16, 255}; break;
            case TEX_VELVET:   c = (Color){  96+ck*30,  16+ck*10,  28+ck*12, 255}; break;
            case TEX_CURTAIN:  c = (Color){  90+ck*24,  14+ck*8,   22+ck*10, 255}; break;
            case TEX_CONCRETE: c = (Color){ 118+ck*10, 116+ck*10, 112+ck*10, 255}; break;
            case TEX_METAL:    c = (Color){ 110+ck*26, 114+ck*26, 122+ck*26, 255}; break;
            case TEX_RUST:     c = (Color){ 122+ck*20,  66+ck*16,  38+ck*12, 255}; break;
            case TEX_SANDBAG:  c = (Color){ 150+ck*16, 132+ck*14,  94+ck*10, 255}; break;
            case TEX_GRASS:    c = (Color){  58+ck*24,  84+ck*26,  38+ck*14, 255}; break;
            case TEX_DIRT:     c = (Color){  96+ck*14,  74+ck*12,  50+ck*8,  255}; break;
            case TEX_GRAVE:    c = (Color){ 138+ck*16, 138+ck*16, 134+ck*14, 255}; break;
            case TEX_GLASS:    c = (Color){ 160, 190, 200, 90+ck*40}; break;
            case TEX_ROOF:     c = (Color){  70+ck*16,  52+ck*12,  44+ck*10, 255}; break;
            case TEX_STUCCO:   c = (Color){ 178+ck*14, 168+ck*12, 148+ck*10, 255}; break;
            case TEX_TILE:     c = (Color){ 168+ck*22, 160+ck*20, 146+ck*18, 255}; break;
            case TEX_ASPHALT:  c = (Color){  72+ck*10,  72+ck*10,  76+ck*10, 255}; break;
            case TEX_ZOMBIE:   c = (Color){  96+ck*28, 112+ck*26,  84+ck*20, 255}; break;
            case TEX_BOX:      c = (Color){ 110+ck*30,  78+ck*22,  40+ck*14, 255}; break;
            case TEX_PARTICLE: c = (Color){ 255, 255, 255, 255}; break;
            case TEX_FLASH:    c = (Color){ 255, 240, 180, 255}; break;
            case TEX_BLOODVIGN:c = (Color){ 120, 0, 0, 0 }; break;
            case TEX_LOGO:     c = (Color){ 230+ck*10, 230+ck*10, 230+ck*10, 255}; break;
            case TEX_ICON:     c = (Color){ 200+ck*20, 180+ck*16, 160+ck*12, 255}; break;
            default: break;
        }
        px[y*64+x] = c;
    }
    Image im = { .data = px, .width = 64, .height = 64, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(im);
    free(px);
    return t;
}

// ---------------------------------------------------------------------------
static void FindAssetBase(void) {
    // assets relative to the executable's directory
    const char *dirs[] = { ".", "./assets" };
    for (unsigned i = 0; i < ARRAY_LEN(dirs); i++) {
        if (DirectoryExists(TextFormat("%s/textures", dirs[i])) ||
            DirectoryExists(TextFormat("%s/sfx", dirs[i]))) {
            strcpy(sAssetBase, dirs[i]);
            return;
        }
    }
    strcpy(sAssetBase, ".");
}

void AssetsLoad(void) {
    FindAssetBase();
    for (int i = 0; i < TEX_COUNT; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s/%s", sAssetBase, TEX_FILES[i]);
        if (FileExists(path)) {
            sTextures[i] = LoadTexture(path);
            sTexValid[i] = sTextures[i].id != 0;
        }
        if (!sTexValid[i]) sTextures[i] = MakeFallbackTex(i);
    }
    SetTextureFilter(sTextures[TEX_LOGO], TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(sTextures[TEX_BLOODVIGN], TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(sTextures[TEX_PARTICLE], TEXTURE_FILTER_BILINEAR);

    if (IsAudioDeviceReady()) {
        for (int i = 0; i < SND_COUNT; i++) {
            char path[600];
            snprintf(path, sizeof path, "%s/%s", sAssetBase, SND_FILES[i]);
            if (FileExists(path)) {
                sSnd[i] = LoadSound(path);
                sSndValid[i] = sSnd[i].frameCount > 0;
            }
        }
    }
}

void AssetsUnload(void) {
    for (int i = 0; i < TEX_COUNT; i++) if (sTexValid[i]) UnloadTexture(sTextures[i]);
    for (int i = 0; i < SND_COUNT; i++) if (sSndValid[i]) UnloadSound(sSnd[i]);
    if (sMusicValid) { UnloadMusicStream(sMusic); sMusicValid = false; }
}

Texture2D AssetsTex(int id) { return sTextures[id]; }
bool AssetsHasTex(int id) { return sTexValid[id]; }

// ---------------------------------------------------------------------------
// Audio playback helpers
// ---------------------------------------------------------------------------
static float sMasterVolume = 1.0f;

void SfxSetVolume(float v) { sMasterVolume = CLAMP(v, 0, 1); }

void SfxPlay(int id, float pitch, float vol) {
    if (id < 0 || id >= SND_COUNT) return;
    if (!sSndValid[id]) return;
    Sound s = sSnd[id];
    SetSoundPitch(s, pitch > 0 ? pitch : 1.0f);
    SetSoundVolume(s, sMasterVolume * CLAMP(vol, 0, 1));
    PlaySound(s);
}

void MusicStart(int mapId) {
    MusicStop();
    if (!IsAudioDeviceReady()) return;
    char path[600];
    snprintf(path, sizeof path, "%s/sfx/ambient%d.wav", sAssetBase, (int)mapId);
    if (FileExists(path)) {
        sMusic = LoadMusicStream(path);
        if (sMusic.stream.buffer != NULL) {
            sMusic.looping = true;
            SetMusicVolume(sMusic, 0.55f*sMasterVolume);
            PlayMusicStream(sMusic);
            sMusicValid = true;
        }
    }
}

void MusicStop(void) {
    if (sMusicValid) { StopMusicStream(sMusic); UnloadMusicStream(sMusic); sMusicValid = false; }
}

void MusicUpdate(void) {
    if (sMusicValid) UpdateMusicStream(sMusic);
}
