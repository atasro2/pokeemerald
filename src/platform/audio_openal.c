#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx-presets.h>
#include <assert.h>

#include "global.h"
#include "platform.h"
#include "m4a.h"
#include "cgb_audio.h"

#undef SOUND_INFO_PTR
extern struct SoundInfo * SOUND_INFO_PTR;

static struct {
    bool8 initialized;
    ALCdevice* device;
    ALCcontext* context;
    ALuint reverbEffect;
    ALuint reverbEffectSlot;
    ALuint sources[2];
    ALuint buffers[2];
} sOALStatus = {0};

LPALBUFFERCALLBACKSOFT alBufferCallbackSOFT;
/* Effect object functions */
static LPALGENEFFECTS alGenEffects;
static LPALDELETEEFFECTS alDeleteEffects;
static LPALISEFFECT alIsEffect;
static LPALEFFECTI alEffecti;
static LPALEFFECTIV alEffectiv;
static LPALEFFECTF alEffectf;
static LPALEFFECTFV alEffectfv;
static LPALGETEFFECTI alGetEffecti;
static LPALGETEFFECTIV alGetEffectiv;
static LPALGETEFFECTF alGetEffectf;
static LPALGETEFFECTFV alGetEffectfv;

/* Auxiliary Effect Slot object functions */
static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
static LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
static LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
static LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
static LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
static LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
static LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
static LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
static LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

static ALsizei cgbBufferCallback(ALvoid * user_data, ALvoid * device_buffer, ALsizei length) {
    int bytes_per_sample = 2 * sizeof (float);
    int sampleCount = length / bytes_per_sample;
    cgb_audio_generate_buf(sampleCount, (float*)device_buffer);
    return length;
}

void SampleMixer(struct SoundMixerState *mixer, u32 scanlineLimit, u16 samplesPerFrame, float *outBuffer, u8 dmaCounter, u16 maxBufSize);
static ALsizei pcmBufferCallback(ALvoid * user_data, ALvoid * device_buffer, ALsizei length) {
    struct SoundMixerState *mixer = (struct SoundMixerState *)SOUND_INFO_PTR;
    if(mixer == NULL) return 0;
    
    int bytes_per_sample = 2 * sizeof (float);
    int sampleCount = length / bytes_per_sample;
    
    float *outBuffer = (float*)device_buffer;
    SampleMixer(mixer, 0, sampleCount, outBuffer, 0, 0);
    
    return length;
}

// TODO: add more proper error checking stuffs to this code
void InitAudio(int sampleRate) {
    cgb_audio_init(sampleRate);
    m4aSoundInit(); // TODO: make a new one of these for PC from scratch later
    
    sOALStatus.device = alcOpenDevice(NULL);
    sOALStatus.context = alcCreateContext(sOALStatus.device, NULL);
    alcMakeContextCurrent(sOALStatus.context);

    if(!alIsExtensionPresent("AL_SOFT_callback_buffer"))
    {
        printf("AL_SOFT_callback_buffer extension not available");
        return;
    }
    
#define LOAD_PROC(T, x)  ((x) = (T)alGetProcAddress(#x))
    LOAD_PROC(LPALGENEFFECTS, alGenEffects);
    LOAD_PROC(LPALDELETEEFFECTS, alDeleteEffects);
    LOAD_PROC(LPALISEFFECT, alIsEffect);
    LOAD_PROC(LPALEFFECTI, alEffecti);
    LOAD_PROC(LPALEFFECTIV, alEffectiv);
    LOAD_PROC(LPALEFFECTF, alEffectf);
    LOAD_PROC(LPALEFFECTFV, alEffectfv);
    LOAD_PROC(LPALGETEFFECTI, alGetEffecti);
    LOAD_PROC(LPALGETEFFECTIV, alGetEffectiv);
    LOAD_PROC(LPALGETEFFECTF, alGetEffectf);
    LOAD_PROC(LPALGETEFFECTFV, alGetEffectfv);

    LOAD_PROC(LPALGENAUXILIARYEFFECTSLOTS, alGenAuxiliaryEffectSlots);
    LOAD_PROC(LPALDELETEAUXILIARYEFFECTSLOTS, alDeleteAuxiliaryEffectSlots);
    LOAD_PROC(LPALISAUXILIARYEFFECTSLOT, alIsAuxiliaryEffectSlot);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTI, alAuxiliaryEffectSloti);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTIV, alAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTF, alAuxiliaryEffectSlotf);
    LOAD_PROC(LPALAUXILIARYEFFECTSLOTFV, alAuxiliaryEffectSlotfv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTI, alGetAuxiliaryEffectSloti);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTIV, alGetAuxiliaryEffectSlotiv);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTF, alGetAuxiliaryEffectSlotf);
    LOAD_PROC(LPALGETAUXILIARYEFFECTSLOTFV, alGetAuxiliaryEffectSlotfv);
    LOAD_PROC(LPALBUFFERCALLBACKSOFT, alBufferCallbackSOFT);
