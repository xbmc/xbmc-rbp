/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXPlayerAudio.h"

#include <stdio.h>
#include <unistd.h>
#include <iomanip>

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#endif

#include "DVDDemuxers/DVDDemuxUtils.h"
#include "utils/MathUtils.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"

#include "OMXPlayer.h"

#include <iostream>
#include <sstream>

class COMXMsgAudioCodecChange : public CDVDMsg
{
public:
  COMXMsgAudioCodecChange(const CDVDStreamInfo &hints, COMXAudioCodecOMX* codec)
    : CDVDMsg(GENERAL_STREAMCHANGE)
    , m_codec(codec)
    , m_hints(hints)
  {}
 ~COMXMsgAudioCodecChange()
  {
    delete m_codec;
  }
  COMXAudioCodecOMX   *m_codec;
  CDVDStreamInfo      m_hints;
};

OMXPlayerAudio::OMXPlayerAudio(OMXClock *av_clock,
                               CDVDMessageQueue& parent)
: CThread("COMXPlayerAudio")
, m_messageQueue("audio")
, m_messageParent(parent)
{
  m_av_clock      = av_clock;
  m_pChannelMap   = NULL;
  m_pAudioCodec   = NULL;
  m_speed         = DVD_PLAYSPEED_NORMAL;
  m_started       = false;
  m_stalled       = false;
  m_audioClock    = 0;
  m_buffer_empty  = false;

  m_av_clock->SetMasterClock(false);
  m_omxAudio.SetClock(m_av_clock);

  m_messageQueue.SetMaxDataSize(3 * 1024 * 1024);
  m_messageQueue.SetMaxTimeSize(8.0);
}


OMXPlayerAudio::~OMXPlayerAudio()
{
  CloseStream(false);
}

bool OMXPlayerAudio::OpenStream(CDVDStreamInfo &hints)
{
  CloseStream(false);

  COMXAudioCodecOMX *codec = new COMXAudioCodecOMX();

  if(!codec || !codec->Open(hints))
  {
    CLog::Log(LOGERROR, "Unsupported audio codec");
    delete codec; codec = NULL;
    return false;
  }

  if(m_messageQueue.IsInited())
    m_messageQueue.Put(new COMXMsgAudioCodecChange(hints, codec), 0);
  else
  {
    if(!OpenStream(hints, codec))
      return false;
    CLog::Log(LOGNOTICE, "Creating audio thread");
    m_messageQueue.Init();
    Create();
  }

  /*
  if(!OpenStream(hints, codec))
    return false;

  CLog::Log(LOGNOTICE, "Creating audio thread");
  m_messageQueue.Init();
  Create();
  */

  return true;
}

bool OMXPlayerAudio::OpenStream(CDVDStreamInfo &hints, COMXAudioCodecOMX *codec)
{
  SAFE_DELETE(m_pAudioCodec);

  m_hints           = hints;
  m_pAudioCodec     = codec;

  if(m_hints.bitspersample == 0)
    m_hints.bitspersample = 16;

  m_speed           = DVD_PLAYSPEED_NORMAL;
  m_audioClock      = 0;
  m_error           = 0;
  m_errorbuff       = 0;
  m_errorcount      = 0;
  m_integral        = 0;
  m_skipdupcount    = 0;
  m_prevskipped     = false;
  m_syncclock       = true;
  m_passthrough     = IAudioRenderer::ENCODED_NONE;
  m_hw_decode       = false;
  m_errortime       = m_av_clock->CurrentHostCounter();
  m_silence         = false;
  m_freq            = m_av_clock->CurrentHostFrequency();
  m_started         = false;
  m_stalled         = m_messageQueue.GetPacketCount(CDVDMsg::DEMUXER_PACKET) == 0;
  m_use_passthrough = (g_guiSettings.GetInt("audiooutput.mode") == IAudioRenderer::ENCODED_NONE) ? false : true ;
  m_use_hw_decode   = g_advancedSettings.m_omHWAudioDecode;

  if(m_use_passthrough)
    m_device = g_guiSettings.GetString("audiooutput.passthroughdevice");
  else
    m_device = g_guiSettings.GetString("audiooutput.audiodevice");

  m_pChannelMap = m_pAudioCodec->GetChannelMap();

  return OpenDecoder();
}

