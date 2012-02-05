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

#include "OMXReader.h"
#include "OMXClock.h"

#include <stdio.h>
#include <unistd.h>

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#endif

static bool g_abort = false;

void OMXReader::AddTimespecs(struct timespec &time, long millisecs)
{
   time.tv_sec  += millisecs / 1000;
   time.tv_nsec += (millisecs % 1000) * 1000000;
   if (time.tv_nsec > 1000000000)
   {
      time.tv_sec  += 1;
      time.tv_nsec -= 1000000000;
   }
}

#ifdef STANDALONE
/* Taken from libavformat/utils.c */
void OMXReader::flush_packet_queue(AVFormatContext *s)
{
  AVPacketList *pktl;

  for(;;) {
    pktl = s->packet_buffer;
    if (!pktl)
      break;
    s->packet_buffer = pktl->next;
    m_dllAvCodec.av_free_packet(&pktl->pkt);
    m_dllAvUtil.av_free(pktl);
  }
  while(s->raw_packet_buffer){
    pktl = s->raw_packet_buffer;
    s->raw_packet_buffer = pktl->next;
    m_dllAvCodec.av_free_packet(&pktl->pkt);
    m_dllAvUtil.av_free(pktl);
  }
  s->packet_buffer_end=
  s->raw_packet_buffer_end= NULL;
#ifdef RAW_PACKET_BUFFER_SIZE
  // Added on: 2009-06-25
  s->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;
#endif
}

/* Taken from libavformat/utils.c */
void OMXReader::av_read_frame_flush(AVFormatContext *s)
{
  AVStream *st;
  unsigned int i, j;

  flush_packet_queue(s);

  s->cur_st = NULL;

  /* for each stream, reset read state */
  for(i = 0; i < s->nb_streams; i++) {
    st = s->streams[i];

    if (st->parser) {
      av_parser_close(st->parser);
      st->parser = NULL;
      m_dllAvCodec.av_free_packet(&st->cur_pkt);
    }
    st->last_IP_pts = AV_NOPTS_VALUE;
    st->cur_dts = AV_NOPTS_VALUE; /* we set the current DTS to an unspecified origin */
    st->reference_dts = AV_NOPTS_VALUE;
    /* fail safe */
    /* fail safe */
    st->cur_ptr = NULL;
    st->cur_len = 0;

    for(j=0; j<MAX_REORDER_DELAY+1; j++)
      st->pts_buffer[j]= AV_NOPTS_VALUE;
  }
}
#endif

OMXReader::OMXReader()
{
  m_open        = false;
  m_filename    = "";
  m_bMatroska   = false;
  m_bAVI        = false;
  m_bMpeg       = false;
  g_abort       = false;
  m_pFile       = NULL;
  m_ioContext   = NULL;
  m_pFormatContext = NULL;
  m_video_count   = 0;
  m_audio_count   = 0;
  m_audio_index   = -1;
  m_video_index   = -1;
  m_eof           = false;
  m_chapter_count = 0;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pVideoStream  = NULL;
  m_pAudioStream  = NULL;
  m_seek_ms       = 0;
  m_seek_req      = false;

  pthread_cond_init(&m_packet_buffer_cond, NULL);
  pthread_mutex_init(&m_lock, NULL);
}

OMXReader::~OMXReader()
{
  Close();

  pthread_cond_destroy(&m_packet_buffer_cond);
  pthread_mutex_destroy(&m_lock);
}

void OMXReader::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void OMXReader::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

static int interrupt_cb(void)
{
  if(g_abort)
    return 1;
  return 0;
}

static int dvd_file_read(void *h, uint8_t* buf, int size)
{
  if(interrupt_cb())
    return -1;

  XFILE::CFile *pFile = (XFILE::CFile *)h;
  return pFile->Read(buf, size);
}

static offset_t dvd_file_seek(void *h, offset_t pos, int whence)
{
  if(interrupt_cb())
    return -1;

  XFILE::CFile *pFile = (XFILE::CFile *)h;
  if(whence == AVSEEK_SIZE)
    return pFile->GetLength();
  else
    return pFile->Seek(pos, whence & ~AVSEEK_FORCE);
}

bool OMXReader::Open(CStdString filename, bool dump_format)
{
  if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load())
    return false;
  
  if(ThreadHandle())
    Close();

  m_iCurrentPts = DVD_NOPTS_VALUE;
  m_filename    = filename; 
  m_speed       = DVD_PLAYSPEED_NORMAL;

  m_dllAvFormat.av_register_all();
  m_dllAvFormat.url_set_interrupt_cb(interrupt_cb);

  int           result    = -1;
  AVInputFormat *iformat  = NULL;
  unsigned char *buffer   = NULL;
  unsigned int  flags     = READ_TRUNCATED | READ_BITRATE | READ_CHUNKED;
#ifndef STANDALONE
  if( CFileItem(m_filename, false).IsInternetStream() )
    flags |= READ_CACHED;
