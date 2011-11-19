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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXAudio.h"
#include "utils/log.h"

#define CLASSNAME "COMXAudio"

#include "linux/XMemUtils.h"

#ifndef STANDALONE
#include "guilib/AudioContext.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "guilib/LocalizeStrings.h"
#endif

#ifndef VOLUME_MINIMUM
#define VOLUME_MINIMUM -6000  // -60dB
#endif

using namespace std;

OMX_API OMX_ERRORTYPE OMX_APIENTRY vc_OMX_Init(void);

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//***********************************************************************************************
COMXAudio::COMXAudio() :
  m_Initialized     (false  ),
  m_Pause           (false  ),
  m_CanPause        (false  ),
  m_CurrentVolume   (0      ),
  m_Passthrough     (false  ),
  m_BytesPerSec     (0      ),
  m_BufferLen       (0      ),
  m_ChunkLen        (0      ),
  m_DataChannels    (0      ),
  m_Channels        (0      ),
  m_BitsPerSample   (0      ),
  m_omx_clock       (NULL   ),
  m_av_clock        (NULL   ),
  m_external_clock  (false  ),
  m_setStartTime    (false  ),
  m_SampleSize      (0      )
{
}

COMXAudio::~COMXAudio()
{
  if(m_Initialized)
    Deinitialize();
}

