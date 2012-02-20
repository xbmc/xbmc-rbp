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

#include "OMXPlayerVideo.h"

#include <stdio.h>
#include <unistd.h>

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#endif

#define MAX_DATA_SIZE    3 * 1024 * 1024

OMXPlayerVideo::OMXPlayerVideo()
{
  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_decoder       = NULL;
  m_fps           = 25.0f;
  m_flush         = false;
  m_omx_pkt       = NULL;
  m_cached_size   = 0;
  m_hdmi_clock_sync = false;
  m_iVideoDelay   = 0;

  pthread_cond_init(&m_packet_cond, NULL);
  pthread_mutex_init(&m_lock, NULL);
}

OMXPlayerVideo::~OMXPlayerVideo()
{
  Close();

  pthread_cond_destroy(&m_packet_cond);
  pthread_mutex_destroy(&m_lock);
}

void OMXPlayerVideo::Lock()
{
  if(m_use_thread)
    pthread_mutex_lock(&m_lock);
}

void OMXPlayerVideo::UnLock()
{
  if(m_use_thread)
    pthread_mutex_unlock(&m_lock);
}

bool OMXPlayerVideo::Open(COMXStreamInfo &hints, OMXClock *av_clock, bool deinterlace, bool mpeg, int has_audio, bool hdmi_clock_sync, bool use_thread)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !av_clock)
    return false;
  
  if(ThreadHandle())
    Close();

  m_dllAvFormat.av_register_all();

  m_hints       = hints;
  m_av_clock    = av_clock;
  m_fps         = 25.0f;
  m_frametime   = 0;
  m_Deinterlace = deinterlace;
  m_bMpeg       = mpeg;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_has_audio   = has_audio;
  m_bAbort      = false;
  m_use_thread  = use_thread;
  m_flush       = false;
  m_cached_size = 0;
  m_iVideoDelay = 0;
  m_hdmi_clock_sync = hdmi_clock_sync;

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

bool OMXPlayerVideo::Close()
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

bool OMXPlayerVideo::Decode(OMXPacket *pkt)
{
  if(!pkt)
    return false;

  /*
  if((unsigned long)m_decoder->GetFreeSpace() < 1024*1024)
    return false;
  */

  if(!((unsigned long)m_decoder->GetFreeSpace() > pkt->size))
    OMXSleep(10);

  if((unsigned long)m_decoder->GetFreeSpace() > pkt->size)
  {
    if (pkt->dts == DVD_NOPTS_VALUE && pkt->pts == DVD_NOPTS_VALUE)
      pkt->pts = m_iCurrentPts;
    else if (pkt->pts == DVD_NOPTS_VALUE)
      pkt->pts = pkt->dts;

    if(pkt->pts != DVD_NOPTS_VALUE)
      m_iCurrentPts = pkt->pts;

    if(m_iCurrentPts != DVD_NOPTS_VALUE)
      m_iCurrentPts += m_iVideoDelay;
      
    if(m_bMpeg)
      m_decoder->Decode(pkt->data, pkt->size, DVD_NOPTS_VALUE, DVD_NOPTS_VALUE);
    else
      m_decoder->Decode(pkt->data, pkt->size, m_iCurrentPts, m_iCurrentPts);

    m_iCurrentPts += m_frametime;
    m_av_clock->SetPTS(m_iCurrentPts);

    return true;
  }
  else
  {
    return false;
  }
}

void OMXPlayerVideo::Process()
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

void OMXPlayerVideo::FlushPackets()
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
  m_cached_size = 0;
  UnLock();
  m_flush = false;
}

void OMXPlayerVideo::FlushDecoder()
{
  m_flush = true;
  Lock();
  if(m_decoder)
    m_decoder->Reset();
  UnLock();
  m_flush = false;
}

bool OMXPlayerVideo::AddPacket(OMXPacket *pkt)
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

bool OMXPlayerVideo::OpenDecoder()
{
  if (m_hints.fpsrate && m_hints.fpsscale)
    m_fps = DVD_TIME_BASE / OMXReader::NormalizeFrameduration((double)DVD_TIME_BASE * m_hints.fpsscale / m_hints.fpsrate);
  else
    m_fps = 25;

  if( m_fps > 100 || m_fps < 5 )
  {
    printf("Invalid framerate %d, using forced 25fps and just trust timestamps\n", (int)m_fps);
    m_fps = 25;
  }

  m_frametime = (double)DVD_TIME_BASE / m_fps;

  m_decoder = new COMXVideo();
  if(!m_decoder->Open(m_hints, m_av_clock, m_Deinterlace, m_hdmi_clock_sync))
  {
    CloseDecoder();
    return false;
  }
  else
  {
    printf("Video codec %s width %d height %d profile %d fps %f\n",
        m_decoder->GetDecoderName().c_str() , m_hints.width, m_hints.height, m_hints.profile, m_fps);
  }

  return true;
}

bool OMXPlayerVideo::CloseDecoder()
{
  if(m_decoder)
    delete m_decoder;
  m_decoder   = NULL;
  return true;
}

int  OMXPlayerVideo::GetDecoderBufferSize()
{
  if(m_decoder)
    return m_decoder->GetInputBufferSize();
  else
    return 0;
}

int  OMXPlayerVideo::GetDecoderFreeSpace()
{
  if(m_decoder)
    return m_decoder->GetFreeSpace();
  else
    return 0;
}

void OMXPlayerVideo::WaitCompletion()
{
  if(m_decoder) m_decoder->WaitCompletion();
}
