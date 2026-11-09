#include "stdafx.h"
#include "SDLSoundSystem.hpp"
#include "SDLSoundBuffer.hpp"
#include "Reverb.hpp"
#include "Sys.hpp"
#include <cmath>

extern bool gLatencyHack;

void SDLSoundSystem::Init(u32 /*sampleRate*/, s32 /*bitsPerSample*/, s32 /*isStereo*/)
{
    mCreated = false;

#ifdef RELIVE_PS2_HOSTFS
    // TEMP diagnostic: start without audio when ps2noaudio.txt is present.
    if (FILE* marker = fopen("ps2noaudio.txt", "r"))
    {
        fclose(marker);
        LOG_INFO("Audio disabled by ps2noaudio.txt");
        return;
    }
#endif

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        LOG_ERROR("SDL_Init(SDL_INIT_AUDIO) failed " << SDL_GetError());
        return;
    }

    for (s32 i = 0; i < SDL_GetNumAudioDrivers(); i++)
    {
        LOG_INFO("SDL Audio Driver " << i << " " << SDL_GetAudioDriver(i));
    }

    mAudioDeviceSpec.format = SDL_AUDIO_S16;
    mAudioDeviceSpec.channels = 2;
#ifdef __PS2__
    // The game's samples are 22 kHz; mixing at 44.1 kHz doubled the EE cost for nothing.
    mAudioDeviceSpec.freq = 22050;
#else
    mAudioDeviceSpec.freq = 44100;
#endif

    mAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &mAudioDeviceSpec, SDLSoundSystem::AudioCallBackStatic, this);
    if (!mAudioStream)
    {
        LOG_ERROR("Couldn't open SDL audio: " << SDL_GetError());
        return;
    }

    mAudioDevice = SDL_GetAudioStreamDevice(mAudioStream);

    //SDL_SetAudioStreamGain(mAudioStream, 0.0f);

    LOG_INFO("-----------------------------");
    LOG_INFO("Audio Device opened, got specs:");
    LOG_INFO(
        "Channels: " << static_cast<s32>(mAudioDeviceSpec.channels) << " "
                     << "nFormat: " << mAudioDeviceSpec.format << " "
                     << "nFreq: " << mAudioDeviceSpec.freq);
    LOG_INFO("Driver: " << SDL_GetCurrentAudioDriver());
    LOG_INFO("-----------------------------");

    Reverb_Init(mAudioDeviceSpec.freq);

    GetSoundAPI().SND_InitVolumeTable();

    if (sLoadedSoundsCount_BBC394)
    {
        for (s32 i = 0; i < 256; i++)
        {
            if (sSoundSamples_BBBF38[i])
            {
                GetSoundAPI().SND_Renew(sSoundSamples_BBBF38[i]);
                GetSoundAPI().SND_LoadSamples(sSoundSamples_BBBF38[i], 0, sSoundSamples_BBBF38[i]->field_8_pSoundBuffer, sSoundSamples_BBBF38[i]->field_C_buffer_size_bytes / (u8) sSoundSamples_BBBF38[i]->field_1D_blockAlign);
                if ((i + 1) == sLoadedSoundsCount_BBC394)
                    break;
            }
        }
    }

    sLastNotePlayTime_BBC33C = SYS_GetTicks();
    mCreated = true;

    // Correctly size the lock free buffer on the main thread before any other threads start
    mAudioRingBuffer.resize(2048 * 2);

    // TODO: Test just running this on the main thread
    if (!gLatencyHack)
    {
        mRenderAudioThread.reset(new std::thread(std::bind(&SDLSoundSystem::RenderAudioThread, this)));
    }

    SDL_ResumeAudioDevice(mAudioDevice);
}


HRESULT SDLSoundSystem::DuplicateSoundBuffer(TSoundBufferType* pDSBufferOriginal, TSoundBufferType** ppDSBufferDuplicate)
{
    pDSBufferOriginal->Duplicate(ppDSBufferDuplicate);
    return S_OK;
}