bool COMXAudio::Initialize(IAudioCallback* pCallback, const CStdString& device, int iChannels, enum PCMChannels *channelMap, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, bool bIsMusic, bool bPassthrough)
{
  CStdString deviceuse;
  if(device == "hdmi") {
    deviceuse = "hdmi";
  } else {
    deviceuse = "local";
  }

  m_Passthrough = bPassthrough;
  m_drc         = 0;

  static enum PCMChannels OMXChannelMap[8] =
  {
    PCM_FRONT_LEFT  , PCM_FRONT_RIGHT  ,
    PCM_BACK_LEFT   , PCM_BACK_RIGHT   ,
    PCM_FRONT_CENTER, PCM_LOW_FREQUENCY,
    PCM_SIDE_LEFT   , PCM_SIDE_RIGHT
  };

  static enum OMX_AUDIO_CHANNELTYPE OMXChannels[8] =
  {
    OMX_AUDIO_ChannelLF, OMX_AUDIO_ChannelRF ,
    OMX_AUDIO_ChannelLR, OMX_AUDIO_ChannelRR ,
    OMX_AUDIO_ChannelCF, OMX_AUDIO_ChannelLFE,
    OMX_AUDIO_ChannelLS, OMX_AUDIO_ChannelRS
  };

#ifndef STANDALONE
  bool bAudioOnAllSpeakers(false);
  g_audioContext.SetupSpeakerConfig(iChannels, bAudioOnAllSpeakers, bIsMusic);

  if(bPassthrough)
  {
    g_audioContext.SetActiveDevice(CAudioContext::DIRECTSOUND_DEVICE_DIGITAL);
  } else {
    g_audioContext.SetActiveDevice(CAudioContext::DIRECTSOUND_DEVICE);
  }

  m_CurrentVolume = g_settings.m_nVolumeLevel; 
#else
  m_CurrentVolume = 0;
#endif

  m_DataChannels = iChannels;
  m_remap.Reset();

  OMX_AUDIO_PARAM_PCMMODETYPE m_pcm;
  OMX_INIT_STRUCTURE(m_pcm);
  m_Channels = 2;
  m_pcm.nChannels = m_Channels;
  m_pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
  m_pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;
  m_pcm.eChannelMapping[2] = OMX_AUDIO_ChannelMax;

  if (!m_Passthrough && channelMap)
  {
    enum PCMChannels *outLayout;

    // set the input format, and get the channel layout so we know what we need to open
    outLayout = m_remap.SetInputFormat (iChannels, channelMap, uiBitsPerSample / 8, uiSamplesPerSec);

    unsigned int outChannels = 0;
    unsigned int ch = 0, map;
    unsigned int chan = 0;
    while(outLayout[ch] != PCM_INVALID)
    {
      for(map = 0; map < 8; ++map)
      {
        if (outLayout[ch] == OMXChannelMap[map])
        {
          m_pcm.eChannelMapping[chan] = OMXChannels[map]; 
          m_pcm.eChannelMapping[chan + 1] = OMX_AUDIO_ChannelMax;
          chan++;
          if (map > outChannels)
            outChannels = map;
          break;
        }
      }
      ++ch;
    }

    m_remap.SetOutputFormat(++outChannels, OMXChannelMap);
    if (m_remap.CanRemap())
    {
      iChannels = outChannels;
      if (m_DataChannels != (unsigned int)iChannels)
        CLog::Log(LOGDEBUG, "COMXAudio:::Initialize Requested channels changed from %i to %i", m_DataChannels, iChannels);
    }

  }

  m_Channels = iChannels;

  // set the m_pcm parameters
  m_pcm.eNumData            = OMX_NumericalDataSigned;
  m_pcm.eEndian             = OMX_EndianLittle;
  m_pcm.bInterleaved        = OMX_TRUE;
  m_pcm.nBitPerSample       = uiBitsPerSample;
  m_pcm.ePCMMode            = OMX_AUDIO_PCMModeLinear;
  m_pcm.nChannels           = m_Channels;
  m_pcm.nSamplingRate       = uiSamplesPerSec;

  m_SampleSize              = (m_pcm.nChannels * m_pcm.nBitPerSample * m_pcm.nSamplingRate)>>3;

  PrintPCM(&m_pcm);

  /* no xternal clock. we got initialized outside omxplaer */
  if(m_av_clock == NULL)
    m_OMX.Initialize();

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  CStdString componentName = "";

  componentName = "OMX.broadcom.audio_render";
  if(!m_omx_render.Initialize((const CStdString)componentName, OMX_IndexParamAudioInit))
    return false;

  if(m_av_clock == NULL)
  {
    /* no external clock set. generate one */
    m_external_clock = false;

    m_av_clock = new OMXClock();
    
    if(!m_av_clock->Initialize())
    {
      delete m_av_clock;
      m_av_clock = NULL;
      CLog::Log(LOGERROR, "COMXAudio::Initialize error creating av clock\n");
      return false;
    }
  }

  m_omx_clock = m_av_clock->GetOMXClock();

  m_omx_tunnel_clock.Initialize(m_omx_clock, m_omx_clock->GetInputPort(), &m_omx_render, m_omx_render.GetOutputPort());

  omx_err = m_omx_tunnel_clock.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize m_omx_tunnel_clock.Establish\n");
    return false;
  }

  if(!m_external_clock)
  {
    omx_err = m_omx_clock->SetStateForComponent(OMX_StateExecuting);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudio::Initialize m_omx_clock.SetStateForComponent\n");
      return false;
    }
  }

  OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
  OMX_INIT_STRUCTURE(audioDest);
  strncpy((char *)audioDest.sName, device.c_str(), strlen(device.c_str()));

  omx_err = m_omx_render.SetConfig(OMX_IndexConfigBrcmAudioDestination, &audioDest);
  if (omx_err != OMX_ErrorNone)
    return false;

  m_BitsPerSample = uiBitsPerSample;
  m_BufferLen     = m_BytesPerSec = uiSamplesPerSec * (uiBitsPerSample >> 3) * iChannels;
  m_BufferLen     *= AUDIO_BUFFER_SECONDS;
  //m_ChunkLen      = 6144; //1024 * iChannels * (uiBitsPerSample >> 3);
  m_ChunkLen      = 2048;

  // set up the number/size of buffers
  OMX_PARAM_PORTDEFINITIONTYPE port_param;
  OMX_INIT_STRUCTURE(port_param);
  port_param.nPortIndex = m_omx_render.GetInputPort();

  omx_err = m_omx_render.GetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  port_param.nPortIndex = m_omx_render.GetInputPort();
  port_param.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

  port_param.nBufferSize = m_ChunkLen;
  port_param.nBufferCountActual = m_BufferLen / m_ChunkLen;

  omx_err = m_omx_render.SetParameter(OMX_IndexParamPortDefinition, &port_param);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
    return false;
  }

  OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = m_omx_render.GetInputPort();

  formatType.eEncoding = OMX_AUDIO_CodingPCM;
  omx_err = m_omx_render.SetParameter(OMX_IndexParamAudioPortFormat, &formatType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize error OMX_IndexParamAudioPortFormat omx_err(0x%08x)\n", omx_err);
    return false;
  }

  m_pcm.nPortIndex          = m_omx_render.GetInputPort();
  omx_err = m_omx_render.SetParameter(OMX_IndexParamAudioPcm, &m_pcm);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize OMX_IndexParamAudioPcm omx_err(0x%08x)\n", omx_err);
    return false;
  }

  omx_err = m_omx_render.AllocInputBuffers();
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "COMXAudio::Initialize - Error alloc buffers 0x%08x", omx_err);
    return false;
  }

  omx_err = m_omx_render.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "COMXAudio::Initialize - Error setting OMX_StateExecuting 0x%08x", omx_err);
    return false;
  }

  m_Initialized   = true;
  m_setStartTime  = true;

  SetCurrentVolume(m_CurrentVolume);

  CLog::Log(LOGDEBUG, "COMXAudio::Initialize bps %d samplerate %d channels %d device %s buffer size %d bytes per second %d", 
      (int)m_pcm.nBitPerSample, (int)m_pcm.nSamplingRate, (int)m_pcm.nChannels, deviceuse.c_str(), m_BufferLen, m_BytesPerSec);

  return true;
}

