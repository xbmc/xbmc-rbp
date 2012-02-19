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

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#endif

#define MAX_DATA_SIZE    1 * 1024 * 1024

OMXPlayerAudio::OMXPlayerAudio()
{
  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_decoder       = NULL;
  m_flush         = false;
  m_omx_pkt       = NULL;
  m_cached_size   = 0;
  m_pChannelMap   = NULL;

  pthread_cond_init(&m_packet_cond, NULL);
  pthread_cond_init(&m_full_cond, NULL);
  pthread_mutex_init(&m_lock, NULL);
}

OMXPlayerAudio::~OMXPlayerAudio()
{
  Close();

  pthread_cond_destroy(&m_packet_cond);
  pthread_cond_destroy(&m_full_cond);
  pthread_mutex_destroy(&m_lock);
}

void OMXPlayerAudio::Lock()
{
  if(m_use_thread)
    pthread_mutex_lock(&m_lock);
}

void OMXPlayerAudio::UnLock()
{
  if(m_use_thread)
    pthread_mutex_unlock(&m_lock);
}

bool OMXPlayerAudio::Open(COMXStreamInfo &hints, OMXClock *av_clock, CStdString codec_name, CStdString device,
                          IAudioRenderer::EEncoded passthrough, bool hw_decode, bool mpeg, bool use_thread, enum PCMChannels *pChannelMap)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !av_clock)
    return false;
  
  if(ThreadHandle())
    Close();

  m_dllAvFormat.av_register_all();

  m_hints       = hints;
  m_av_clock    = av_clock;
  m_codec_name  = codec_name;
  m_device      = device;
  m_passthrough = passthrough;
  m_hw_decode   = hw_decode;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort      = false;
  m_bMpeg       = mpeg;
  m_use_thread  = use_thread;
  m_flush       = false;
  m_cached_size = 0;
  m_pChannelMap = pChannelMap;

  if(!OpenDecoder())
  {
    Close();
    return false;
  }

  if(m_use_thread)
    Create();

  m_open        = true;

  return true;
}

bool OMXPlayerAudio::Close()
{
  m_bAbort  = true;
  m_flush   = true;

  if(ThreadHandle())
  {
    Lock();
    pthread_cond_broadcast(&m_packet_cond);
    pthread_cond_broadcast(&m_full_cond);
    UnLock();

    StopThread();
  }

  FlushPackets();
  FlushDecoder();

  CloseDecoder();

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  m_open          = false;
  m_stream_id     = -1;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pStream       = NULL;

  return true;
}

bool OMXPlayerAudio::Decode(OMXPacket *pkt)
{
  if(!pkt)
    return false;

  if(!((unsigned long)m_decoder->GetSpace() > pkt->size))
    OMXSleep(1);

  //if(GetDelay() < (AUDIO_BUFFER_SECONDS * 0.90f))
  if((unsigned long)m_decoder->GetSpace() > pkt->size)
  {
    if(pkt->dts != DVD_NOPTS_VALUE)
      m_iCurrentPts = pkt->dts;

    m_av_clock->SetPTS(m_iCurrentPts);

    if(m_bMpeg)
      m_decoder->AddPackets(pkt->data, pkt->size, DVD_NOPTS_VALUE, DVD_NOPTS_VALUE);
    else
      m_decoder->AddPackets(pkt->data, pkt->size, m_iCurrentPts, m_iCurrentPts);

    return true;
  }
  else
  {
    return false;
  }
}

void OMXPlayerAudio::Process()
{
  while(!m_bStop && !m_bAbort)
  {
    Lock();
    if(m_packets.empty())
      pthread_cond_wait(&m_packet_cond, &m_lock);
    UnLock();

    if(m_bAbort)
      break;

    Lock();
    if(!m_omx_pkt && !m_packets.empty())
    {
      m_omx_pkt = m_packets.front();
      m_packets.pop();
    }

    if(Decode(m_omx_pkt))
    {
      m_cached_size -= m_omx_pkt->size;
      if(m_omx_pkt)
      {
        OMXReader *reader = m_omx_pkt->owner;
        reader->FreePacket(m_omx_pkt);
      }
      m_omx_pkt = NULL;
      //pthread_cond_broadcast(&m_full_cond);
    }
    UnLock();
  }
}