bool OMXPlayerAudio::CloseStream(bool bWaitForBuffers)
{
  // wait until buffers are empty
  if (bWaitForBuffers && m_speed > 0) m_messageQueue.WaitUntilEmpty();

  m_messageQueue.Abort();

  StopThread();

  if (m_pAudioCodec)
  {
    m_pAudioCodec->Dispose();
    delete m_pAudioCodec;
    m_pAudioCodec = NULL;
  }

  m_omxAudio.Deinitialize();

  m_messageQueue.End();

  m_speed         = DVD_PLAYSPEED_NORMAL;
  m_started       = false;

  return true;
}

void OMXPlayerAudio::HandleSyncError(double duration, double pts)
{
  double clock = m_av_clock->GetClock();
  double error = pts - clock;
  int64_t now;

  if( fabs(error) > DVD_MSEC_TO_TIME(100) || m_syncclock )
  {
    m_av_clock->Discontinuity(clock+error);
    /*
    if(m_speed == DVD_PLAYSPEED_NORMAL)
      printf("OMXPlayerAudio:: Discontinuity - was:%f, should be:%f, error:%f\n", clock, clock+error, error);
    */

    m_errorbuff = 0;
    m_errorcount = 0;
    m_skipdupcount = 0;
    m_error = 0;
    m_syncclock = false;
    m_errortime = m_av_clock->CurrentHostCounter();

    return;
  }

  if (m_speed != DVD_PLAYSPEED_NORMAL)
  {
    m_errorbuff = 0;
    m_errorcount = 0;
    m_integral = 0;
    m_skipdupcount = 0;
    m_error = 0;
    m_errortime = m_av_clock->CurrentHostCounter();
    return;
  }

  //check if measured error for 1 second
  now = m_av_clock->CurrentHostCounter();
  if ((now - m_errortime) >= m_freq)
  {
    m_errortime = now;
    m_error = m_errorbuff / m_errorcount;

    m_errorbuff = 0;
    m_errorcount = 0;

/*
    if (m_synctype == SYNC_DISCON)
    {
*/
      double limit, error;
      if (m_av_clock->GetRefreshRate(&limit) > 0)
      {
        //when the videoreferenceclock is running, the discontinuity limit is one vblank period
        limit *= DVD_TIME_BASE;

        //make error a multiple of limit, rounded towards zero,
        //so it won't interfere with the sync methods in CXBMCRenderManager::WaitPresentTime
        if (m_error > 0.0)
          error = limit * floor(m_error / limit);
        else
          error = limit * ceil(m_error / limit);
      }
      else
      {
        limit = DVD_MSEC_TO_TIME(10);
        error = m_error;
      }

      if (fabs(error) > limit - 0.001)
      {
        m_av_clock->Discontinuity(clock+error);
        /*
        if(m_speed == DVD_PLAYSPEED_NORMAL)
          CLog::Log(LOGDEBUG, "COMXPlayerAudio:: Discontinuity - was:%f, should be:%f, error:%f", clock, clock+error, error);
        */
      }
    }
/*
    else if (m_synctype == SYNC_SKIPDUP && m_skipdupcount == 0 && fabs(m_error) > DVD_MSEC_TO_TIME(10))
    if (m_skipdupcount == 0 && fabs(m_error) > DVD_MSEC_TO_TIME(10))
    {
      //check how many packets to skip/duplicate
      m_skipdupcount = (int)(m_error / duration);
      //if less than one frame off, see if it's more than two thirds of a frame, so we can get better in sync
      if (m_skipdupcount == 0 && fabs(m_error) > duration / 3 * 2)
        m_skipdupcount = (int)(m_error / (duration / 3 * 2));

      if (m_skipdupcount > 0)
        CLog::Log(LOGDEBUG, "OMXPlayerAudio:: Duplicating %i packet(s) of %.2f ms duration",
                  m_skipdupcount, duration / DVD_TIME_BASE * 1000.0);
      else if (m_skipdupcount < 0)
        CLog::Log(LOGDEBUG, "OMXPlayerAudio:: Skipping %i packet(s) of %.2f ms duration ",
                  m_skipdupcount * -1,  duration / DVD_TIME_BASE * 1000.0);
    }
  }
*/
}