//***********************************************************************************************
bool COMXAudio::Deinitialize()
{
  if(!m_Initialized)
    return true;

  if(!m_external_clock && m_av_clock != NULL)
  {
    m_av_clock->Pause();
  }

  m_omx_tunnel_clock.Deestablish();

  m_omx_render.Deinitialize();

  m_Initialized = false;
  m_BytesPerSec = 0;
  m_BufferLen   = 0;

  if(!m_external_clock && m_av_clock != NULL)
  {
    delete m_av_clock;
    m_av_clock  = NULL;
    m_external_clock = false;

    /* not initialized in omxplayer */
    m_OMX.Deinitialize();
  }

  m_omx_clock = NULL;
  m_av_clock  = NULL;

  m_Initialized = false;
  return true;
}

void COMXAudio::Flush()
{
  if(!m_Initialized)
    return;

  m_omx_render.FlushInput();

  m_omx_tunnel_clock.Flush();

  m_setStartTime = true;
}

//***********************************************************************************************
bool COMXAudio::Pause()
{
  if (!m_Initialized)
     return -1;

  if(m_Pause) return true;
  m_Pause = true;

  m_omx_render.SetStateForComponent(OMX_StatePause);

  return true;
}

//***********************************************************************************************
bool COMXAudio::Resume()
{
  if (!m_Initialized)
     return -1;

  if(!m_Pause) return true;
  m_Pause = false;

  m_omx_render.SetStateForComponent(OMX_StateExecuting);

  return true;
}

//***********************************************************************************************
bool COMXAudio::Stop()
{
  if (!m_Initialized)
     return -1;

  Flush();

  m_Pause = false;

  return true;
}

//***********************************************************************************************
long COMXAudio::GetCurrentVolume() const
{
  return m_CurrentVolume;
}

//***********************************************************************************************
void COMXAudio::Mute(bool bMute)
{
  if(!m_Initialized)
    return;

  if (bMute)
    SetCurrentVolume(VOLUME_MINIMUM);
  else
    SetCurrentVolume(m_CurrentVolume);
}

//***********************************************************************************************
bool COMXAudio::SetCurrentVolume(long nVolume)
{
  if(!m_Initialized || m_Passthrough)
    return -1;

  m_CurrentVolume = nVolume;

  OMX_AUDIO_CONFIG_VOLUMETYPE volume;
  OMX_INIT_STRUCTURE(volume);
  volume.nPortIndex = m_omx_render.GetInputPort();

  /*
  volume.bLinear = OMX_FALSE;
  volume.sVolume.nMin = 0;
  volume.sVolume.nMax = 100;
  */
  volume.sVolume.nValue = nVolume;

  m_omx_render.SetConfig(OMX_IndexConfigAudioVolume, &volume);

  return true;
}


//***********************************************************************************************
unsigned int COMXAudio::GetSpace()
{
  int free = m_omx_render.GetInputBufferSpace();
  return (free / m_Channels) * m_DataChannels;
}

unsigned int COMXAudio::AddPackets(const void* data, unsigned int len)
{
  return AddPackets(data, len, 0, 0);
}