#endif

  if(m_filename.substr(0, 8) == "shout://" )
    m_filename.replace(0, 8, "http://");

  if(m_filename.substr(0,6) == "mms://" || m_filename.substr(0,7) == "http://" || m_filename.substr(0,7) == "rtmp://")
  {
    result = m_dllAvFormat.av_open_input_file(&m_pFormatContext, m_filename.c_str(), iformat, FFMPEG_FILE_BUFFER_SIZE, NULL);
    if(result < 0)
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - av_open_input_file %s ", m_filename.c_str());
      Close();
      return false;
    }
  }
  else
  {
    m_pFile = new CFile();

    if (!m_pFile->Open(m_filename, flags))
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - %s ", m_filename.c_str());
      Close();
      return false;
    }

    buffer = (unsigned char*)m_dllAvUtil.av_malloc(FFMPEG_FILE_BUFFER_SIZE);
    m_ioContext = m_dllAvFormat.av_alloc_put_byte(buffer, FFMPEG_FILE_BUFFER_SIZE, 0, m_pFile, dvd_file_read, NULL, dvd_file_seek);
    m_ioContext->max_packet_size = 6144 /*m_pFile->GetChunkSize()*/;
    if(m_ioContext->max_packet_size)
      m_ioContext->max_packet_size *= FFMPEG_FILE_BUFFER_SIZE / m_ioContext->max_packet_size;

    if(m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL) == 0)
      m_ioContext->is_streamed = 1;

    m_dllAvFormat.av_probe_input_buffer(m_ioContext, &iformat, m_filename.c_str(), NULL, 0, 0);

    if(!iformat)
    {
      CLog::Log(LOGERROR, "COMXPlayer::OpenFile - av_probe_input_buffer %s ", m_filename.c_str());
      Close();
      return false;
    }

    result = m_dllAvFormat.av_open_input_stream(&m_pFormatContext, m_ioContext, m_filename.c_str(), iformat, NULL);
    if(result < 0)
    {
      Close();
      return false;
    }
  }

  m_bMatroska = strncmp(m_pFormatContext->iformat->name, "matroska", 8) == 0; // for "matroska.webm"
  m_bAVI = strcmp(m_pFormatContext->iformat->name, "avi") == 0;
  m_bMpeg = strcmp(m_pFormatContext->iformat->name, "mpeg") == 0;

  // if format can be nonblocking, let's use that
  m_pFormatContext->flags |= AVFMT_FLAG_NONBLOCK;

  // analyse very short to speed up mjpeg playback start
  if (iformat && (strcmp(iformat->name, "mjpeg") == 0) && m_ioContext->is_streamed)
    m_pFormatContext->max_analyze_duration = 500000;

#ifdef STANDALONE
  if(m_bAVI || m_bMatroska)
    m_pFormatContext->max_analyze_duration = 0;
#endif

  result = m_dllAvFormat.av_find_stream_info(m_pFormatContext);
  if(result < 0)
  {
    m_dllAvFormat.av_close_input_file(m_pFormatContext);
    Close();
    return false;
  }

  if(!GetStreams())
  {
    Close();
    return false;
  }

  if(m_pFile)
  {
    int64_t len = m_pFile->GetLength();
    int64_t tim = (int)(m_pFormatContext->duration / (AV_TIME_BASE / 1000));

    if(len > 0 && tim > 0)
    {
      unsigned rate = len * 1000 / tim;
      unsigned maxrate = rate + 1024 * 1024 / 8;
      if(m_pFile->IoControl(IOCTRL_CACHE_SETRATE, &maxrate) >= 0)
        CLog::Log(LOGDEBUG, "COMXPlayer::OpenFile - set cache throttle rate to %u bytes per second", maxrate);
    }
  }

  printf("file : %s reult %d format %s audio streams %d viodeo streams %d chapters %d\n", 
      m_filename.c_str(), result, m_pFormatContext->iformat->name, m_audio_count, m_video_count, m_chapter_count);


  m_speed       = DVD_PLAYSPEED_NORMAL;

  if(dump_format)
    m_dllAvFormat.dump_format(m_pFormatContext, 0, m_filename.c_str(), 0);

  UpdateCurrentPTS();

  Create();

  m_open        = true;

  m_duration_ms = (int)(m_pFormatContext->duration / (AV_TIME_BASE /  1000));
  return true;
}