HRESULT SDLSoundSystem::CreateSoundBuffer(LPCDSBUFFERDESC pcDSBufferDesc, TSoundBufferType** ppDSBuffer, void* /*pUnkOuter*/)
{
    *ppDSBuffer = new SDLSoundBuffer(*pcDSBufferDesc, mAudioDeviceSpec.freq);
    return S_OK;
}


HRESULT SDLSoundSystem::Release()
{
    TRACE_ENTRYEXIT;

    if (mCreated)
    {
        // Stop the audio call back
        SDL_PauseAudioStreamDevice(mAudioStream);

        // Stop audio rendering thread
        mRenderAudioThreadQuit = true;
        if (mRenderAudioThread && mRenderAudioThread->joinable())
        {
            mRenderAudioThread->join();
        }
    }

    if (mAudioStream)
    {
        SDL_DestroyAudioStream(mAudioStream);
        mAudioStream = nullptr;
        mAudioDevice = 0;
    }

    // Shutdown the sound system
    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    delete this;
    return S_OK;
}


SDLSoundSystem::~SDLSoundSystem()
{
    TRACE_ENTRYEXIT;

    Reverb_DeInit();

    // TODO: Clean up outstanding samples in sAE_ActiveVoices
}

void SDLSoundSystem::AudioCallBack(SDL_AudioStream* stream, s32 additionalAmount)
{
    if (additionalAmount <= 0)
    {
        return;
    }

    const s32 bufferLenSamples = additionalAmount / sizeof(StereoSample_S16);
#ifdef __PS2__
    if (static_cast<s32>(mPs2Out.size()) < bufferLenSamples)
    {
        mPs2Out.resize(bufferLenSamples);
    }
    #ifdef RELIVE_PS2_HOSTFS
    // TEMP diagnostic: ps2nomix.txt -> output silence without touching any voice.
    static const bool sNoMix = []()
    {
        FILE* f = fopen("ps2nomix.txt", "r");
        if (f)
        {
            fclose(f);
        }
        return f != nullptr;
    }();
    if (sNoMix)
    {
        memset(mPs2Out.data(), 0, bufferLenSamples * sizeof(StereoSample_S16));
    }
    else
    #endif
    {
        RenderAudioPs2(mPs2Out.data(), bufferLenSamples);
    }
    SDL_PutAudioStreamData(stream, mPs2Out.data(), bufferLenSamples * sizeof(StereoSample_S16));
    #if defined(RELIVE_PS2_HOSTFS) || defined(RELIVE_PS2_DISCTRACE)
    static u32 sBytes = 0;
    static u32 sCalls = 0;
    sBytes += static_cast<u32>(additionalAmount);
    sCalls++;
    if (sBytes >= static_cast<u32>(mAudioDeviceSpec.freq) * 4u * 5u)
    {
        LOG_INFO("Audio: " << sCalls << " callbacks, " << sBytes << " bytes (5 s of sound) at " << SDL_GetTicks() << " ms");
        sBytes = 0;
        sCalls = 0;
    }
    #endif
    return;
#endif
    std::vector<StereoSample_S16> buffer(bufferLenSamples);

    if (gLatencyHack)
    {
        // Calculate the audio in the callback instead of another thread with a busy loop to reduce CPU usage
        // this will probably cause audio glitching in a lot of cases
        RenderAudio(buffer.data(), bufferLenSamples);
    }
    else
    {
        const s32 readAvilSamples = static_cast<s32>(mAudioRingBuffer.getAvailableRead());
        if (readAvilSamples > 0 && readAvilSamples < bufferLenSamples)
        {
            LOG_WARNING("Audio buffer underflow!");
        }

        if (!mAudioRingBuffer.read(buffer.data(), bufferLenSamples))
        {
            LOG_ERROR("Ring buffer read failure!");
        }
    }

    SDL_PutAudioStreamData(stream, buffer.data(), additionalAmount);

}