#undef LOAD_PROC
    
    
    
    // --- About reverb ---
    // The reverb in m4a mixer works based on the previous 2 generated audio frames,
    // they are added to the current audio frame based on the reverb variable in the mixer.
    // The "dumber" approach of adding an effect with OpenAL instead of implementing
    // the reverb actually used in the original m4a engine sound good enough:tm: and is
    // supposed to be properly simulated reverb (with terrible parameters) and is more flexible
    // if it were to be edited later. So for the purpose of this port it should be sufficient.
    // --------------------
    
    alGenEffects(1, &sOALStatus.reverbEffect);
    alEffecti(sOALStatus.reverbEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    
    EFXEAXREVERBPROPERTIES reverb = EFX_REVERB_PRESET_GENERIC;
    
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_DENSITY, 0.0);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_DIFFUSION, 1.0f/4.0f);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_GAIN, 0.0);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_GAINHF, reverb.flGainHF);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_DECAY_TIME, 1.0f);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_DECAY_HFRATIO, reverb.flDecayHFRatio);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_REFLECTIONS_GAIN, reverb.flReflectionsGain);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_REFLECTIONS_DELAY, reverb.flReflectionsDelay);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_LATE_REVERB_GAIN, reverb.flLateReverbGain);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_LATE_REVERB_DELAY, reverb.flLateReverbDelay);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_AIR_ABSORPTION_GAINHF, reverb.flAirAbsorptionGainHF);
    alEffectf(sOALStatus.reverbEffect, AL_REVERB_ROOM_ROLLOFF_FACTOR, reverb.flRoomRolloffFactor);
    alEffecti(sOALStatus.reverbEffect, AL_REVERB_DECAY_HFLIMIT, reverb.iDecayHFLimit);
    
    int err = alGetError();
    if(err != AL_NO_ERROR)
    {
        //fprintf(stderr, "OpenAL error: %s\n", alGetString(err));
        if(alIsEffect(sOALStatus.reverbEffect))
            alDeleteEffects(1, &sOALStatus.reverbEffect);
        return;
    }
    
    alGenAuxiliaryEffectSlots(1, &sOALStatus.reverbEffectSlot);
    
    alAuxiliaryEffectSlotf(sOALStatus.reverbEffectSlot, AL_EFFECTSLOT_GAIN, 1.0f);
    alAuxiliaryEffectSloti(sOALStatus.reverbEffectSlot, AL_EFFECTSLOT_EFFECT, (ALint)sOALStatus.reverbEffect);
    assert(alGetError()==AL_NO_ERROR && "Failed to set effect slot");
    
    alGenSources((ALuint)2, sOALStatus.sources);
    
    alGenBuffers((ALuint)2, sOALStatus.buffers);
    
    alBufferCallbackSOFT(sOALStatus.buffers[0], AL_FORMAT_STEREO_FLOAT32, sampleRate, pcmBufferCallback, NULL);
    alBufferCallbackSOFT(sOALStatus.buffers[1], AL_FORMAT_STEREO_FLOAT32, sampleRate, cgbBufferCallback, NULL);
    
    alSourcei(sOALStatus.sources[0], AL_BUFFER, sOALStatus.buffers[0]);
    alSourcei(sOALStatus.sources[1], AL_BUFFER, sOALStatus.buffers[1]);
    
    alSource3i(sOALStatus.sources[0], AL_AUXILIARY_SEND_FILTER, (ALint)sOALStatus.reverbEffectSlot, 0, AL_FILTER_NULL);
    assert(alGetError()==AL_NO_ERROR && "Failed to setup sound source");
    alSourcePlay(sOALStatus.sources[0]);
    alSourcePlay(sOALStatus.sources[1]);
}

void UpdateAudio(void) { // This should be called at a rate of 60hz because m4a's state is updated at a rate of 60hz normally
    struct SoundMixerState *mixer = (struct SoundMixerState *)SOUND_INFO_PTR;
    static u8 oldReverb = 0;
    if(oldReverb != mixer->reverb) {
        oldReverb = mixer->reverb;
        alEffectf(sOALStatus.reverbEffect, AL_REVERB_GAIN, mixer->reverb / 127.0f);
        alAuxiliaryEffectSloti(sOALStatus.reverbEffectSlot, AL_EFFECTSLOT_EFFECT, (ALint)sOALStatus.reverbEffect);            
    }
    m4aSoundMain(); 
}

void QuitAudio(void) // TODO: proper cleanup
{
    ALCdevice *device;
    ALCcontext *ctx;

    ctx = alcGetCurrentContext();
    if(ctx == NULL)
        return;

    device = alcGetContextsDevice(ctx);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(device);
}