bool OMXReader::Close()
{
  pthread_cond_broadcast(&m_packet_buffer_cond);

  if(ThreadHandle())
    StopThread();

  m_video_streams.clear();
  m_audio_streams.clear();

  FlushVideoPackets();
  FlushAudioPackets();

  if (m_pFormatContext)
  {
    if (m_ioContext)
    {
      if(m_pFormatContext->pb && m_pFormatContext->pb != m_ioContext)
      {
        CLog::Log(LOGWARNING, "OMXReader::Close - demuxer changed our byte context behind our back, possible memleak");
        m_ioContext = m_pFormatContext->pb;
      }
      m_dllAvFormat.av_close_input_stream(m_pFormatContext);
      if (m_ioContext->buffer)
        m_dllAvUtil.av_free(m_ioContext->buffer);
      m_dllAvUtil.av_free(m_ioContext);
    }
    else
      m_dllAvFormat.av_close_input_file(m_pFormatContext);
  }
  m_ioContext       = NULL;
  m_pFormatContext  = NULL;

  if(m_pFile)
  {
    m_pFile->Close();
    delete m_pFile;
    m_pFile = NULL;
  }

  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();

  m_open        = false;
  m_filename    = "";
  m_bMatroska   = false;
  m_bAVI        = false;
  m_bMpeg       = false;
  m_video_count = 0;
  m_audio_count = 0;
  m_audio_index = -1;
  m_video_index = -1;
  m_eof         = false;
  m_chapter_count = 0;
  m_iCurrentPts   = DVD_NOPTS_VALUE;
  m_pVideoStream = NULL;
  m_pAudioStream = NULL;
  m_seek_ms       = 0;
  m_seek_req      = false;
  m_speed         = DVD_PLAYSPEED_NORMAL;

  return true;
}

void OMXReader::FlushRead()
{
  if(!m_pFormatContext)
    return;

#ifdef STANDALONE
    av_read_frame_flush(m_pFormatContext);
#else
    m_dllAvFormat.av_read_frame_flush(m_pFormatContext);
#endif
}

bool OMXReader::SeekTime(int64_t seek_ms, int seek_flags, double *startpts)
{
  Lock();
  m_seek_flags = seek_flags;
  if(m_pFile && m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL))
  {
    m_seek_ms = seek_ms;
    m_seek_req = true;
  }

  //printf("m_seek_ms %lld seek_ms %lld\n", m_seek_ms, seek_ms);
  if(m_seek_req)
  {
    int64_t pos = m_seek_ms * 1000;
    if(pos < 0)
      pos = 0;

    FlushRead();

    int stream_index = -1;
    int64_t seek_target = pos;

    if(m_pVideoStream)
      stream_index = m_pVideoStream->index;
    else if(m_pAudioStream)
      stream_index = m_pAudioStream->index;

    if(stream_index >= 0)
    {
      seek_target = m_dllAvUtil.av_rescale_q(seek_target, AV_TIME_BASE_Q, m_pFormatContext->streams[stream_index]->time_base);
    }
    else
    {
      if(m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
        seek_target += m_pFormatContext->start_time;
    }

    int ret = m_dllAvFormat.av_seek_frame(m_pFormatContext, stream_index, seek_target, m_seek_flags);

    if(ret < 0)
    {
      printf("error while seeking seek_flags %d pos %f\n", m_seek_flags, (double)pos / AV_TIME_BASE);
      UnLock();
      return false;
    }
    else
    {
      FlushVideoPackets();
      FlushAudioPackets();
      UpdateCurrentPTS();
    }

    if(startpts)
      *startpts = DVD_MSEC_TO_TIME(seek_ms);

    m_seek_req = false;
    m_seek_ms = 0;
  }

  UnLock();
  return true;
}

