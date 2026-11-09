#include "stdafx.h"
#include "SDLSoundBuffer.hpp"

#define MAX_VOICE_COUNT 1024

// TODO: This probably shouldn't be a atomic as it could result in samples being leaked, still better than having torn pointer values I guess
std::atomic<SDLSoundBuffer*> sAE_ActiveVoices[MAX_VOICE_COUNT] = {};

#ifdef __PS2__
std::recursive_mutex& Ps2VoiceListLock()
{
    static std::recursive_mutex sLock;
    return sLock;
}
#endif

void AddVoiceToActiveList(SDLSoundBuffer* pVoice)
{
#ifdef __PS2__
    std::lock_guard<std::recursive_mutex> listLock(Ps2VoiceListLock());
#endif
    for (s32 i = 0; i < MAX_VOICE_COUNT; i++)
    {
        if (sAE_ActiveVoices[i].load() == nullptr)
        {
            sAE_ActiveVoices[i] = pVoice;
            return;
        }
    }

    LOG_ERROR("Failed to allocate voice! No space left");
}

void RemoveVoiceFromActiveList(SDLSoundBuffer* pVoice)
{
#ifdef __PS2__
    std::lock_guard<std::recursive_mutex> listLock(Ps2VoiceListLock());
#endif
    for (s32 i = 0; i < MAX_VOICE_COUNT; i++)
    {
        if (sAE_ActiveVoices[i] == pVoice)
        {
            sAE_ActiveVoices[i] = nullptr;
            return;
        }
    }

    LOG_ERROR("Could not find voice to remove");
}

SDLSoundBuffer::SDLSoundBuffer()
{
    mState.iVolume = 0;
    mState.iVolumeTarget = 127;
    mState.bVolDirty = false;
    mState.iPan = 0;
    mState.fFrequency = 1.0f;
    mState.bIsReleased = false;
    mState.bLoop = false;
    mState.iChannels = 1;
    mState.fPlaybackPosition = 0;
    mState.eStatus = SDLSoundBufferStatus::Stopped;

    AddVoiceToActiveList(this);
}

SDLSoundBuffer::SDLSoundBuffer(const DSBUFFERDESC& bufferDesc, s32 soundSysFreq)
    : mSoundSysFreq(soundSysFreq)
{
    mState.iVolume = 0;
    mState.iVolumeTarget = 127;
    mState.bVolDirty = false;
    mState.iPan = 0;
    mState.fFrequency = 1.0f;
    mState.bIsReleased = false;
    mState.bLoop = false;
    mState.iChannels = 1;
    mState.fPlaybackPosition = 0;
    mState.eStatus = SDLSoundBufferStatus::Stopped;


    mState.iSampleCount = bufferDesc.dwBufferBytes / 2;
    mBuffer = MakeVoiceBuffer(bufferDesc.dwBufferBytes);
    mState.iBlockAlign = bufferDesc.lpwfxFormat->nBlockAlign;
    mState.iChannels = bufferDesc.lpwfxFormat->nChannels;
#ifdef __PS2__
    // The PS2 mixes at 22 kHz, so a buffer that never gets SetFrequency() (FMV audio) must still
    // play at its own rate instead of 1:1 with the output.
    if (soundSysFreq > 0 && bufferDesc.lpwfxFormat->nSamplesPerSec > 0)
    {
        mState.fFrequency = static_cast<f32>(bufferDesc.lpwfxFormat->nSamplesPerSec) / static_cast<f32>(soundSysFreq);
    }
#endif

    AddVoiceToActiveList(this);
}


SDLSoundBuffer::SDLSoundBuffer(const SDLSoundBuffer& rhs)
{
    *this = rhs;
    AddVoiceToActiveList(this);
}

SDLSoundBuffer& SDLSoundBuffer::operator=(const SDLSoundBuffer& rhs)
{
    if (this != &rhs)
    {
        mSoundSysFreq = rhs.mSoundSysFreq;
        mState = rhs.mState;
        mBuffer = rhs.mBuffer;
    }
    return *this;
}

HRESULT SDLSoundBuffer::SetVolume(s32 volume)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.iVolumeTarget = volume;

    if (!mState.bVolDirty)
    {
        mState.iVolume = mState.iVolumeTarget;
    }

    mState.bVolDirty = true;

    return S_OK;
}

HRESULT SDLSoundBuffer::Play(s32, s32, s32 flags)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.fPlaybackPosition = 0;
    mState.eStatus = SDLSoundBufferStatus::Playing;

    if (flags & DSBPLAY_LOOPING)
    {
        mState.bLoop = true;
    }

    return S_OK;
}

HRESULT SDLSoundBuffer::Stop()
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.eStatus = SDLSoundBufferStatus::Stopped;
    return S_OK;
}

HRESULT SDLSoundBuffer::SetFrequency(s32 frequency)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.fFrequency = frequency / static_cast<f32>(mSoundSysFreq);
    return S_OK;
}

HRESULT SDLSoundBuffer::SetCurrentPosition(s32 position) // This offset is apparently in bytes
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.fPlaybackPosition = static_cast<f32>(position / mState.iBlockAlign);
    return S_OK;
}

HRESULT SDLSoundBuffer::GetCurrentPosition(u32* readPos, u32* writePos)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    *readPos = static_cast<u32>(mState.fPlaybackPosition * mState.iBlockAlign);
    *writePos = 0;

    return S_OK;
}

HRESULT SDLSoundBuffer::GetFrequency(u32* freq)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    *freq = static_cast<u32>(mState.fFrequency * mSoundSysFreq);
    return S_OK;
}

HRESULT SDLSoundBuffer::SetPan(s32 pan)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.iPan = pan;
    return S_OK;
}

void SDLSoundBuffer::Release()
{
#ifdef __PS2__
    // Delete right here on the game thread; RemoveVoiceFromActiveList takes the list lock, so the
    // mixer is never in the middle of using this voice when it goes away.
    RemoveVoiceFromActiveList(this);
    delete this;
    return;
#endif
    std::lock_guard<VoiceMutex> lock(mLock);

    mState.bLoop = false; // because the SDL copy paste of SND_Free_4EFA30 was doing this
    mState.bIsReleased = true;
}

HRESULT SDLSoundBuffer::GetStatus(u32* r)
{
    std::lock_guard<VoiceMutex> lock(mLock);

    if (mState.eStatus == SDLSoundBufferStatus::Playing)
    {
        *r |= DSBSTATUS_PLAYING;
    }
    if (mState.bLoop)
    {
        *r |= DSBSTATUS_LOOPING;
    }
    if (mState.bIsReleased)
    {
        *r |= DSBSTATUS_TERMINATED;
    }
    return S_OK;
}

void SDLSoundBuffer::Destroy()
{
    // remove self from global list and
    // decrement shared mem ptr to audio buffer
    {
        std::lock_guard<VoiceMutex> lock(mLock);

        RemoveVoiceFromActiveList(this);
    }
    delete this;
}

std::vector<u8>* SDLSoundBuffer::GetBuffer()
{
    return mBuffer.get();
}

void SDLSoundBuffer::Duplicate(SDLSoundBuffer** dupePtr)
{
    std::lock_guard<VoiceMutex> lock(mLock);
    *dupePtr = new SDLSoundBuffer(*this);
}