void OMXPlayerAudio::FlushPackets()
{
  m_flush = true;
  Lock();
  while (!m_packets.empty())
  {
    OMXPacket *pkt = m_packets.front(); 
    m_packets.pop();
    OMXReader *reader = pkt->owner;
    reader->FreePacket(pkt);
  }
  if(m_omx_pkt)
  {
    OMXReader *reader = m_omx_pkt->owner;
    reader->FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_cached_size   = 0;
  UnLock();
  m_flush = false;
}

void OMXPlayerAudio::FlushDecoder()
{
  m_flush = true;
  Lock();
  if(m_decoder)
    m_decoder->Flush();
  UnLock();
  m_flush = false;
}

bool OMXPlayerAudio::AddPacket(OMXPacket *pkt)
{
  bool ret = false;

  if(!pkt)
    return false;

  if(!m_use_thread)
  {
    if(Decode(pkt))
    {
      OMXReader *reader = pkt->owner;
      reader->FreePacket(pkt);
      ret = true;
    }
  }
  else
  {
    if(m_flush || m_bStop || m_bAbort)
      return true;

    Lock();
    if((m_cached_size + pkt->size) < MAX_DATA_SIZE)
    {
      m_cached_size += pkt->size;
      m_packets.push(pkt);
      ret = true;
      pthread_cond_broadcast(&m_packet_cond);
    }
    /*
    else
    {
      pthread_cond_wait(&m_full_cond, &m_lock);
    }
    */
    UnLock();
  }
  return ret;
}

bool OMXPlayerAudio::OpenDecoder()
{
  bool bAudioRenderOpen = false;

  m_decoder = new COMXAudio();
  m_decoder->SetClock(m_av_clock);

  if(m_passthrough)
  {
    m_hw_decode = false;
    bAudioRenderOpen = m_decoder->Initialize(NULL, m_device.substr(4), m_pChannelMap,
                                                   m_hints, m_av_clock, m_passthrough, m_hw_decode);
  }
  else
  {
    /*
    if(m_hints.channels == 6)
      m_hints.channels = 8;
    */

    if(m_hw_decode)
    {
      bAudioRenderOpen = m_decoder->Initialize(NULL, m_device.substr(4), m_pChannelMap,
                                                     m_hints, m_av_clock, m_passthrough, m_hw_decode);
    }
    else
    {
      bAudioRenderOpen = m_decoder->Initialize(NULL, m_device.substr(4), m_hints.channels, m_pChannelMap,
                                                     m_hints.samplerate, m_hints.bitspersample, 
                                                     false, false, m_passthrough);
    }
  }

  if(!bAudioRenderOpen)
  {
    delete m_decoder; 
    m_decoder = NULL;
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

bool OMXPlayerAudio::CloseDecoder()
{
  if(m_decoder)
    delete m_decoder;
  m_decoder   = NULL;
  return true;
}

double OMXPlayerAudio::GetDelay()
{
  if(m_decoder)
    return m_decoder->GetDelay();
  else
    return 0;
}

void OMXPlayerAudio::WaitCompletion()
{
  if(m_decoder) m_decoder->WaitCompletion();
}

void OMXPlayerAudio::RegisterAudioCallback(IAudioCallback *pCallback)
{
  if(m_decoder) m_decoder->RegisterAudioCallback(pCallback);

}
void OMXPlayerAudio::UnRegisterAudioCallback()
{
  if(m_decoder) m_decoder->UnRegisterAudioCallback();
}

void OMXPlayerAudio::DoAudioWork()
{
  if(m_decoder) m_decoder->DoAudioWork();
}

void OMXPlayerAudio::SetCurrentVolume(long nVolume)
{
  if(m_decoder) m_decoder->SetCurrentVolume(nVolume);
}