void OMXReader::Process()
{
  OMXPacket *m_omx_pkt = NULL;
  int       result = -1;
  int       m_video_index_use = -1;
  int       m_audio_index_use = -1;

  m_video_index_use = m_video_index;
  m_audio_index_use = m_audio_index;

  while(!m_bStop)
  {
    if(!m_omx_pkt && !m_eof)
    {
      AVPacket  pkt;

      Lock();
      // assume we are not eof
      if(m_pFormatContext->pb)
        m_pFormatContext->pb->eof_reached = 0;

      m_dllAvCodec.av_init_packet(&pkt);

      pkt.size = 0;
      pkt.data = NULL;
      pkt.stream_index = MAX_OMX_STREAMS;

      result = m_dllAvFormat.av_read_frame(m_pFormatContext, &pkt);
      if (result < 0)
      {
        m_eof = true;
        m_bStop = true;
        FlushRead();
        UnLock();
        continue;
      }
      else if (pkt.size < 0 || pkt.stream_index >= MAX_OMX_STREAMS)
      {
        // XXX, in some cases ffmpeg returns a negative packet size
        if(m_pFormatContext->pb && !m_pFormatContext->pb->eof_reached)
        {
          CLog::Log(LOGERROR, "OMXReader::Process no valid packet");
          FlushRead();
        }

        m_dllAvCodec.av_free_packet(&pkt);

        m_eof = true;
        m_bStop = true;
        UnLock();
        continue;
      }

      AVStream *pStream = m_pFormatContext->streams[pkt.stream_index];

      // lavf sometimes bugs out and gives 0 dts/pts instead of no dts/pts
      // since this could only happens on initial frame under normal
      // circomstances, let's assume it is wrong all the time
      if(pkt.dts == 0)
        pkt.dts = AV_NOPTS_VALUE;
      if(pkt.pts == 0)
        pkt.pts = AV_NOPTS_VALUE;

      if(m_bMatroska && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      { // matroska can store different timestamps
        // for different formats, for native stored
        // stuff it is pts, but for ms compatibility
        // tracks, it is really dts. sadly ffmpeg
        // sets these two timestamps equal all the
        // time, so we select it here instead
        if(pStream->codec->codec_tag == 0)
          pkt.dts = AV_NOPTS_VALUE;
        else
          pkt.pts = AV_NOPTS_VALUE;
      }
      // we need to get duration slightly different for matroska embedded text subtitels
      if(m_bMatroska && pStream->codec->codec_id == CODEC_ID_TEXT && pkt.convergence_duration != 0)
        pkt.duration = pkt.convergence_duration;

      if(m_bAVI && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        // AVI's always have borked pts, specially if m_pFormatContext->flags includes
        // AVFMT_FLAG_GENPTS so always use dts
        pkt.pts = AV_NOPTS_VALUE;
      }

      m_omx_pkt = AllocPacket(pkt.size);
      /* oom error allocation av packet */
      if(!m_omx_pkt)
      {
        m_eof = true;
        m_dllAvCodec.av_free_packet(&pkt);
        UnLock();
        continue;
      }

      // copy contents into our own packet
      m_omx_pkt->size = pkt.size;

      if (pkt.data)
        memcpy(m_omx_pkt->data, pkt.data, m_omx_pkt->size);

      m_omx_pkt->dts = ConvertTimestamp(pkt.dts, pStream->time_base.den, pStream->time_base.num);
      m_omx_pkt->pts = ConvertTimestamp(pkt.pts, pStream->time_base.den, pStream->time_base.num);
      //m_omx_pkt->dts = ConvertTimestamp(pkt.dts, &pStream->time_base);
      //m_omx_pkt->pts = ConvertTimestamp(pkt.pts, &pStream->time_base);
      m_omx_pkt->duration = DVD_SEC_TO_TIME((double)pkt.duration * pStream->time_base.num / pStream->time_base.den);

      m_omx_pkt->stream_index = pkt.stream_index;
      m_omx_pkt->pStream = m_pFormatContext->streams[pkt.stream_index];

      // used to guess streamlength
      if (m_omx_pkt->dts != DVD_NOPTS_VALUE && (m_omx_pkt->dts > m_iCurrentPts || m_iCurrentPts == DVD_NOPTS_VALUE))
        m_iCurrentPts = m_omx_pkt->dts;

      // check if stream has passed full duration, needed for live streams
      if(pkt.dts != (int64_t)AV_NOPTS_VALUE)
      {
        int64_t duration;
        duration = pkt.dts;
        if(pStream->start_time != (int64_t)AV_NOPTS_VALUE)
          duration -= pStream->start_time;

        if(duration > pStream->duration)
        {
          pStream->duration = duration;
          duration = m_dllAvUtil.av_rescale_rnd(pStream->duration, (int64_t)pStream->time_base.num * AV_TIME_BASE, 
                                                pStream->time_base.den, AV_ROUND_NEAR_INF);
          if ((m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE && m_pFormatContext->file_size > 0)
              ||  (m_pFormatContext->duration != (int64_t)AV_NOPTS_VALUE && duration > m_pFormatContext->duration))
            m_pFormatContext->duration = duration;
        }
      }

      // check if stream seem to have grown since start
      if(m_pFormatContext->file_size > 0 && m_pFormatContext->pb)
      {
        if(m_pFormatContext->pb->pos > m_pFormatContext->file_size)
          m_pFormatContext->file_size = m_pFormatContext->pb->pos;
      }

      m_dllAvCodec.av_free_packet(&pkt);

      UnLock();
    }

    if(m_omx_pkt)
    {
      unsigned timeout = 100;
      struct timespec endtime;
      clock_gettime(CLOCK_REALTIME, &endtime);
      AddTimespecs(endtime, timeout);

      Lock();
      while (1 && !m_bStop)
      {
        //if(m_pVideoStream == m_pFormatContext->streams[m_omx_pkt->stream_index])
        if(m_pFormatContext->streams[m_omx_pkt->stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
          if(m_pkt_video.size() < MAX_OMX_VIDEO_PACKETS)
          {
            m_pkt_video.push(m_omx_pkt);
            m_omx_pkt = NULL;
            break;
          }
        }
        //else if(m_pAudioStream == m_pFormatContext->streams[m_omx_pkt->stream_index])
        else if(m_pFormatContext->streams[m_omx_pkt->stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        {
          if(m_pkt_audio.size() < MAX_OMX_AUDIO_PACKETS)
          {
            m_pkt_audio.push(m_omx_pkt);
            m_omx_pkt = NULL;
            break;
          }
        }
        else
        {
          FreePacket(m_omx_pkt);
          m_omx_pkt = NULL;
          break;
        }
        int retcode = pthread_cond_timedwait(&m_packet_buffer_cond, &m_lock, &endtime);
        if (retcode != 0) {
          CLog::Log(LOGERROR, "OMXReader::Process wait event timeout\n");
          break;
        }
      }
      UnLock();
    }

    Lock();
    /* nothing more to handle */
    if(m_pkt_video.empty() && m_pkt_audio.empty() && m_eof)
      m_bStop = true;
    UnLock();
    //printf("m_pkt_video.size %d m_pkt_audio.size %d\n", m_pkt_video.size(), m_pkt_audio.size());
  }

  if(m_omx_pkt)
    FreePacket(m_omx_pkt);

  m_bStop = true;
}

void OMXReader::FlushVideoPackets()
{
  if(!m_open)
    return;

  while (!m_pkt_video.empty())
  {
    OMXPacket *pkt = m_pkt_video.front(); 
    m_pkt_video.pop();
    FreePacket(pkt);
  }
}

void OMXReader::FlushAudioPackets()
{
  if(!m_open)
    return;

  while (!m_pkt_audio.empty())
  {
    OMXPacket *pkt = m_pkt_audio.front(); 
    m_pkt_audio.pop();
    FreePacket(pkt);
  }
}

bool OMXReader::GetStreams()
{
  if(!m_pFormatContext)
    return false;

  unsigned int    m_program         = UINT_MAX;

  if (m_pFormatContext->nb_programs)
  {
    // look for first non empty stream and discard nonselected programs
    for (unsigned int i = 0; i < m_pFormatContext->nb_programs; i++)
    {
      if(m_program == UINT_MAX && m_pFormatContext->programs[i]->nb_stream_indexes > 0)
        m_program = i;
      if(i != m_program)
        m_pFormatContext->programs[i]->discard = AVDISCARD_ALL;
      if(m_program != UINT_MAX)
      {
        // TODO: build stream array
        // add streams from selected program
        for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
        {
          int index = m_pFormatContext->programs[m_program]->stream_index[i];
          if(m_pFormatContext->streams[index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
          {
            m_video_streams.push_back(m_pFormatContext->streams[index]);
          }
          if(m_pFormatContext->streams[index]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
          {
            m_audio_streams.push_back(m_pFormatContext->streams[index]);
          }
        }
      }
    }
  }

  // if there were no programs or they were all empty, add all streams
  // if there were no programs or they were all empty, add all streams
  if (m_program == UINT_MAX)
  {
    // TODO: build stream array
    for (unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
    {
      if(m_pFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        m_video_streams.push_back(m_pFormatContext->streams[i]);
      }
      if(m_pFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
      {
        m_audio_streams.push_back(m_pFormatContext->streams[i]);
      }
    }
  }

  m_video_count = m_video_streams.size();
  m_audio_count = m_audio_streams.size();

  if(m_video_count)
  {
    m_pVideoStream = m_video_streams[0];
    m_video_index = 0;
  }

  if(m_audio_count)
  {
    m_pAudioStream = m_audio_streams[0];
    m_audio_index = 0;
  }

  GetHints(m_pVideoStream, &m_hints_video);
  GetHints(m_pAudioStream, &m_hints_audio);

  int i = 0;
  for(i = 0; i < MAX_OMX_CHAPTERS; i++)
  {
    m_chapters[i].name      = "";
    m_chapters[i].seekto_ms = 0;
    m_chapters[i].ts        = 0;
  }

  m_chapter_count = 0;

  if(m_pVideoStream)
  {
    //m_current_chapter = 0;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
    m_chapter_count = (m_pFormatContext->nb_chapters > MAX_OMX_CHAPTERS) ? MAX_OMX_CHAPTERS : m_pFormatContext->nb_chapters;
    for(i = 0; i < m_chapter_count; i++)
    {
      if(i > MAX_OMX_CHAPTERS)
        break;

      AVChapter *chapter = m_pFormatContext->chapters[i];
      if(!chapter)
        continue;

      m_chapters[i].seekto_ms = ConvertTimestamp(chapter->start, chapter->time_base.den, chapter->time_base.num) / 1000;
      //m_chapters[i].seekto_ms = ConvertTimestamp(chapter->start, &chapter->time_base) / 1000;
      m_chapters[i].ts        = m_chapters[i].seekto_ms / 1000;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
      AVMetadataTag *titleTag = m_dllAvFormat.av_metadata_get(m_pFormatContext->chapters[i]->metadata,"title", NULL, 0);
      if (titleTag)
        m_chapters[i].name = titleTag->value;
#else
      if(m_pFormatContext->chapters[i]->title)
        m_chapters[i].name = m_pFormatContext->chapters[i]->title;
#endif
      printf("Chapter : \t%d \t%s \t%8.2f\n", i, m_chapters[i].name.c_str(), m_chapters[i].ts);
    }
  }
#endif

  return true;
}

bool OMXReader::GetHints(AVStream *stream, COMXStreamInfo *hints)
{
  if(!hints || !stream)
    return false;

  hints->codec         = stream->codec->codec_id;
  hints->extradata     = stream->codec->extradata;
  hints->extrasize     = stream->codec->extradata_size;
  hints->codec         = stream->codec->codec_id;
  hints->extradata     = stream->codec->extradata;
  hints->extrasize     = stream->codec->extradata_size;
  hints->channels      = stream->codec->channels;
  hints->samplerate    = stream->codec->sample_rate;
  hints->blockalign    = stream->codec->block_align;
  hints->bitrate       = stream->codec->bit_rate;
  hints->bitspersample = stream->codec->bits_per_coded_sample;
  if(hints->bitspersample == 0)
    hints->bitspersample = 16;

  hints->width         = stream->codec->width;
  hints->height        = stream->codec->height;
  hints->profile       = stream->codec->profile;

  if(stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
  {
    hints->fpsrate       = stream->r_frame_rate.num;
    hints->fpsscale      = stream->r_frame_rate.den;

    if(m_bMatroska && stream->avg_frame_rate.den && stream->avg_frame_rate.num)
    {
      hints->fpsrate      = stream->avg_frame_rate.num;
      hints->fpsscale     = stream->avg_frame_rate.den;
    }
    else if(stream->r_frame_rate.num && stream->r_frame_rate.den)
    {
      hints->fpsrate      = stream->r_frame_rate.num;
      hints->fpsscale     = stream->r_frame_rate.den;
    }
    else
    {
      hints->fpsscale     = 0;
      hints->fpsrate      = 0;
    }

    if (stream->sample_aspect_ratio.num == 0)
      hints->aspect = 0.0f;
    else
      hints->aspect = av_q2d(stream->sample_aspect_ratio) * stream->codec->width / stream->codec->height;
  }

  return true;
}

OMXPacket *OMXReader::GetVideoPacket()
{
  OMXPacket *pkt = NULL;
  Lock();
  if(!m_pkt_video.empty())
  {
    pkt = m_pkt_video.front();
    m_pkt_video.pop();
    pthread_cond_broadcast(&m_packet_buffer_cond);
  }
  UnLock();

  return pkt;
}

OMXPacket *OMXReader::GetAudioPacket()
{
  OMXPacket *pkt = NULL;
  Lock();
  if(!m_pkt_audio.empty())
  {
    pkt = m_pkt_audio.front();
    m_pkt_audio.pop();
    pthread_cond_broadcast(&m_packet_buffer_cond);
  }
  UnLock();

  return pkt;
}

int OMXReader::GetVideoPacketsFree()
{
  int ret = 0;

  Lock();
  ret = MAX_OMX_VIDEO_PACKETS - m_pkt_video.size();
  UnLock();

  return ret;
}

int OMXReader::GetAudioPacketsFree()
{
  int ret = 0;

  Lock();
  ret = MAX_OMX_AUDIO_PACKETS - m_pkt_audio.size();
  UnLock();

  return ret;
}

void OMXReader::FreePacket(OMXPacket *pkt)
{
  if(pkt)
  {
    if(pkt->data)
      _aligned_free(pkt->data);
    free(pkt);
  }
}

OMXPacket *OMXReader::AllocPacket(int size)
{
  OMXPacket *pkt = (OMXPacket *)malloc(sizeof(OMXPacket));
  if(pkt)
  {
    memset(pkt, 0, sizeof(OMXPacket));

    pkt->data =(uint8_t*) _aligned_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE, 16);
    if(!pkt->data)
    {
      free(pkt);
      pkt = NULL;
    }
    else
    {
      memset(pkt->data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
      pkt->size = size;
      pkt->dts  = DVD_NOPTS_VALUE;
      pkt->pts  = DVD_NOPTS_VALUE;
    }
  }
  return pkt;
}

bool OMXReader::SetAudioStream(unsigned int index)
{
  if(m_pFormatContext == NULL)
    return false;

  if(m_audio_count < 1)
    return false;

  if(index > (m_audio_count - 1))
  {
    m_audio_index = (m_audio_count - 1);
  }
  else
  {
    m_audio_index = index;
  }

  pthread_cond_broadcast(&m_packet_buffer_cond);
  Lock();
  m_pAudioStream = m_audio_streams[m_audio_index];
  //FlushAudioPackets();
  //FlushVideoPackets();
  GetHints(m_pAudioStream, &m_hints_audio);
  UnLock();
  return true;
}

bool OMXReader::SeekChapter(int chapter, double* startpts)
{
  if(chapter < 1)
    chapter = 1;

  if(m_pFormatContext == NULL)
    return false;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  if(chapter < 1 || chapter > (int)m_pFormatContext->nb_chapters)
    return false;

  AVChapter *ch = m_pFormatContext->chapters[chapter-1];
  double dts = ConvertTimestamp(ch->start, ch->time_base.den, ch->time_base.num);
  //double dts = ConvertTimestamp(ch->start, &ch->time_base);
  return SeekTime(DVD_TIME_TO_MSEC(dts), 0, startpts);
#else
  return false;
#endif
}

double OMXReader::ConvertTimestamp(int64_t pts, int den, int num)
{
  if(m_pFormatContext == NULL)
    return false;

  if (pts == (int64_t)AV_NOPTS_VALUE)
    return DVD_NOPTS_VALUE;

  // do calculations in floats as they can easily overflow otherwise
  // we don't care for having a completly exact timestamp anyway
  double timestamp = (double)pts * num  / den;
  double starttime = 0.0f;

  // for dvd's we need the original time
  if (m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    starttime = (double)m_pFormatContext->start_time / AV_TIME_BASE;

  if(timestamp > starttime)
    timestamp -= starttime;
  else if( timestamp + 0.1f > starttime )
    timestamp = 0;

  return timestamp*DVD_TIME_BASE;
}

double OMXReader::ConvertTimestamp(int64_t pts, AVRational *time_base)
{
  double new_pts = pts;

  if(m_pFormatContext == NULL)
    return false;

  if (pts == (int64_t)AV_NOPTS_VALUE)
    return DVD_NOPTS_VALUE;

  if (m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
    new_pts += m_pFormatContext->start_time;

  new_pts *= av_q2d(*time_base);

  return (double)new_pts * DVD_TIME_BASE;
}

int OMXReader::GetChapter()
{
  if(m_pFormatContext == NULL
  || m_iCurrentPts == DVD_NOPTS_VALUE)
    return 0;

#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  for(unsigned i = 0; i < m_pFormatContext->nb_chapters; i++)
  {
    AVChapter *chapter = m_pFormatContext->chapters[i];
    if(m_iCurrentPts >= ConvertTimestamp(chapter->start, chapter->time_base.den, chapter->time_base.num)
      && m_iCurrentPts <  ConvertTimestamp(chapter->end,   chapter->time_base.den, chapter->time_base.num))
    //if(m_iCurrentPts >= ConvertTimestamp(chapter->start, &chapter->time_base)
    //  && m_iCurrentPts <  ConvertTimestamp(chapter->end, &chapter->time_base))
      return i + 1;
  }
#endif
  return 0;
}

void OMXReader::GetChapterName(std::string& strChapterName)
{
  strChapterName = "";
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
  int chapterIdx = GetChapter();
  if(chapterIdx <= 0)
    return;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
  // API added on: 2010-10-15
  // (Note that while the function was available earlier, the generic
  // metadata tags were not populated by default)
  AVMetadataTag *titleTag = m_dllAvFormat.av_metadata_get(m_pFormatContext->chapters[chapterIdx-1]->metadata,
                                                              "title", NULL, 0);
  if (titleTag)
    strChapterName = titleTag->value;
#else
  if (m_pFormatContext->chapters[chapterIdx-1]->title)
    strChapterName = m_pFormatContext->chapters[chapterIdx-1]->title;
#endif
#endif
}

void OMXReader::UpdateCurrentPTS()
{
  m_iCurrentPts = DVD_NOPTS_VALUE;
  for(unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
  {
    AVStream *stream = m_pFormatContext->streams[i];
    if(stream && stream->cur_dts != (int64_t)AV_NOPTS_VALUE)
    {
      double ts = ConvertTimestamp(stream->cur_dts, stream->time_base.den, stream->time_base.num);
      //double ts = ConvertTimestamp(stream->cur_dts, &stream->time_base);
      if(m_iCurrentPts == DVD_NOPTS_VALUE || m_iCurrentPts > ts )
        m_iCurrentPts = ts;
    }
  }
}

void OMXReader::SetSpeed(int iSpeed)
{
  if(!m_pFormatContext)
    return;

  if(m_speed != DVD_PLAYSPEED_PAUSE && iSpeed == DVD_PLAYSPEED_PAUSE)
  {
    m_dllAvFormat.av_read_pause(m_pFormatContext);
  }
  else if(m_speed == DVD_PLAYSPEED_PAUSE && iSpeed != DVD_PLAYSPEED_PAUSE)
  {
    m_dllAvFormat.av_read_play(m_pFormatContext);
  }
  m_speed = iSpeed;

  AVDiscard discard = AVDISCARD_NONE;
  if(m_speed > 4*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_NONKEY;
  else if(m_speed > 2*DVD_PLAYSPEED_NORMAL)
    discard = AVDISCARD_BIDIR;
  else if(m_speed < DVD_PLAYSPEED_PAUSE)
    discard = AVDISCARD_NONKEY;

  for(unsigned int i = 0; i < m_pFormatContext->nb_streams; i++)
  {
    if(m_pFormatContext->streams[i])
    {
      if(m_pFormatContext->streams[i]->discard != AVDISCARD_ALL)
        m_pFormatContext->streams[i]->discard = discard;
    }
  }
}

int OMXReader::GetStreamLength()
{
  if (!m_pFormatContext)
    return 0;

  /* apperently ffmpeg messes up sometimes, so check for negative value too */
  if (m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE || m_pFormatContext->duration < 0LL)
  {
    // no duration is available for us
    // try to calculate it
    int iLength = 0;
    if (m_iCurrentPts != DVD_NOPTS_VALUE && m_pFormatContext->file_size > 0 && m_pFormatContext->pb && m_pFormatContext->pb->pos > 0)
    {
      iLength = (int)(((m_iCurrentPts * m_pFormatContext->file_size) / m_pFormatContext->pb->pos) / 1000) & 0xFFFFFFFF;
    }
    return iLength;
  }

  return (int)(m_pFormatContext->duration / (AV_TIME_BASE / 1000));
}

double OMXReader::NormalizeFrameduration(double frameduration)
{
  //if the duration is within 20 microseconds of a common duration, use that
  const double durations[] = {DVD_TIME_BASE * 1.001 / 24.0, DVD_TIME_BASE / 24.0, DVD_TIME_BASE / 25.0,
                              DVD_TIME_BASE * 1.001 / 30.0, DVD_TIME_BASE / 30.0, DVD_TIME_BASE / 50.0,
                              DVD_TIME_BASE * 1.001 / 60.0, DVD_TIME_BASE / 60.0};

  double lowestdiff = DVD_TIME_BASE;
  int    selected   = -1;
  for (size_t i = 0; i < sizeof(durations) / sizeof(durations[0]); i++)
  {
    double diff = fabs(frameduration - durations[i]);
    if (diff < DVD_MSEC_TO_TIME(0.02) && diff < lowestdiff)
    {
      selected = i;
      lowestdiff = diff;
    }
  }

  if (selected != -1)
    return durations[selected];
  else
    return frameduration;
}

void OMXReader::GetStreamCodecName(AVStream *stream, CStdString &strStreamName)
{
  strStreamName = "";

  if(!stream)
    return;

  unsigned int in = stream->codec->codec_tag;
  // FourCC codes are only valid on video streams, audio codecs in AVI/WAV
  // are 2 bytes and audio codecs in transport streams have subtle variation
  // e.g AC-3 instead of ac3
  if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && in != 0)
  {
    char fourcc[5];
    memcpy(fourcc, &in, 4);
    fourcc[4] = 0;
    // fourccs have to be 4 characters
    if (strlen(fourcc) == 4)
    {
      strStreamName = fourcc;
      strStreamName.MakeLower();
      return;
    }
  }

#ifdef FF_PROFILE_DTS_HD_MA
  /* use profile to determine the DTS type */
  if (stream->codec->codec_id == CODEC_ID_DTS)
  {
    if (stream->codec->profile == FF_PROFILE_DTS_HD_MA)
      strStreamName = "dtshd_ma";
    else if (stream->codec->profile == FF_PROFILE_DTS_HD_HRA)
      strStreamName = "dtshd_hra";
    else
      strStreamName = "dca";
    return;
  }
#endif

  AVCodec *codec = m_dllAvCodec.avcodec_find_decoder(stream->codec->codec_id);

  if (codec)
    strStreamName = codec->name;
}

CStdString OMXReader::GetVideoCodecName()
{
  CStdString strStreamName;

  Lock();
  GetStreamCodecName(m_pVideoStream, strStreamName);
  UnLock();

  return strStreamName;
}

CStdString OMXReader::GetAudioCodecName()
{
  CStdString strStreamName;

  Lock();
  GetStreamCodecName(m_pAudioStream, strStreamName);
  UnLock();

  return strStreamName;
}

CStdString OMXReader::GetVideoCodecName(int index)
{
  CStdString strStreamName;

  if((index + 1) > m_video_streams.size())
    return strStreamName;

  Lock();
  GetStreamCodecName(m_video_streams[index], strStreamName);
  UnLock();

  return strStreamName;
}

CStdString OMXReader::GetAudioCodecName(int index)
{
  CStdString strStreamName;

  if((index + 1) > m_audio_streams.size())
    return strStreamName;

  Lock();
  GetStreamCodecName(m_audio_streams[index], strStreamName);
  UnLock();

  return strStreamName;
}

bool OMXReader::GetAudioStreamLanguage(int iStream, CStdString &strLanguage)
{
  char language[4];
  memset(language, 0, sizeof(language));

  Lock();
  if((unsigned int)iStream > m_audio_streams.size())
  {
    UnLock();
    return false;
  }

  AVStream *stream = m_audio_streams[iStream];
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,83,0)
    // API added on: 2010-10-15
    // (Note that while the function was available earlier, the generic
    // metadata tags were not populated by default)
  AVMetadataTag *langTag = m_dllAvFormat.av_metadata_get(stream->metadata, "language", NULL, 0);
  if (langTag)
    strncpy(language, langTag->value, 3);
#else
  strcpy(language, stream->language );
#endif

  if(language[0] != 0)
    strLanguage = language;
  UnLock();

  return true;
}

#ifndef STANDALONE
int OMXReader::GetSourceBitrate()
{
  int ret = 0;

  if(!m_pFile)
    return 0;

  if(m_pFile->GetBitstreamStats())
  {
    BitstreamStats *status = m_pFile->GetBitstreamStats();
    ret = status->GetBitrate();
  }

  return ret;
}
#endif

COMXStreamInfo OMXReader::GetVideoHints(int index)
{
  COMXStreamInfo hints;

  if((index + 1) > m_video_streams.size())
    return hints;

  GetHints(m_video_streams[index], &hints);

  return hints;  
}

COMXStreamInfo OMXReader::GetAudioHints(int index)
{
  COMXStreamInfo hints;

  if((index + 1) > m_audio_streams.size())
    return hints;

  GetHints(m_audio_streams[index], &hints);

  return hints;
}