#ifdef __PS2__
void SDLSoundSystem::RenderAudioPs2(StereoSample_S16* pOut, s32 count)
{
    std::lock_guard<std::recursive_mutex> listLock(Ps2VoiceListLock());
    if (static_cast<s32>(mPs2Mix.size()) < count * 2)
    {
        mPs2Mix.resize(count * 2);
    }
    s32* pMix = mPs2Mix.data();
    memset(pMix, 0, count * 2 * sizeof(s32));

    // Same overall gain as the PC mixer: sample * pan/10000 * volume/127 * 0.35, as a Q15 factor.
    auto gainFor = [](s32 pan, s32 volume) -> s32
    {
        return static_cast<s32>((static_cast<s64>(pan) * volume * 11469) / (10000 * 127));
    };

    for (s32 vi = 0; vi < MAX_VOICE_COUNT; vi++)
    {
        SDLSoundBuffer* pVoice = sAE_ActiveVoices[vi];
        if (!pVoice || !pVoice->mBuffer || pVoice->mBuffer->empty())
        {
            continue;
        }

        // Voices are deleted by the game thread (SDLSoundBuffer::Release), never here.
        auto& st = pVoice->mState;
        if (st.eStatus != SDLSoundBufferStatus::Playing || st.iSampleCount == 0)
        {
            continue;
        }

        const s16* pSamples = reinterpret_cast<const s16*>(pVoice->GetBuffer()->data());
        const u32 frames = static_cast<u32>(st.iSampleCount / st.iChannels);
        const u32 endPos = frames << 16;
        u32 pos = static_cast<u32>(st.fPlaybackPosition * 65536.0f);
        const u32 step = static_cast<u32>(st.fFrequency * 65536.0f);

        s32 leftPan = 10000;
        s32 rightPan = 10000;
        if (st.iChannels == 1 && gAudioStereo)
        {
            if (st.iPan < 0)
            {
                rightPan = 10000 - abs(st.iPan);
            }
            else if (st.iPan > 0)
            {
                leftPan = 10000 - abs(st.iPan);
            }
        }
        s32 gainL = gainFor(leftPan, st.iVolume);
        s32 gainR = gainFor(rightPan, st.iVolume);

        for (s32 i = 0; i < count; i++)
        {
            if (st.iVolume != st.iVolumeTarget)
            {
                st.iVolume += st.iVolume < st.iVolumeTarget ? 1 : -1;
                gainL = gainFor(leftPan, st.iVolume);
                gainR = gainFor(rightPan, st.iVolume);
            }

            const u32 frame = pos >> 16;
            s32 l;
            s32 r;
            if (st.iChannels == 2)
            {
                l = pSamples[frame * 2];
                r = pSamples[frame * 2 + 1];
            }
            else
            {
                l = r = pSamples[frame];
            }
            pMix[i * 2] += (l * gainL) >> 15;
            pMix[i * 2 + 1] += (r * gainR) >> 15;

            pos += step;
            if (pos >= endPos)
            {
                pos = 0;
                if (!st.bLoop)
                {
                    st.eStatus = SDLSoundBufferStatus::Stopped;
                    break;
                }
            }
        }
        st.fPlaybackPosition = static_cast<f32>(pos) / 65536.0f;
    }

    for (s32 i = 0; i < count; i++)
    {
        const s32 l = pMix[i * 2];
        const s32 r = pMix[i * 2 + 1];
        pOut[i].left = static_cast<s16>(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
        pOut[i].right = static_cast<s16>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
    }
}
#endif

void SDLSoundSystem::RenderAudioThread()
{
    std::vector<StereoSample_S16> tmpBuffer;
    while (!mRenderAudioThreadQuit)
    {
        const size_t bufferSize = mAudioRingBuffer.getAvailableWrite();
        if (bufferSize > 0)
        {
            tmpBuffer.resize(bufferSize);
            memset(tmpBuffer.data(), 0, tmpBuffer.size() * sizeof(StereoSample_S16));
            RenderAudio(tmpBuffer.data(), static_cast<s32>(tmpBuffer.size()));
            if (!mAudioRingBuffer.write(tmpBuffer.data(), tmpBuffer.size()))
            {
                // Couldn't write all the data, should never happen ??
                LOG_ERROR("Ring buffer write failed");
            }
        }
    }

}

void SDLSoundSystem::RenderAudio(StereoSample_S16* pSampleBuffer, s32 sampleBufferCount)
{
    // Check if our buffer size changes, and if its buffer, then resize the array
    if (sampleBufferCount > mCurrentSoundBufferSize)
    {
        mTempSoundBuffer.resize(sampleBufferCount);
        mNoReverbBuffer.resize(sampleBufferCount);
        mCurrentSoundBufferSize = sampleBufferCount;
    }

    if (mTempSoundBuffer.empty())
    {
        mTempSoundBuffer.resize(sampleBufferCount);
        mNoReverbBuffer.resize(sampleBufferCount);
    }

    memset(mNoReverbBuffer.data(), 0, sampleBufferCount * sizeof(StereoSample_S16));

    for (s32 vi = 0; vi < MAX_VOICE_COUNT; vi++)
    {
        SDLSoundBuffer* pVoice = sAE_ActiveVoices[vi];
        if (pVoice)
        {
            RenderSoundBuffer(*pVoice, pSampleBuffer, sampleBufferCount);
        }
    }

    // Do Reverb Pass
    if (gReverbEnabled)
    {
        Reverb_Mix(pSampleBuffer, SDL_AUDIO_S16, sampleBufferCount * sizeof(StereoSample_S16), kMixVolume);

        // Mix our no reverb buffer
        SDL_MixAudio(reinterpret_cast<Uint8*>(pSampleBuffer), reinterpret_cast<Uint8*>(mNoReverbBuffer.data()), SDL_AUDIO_S16, sampleBufferCount * sizeof(StereoSample_S16), kMixVolume);
    }
}


void SDLSoundSystem::RenderSoundBuffer(SDLSoundBuffer& entry, StereoSample_S16* pSampleBuffer, s32 sampleBufferCount)
{
    bool reverbPass = false;

    SDLSoundBuffer* pVoice = &entry;

    if (pVoice == nullptr || !pVoice->mBuffer || pVoice->mBuffer->empty())
    {
        return;
    }

    if (pVoice->mState.bIsReleased)
    {
        pVoice->Destroy(); // TODO: Still correct ??
        return;
    }

    // Clear Temp Sample Buffer
    memset(mTempSoundBuffer.data(), 0, sampleBufferCount * sizeof(StereoSample_S16));

    Sint16* pVoiceBufferPtr = reinterpret_cast<Sint16*>(pVoice->GetBuffer()->data());

    for (s32 i = 0; i < sampleBufferCount; i++)
    {
        if (pVoice->mBuffer->empty() || pVoice->mState.eStatus != SDLSoundBufferStatus::Playing || pVoice->mState.iSampleCount == 0)
        {
            break;
        }

        if (pVoice->mState.iVolume < pVoice->mState.iVolumeTarget)
        {
            pVoice->mState.iVolume++;
        }
        else if (pVoice->mState.iVolume > pVoice->mState.iVolumeTarget)
        {
            pVoice->mState.iVolume--;
        }

        if (pVoice->mState.iChannels == 2)
        {
            reverbPass = true; // Todo: determine this with flags in the sound object itself.
                               // For Stereo buffers. The only time this is played is for FMV's.
                               // Right now, unless the playback device is at 44100 hz, it sounds awful.
                               // TODO: Resampling for stereo

            RenderStereoSample(pVoiceBufferPtr, pVoice, i);
        }
        else
        {
            reverbPass = true;

            RenderMonoSample(pVoiceBufferPtr, pVoice, i);
        }

        if (pVoice->mState.fPlaybackPosition >= pVoice->mState.iSampleCount / pVoice->mState.iChannels)
        {
            pVoice->mState.fPlaybackPosition = 0;
            if (!pVoice->mState.bLoop)
            {
                pVoice->mState.eStatus = SDLSoundBufferStatus::Stopped;
            }
        }
    }

    if (reverbPass)
    {
        SDL_MixAudio(reinterpret_cast<Uint8*>(pSampleBuffer), reinterpret_cast<Uint8*>(mTempSoundBuffer.data()), SDL_AUDIO_S16, sampleBufferCount * sizeof(StereoSample_S16), 0.35f);
    }
    else
    {
        SDL_MixAudio(reinterpret_cast<Uint8*>(mNoReverbBuffer.data()), reinterpret_cast<Uint8*>(mTempSoundBuffer.data()), SDL_AUDIO_S16, sampleBufferCount * sizeof(StereoSample_S16), 0.35f);
    }
}


void SDLSoundSystem::RenderMonoSample(Sint16* pVoiceBufferPtr, SDLSoundBuffer* pVoice, s32 i)
{
    s32 s = 0;

    switch (mAudioFilterMode)
    {
        case AudioFilterMode::NoFilter:
            s = pVoiceBufferPtr[static_cast<s32>(pVoice->mState.fPlaybackPosition)];
            break;
        case AudioFilterMode::Linear:
            const s16 s1 = pVoiceBufferPtr[static_cast<s32>(pVoice->mState.fPlaybackPosition)];
            const s16 s2 = pVoiceBufferPtr[(static_cast<s32>(pVoice->mState.fPlaybackPosition) + 1) % pVoice->mState.iSampleCount];

            s = static_cast<s32>((s1 + ((s2 - s1) * (pVoice->mState.fPlaybackPosition - floorf(pVoice->mState.fPlaybackPosition)))));
            break;
    }

    s32 leftPan = 10000;
    s32 rightPan = 10000;

    if (gAudioStereo)
    {
        if (pVoice->mState.iPan < 0)
        {
            rightPan = 10000 - abs(pVoice->mState.iPan);
        }
        else if (pVoice->mState.iPan > 0)
        {
            leftPan = 10000 - abs(pVoice->mState.iPan);
        }
    }
    else
    {
        s32 r = (leftPan + rightPan) / 2;
        leftPan = r;
        rightPan = r;
    }

    mTempSoundBuffer[i].left = static_cast<s16>((((s * leftPan) / 10000) * pVoice->mState.iVolume) / 127);
    mTempSoundBuffer[i].right = static_cast<s16>((((s * rightPan) / 10000) * pVoice->mState.iVolume) / 127);

    pVoice->mState.fPlaybackPosition += pVoice->mState.fFrequency;
}

void SDLSoundSystem::RenderStereoSample(Sint16* pVoiceBufferPtr, SDLSoundBuffer* pVoice, s32 i)
{
    StereoSample_S16 pSample = reinterpret_cast<StereoSample_S16*>(pVoiceBufferPtr)[static_cast<s32>(pVoice->mState.fPlaybackPosition)];
    mTempSoundBuffer[i].left = static_cast<s16>((pSample.left * pVoice->mState.iVolume) / 127);
    mTempSoundBuffer[i].right = static_cast<s16>((pSample.right * pVoice->mState.iVolume) / 127);

    pVoice->mState.fPlaybackPosition += pVoice->mState.fFrequency;
}

void SDLSoundSystem::AudioCallBackStatic(void* userdata, SDL_AudioStream* stream, s32 additionalAmount, s32 /*totalAmount*/)
{
    static_cast<SDLSoundSystem*>(userdata)->AudioCallBack(stream, additionalAmount);
}