bool OMXPlayerAudio::Decode(DemuxPacket *pkt, bool bDropPacket)
{
  if(!pkt)
    return false;

  /* last decoder reinit went wrong */
  if(!m_pAudioCodec)
    return true;

  int channels = m_hints.channels;

  /* 6 channel have to be mapped to 8 for PCM */
  if(!m_passthrough && !m_hw_decode)
  {
    if(channels == 6)
      channels = 8;
  }
 
  if(pkt->dts != DVD_NOPTS_VALUE)
    m_audioClock = pkt->dts;

  const uint8_t *data_dec = pkt->pData;
  int            data_len = pkt->iSize;

  if(!m_passthrough && !m_hw_decode)
  {
    while(data_len > 0)
    {
      int len = m_pAudioCodec->Decode((BYTE *)data_dec, data_len);
      if( (len < 0) || (len >  data_len) )
      {
        m_pAudioCodec->Reset();
        break;
      }

      data_dec+= len;
      data_len -= len;

      uint8_t *decoded;
      int decoded_size = m_pAudioCodec->GetData(&decoded);

      if(decoded_size <=0)
        continue;

      int ret = 0;

      m_audioStats.AddSampleBytes(decoded_size);

      if(!bDropPacket)
      {
        // Zero out the frame data if we are supposed to silence the audio
        if(m_silence)
          memset(decoded, 0x0, decoded_size);

        ret = m_omxAudio.AddPackets(decoded, decoded_size, m_audioClock, m_audioClock);

        if(ret != decoded_size)
        {
          printf("error ret %d decoded_size %d\n", ret, decoded_size);
        }
      }

      int n = (m_hints.channels * m_hints.bitspersample * m_hints.samplerate)>>3;
      if (n > 0)
        m_audioClock += ((double)decoded_size * DVD_TIME_BASE) / n;

      HandleSyncError((((double)decoded_size * DVD_TIME_BASE) / n), m_audioClock);
    }
  }
  else
  {
    if(!bDropPacket)
    {
      if(m_silence)
        memset(pkt->pData, 0x0, pkt->iSize);

      m_omxAudio.AddPackets(pkt->pData, pkt->iSize, m_audioClock, m_audioClock);
    }

    HandleSyncError(0, m_audioClock);
  }

  if(bDropPacket)
    m_stalled = false;

  if(m_omxAudio.GetDelay() < 0.1)
    m_stalled = true;

  // signal to our parent that we have initialized
  if(m_started == false)
  {
    m_started = true;
    m_messageParent.Put(new CDVDMsgInt(CDVDMsg::PLAYER_STARTED, DVDPLAYER_AUDIO));
  }

  if(!m_av_clock->OMXIsPaused() && !bDropPacket)
  {
    if((m_speed == DVD_PLAYSPEED_PAUSE || m_speed == DVD_PLAYSPEED_NORMAL))
    {
      if(GetDelay() < 0.1f)
      {
        m_buffer_empty = true;
        clock_gettime(CLOCK_REALTIME, &m_starttime);
        m_av_clock->OMXSetPlaySpeed(OMX_PLAYSPEED_PAUSE);
      }
      else if(GetDelay() > (AUDIO_BUFFER_SECONDS * 0.75f))
      {
        m_buffer_empty = false;
        m_av_clock->OMXSetPlaySpeed(OMX_PLAYSPEED_NORMAL);
      }
      if(m_buffer_empty && m_av_clock->OMXGetPlaySpeed() == OMX_PLAYSPEED_PAUSE)
      {
        clock_gettime(CLOCK_REALTIME, &m_endtime);
        if((m_endtime.tv_sec - m_starttime.tv_sec) > 1)
        {
          m_buffer_empty = false;
          m_av_clock->OMXSetPlaySpeed(OMX_PLAYSPEED_NORMAL);
        }
      }
    }
  }

  return true;
}

