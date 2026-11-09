#pragma once

#include "Sound.hpp"
#include "SoundSDL.hpp"
#include <mutex>

#define MAX_VOICE_COUNT 1024
extern std::atomic<SDLSoundBuffer*> sAE_ActiveVoices[MAX_VOICE_COUNT];

#ifdef __PS2__
// PS2: the voice list and voice lifetimes are guarded by one lock that the mixer holds while it
// runs, and voices are only ever deleted by the game thread. Letting the audio thread delete
// released voices raced with the game thread and destroyed semaphore 0 (the EE kernel's
// topThread semaphore), which froze the whole console while loading a level.
//
// Each std::mutex costs one EE kernel semaphore and the kernel only has a few hundred; with one
// mutex per voice a level load ran out, CreateSema failed silently, and destroying that mutex then
// deleted semaphore 0 (topThread's). So on PS2 every voice shares this single recursive lock.
std::recursive_mutex& Ps2VoiceListLock();

// Stand-in for the per-voice mutex on PS2: locks the shared voice lock.
struct Ps2VoiceMutex final
{
    void lock()
    {
        Ps2VoiceListLock().lock();
    }
    void unlock()
    {
        Ps2VoiceListLock().unlock();
    }
};
using VoiceMutex = Ps2VoiceMutex;

// Without atomic instructions libstdc++ gives every std::shared_ptr control block its own mutex,
// i.e. one more kernel semaphore per sound buffer; loading a few levels ran out of them again and
// destroying one then deleted semaphore 0. Voices are only created/destroyed on the game thread, so
// the single-threaded reference count is enough.
using VoiceBufferPtr = std::__shared_ptr<std::vector<u8>, __gnu_cxx::_S_single>;
inline VoiceBufferPtr MakeVoiceBuffer(size_t bytes)
{
    return std::__make_shared<std::vector<u8>, __gnu_cxx::_S_single>(bytes);
}
#else
using VoiceMutex = std::mutex;
using VoiceBufferPtr = std::shared_ptr<std::vector<u8>>;
inline VoiceBufferPtr MakeVoiceBuffer(size_t bytes)
{
    return std::make_shared<std::vector<u8>>(bytes);
}
#endif


enum SDLSoundBufferStatus
{
    Stopped = 0,
    Paused = 1,
    Playing = 2,
};

class SDLSoundBuffer final
{
public:
    SDLSoundBuffer(const SDLSoundBuffer& rhs);

    SDLSoundBuffer& operator=(const SDLSoundBuffer& rhs);

    SDLSoundBuffer();


    SDLSoundBuffer(const DSBUFFERDESC& bufferDesc, s32 soundSysFreq);


    HRESULT SetVolume(s32 volume);
    HRESULT Play(s32 /*reserved*/, s32 /*priority*/, s32 flags);
    HRESULT Stop();

    HRESULT SetFrequency(s32 frequency);
    HRESULT SetCurrentPosition(s32 position);
    HRESULT GetCurrentPosition(u32* readPos, u32* writePos);
    HRESULT GetFrequency(u32* freq);
    HRESULT SetPan(s32 pan);
    void Release();
    HRESULT GetStatus(u32* r);

    void Destroy();

    std::vector<u8>* GetBuffer();
    void Duplicate(SDLSoundBuffer** dupePtr);

public:
    struct AE_SDL_Voice_State final
    {
        s32 iVolume;
        bool bVolDirty;
        s32 iVolumeTarget;
        f32 fFrequency;
        s32 iPan;

        SDLSoundBufferStatus eStatus;
        bool bLoop;
        bool bIsReleased;
        f32 fPlaybackPosition;

        s32 iSampleCount;
        s32 iChannels;
        s32 iBlockAlign;
    };

    s32 mSoundSysFreq = 0;
    AE_SDL_Voice_State mState;
    VoiceBufferPtr mBuffer;
    VoiceMutex mLock;
};
