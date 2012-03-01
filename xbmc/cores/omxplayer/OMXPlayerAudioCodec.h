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

#ifndef _OMX_PLAYERAUDIOCODEC_H_
#define _OMX_PLAYERAUDIOCODEC_H_

#include "utils/StdString.h"
#include "DllAvUtil.h"
#include "DllAvFormat.h"
#include "DllAvFilter.h"
#include "DllAvCodec.h"
#include "DllAvCore.h"

#include "utils/PCMRemap.h"

#include "OMXReader.h"
#include "OMXClock.h"
#include "OMXAudio.h"
#include "OMXStreamInfo.h"
#include "OMXAudioCodecOMX.h"
#ifdef STANDALONE
#include "OMXThread.h"
#else
#include "threads/Thread.h"
#endif

#include <queue>
#include <sys/types.h>

using namespace std;

#ifdef STANDALONE
class OMXPlayerAudioCodec : public OMXThread
#else
class OMXPlayerAudioCodec : public CThread
#endif
{
protected:
  AVStream                  *m_pStream;
  int                       m_stream_id;
  std::queue<OMXPacket *>   m_input_packets;
  std::queue<OMXPacket *>   m_output_packets;
  DllAvUtil                 m_dllAvUtil;
  DllAvCodec                m_dllAvCodec;
  DllAvFormat               m_dllAvFormat;
  bool                      m_open;
  COMXStreamInfo            m_hints;
  double                    m_iCurrentPts;
  pthread_cond_t            m_packet_cond;
  pthread_cond_t            m_full_cond;
  pthread_mutex_t           m_lock_input;
  pthread_mutex_t           m_lock_output;
  OMXClock                  *m_av_clock;
  COMXAudioCodecOMX         *m_pAudioCodec;
  IAudioRenderer::EEncoded  m_passthrough;
  bool                      m_hw_decode;
  bool                      m_bAbort;
  bool                      m_flush;
  enum PCMChannels          *m_pChannelMap;
  OMXPacket                 *m_omx_pkt;
  unsigned int              m_cached_input_size;
  unsigned int              m_cached_output_size;
  void LockInput();
  void UnLockInput();
  void LockOutput();
  void UnLockOutput();
private:
public:
  OMXPlayerAudioCodec();
  ~OMXPlayerAudioCodec();
  bool Open(COMXStreamInfo &hints, OMXClock *av_clock, IAudioRenderer::EEncoded passthrough, bool hw_decode);
  bool Close();
  bool Decode(OMXPacket *pkt);
  void Process();
  void Flush();
  bool AddPacket(OMXPacket *pkt);
  OMXPacket *GetPacket();
  bool OpenAudioCodec();
  void CloseAudioCodec();
  double GetDelay();
  double GetCurrentPTS() { return m_iCurrentPts; };
  unsigned int GetCachedInput()   { return m_cached_input_size;   };
  unsigned int GetCachedOutput()  { return m_cached_output_size;  };
  enum PCMChannels* GetChannelMap();
  int GetSampleRate();
  int GetBitsPerSample();
  int GetChannels();
};
#endif
