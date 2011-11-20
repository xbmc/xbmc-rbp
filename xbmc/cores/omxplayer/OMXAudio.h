/*
* XBMC Media Center
* Copyright (c) 2002 d7o3g4q and RUNTiME
* Portions Copyright (c) by the authors of ffmpeg and xvid
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

//////////////////////////////////////////////////////////////////////

#ifndef __OPENMAXAUDIORENDER_H__
#define __OPENMAXAUDIORENDER_H__

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

//#define STANDALONE

#ifdef STANDALONE
#include "IAudioRenderer.h"
#else
#include "../AudioRenderers/IAudioRenderer.h"
#endif
#include "cores/IAudioCallback.h"
#include "linux/PlatformDefs.h"
#include "DllAvCodec.h"
#include "OMXCore.h"
#include "OMXClock.h"

#define AUDIO_BUFFER_SECONDS 3

extern void RegisterAudioCallback(IAudioCallback* pCallback);
extern void UnRegisterAudioCallback();

class COMXAudio : public IAudioRenderer
{
public:
  virtual void UnRegisterAudioCallback();
  virtual void RegisterAudioCallback(IAudioCallback* pCallback);
  virtual unsigned int GetChunkLen();
  virtual float GetDelay();
  virtual float GetCacheTime();
  virtual float GetCacheTotal();
  COMXAudio();
  virtual bool Initialize(IAudioCallback* pCallback, const CStdString& device, int iChannels, enum PCMChannels *channelMap, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, bool bIsMusic=false, bool bPassthrough = false);
  virtual ~COMXAudio();

  virtual unsigned int AddPackets(const void* data, unsigned int len);
  virtual unsigned int AddPackets(const void* data, unsigned int len, int64_t dts, int64_t pts);
  virtual unsigned int GetSpace();
  virtual bool Deinitialize();
  virtual bool Pause();
  virtual bool Stop();
  virtual bool Resume();

  virtual long GetCurrentVolume() const;
  virtual void Mute(bool bMute);
  virtual bool SetCurrentVolume(long nVolume);
  virtual void SetDynamicRangeCompression(long drc) { m_drc = drc; }
  virtual int SetPlaySpeed(int iSpeed);
  virtual void WaitCompletion();
  virtual void SwitchChannels(int iAudioStream, bool bAudioOnAllSpeakers);

  virtual void Flush();
  static void EnumerateAudioSinks(AudioSinkList& vAudioSinks, bool passthrough);

  void Process();

  bool SetClock(OMXClock *clock);
  void SetCodingType(CodecID codec);

  void PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm);
  void PrintDDP(OMX_AUDIO_PARAM_DDPTYPE *ddparm);
  void PrintDTS(OMX_AUDIO_PARAM_DTSTYPE *dtsparam);

private:
  IAudioCallback* m_pCallback;
  bool          m_Initialized;
  bool          m_Pause;
  bool          m_CanPause;
  long          m_CurrentVolume;
  long          m_drc;
  bool          m_Passthrough;
  unsigned int  m_BytesPerSec;
  unsigned int  m_BufferLen;
  unsigned int  m_ChunkLen;
  unsigned int  m_DataChannels;
  unsigned int  m_Channels;
  unsigned int  m_BitsPerSample;
  COMXCoreComponent *m_omx_clock;
  OMXClock       *m_av_clock;
  bool          m_external_clock;
  bool          m_setStartTime;
  int           m_SampleSize;
  bool          m_firstFrame;
  OMX_AUDIO_CODINGTYPE m_eEncoding;
  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm;
  WAVEFORMATEXTENSIBLE        m_wave_header;

protected:
  COMXCoreComponent m_omx_render;
  COMXCoreComponent m_omx_decoder;
  COMXCoreTunel     m_omx_tunnel_clock;
  COMXCoreTunel     m_omx_tunnel_decoder;
  COMXCore          m_OMX;
};
#endif

