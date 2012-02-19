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

#include "OMXPlayerAudioCodec.h"

#include <stdio.h>
#include <unistd.h>

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#endif

#define MAX_INPUT_DATA_SIZE   1 * 1024 * 1024
#define MAX_OUTPUT_DATA_SIZE  2 * 1024 * 1024

OMXPlayerAudioCodec::OMXPlayerAudioCodec()
{
  m_open          = false;
  m_stream_id     = -1;
  m_pStream       = NULL;
  m_av_clock      = NULL;
  m_pAudioCodec   = NULL;
  m_flush         = false;
  m_omx_pkt       = NULL;
  m_cached_input_size     = 0;
  m_cached_output_size    = 0;

  pthread_cond_init(&m_packet_cond, NULL);
  pthread_cond_init(&m_full_cond, NULL);
  pthread_mutex_init(&m_lock_input, NULL);
  pthread_mutex_init(&m_lock_output, NULL);
}

OMXPlayerAudioCodec::~OMXPlayerAudioCodec()
{
  Close();

  pthread_cond_destroy(&m_packet_cond);
  pthread_cond_destroy(&m_full_cond);
  pthread_mutex_destroy(&m_lock_input);
  pthread_mutex_destroy(&m_lock_output);
}

void OMXPlayerAudioCodec::LockInput()
{
  pthread_mutex_lock(&m_lock_input);
}

void OMXPlayerAudioCodec::UnLockInput()
{
  pthread_mutex_unlock(&m_lock_input);
}

void OMXPlayerAudioCodec::LockOutput()
{
  pthread_mutex_lock(&m_lock_output);
}

void OMXPlayerAudioCodec::UnLockOutput()
{
  pthread_mutex_unlock(&m_lock_output);
}

bool OMXPlayerAudioCodec::Open(COMXStreamInfo &hints, OMXClock *av_clock, IAudioRenderer::EEncoded passthrough, bool hw_decode)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !av_clock)
    return false;
  
  if(ThreadHandle())
    Close();

  m_dllAvFormat.av_register_all();

  m_hints       = hints;
  m_av_clock    = av_clock;
  m_passthrough = passthrough;
  m_hw_decode   = hw_decode;
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_bAbort      = false;
  m_pAudioCodec = NULL;
  m_flush       = false;
  m_cached_input_size   = 0;
  m_cached_output_size  = 0;

  if(!OpenAudioCodec())
  {
    Close();
    return false;
  }

  Create();

  m_open        = true;

  return true;
}

bool OMXPlayerAudioCodec::Close()
{
  m_bAbort  = true;
  m_flush   = true;

  if(ThreadHandle())
  {
    LockInput();
    pthread_cond_broadcast(&m_packet_cond);
    pthread_cond_broadcast(&m_full_cond);
    UnLockInput();

    StopThread();
  }

  Flush();

  CloseAudioCodec();

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  m_open          = false;
  m_stream_id     = -1;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pStream       = NULL;

  return true;
}

bool OMXPlayerAudioCodec::Decode(OMXPacket *pkt)
{
  if(!pkt || !m_pAudioCodec)
    return false;

  OMXReader *reader = pkt->owner;

  if((m_cached_output_size + pkt->size) < MAX_OUTPUT_DATA_SIZE)
  {
    if(pkt->dts != DVD_NOPTS_VALUE)
      m_iCurrentPts = pkt->dts;

    m_av_clock->SetPTS(m_iCurrentPts);

    const uint8_t *data_dec = pkt->data;
    int            data_len = pkt->size;

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

        OMXPacket *new_packet = reader->AllocPacket(decoded_size);
        if(new_packet)
        {
          new_packet->pts = m_iCurrentPts;          
          new_packet->dts = m_iCurrentPts;
          new_packet->duration = 0;
          new_packet->size = decoded_size;
          memcpy(new_packet->data, decoded, decoded_size);
          new_packet->stream_index = pkt->stream_index;
          new_packet->hints = pkt->hints;
          new_packet->pStream = pkt->pStream;
          m_cached_output_size += new_packet->size;
          LockOutput();
          m_output_packets.push(new_packet);
          UnLockOutput();
        }

        int n = (m_hints.channels * m_hints.bitspersample * m_hints.samplerate)>>3;
        if (n > 0 && m_iCurrentPts != DVD_NOPTS_VALUE)
        {
          m_iCurrentPts += ((double)decoded_size * DVD_TIME_BASE) / n;
        }
      }
      m_cached_input_size -= pkt->size;
      reader->FreePacket(pkt);
    }
    else
    {
      m_cached_output_size  += pkt->size;
      m_cached_input_size   -= pkt->size;
      m_output_packets.push(pkt);
    }

    return true;
  }
  else
  {
    return false;
  }
}