//***********************************************************************************************
unsigned int COMXAudio::AddPackets(const void* data, unsigned int len, int64_t dts, int64_t pts)
{
  if(!m_Initialized) {
    CLog::Log(LOGERROR,"COMXAudio::AddPackets - sanity failed. no valid play handle!");
    return len;
  }

  unsigned int length, frames;

  if (m_remap.CanRemap() && !m_Passthrough && m_Channels != m_DataChannels)
    length = (len / m_DataChannels) * m_Channels;
  else
    length = len;

  uint8_t *outData = (uint8_t *)malloc(length);

  if (m_remap.CanRemap() && !m_Passthrough && m_Channels != m_DataChannels)
  {
    frames = length / m_Channels / (m_BitsPerSample >> 3);
    if (frames > 0)
    {
      // remap the audio channels using the frame count
      m_remap.Remap((void*)data, outData, frames, m_drc);
    
      // return the number of input bytes we accepted
      len = (length / m_Channels) * m_DataChannels;
    } 
  }
  else
  {
    memcpy(outData, (uint8_t*) data, length);
  }

  unsigned int demuxer_bytes = (unsigned int)length;
  uint8_t *demuxer_content = outData;

  OMX_ERRORTYPE omx_err;

  unsigned int nSleepTime = 0;

  OMX_BUFFERHEADERTYPE *omx_buffer = NULL;

  while(demuxer_bytes)
  {
    omx_buffer = m_omx_render.GetInputBuffer();

    if(omx_buffer == NULL)
    {
      OMXSleep(1);
      nSleepTime += 1;
      if(nSleepTime >= 200)
      {
        CLog::Log(LOGERROR, "COMXAudio::Decode timeout\n");
        printf("COMXAudio::Decode timeout\n");
        return len;
      }
      continue;
    }

    nSleepTime = 0;

    omx_buffer->nOffset = 0;
    omx_buffer->nFlags  = 0;

    omx_buffer->nFilledLen = (demuxer_bytes > omx_buffer->nAllocLen) ? omx_buffer->nAllocLen : demuxer_bytes;
    memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

    if(m_setStartTime) 
    {
      omx_buffer->nFlags = OMX_BUFFERFLAG_STARTTIME;

      m_setStartTime = false;
    }
    else
    {
      if((uint64_t)pts == AV_NOPTS_VALUE)
      {
        omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
      }
    }

    uint64_t val = ((uint64_t)pts == AV_NOPTS_VALUE) ? 0 : pts;
#ifdef OMX_SKIP64BIT
    if((uint64_t)pts == AV_NOPTS_VALUE)
    {
      omx_buffer->nTimeStamp.nLowPart = 0;
      omx_buffer->nTimeStamp.nHighPart = 0;
    }
    else
    {
      omx_buffer->nTimeStamp.nLowPart = val & 0x00000000FFFFFFFF;
      omx_buffer->nTimeStamp.nHighPart = (val & 0xFFFFFFFF00000000) >> 32;
    }
#else
    omx_buffer->nTimeStamp = val; // in microseconds
#endif

    /*
    CLog::Log(LOGDEBUG, "COMXAudio::AddPackets ADec : pts %lld omx_buffer 0x%08x buffer 0x%08x number %d\n", 
        pts, omx_buffer, omx_buffer->pBuffer, (int)omx_buffer->pAppPrivate);
    printf("ADec : pts %lld omx_buffer 0x%08x buffer 0x%08x number %d\n", 
        pts, omx_buffer, omx_buffer->pBuffer, (int)omx_buffer->pAppPrivate);
    */

    if (m_SampleSize > 0 && (uint64_t)pts != AV_NOPTS_VALUE)
    {
      pts += ((double)omx_buffer->nFilledLen * DVD_TIME_BASE) / m_SampleSize;
    }

    demuxer_bytes -= omx_buffer->nFilledLen;
    demuxer_content += omx_buffer->nFilledLen;

    if(demuxer_bytes == 0)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    omx_err = m_omx_render.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

      printf("%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

      free(outData);
      return 0;
    }

    /*
    if(m_firstFrame)
    {
      m_firstFrame = false;
      m_omx_render.WaitForEvent(OMX_EventPortSettingsChanged);

      m_omx_render.SendCommand(OMX_CommandPortDisable, m_omx_render.GetInputPort(), NULL);
      m_omx_decoder.SendCommand(OMX_CommandPortDisable, m_omx_decoder.GetOutputPort(), NULL);

      m_omx_render.WaitForCommand(OMX_CommandPortDisable, m_omx_render.GetInputPort());
      m_omx_decoder.WaitForCommand(OMX_CommandPortDisable, m_omx_decoder.GetOutputPort());

      //m_pcm.nChannels  = m_Channels;
      m_pcm.nPortIndex = m_omx_render.GetInputPort();
      m_omx_render.SetParameter(OMX_IndexParamAudioPcm, &m_pcm);


      m_omx_render.SendCommand(OMX_CommandPortEnable, m_omx_render.GetInputPort(), NULL);
      m_omx_decoder.SendCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort(), NULL);

      m_omx_render.WaitForCommand(OMX_CommandPortEnable, m_omx_render.GetInputPort());
      m_omx_decoder.WaitForCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort());
    }
    */
 
  }
  free(outData);

  return len;
}