void OMXPlayerAudio::Process()
{
  m_audioStats.Start();

  while(!m_bStop)
  {
    
    CDVDMsg* pMsg;
    int priority = (m_speed == DVD_PLAYSPEED_PAUSE && m_started) ? 1 : 0;
    int timeout = 1000;

    MsgQueueReturnCode ret = m_messageQueue.Get(&pMsg, timeout, priority);

    if (ret == MSGQ_TIMEOUT)
    {
      Sleep(10);
      continue;
    }

    if (MSGQ_IS_ERROR(ret) || ret == MSGQ_ABORT)
    {
      Sleep(10);
      continue;
    }

    if (pMsg->IsType(CDVDMsg::DEMUXER_PACKET))
    {
      DemuxPacket* pPacket = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacket();
      bool bPacketDrop     = ((CDVDMsgDemuxerPacket*)pMsg)->GetPacketDrop();

      while (!m_bStop)
      {
        if((unsigned long)m_omxAudio.GetSpace() < pPacket->iSize)
        {
          Sleep(10);
          continue;
        }
  
        if(Decode(pPacket, m_speed > DVD_PLAYSPEED_NORMAL || m_speed < 0 || bPacketDrop))
        {
          if (m_stalled && (m_omxAudio.GetDelay() > (AUDIO_BUFFER_SECONDS * 0.75f)))
          {
            CLog::Log(LOGINFO, "COMXPlayerAudio - Switching to normal playback");
            m_stalled = false;
          }
          break;
        }
      }
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_SYNCHRONIZE))
    {
      ((CDVDMsgGeneralSynchronize*)pMsg)->Wait( &m_bStop, SYNCSOURCE_AUDIO );
      CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::GENERAL_SYNCHRONIZE");
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_RESYNC))
    { //player asked us to set internal clock
      CDVDMsgGeneralResync* pMsgGeneralResync = (CDVDMsgGeneralResync*)pMsg;

      if (pMsgGeneralResync->m_timestamp != DVD_NOPTS_VALUE)
        m_audioClock = pMsgGeneralResync->m_timestamp;

      //m_ptsOutput.Add(m_audioClock, m_dvdAudio.GetDelay(), 0);
      if (pMsgGeneralResync->m_clock)
      {
        CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::GENERAL_RESYNC(%f, 1)", m_audioClock);
        //m_pClock->Discontinuity(m_ptsOutput.Current());
        m_av_clock->Discontinuity(m_audioClock + GetDelay());
      }
      else
        CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::GENERAL_RESYNC(%f, 0)", m_audioClock);
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_RESET))
    {
      if (m_pAudioCodec)
        m_pAudioCodec->Reset();
      m_started = false;
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_FLUSH))
    {
      m_omxAudio.Flush();
      m_syncclock = true;
      m_stalled   = true;
      m_started   = false;

      if (m_pAudioCodec)
        m_pAudioCodec->Reset();
    }
    else if (pMsg->IsType(CDVDMsg::PLAYER_STARTED))
    {
      if(m_started)
        m_messageParent.Put(new CDVDMsgInt(CDVDMsg::PLAYER_STARTED, DVDPLAYER_AUDIO));
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_EOF))
    {
      CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::GENERAL_EOF");
      WaitCompletion();
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_DELAY))
    {
      if (m_speed != DVD_PLAYSPEED_PAUSE)
      {
        double timeout = static_cast<CDVDMsgDouble*>(pMsg)->m_value;

        CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::GENERAL_DELAY(%f)", timeout);

        timeout *= (double)DVD_PLAYSPEED_NORMAL / abs(m_speed);
        timeout += CDVDClock::GetAbsoluteClock();

        while(!m_bStop && CDVDClock::GetAbsoluteClock() < timeout)
          Sleep(1);
      }
    }
    else if (pMsg->IsType(CDVDMsg::PLAYER_SETSPEED))
    {
      m_speed = static_cast<CDVDMsgInt*>(pMsg)->m_value;
      if (m_speed == DVD_PLAYSPEED_NORMAL)
      {
        //m_dvdAudio.Resume();
      }
      else
      {
        m_syncclock = true;
        if (m_speed != DVD_PLAYSPEED_PAUSE)
          m_omxAudio.Flush();
        //m_dvdAudio.Pause();
      }
    }
    else if (pMsg->IsType(CDVDMsg::AUDIO_SILENCE))
    {
      m_silence = static_cast<CDVDMsgBool*>(pMsg)->m_value;
      if (m_silence)
        CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::AUDIO_SILENCE(%f, 1)", m_audioClock);
      else
        CLog::Log(LOGDEBUG, "COMXPlayerAudio - CDVDMsg::AUDIO_SILENCE(%f, 0)", m_audioClock);
    }
    else if (pMsg->IsType(CDVDMsg::GENERAL_STREAMCHANGE))
    {
      COMXMsgAudioCodecChange* msg(static_cast<COMXMsgAudioCodecChange*>(pMsg));
      OpenStream(msg->m_hints, msg->m_codec);
      msg->m_codec = NULL;
    }

    pMsg->Release();
  }
}

void OMXPlayerAudio::Flush()
{
  m_messageQueue.Flush();
  m_messageQueue.Put( new CDVDMsg(CDVDMsg::GENERAL_FLUSH), 1);
}

void OMXPlayerAudio::WaitForBuffers()
{
  // make sure there are no more packets available
  m_messageQueue.WaitUntilEmpty();

  // make sure almost all has been rendered
  // leave 500ms to avound buffer underruns
  double delay = GetCacheTime();
  if(delay > 0.5)
    Sleep((int)(1000 * (delay - 0.5)));
}