void OMXPlayerAudioCodec::Process()
{
  while(!m_bStop && !m_bAbort)
  {
    LockInput();
    if(m_input_packets.empty())
      pthread_cond_wait(&m_packet_cond, &m_lock_input);
    UnLockInput();

    if(m_bAbort)
      break;

    LockInput();
    if(!m_omx_pkt && !m_input_packets.empty())
    {
      m_omx_pkt = m_input_packets.front();
      m_input_packets.pop();
    }
    UnLockInput();

    LockInput();
    if(Decode(m_omx_pkt))
    {
      m_omx_pkt = NULL;
    }
    UnLockInput();
    //pthread_cond_broadcast(&m_full_cond);
  }
}

void OMXPlayerAudioCodec::Flush()
{
  m_flush = true;
  LockInput();
  LockOutput();
  while (!m_input_packets.empty())
  {
    OMXPacket *pkt = m_input_packets.front(); 
    m_input_packets.pop();
    OMXReader *reader = pkt->owner;
    reader->FreePacket(pkt);
  }
  if(m_omx_pkt)
  {
    OMXReader *reader = m_omx_pkt->owner;
    reader->FreePacket(m_omx_pkt);
    m_omx_pkt = NULL;
  }
  while (!m_output_packets.empty())
  {
    OMXPacket *pkt = m_output_packets.front(); 
    m_output_packets.pop();
    OMXReader *reader = pkt->owner;
    reader->FreePacket(pkt);
  }
  UnLockOutput();
  UnLockInput();
  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_cached_input_size   = 0;
  m_cached_output_size  = 0;
  m_flush = false;

  if(m_pAudioCodec)
    m_pAudioCodec->Reset();

  UnLockInput();
  m_flush = false;
}

bool OMXPlayerAudioCodec::AddPacket(OMXPacket *pkt)
{
  bool ret = false;

  if(!pkt)
    return false;

  if(m_flush || m_bStop || m_bAbort || !pkt)
    return false;

  LockInput();
  if((m_cached_input_size + pkt->size) < MAX_INPUT_DATA_SIZE)
  {
    m_cached_input_size += pkt->size;
    m_input_packets.push(pkt);
    ret = true;
    pthread_cond_broadcast(&m_packet_cond);
  }
  //else
  //{
  //  pthread_cond_wait(&m_full_cond, &m_lock_input);
  //}
  UnLockInput();

  return ret;
}

OMXPacket *OMXPlayerAudioCodec::GetPacket()
{
  OMXPacket *pkt = NULL;
  LockOutput();
  if(!m_output_packets.empty())
  {
    pkt = m_output_packets.front();
    m_output_packets.pop();
    m_cached_output_size -= pkt->size;
//    pthread_cond_broadcast(&m_packet_buffer_cond);
  }
  UnLockOutput();

  return pkt;
}

bool OMXPlayerAudioCodec::OpenAudioCodec()
{
  m_pAudioCodec = new COMXAudioCodecOMX();

  if(!m_pAudioCodec->Open(m_hints))
  {
    delete m_pAudioCodec; m_pAudioCodec = NULL;
    return false;
  }

  return true;
}

void OMXPlayerAudioCodec::CloseAudioCodec()
{
  if(m_pAudioCodec)
    delete m_pAudioCodec;
  m_pAudioCodec = NULL;
}

enum PCMChannels* OMXPlayerAudioCodec::GetChannelMap()
{
  if(m_pAudioCodec)
    return m_pAudioCodec->GetChannelMap();
  else
    return NULL;
}

int OMXPlayerAudioCodec::GetSampleRate()
{
  if(m_pAudioCodec)
    return m_pAudioCodec->GetSampleRate();
  else
    return 0;
}

int OMXPlayerAudioCodec::GetBitsPerSample()
{
  if(m_pAudioCodec)
    return m_pAudioCodec->GetSampleRate();
  else
    return 0;
}

int OMXPlayerAudioCodec::GetChannels()
{
  if(m_pAudioCodec)
    return m_pAudioCodec->GetSampleRate();
  else
    return 0;
}
