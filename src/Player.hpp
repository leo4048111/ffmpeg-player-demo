#ifndef _MY_PLAYER_H
#define MY_PLAYER_H

#include <Windows.h>

#ifdef AVPLAYER_EXPORTS
#define FFPLAYWRAPPER_API __declspec(dllexport)
#else
#define FFPLAYWRAPPER_API __declspec(dllimport)
#endif

typedef struct CMediaInfo
{
    int width;
    int height;
    long long duration;
    int samplerate;
    int channels;
} CMediaInfo;

typedef enum EPlayState
{
    PlayState_Error = -1,
    PlayState_Stopped = 0,
    PlayState_Opening,
    PlayState_Opened,
    PlayState_Playing,
    PlayState_Paused,
    PlayState_End,
    PlayState_NetError,
    PlayState_Last
} EPlayState;

typedef void *HPLAYER;

typedef void(WINAPI *fnLogCallBack)(char *logText);

typedef void(WINAPI *fnVideoFormatCallBack)(HPLAYER player, int videoWidth, int videoHeight, int pixelFormat);
typedef void(WINAPI *fnVideoCallBack)(HPLAYER player, int width, int height, byte *planeY, int lineY, byte *planeU, int lineU, byte *planeV, int lineV);

typedef void(WINAPI *fnAudioFormatCallBack)(HPLAYER player, int sampleFormat, int sampleRate, int channels);
typedef void(WINAPI *fnAudioCallBack)(HPLAYER player, byte *audioData, int length);
typedef void(WINAPI *fnAudioMeterCallBack)(HPLAYER player, float left, float right);

typedef void(WINAPI *fnPlayPosCallBack)(HPLAYER player, long curTimeMs, long totalDurationMs, double syncDiff, int vq, int aq);
typedef void(WINAPI *fnPlayStateCallBack)(HPLAYER player, EPlayState playState);

typedef void(WINAPI *fnRawPacketCallBack)(HPLAYER player, AVPacket *packet, int isVideo, int timebase_num, int timebase_den);

extern "C"
{
    FFPLAYWRAPPER_API void PL_SetLogCallback(int logLevel, fnLogCallBack callBack);

    FFPLAYWRAPPER_API int PL_GetMediaInfo(char *url, CMediaInfo *pMediaInfo, char *thumbPathName, int thumbWidth, char *starttime);

    FFPLAYWRAPPER_API void PL_InitRuntime();
    FFPLAYWRAPPER_API void PL_DestoryRuntime();

    FFPLAYWRAPPER_API HPLAYER PL_New(HWND videoContainer, char *audioDeviceName);

    FFPLAYWRAPPER_API int PL_Destory(HPLAYER player);

    FFPLAYWRAPPER_API void PL_SetPlayMode(HPLAYER player, int mode /*0-Video and Audio 1-Video only 2-Audio only*/);

    FFPLAYWRAPPER_API int PL_InitPlayer(HPLAYER player, int width, int height);

    FFPLAYWRAPPER_API int PL_Play(HPLAYER player, char *url, char *offsetTimeStamp, int staticFrame);

    FFPLAYWRAPPER_API int PL_Pause(HPLAYER player);

    FFPLAYWRAPPER_API int PL_Continue(HPLAYER player);

    FFPLAYWRAPPER_API int PL_Stop(HPLAYER player);

    FFPLAYWRAPPER_API int PL_StepFrames(HPLAYER player, long long frames);

    FFPLAYWRAPPER_API int PL_SetStepPlay(HPLAYER player, int stepPlay);

    FFPLAYWRAPPER_API int PL_SetPosition(HPLAYER player, long long mseconds);

    FFPLAYWRAPPER_API int PL_ClearBuffer(HPLAYER player);

    FFPLAYWRAPPER_API long long PL_GetPosition(HPLAYER player);

    FFPLAYWRAPPER_API int PL_SetVolume(HPLAYER player, int volume);

    FFPLAYWRAPPER_API int PL_GetVolume(HPLAYER player);

    FFPLAYWRAPPER_API int PL_SetAutoStopped(HPLAYER player, int autoStopped);

    FFPLAYWRAPPER_API EPlayState PL_GetPlayState(HPLAYER player);

    FFPLAYWRAPPER_API int PL_SetAudioOutputFormat(HPLAYER player, int channels, int sampleRate);

    FFPLAYWRAPPER_API int PL_SetVideoKeepRatio(HPLAYER player, int keepRatio);

    FFPLAYWRAPPER_API int PL_SetVideoFormatCallback(HPLAYER player, fnVideoFormatCallBack callBack);
    FFPLAYWRAPPER_API int PL_SetVideoFrameCallback(HPLAYER player, fnVideoCallBack callBack);

    // FFPLAYWRAPPER_API int PL_SetAudioFormatCallback(HPLAYER player, fnAudioFormatCallBack callBack);
    FFPLAYWRAPPER_API int PL_SetAudioFrameCallback(HPLAYER player, fnAudioCallBack callBack);
    FFPLAYWRAPPER_API int PL_SetAudioMeterCallback(HPLAYER player, fnAudioMeterCallBack callBack);

    FFPLAYWRAPPER_API int PL_SetPlayPositionCallback(HPLAYER player, fnPlayPosCallBack callBack);

    FFPLAYWRAPPER_API int PL_SetPlayStateCallback(HPLAYER player, fnPlayStateCallBack callBack);
}

#endif