bool OMXPlayerAudio::Passthrough() const
{
  return m_passthrough;
}

IAudioRenderer::EEncoded OMXPlayerAudio::IsPassthrough(CDVDStreamInfo hints)
{
  int  m_outputmode = 0;
  bool bitstream = false;
  IAudioRenderer::EEncoded passthrough = IAudioRenderer::ENCODED_NONE;

  m_outputmode = g_guiSettings.GetInt("audiooutput.mode");

  switch(m_outputmode)
  {
    case 0:
      passthrough = IAudioRenderer::ENCODED_NONE;
      break;
    case 1:
      bitstream = true;
      break;
    case 2:
      bitstream = true;
      break;
  }

  if(bitstream)
  {
    if(hints.codec == CODEC_ID_AC3 && g_guiSettings.GetBool("audiooutput.ac3passthrough"))
    {
      passthrough = IAudioRenderer::ENCODED_IEC61937_AC3;
    }
    if(hints.codec == CODEC_ID_DTS && g_guiSettings.GetBool("audiooutput.dtspassthrough"))
    {
      passthrough = IAudioRenderer::ENCODED_IEC61937_DTS;
    }
  }

  return passthrough;
}

bool OMXPlayerAudio::OpenDecoder()
{
  bool bAudioRenderOpen = false;

  if(m_use_passthrough)
    m_passthrough = IsPassthrough(m_hints);

  if(!m_passthrough && m_use_hw_decode)
    m_hw_decode = COMXAudio::HWDecode(m_hints.codec);

  if(m_passthrough || m_use_hw_decode)
  {
    if(m_passthrough)
      m_hw_decode = false;
    bAudioRenderOpen = m_omxAudio.Initialize(NULL, m_device.substr(4), m_pChannelMap,
                                             m_hints, m_av_clock, m_passthrough, m_hw_decode);
  }
  else
  {
    /* omx needs 6 channels packed into 8 for PCM */
    if(m_hints.channels == 6)
      m_hints.channels = 8;

    bAudioRenderOpen = m_omxAudio.Initialize(NULL, m_device.substr(4), m_hints.channels, m_pChannelMap,
                                             m_hints.samplerate, m_hints.bitspersample, 
                                             false, false, m_passthrough);
  }

  m_codec_name = "";
  
  if(!bAudioRenderOpen)
  {
    return false;
  }
  else
  {
    if(m_passthrough)
    {
      printf("Audio codec %s channels %d samplerate %d bitspersample %d\n",
        m_codec_name.c_str(), 2, m_hints.samplerate, m_hints.bitspersample);
    }
    else
    {
      printf("Audio codec %s channels %d samplerate %d bitspersample %d\n",
        m_codec_name.c_str(), m_hints.channels, m_hints.samplerate, m_hints.bitspersample);
    }
  }
  return true;
}

double OMXPlayerAudio::GetDelay()
{
  return m_omxAudio.GetDelay();
}

double OMXPlayerAudio::GetCacheTime()
{
  return m_omxAudio.GetCacheTime();
}

void OMXPlayerAudio::WaitCompletion()
{
  m_omxAudio.WaitCompletion();
}

void OMXPlayerAudio::RegisterAudioCallback(IAudioCallback *pCallback)
{
  m_omxAudio.RegisterAudioCallback(pCallback);

}
void OMXPlayerAudio::UnRegisterAudioCallback()
{
  m_omxAudio.UnRegisterAudioCallback();
}

void OMXPlayerAudio::DoAudioWork()
{
  m_omxAudio.DoAudioWork();
}

void OMXPlayerAudio::SetCurrentVolume(long nVolume)
{
  m_omxAudio.SetCurrentVolume(nVolume);
}

void OMXPlayerAudio::SetSpeed(int speed)
{
  if(m_messageQueue.IsInited())
    m_messageQueue.Put( new CDVDMsgInt(CDVDMsg::PLAYER_SETSPEED, speed), 1 );
  else
    m_speed = speed;
}

int OMXPlayerAudio::GetAudioBitrate()
{
  return (int)m_audioStats.GetBitrate();
}

std::string OMXPlayerAudio::GetPlayerInfo()
{
  std::ostringstream s;
  s << "aq:"     << setw(2) << min(99,m_messageQueue.GetLevel() + MathUtils::round_int(100.0/8.0*GetCacheTime())) << "%";
  s << ", kB/s:" << fixed << setprecision(2) << (double)GetAudioBitrate() / 1024.0;

  return s.str();
}