//***********************************************************************************************
float COMXAudio::GetDelay()
{
  unsigned int free = m_omx_render.GetInputBufferSize() - m_omx_render.GetInputBufferSpace();
  return (float)free / (float)m_BytesPerSec;
}

float COMXAudio::GetCacheTime()
{
  /*
  unsigned int nBufferLenFull = (m_BufferLen / m_Channels) * m_DataChannels; 
  return (float)(nBufferLenFull - GetSpace()) / (float)m_BytesPerSec;
  */
  float fBufferLenFull = (float)m_BufferLen - (float)GetSpace();
  if(fBufferLenFull < 0)
    fBufferLenFull = 0;
  float ret = fBufferLenFull / (float)m_BytesPerSec;
  return ret;
}

float COMXAudio::GetCacheTotal()
{
  return (float)m_BufferLen / (float)m_BytesPerSec;
}

//***********************************************************************************************
unsigned int COMXAudio::GetChunkLen()
{
  return (m_ChunkLen / m_Channels) * m_DataChannels;
}
//***********************************************************************************************
int COMXAudio::SetPlaySpeed(int iSpeed)
{
  return 0;
}

void COMXAudio::RegisterAudioCallback(IAudioCallback *pCallback)
{
  m_pCallback = pCallback;
}

void COMXAudio::UnRegisterAudioCallback()
{
  m_pCallback = NULL;
}

void COMXAudio::WaitCompletion()
{
  if(!m_Initialized || m_Pause)
    return;

  /*
  OMX_PARAM_U32TYPE param;

  memset(&param, 0, sizeof(OMX_PARAM_U32TYPE));
  param.nSize = sizeof(OMX_PARAM_U32TYPE);
  param.nVersion.nVersion = OMX_VERSION;
  param.nPortIndex = m_omx_render.GetInputPort();

  unsigned int start = XbmcThreads::SystemClockMillis();

  // maximum wait 1s.
  while((XbmcThreads::SystemClockMillis() - start) < 1000) {
    if(m_BufferCount == m_omx_input_avaliable.size())
      break;

    OMXSleep(100);
    //printf("WaitCompletion\n");
  }
  */
}

void COMXAudio::SwitchChannels(int iAudioStream, bool bAudioOnAllSpeakers)
{
    return ;
}

void COMXAudio::EnumerateAudioSinks(AudioSinkList& vAudioSinks, bool passthrough)
{
#ifndef STANDALONE
  if (!passthrough)
  {
    vAudioSinks.push_back(AudioSink(g_localizeStrings.Get(409) + " (OMX)", "omx:default"));
    vAudioSinks.push_back(AudioSink("analog (OMX)" , "omx:analog"));
    vAudioSinks.push_back(AudioSink("hdmi (OMX)"   , "omx:hdmi"));
  }
  else
  {
    vAudioSinks.push_back(AudioSink("hdmi (OMX)"   , "omx:hdmi"));
  }
#endif
}

bool COMXAudio::SetClock(OMXClock *clock)
{
  if(m_av_clock != NULL)
    return false;

  m_av_clock = clock;
  m_external_clock = true;
  return true;
}

void COMXAudio::PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm)
{
  CLog::Log(LOGDEBUG, "pcm->nPortIndex     : %d\n", (int)pcm->nPortIndex);
  CLog::Log(LOGDEBUG, "pcm->eNumData       : %d\n", pcm->eNumData);
  CLog::Log(LOGDEBUG, "pcm->eEndian        : %d\n", pcm->eEndian);
  CLog::Log(LOGDEBUG, "pcm->bInterleaved   : %d\n", (int)pcm->bInterleaved);
  CLog::Log(LOGDEBUG, "pcm->nBitPerSample  : %d\n", (int)pcm->nBitPerSample);
  CLog::Log(LOGDEBUG, "pcm->ePCMMode       : %d\n", pcm->ePCMMode);
  CLog::Log(LOGDEBUG, "pcm->nChannels      : %d\n", (int)pcm->nChannels);
  CLog::Log(LOGDEBUG, "pcm->nSamplingRate  : %d\n", (int)pcm->nSamplingRate);
}
