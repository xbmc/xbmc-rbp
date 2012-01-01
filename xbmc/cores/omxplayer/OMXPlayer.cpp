/*
 *      Copyright (C) 2011 Team XBMC
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

#include "system.h"

#if defined (HAVE_OMXPLAYER)
#include "OMXPlayer.h"
#include "Application.h"
#include "FileItem.h"
#include "GUIInfoManager.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "filesystem/File.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/MathUtils.h"
#include "utils/TimeUtils.h"
#include "utils/URIUtils.h"
#include "utils/XMLUtils.h"

#include "FileItem.h"
#include "filesystem/File.h"
#include "utils/BitstreamStats.h"

#include "utils/LangCodeExpander.h"
#include "utils/StreamDetails.h"

#include <sstream>
#include <iomanip>

#include "BitstreamConverter.h"

#define FFMPEG_FILE_BUFFER_SIZE   32768 // default reading size for ffmpeg
#ifndef MAX_STREAMS
#define MAX_STREAMS 100
#endif

using namespace XFILE;

// ****************************************************************
// ****************************************************************
COMXPlayer::COMXPlayer(IPlayerCallback &callback) 
  : IPlayer(callback),
  CThread("COMXPlayer"),
  m_ready(true)
{
  m_speed = 1;
  m_paused = false;
  m_StopPlaying = false;

  m_OMX.Initialize();

  m_av_clock = new OMXClock();
}

COMXPlayer::~COMXPlayer()
{
  CloseFile();

  if(m_av_clock)
    delete m_av_clock;
  m_av_clock = NULL;

  m_OMX.Deinitialize();
}

bool COMXPlayer::Initialize(TiXmlElement* pConfig)
{
  return true;
}

bool COMXPlayer::GetHints(AVStream *stream, COMXStreamInfo *hints)
{
  if(!hints || !stream)
    return false;

  //hints->codec_fourcc  = stream->codec->codec_tag;
  hints->width         = stream->codec->width;
  hints->height        = stream->codec->height;
  hints->codec         = stream->codec->codec_id;
  hints->extradata     = stream->codec->extradata;
  hints->extrasize     = stream->codec->extradata_size;
  hints->profile       = stream->codec->profile;
  hints->fpsscale      = stream->r_frame_rate.num;
  hints->fpsrate       = stream->r_frame_rate.den;
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


  return true;
}

bool COMXPlayer::GetStreams()
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
          if(m_pFormatContext->streams[m_pFormatContext->programs[m_program]->stream_index[i]]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
          {
            m_video_streams.push_back(m_pFormatContext->streams[m_pFormatContext->programs[m_program]->stream_index[i]]);
          }
          if(m_pFormatContext->streams[m_pFormatContext->programs[m_program]->stream_index[i]]->codec->codec_type == AVMEDIA_TYPE_AUDIO)
          {
            m_audio_streams.push_back(m_pFormatContext->streams[m_pFormatContext->programs[m_program]->stream_index[i]]);
          }
        }
      }
    }
  }

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

  return true;
}

bool COMXPlayer::OpenVideoDecoder(AVStream *stream)
{
  if(!stream)
    return false;
  
  GetHints(stream, &m_hints_video);

  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  m_video_width   = g_settings.m_ResInfo[res].iWidth;
  m_video_height  = g_settings.m_ResInfo[res].iHeight;

  m_hints_video.fpsrate       = stream->r_frame_rate.num;
  m_hints_video.fpsscale      = stream->r_frame_rate.den;

  if(m_bMatroska && stream->avg_frame_rate.den && stream->avg_frame_rate.num)
  {
    m_hints_video.fpsrate      = stream->avg_frame_rate.num;
    m_hints_video.fpsscale     = stream->avg_frame_rate.den;
  }
  else if(stream->r_frame_rate.num && stream->r_frame_rate.den)
  {
    m_hints_video.fpsrate      = stream->r_frame_rate.num;
    m_hints_video.fpsscale     = stream->r_frame_rate.den;
  }
  else
  {
    m_hints_video.fpsscale     = 0;
    m_hints_video.fpsrate      = 0;
  }

  if (stream->sample_aspect_ratio.num == 0)
    m_hints_video.aspect = 0.0f;
  else
    m_hints_video.aspect = av_q2d(stream->sample_aspect_ratio) * stream->codec->width / stream->codec->height;

  m_hints_video.extradata     = stream->codec->extradata;
  m_hints_video.extrasize     = stream->codec->extradata_size;

  if (m_hints_video.fpsrate && m_hints_video.fpsscale)
    m_video_fps = DVD_TIME_BASE / OMXClock::NormalizeFrameduration((double)DVD_TIME_BASE * m_hints_video.fpsscale / m_hints_video.fpsrate);
  else
    m_video_fps = 25;

  if( m_video_fps > 100 || m_video_fps < 5 )
  {
    printf("Invalid framerate %d, using forced 25fps and just trust timestamps\n", (int)m_video_fps);
    m_video_fps = 25;
  }

  m_frametime = (double)DVD_TIME_BASE / m_video_fps;

  m_video_decoder = new COMXVideo();
  m_VideoCodecOpen = m_video_decoder->Open(m_hints_video, m_av_clock, true);

  if(!m_VideoCodecOpen)
  {
    delete m_video_decoder;
    m_video_decoder = NULL;
    return false;
  }
  else
  {
    printf("Video codec 0x%08x width %d height %d profile %d r_frame_rate.num %d r_frame_rate.den %d m_VideoCodecOpen %d\n",
        m_hints_video.codec, m_hints_video.width, m_hints_video.height, m_hints_video.profile, m_hints_video.fpsscale, m_hints_video.fpsrate, m_VideoCodecOpen);

    int i = 0;
    for(i = 0; i < MAX_CHAPTERS; i++)
    {
      m_chapters[i].name      = "";
      m_chapters[i].seekto_ms = 0;
      m_chapters[i].ts        = 0;
    }

    //m_current_chapter = 0;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(52,14,0)
    m_chapter_count = (m_pFormatContext->nb_chapters > MAX_CHAPTERS) ? MAX_CHAPTERS : m_pFormatContext->nb_chapters;
    for(i = 0; i < m_chapter_count; i++)
    {
      if(i > MAX_CHAPTERS)
        break;

      AVChapter *chapter = m_pFormatContext->chapters[i];
      if(!chapter)
        continue;

      m_chapters[i].seekto_ms = OMXClock::ConvertTimestamp(chapter->start, m_pFormatContext->start_time, &m_pVideoStream->time_base) / AV_TIME_BASE / 1000;
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
#endif
  }

  m_video_codec_name = m_video_decoder->GetDecoderName();
  return true;
}

void COMXPlayer::CloseVideoDecoder()
{
  if(m_video_decoder)
    delete m_video_decoder;
  m_video_decoder   = NULL;
  m_VideoCodecOpen  = false;

  m_chapter_count   = 0;

  m_video_codec_name = "";
}

bool COMXPlayer::OpenAudioCodec(AVStream *stream)
{
  if(!stream)
    return false;

  GetHints(stream, &m_hints_audio);

  m_pAudioCodec = new COMXAudioCodecOMX();
  m_AudioCodecOpen = m_pAudioCodec->Open(m_hints_audio);

  if(!m_AudioCodecOpen)
  {
    delete m_pAudioCodec; m_pAudioCodec = NULL;
    return false;
  }

  GetStreamCodecName(stream, m_audio_codec_name);
  return true;
}

void COMXPlayer::CloseAudioCodec()
{
  if(m_pAudioCodec)
    delete m_pAudioCodec;
  m_pAudioCodec = NULL;

  m_AudioCodecOpen  = false;

  m_audio_codec_name = "";
}

bool COMXPlayer::IsPassthrough(AVStream *stream)
{
  if(!stream || !m_pAudioCodec)
    return false;

  GetHints(stream, &m_hints_audio);

  int  m_outputmode = 0;
  bool bitstream = false;
  bool passthrough = false;

  m_outputmode = g_guiSettings.GetInt("audiooutput.mode");

  switch(m_outputmode)
  {
    case 0:
      passthrough = false;
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
    if(m_hints_audio.codec == CODEC_ID_AC3 && g_guiSettings.GetBool("audiooutput.ac3passthrough"))
    {
      passthrough = true;
    }
    if(m_hints_audio.codec == CODEC_ID_DTS && g_guiSettings.GetBool("audiooutput.dtspassthrough"))
    {
      passthrough = true;
    }
  }

  return passthrough;
}

bool COMXPlayer::OpenAudioDecoder(AVStream *stream)
{
  if(!stream || !m_pAudioCodec)
    return false;

  GetHints(stream, &m_hints_audio);

  m_pChannelMap = m_pAudioCodec->GetChannelMap();

  m_audio_render = new COMXAudio();
  m_audio_render->SetClock(m_av_clock);

  CStdString deviceString;

  if(m_Passthrough)
  {
    m_HWDecode = false;

    deviceString = g_guiSettings.GetString("audiooutput.passthroughdevice");
    m_audio_render->SetCodingType(m_hints_audio.codec);

    //m_hints_audio.channels = 2;
    m_AudioRenderOpen = m_audio_render->Initialize(NULL, deviceString.substr(4), m_pChannelMap,
                                                   m_hints_audio, m_av_clock, m_Passthrough, m_HWDecode);
  }
  else
  {
    deviceString = g_guiSettings.GetString("audiooutput.audiodevice");
    m_audio_render->SetCodingType(CODEC_ID_PCM_S16LE);

    if(m_HWDecode)
    {
      m_AudioRenderOpen = m_audio_render->Initialize(NULL, deviceString.substr(4), m_pChannelMap,
                                                     m_hints_audio, m_av_clock, m_Passthrough, m_HWDecode);
    }
    else
    {
      m_AudioRenderOpen = m_audio_render->Initialize(NULL, deviceString.substr(4), m_pAudioCodec->GetChannels(), m_pChannelMap,
          m_pAudioCodec->GetSampleRate(), m_pAudioCodec->GetBitsPerSample(), false, false, m_Passthrough);
    }
  }

  if(!m_AudioRenderOpen)
  {
    delete m_audio_render; m_audio_render = NULL;
    return false;
  }
  else
  {
    if(m_Passthrough)
    {
      printf("Audio codec 0x%08x channels %d samplerate %d bitspersample %d m_AudioCodecOpen %d\n",
        m_hints_audio.codec, 2, m_hints_audio.samplerate, m_hints_audio.bitspersample, m_AudioCodecOpen);
    }
    else
    {
      printf("Audio codec 0x%08x channels %d samplerate %d bitspersample %d m_AudioCodecOpen %d\n",
        m_hints_audio.codec, m_hints_audio.channels, m_hints_audio.samplerate, m_hints_audio.bitspersample, m_AudioCodecOpen);
    }
  }

  GetStreamCodecName(stream, m_audio_codec_name);
  return true;
}

void COMXPlayer::CloseAudioDecoder()
{
  if(m_audio_render)
    delete m_audio_render;
  m_audio_render  = NULL;

  m_AudioRenderOpen = false;

  m_audio_codec_name = "";
}

unsigned int g_abort = false;

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

bool COMXPlayer::OpenFile(const CFileItem &file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "COMXPlayer: Opening: %s", file.GetPath().c_str());
    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if(ThreadHandle())
      CloseFile();

    unsigned char *buffer = NULL;
    int           result  = -1;
    AVInputFormat       *iformat          = NULL;

    std::string url;

    m_item = file;
    m_options = options;
    m_StopPlaying = false;

    m_elapsed_ms  = 0;
    m_duration_ms = 0;

    m_audio_index = 0;
    m_audio_count = 0;

    m_video_index = 0;
    m_video_count = 0;
    m_video_fps   = 0.0;
    m_video_width = 0;
    m_video_height= 0;

    m_subtitle_index = 0;
    m_subtitle_count = 0;
    m_chapter_count  = 0;

    m_subtitle_show  = g_settings.m_currentVideoSettings.m_SubtitleOn;

    SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);
    SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

    m_pFile          = NULL;
    
    m_hints_audio.Clear();
    m_hints_video.Clear();

    m_pFormatContext  = NULL;
    m_ioContext       = NULL;

    m_seek_ms         = 0;
    m_seek_req        = false;

    m_pVideoStream    = NULL;
    m_pAudioStream    = NULL;

    m_AudioCodecOpen  = false;
    m_VideoCodecOpen  = false;
    m_AudioRenderOpen = false;

    m_pAudioCodec     = NULL;
    m_audio_render    = NULL;
    m_video_decoder   = NULL;

    m_audio_codec_name = "";
    m_video_codec_name = "";

    // open file and start playing here.

    m_bMatroska       = false;
    m_bAVI            = false;
    m_last_pts        = 0;
    m_videoClock      = 0;
    m_audioClock      = 0;
    m_frametime       = 0;
    m_pkt_consumed    = true;
    m_buffer_seek     = true;

    m_Passthrough     = false;
    m_HWDecode        = false;
    m_use_hw_audio    = g_advancedSettings.m_omHWAudioDecode;

    m_dst_rect.SetRect(0, 0, 0, 0);

    m_filename = file.GetPath();
    
    if (!m_dllAvUtil.Load() || !m_dllAvCodec.Load() || !m_dllAvFormat.Load() || !m_BcmHostDisplay.Load() || !m_BcmHost.Load())
      return false;

    memset(&m_tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
    m_BcmHost.vc_tv_get_state(&m_tv_state);

    unsigned int flags = READ_TRUNCATED | READ_BITRATE | READ_CHUNKED;
    if( CFileItem(m_filename, false).IsInternetStream() )
      flags |= READ_CACHED;

    m_dllAvFormat.av_register_all();

    m_dllAvFormat.url_set_interrupt_cb(interrupt_cb);

    if(m_filename.substr(0, 8) == "shout://" )
      m_filename.replace(0, 8, "http://");

    if(m_filename.substr(0,6) == "mms://" || m_filename.substr(0,7) == "http://" || m_filename.substr(0,7) == "rtmp://")
    {
      result = m_dllAvFormat.av_open_input_file(&m_pFormatContext, m_filename.c_str(), iformat, FFMPEG_FILE_BUFFER_SIZE, NULL);
      if(result < 0)
      {
        CloseFile();
        return false;
      }
    }
    else
    {
      m_pFile = new CFile();

      if (!m_pFile->Open(m_filename, flags))
      {
        CloseFile();
        return false;
      }

      buffer = (unsigned char*)m_dllAvUtil.av_malloc(FFMPEG_FILE_BUFFER_SIZE);
      m_ioContext = m_dllAvFormat.av_alloc_put_byte(buffer, FFMPEG_FILE_BUFFER_SIZE, 0, m_pFile, dvd_file_read, NULL, dvd_file_seek);
      m_ioContext->max_packet_size = m_pFile->GetChunkSize();
      if(m_ioContext->max_packet_size)
        m_ioContext->max_packet_size *= FFMPEG_FILE_BUFFER_SIZE / m_ioContext->max_packet_size;

      if(m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL) == 0)
        m_ioContext->is_streamed = 1;

      m_dllAvFormat.av_probe_input_buffer(m_ioContext, &iformat, m_filename.c_str(), NULL, 0, 0);

      if(!iformat)
      {
        CloseFile();
        return false;
      }

      result = m_dllAvFormat.av_open_input_stream(&m_pFormatContext, m_ioContext, m_filename.c_str(), iformat, NULL);
      if(result < 0)
      {
        CloseFile();
        return false;
      }
    }

    printf("file : %s reult %d format %s\n", m_filename.c_str(), result, m_pFormatContext->iformat->name);

    m_bMatroska = strncmp(m_pFormatContext->iformat->name, "matroska", 8) == 0; // for "matroska.webm"
    m_bAVI = strcmp(m_pFormatContext->iformat->name, "avi") == 0;
    m_bMpeg = strcmp(m_pFormatContext->iformat->name, "mpeg") == 0;

    // if format can be nonblocking, let's use that
    m_pFormatContext->flags |= AVFMT_FLAG_NONBLOCK;
    if(m_bMatroska || m_bAVI)
      m_pFormatContext->max_analyze_duration = 0;
    else
      m_pFormatContext->max_analyze_duration = 5000000;

    result = m_dllAvFormat.av_find_stream_info(m_pFormatContext);
    if(result < 0)
    {
      m_dllAvFormat.av_close_input_file(m_pFormatContext);
      CloseFile();
      return false;
    }

    if(!GetStreams())
    {
      CloseFile();
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

    m_ready.Reset();

    g_renderManager.PreInit();

    Create();

    if (!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy* dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while(!m_ready.WaitMSec(1))
        g_windowManager.ProcessRenderLoop(false);
      dialog->Close();
    }
    // just in case process thread throws.
    //m_ready.Set();

    // Playback might have been stopped due to some error
    if (m_bStop || m_StopPlaying)
      return false;

    return true;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "%s - Exception thrown on open", __FUNCTION__);
    return false;
  }
}

bool COMXPlayer::CloseFile()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::CloseFile");

  m_StopPlaying = true;

  CLog::Log(LOGDEBUG, "COMXPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();
  
  m_av_clock->Pause();

  CloseVideoDecoder();
  CloseAudioDecoder();
  CloseAudioCodec();

  m_video_streams.clear();
  m_audio_streams.clear();

  /* nobody tooked care about the av packet */
  if(!m_pkt_consumed)
  {
    m_dllAvCodec.av_free_packet(&m_pkt);
    m_pkt_consumed = true;
  }

  if (m_pFormatContext)
  {
    if (m_ioContext)
    {
      if(m_pFormatContext->pb && m_pFormatContext->pb != m_ioContext)
      {
        CLog::Log(LOGWARNING, "OMXPlayer::CloseFile - demuxer changed our byte context behind our back, possible memleak");
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

  if (m_pFile)
  {
    m_pFile->Close();
    delete m_pFile;
    m_pFile = NULL;
  }
  
  //m_BcmHost.vc_tv_hdmi_power_on_best(m_tv_state.width, m_tv_state.height, m_tv_state.frame_rate,
  //                                   HDMI_NONINTERLACED, HDMI_MODE_MATCH_FRAMERATE);
  m_dllAvUtil.Unload();
  m_dllAvCodec.Unload();
  m_dllAvFormat.Unload();
  m_BcmHostDisplay.Unload();
  m_BcmHost.Unload();

  CLog::Log(LOGDEBUG, "COMXPlayer: finished waiting");

  g_renderManager.UnInit();

  return true;
}

bool COMXPlayer::IsPlaying() const
{
  return !m_bStop;
}

void COMXPlayer::Pause()
{
  CSingleLock lock(m_csection);

  if(!m_av_clock)
    return;

  if (m_StopPlaying)
    return;

  if (m_paused == true)
  {
    // pause here
    m_callback.OnPlayBackResumed();
  }
  else
  {
    // unpause here
    m_callback.OnPlayBackPaused();
  }
  m_paused = !m_paused;
}

bool COMXPlayer::IsPaused() const
{
  return m_paused;
}

bool COMXPlayer::HasVideo() const
{
  return (m_video_count > 0);
}

bool COMXPlayer::HasAudio() const
{
  return (m_audio_count > 0);
}

void COMXPlayer::ToggleFrameDrop()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::ToggleFrameDrop");
}

bool COMXPlayer::CanSeek()
{
  return m_bMpeg ? 0 : GetTotalTime() > 0;
}

void COMXPlayer::Seek(bool bPlus, bool bLargeStep)
{
  int chapter_index = GetChapter();
  if (bLargeStep)
  {
    // seek to next chapter
    if (bPlus && chapter_index < m_chapter_count)
    {
      SeekChapter(chapter_index + 1);
      return;
    }
    // seek to previous chapter
    if (!bPlus && chapter_index)
    {
      SeekChapter(chapter_index - 1);
      return;
    }
  }

  // update m_elapsed_ms and m_duration_ms.
  GetTime();
  GetTotalTime();

  int64_t seek_ms;
  if (g_advancedSettings.m_videoUseTimeSeeking &&
    (GetTotalTime() > (2 * g_advancedSettings.m_videoTimeSeekForwardBig)))
  {
    if (bLargeStep)
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForwardBig : g_advancedSettings.m_videoTimeSeekBackwardBig;
    else
      seek_ms = bPlus ? g_advancedSettings.m_videoTimeSeekForward    : g_advancedSettings.m_videoTimeSeekBackward;
    // convert to milliseconds
    seek_ms *= 1000;
    seek_ms += m_elapsed_ms;
  }
  else
  {
    float percent;
    if (bLargeStep)
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForwardBig : g_advancedSettings.m_videoPercentSeekBackwardBig;
    else
      percent = bPlus ? g_advancedSettings.m_videoPercentSeekForward    : g_advancedSettings.m_videoPercentSeekBackward;
    percent /= 100.0f;
    percent += (float)m_elapsed_ms/(float)m_duration_ms;
    // convert to milliseconds
    seek_ms = m_duration_ms * percent;
  }

  // handle stacked videos, dvdplayer does it so we do it too.
  if (g_application.CurrentFileItem().IsStack() &&
    (seek_ms > m_duration_ms || seek_ms < 0))
  {
    CLog::Log(LOGDEBUG, "COMXPlayer::Seek: In mystery code, what did I do");
    g_application.SeekTime((seek_ms - m_elapsed_ms) * 0.001 + g_application.GetTime());
    // warning, don't access any object variables here as
    // the object may have been destroyed
    return;
  }

  if (seek_ms > m_duration_ms)
    seek_ms = m_duration_ms;

  g_infoManager.SetDisplayAfterSeek(100000);
  SeekTime(seek_ms);
  m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - m_elapsed_ms));
  g_infoManager.SetDisplayAfterSeek();
}

bool COMXPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "COMXPlayer::SeekScene");
  return false;
}

void COMXPlayer::SeekPercentage(float fPercent)
{
  if (!m_duration_ms)
    return;

  SeekTime((int64_t)(m_duration_ms * fPercent / 100));
  /*
  // update m_elapsed_ms and m_duration_ms.
  GetTime();
  GetTotalTime();

  fPercent /= 100.0f;
  fPercent += (float)m_elapsed_ms/(float)m_duration_ms;
  // convert to milliseconds
  int64_t seek_ms = m_duration_ms * fPercent;
  SeekTime(seek_ms);
  */
}

float COMXPlayer::GetPercentage()
{
  // update m_elapsed_ms and m_duration_ms.
  GetTime();
  GetTotalTime();
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

float COMXPlayer::GetCachePercentage()
{
  CSingleLock lock(m_csection);
  return std::min(100.0, (double)(GetPercentage() + GetCacheLevel()));
}

void COMXPlayer::SetAVDelay(float fValue)
{
  // time offset in seconds of audio with respect to video
  m_audio_offset_ms = fValue * 1e3;
  // set a/v offset here
}

float COMXPlayer::GetAVDelay()
{
  return ((float)m_audio_offset_ms / 1e3);
}

void COMXPlayer::SetSubTitleDelay(float fValue)
{
  // time offset in seconds of subtitle with respect to playback
  m_subtitle_offset_ms = fValue * 1e3;
  // set sub offset here
}

float COMXPlayer::GetSubTitleDelay()
{
  return ((float)m_subtitle_offset_ms / 1e3);
}

void COMXPlayer::SetVolume(long nVolume)
{
  // nVolume is a milliBels from -6000 (-60dB or mute) to 0 (0dB or full volume)
  CSingleLock lock(m_csection);

  /*
  float volume = 0.0f;
  if (nVolume == -6000) {
    // We are muted
    volume = 0.0f;
  } else {
    // Convert what XBMC gives into what omx needs
    volume = (double)nVolume / -10000.0f;
  }

  if(m_AudioRenderOpen && m_audio_render)
    m_audio_render->SetCurrentVolume(volume);
  */

  if(m_AudioRenderOpen && m_audio_render)
    m_audio_render->SetCurrentVolume(nVolume);
}

void COMXPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  std::ostringstream s;
    s << "kB/s:" << fixed << setprecision(2) << (double)m_hints_audio.bitrate / 1024.0;

  strAudioInfo.Format("Audio stream (%s) [%s]", m_audio_codec_name.c_str(), s.str());
}

void COMXPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  std::ostringstream s;
    s << "fr:"     << fixed << setprecision(3) << m_video_fps;
    s << ", Mb/s:" << fixed << setprecision(2) << (double)GetVideoBitrate() / (1024.0*1024.0);

  strVideoInfo.Format("Video stream (%s) [%s]", m_video_codec_name.c_str(), s.str());
}

void COMXPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
  //CLog::Log(LOGDEBUG, "COMXPlayer::GetGeneralInfo");
}

int COMXPlayer::GetAudioStreamCount()
{
  return m_audio_count;
}

int COMXPlayer::GetAudioStream()
{
	return m_audio_index;
}

void COMXPlayer::GetStreamCodecName(AVStream *stream, CStdString &strStreamName)
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

void COMXPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  if((unsigned int)iStream > m_audio_streams.size())
    return;

  //AVStream *stream = m_audio_streams[iStream];
  //GetStreamCodecName(stream, strStreamName);
  GetAudioStreamLanguage(iStream, strStreamName);
}
 
void COMXPlayer::SetAudioStream(int SetAudioStream)
{
  m_audio_index = SetAudioStream;
}

void COMXPlayer::GetAudioStreamLanguage(int iStream, CStdString &strLanguage)
{
  char language[4];
  memset(language, 0, sizeof(language));

  strLanguage.Format("Undefined");
  
  if((unsigned int)iStream > m_audio_streams.size())
    return;

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
  {
    g_LangCodeExpander.Lookup( strLanguage, language );
  }
}

int COMXPlayer::GetSubtitleCount()
{
	return m_subtitle_count;
}

int COMXPlayer::GetSubtitle()
{
	return m_subtitle_index;
}

void COMXPlayer::GetSubtitleName(int iStream, CStdString &strStreamName)
{
  //strStreamName = tcodec_language[iStream];
}

void COMXPlayer::SetSubtitle(int iStream)
{
  m_subtitle_index = iStream;
}

bool COMXPlayer::GetSubtitleVisible()
{
  return m_subtitle_show;
}

void COMXPlayer::SetSubtitleVisible(bool bVisible)
{
  m_subtitle_show = bVisible;
  g_settings.m_currentVideoSettings.m_SubtitleOn = bVisible;

  // show/hide subs here
}

int COMXPlayer::AddSubtitle(const CStdString& strSubPath)
{
  // dymamic add sub here
  return -1;
}

void COMXPlayer::Update(bool bPauseDrawing)
{
  g_renderManager.Update(bPauseDrawing);
}

void COMXPlayer::GetVideoRect(CRect& SrcRect, CRect& DestRect)
{
  g_renderManager.GetVideoRect(SrcRect, DestRect);
}

void COMXPlayer::SetVideoRect(const CRect &SrcRect, const CRect &DestRect)
{
  // check if destination rect or video view mode has changed
  if ((m_dst_rect != DestRect) || (m_view_mode != g_settings.m_currentVideoSettings.m_ViewMode))
  {
    m_dst_rect  = DestRect;
    m_view_mode = g_settings.m_currentVideoSettings.m_ViewMode;
  }
  else
  {
    return;
  }

  // might need to scale up m_dst_rect to display size as video decodes
  // to separate video plane that is at display size.
  CRect gui, display, dst_rect;
  RESOLUTION res = g_graphicsContext.GetVideoResolution();
  gui.SetRect(0, 0, g_settings.m_ResInfo[res].iWidth, g_settings.m_ResInfo[res].iHeight);
  display.SetRect(0, 0, g_settings.m_ResInfo[res].iWidth, g_settings.m_ResInfo[res].iHeight);
  
  dst_rect = m_dst_rect;
  if (gui != display)
  {
    float xscale = display.Width()  / gui.Width();
    float yscale = display.Height() / gui.Height();
    dst_rect.x1 *= xscale;
    dst_rect.x2 *= xscale;
    dst_rect.y1 *= yscale;
    dst_rect.y2 *= yscale;
  }

  if(m_VideoCodecOpen)
  {
    //xxx m_video_decoder->SetVideoRect(SrcRect, m_dst_rect);
  }
}

void COMXPlayer::GetVideoAspectRatio(float &fAR)
{
  fAR = g_renderManager.GetAspectRatio();
}

int COMXPlayer::GetChapterCount()
{
  return m_chapter_count;
}

int COMXPlayer::GetChapter()
{
  // returns a one based value.
  // if we have a chapter list, we need to figure out which chapter we are in.
  if(m_pFormatContext == NULL || m_chapter_count < 1)
    return 0;

  for(int i = 0; i < m_chapter_count - 1; i++)
  {
    if(m_elapsed_ms >= m_chapters[i].seekto_ms && m_elapsed_ms < m_chapters[i + 1].seekto_ms)
      return i + 1;
  }

  return 0;
}

void COMXPlayer::GetChapterName(CStdString& strChapterName)
{
  if(m_chapter_count)
    strChapterName = m_chapters[GetChapter() - 1].name;
}

int COMXPlayer::SeekChapter(int chapter_index)
{
  // chapter_index is a one based value.
  CLog::Log(LOGDEBUG, "COMXPlayer::SeekChapter:chapter_index(%d)", chapter_index);
  if(m_chapter_count > 1)
  {
    if (chapter_index < 0)
      chapter_index = 0;
    if (chapter_index > m_chapter_count)
      return 0;

    // Seek to the chapter.
    g_infoManager.SetDisplayAfterSeek(100000);
    SeekTime(m_chapters[chapter_index - 1].seekto_ms);
    m_callback.OnPlayBackSeekChapter(chapter_index);
    g_infoManager.SetDisplayAfterSeek();
  }
  else
  {
    // we do not have a chapter list so do a regular big jump.
    if (chapter_index > 0)
      Seek(true,  true);
    else
      Seek(false, true);
  }

  return 0;
}

float COMXPlayer::GetActualFPS()
{
  return m_video_fps;
}

void COMXPlayer::SeekTime(__int64 seek_ms)
{
  CSingleLock lock(m_SeekSection);
  
  if(m_pFile && m_pFile->IoControl(IOCTRL_SEEK_POSSIBLE, NULL))
  {
    m_seek_ms = seek_ms;
    //printf("m_seek_ms %lld seek_ms %lld\n", m_seek_ms, seek_ms);
    m_seek_req = true;
  }
}

__int64 COMXPlayer::GetTime()
{
  return m_elapsed_ms;
}

int COMXPlayer::GetTotalTime()
{
	return m_duration_ms / 1000;
}

int COMXPlayer::GetAudioBitrate()
{
  return m_hints_audio.bitrate;
}
int COMXPlayer::GetVideoBitrate()
{
  return (int)m_videoStats.GetBitrate();
}

int COMXPlayer::GetSourceBitrate()
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

int COMXPlayer::GetChannels()
{
  return m_hints_audio.channels;
}

int COMXPlayer::GetBitsPerSample()
{
  return m_hints_audio.bitspersample;
}

int COMXPlayer::GetSampleRate()
{
  return m_hints_audio.samplerate;
}

CStdString COMXPlayer::GetAudioCodecName()
{
  return m_audio_codec_name;
}

CStdString COMXPlayer::GetVideoCodecName()
{
  return m_video_codec_name;
}

int COMXPlayer::GetPictureWidth()
{
  return m_hints_video.width;
}

int COMXPlayer::GetPictureHeight()
{
  return m_hints_video.height;
}

bool COMXPlayer::GetStreamDetails(CStreamDetails &details)
{
  unsigned int i;
  bool retVal = false;
  details.Reset();
  
  for(i = 0; i < m_video_streams.size(); i++)
  {
    CStreamDetailVideo *p = new CStreamDetailVideo();
    AVStream *pStream = m_video_streams[i];

    p->m_iWidth   = pStream->codec->width;
    p->m_iHeight  = pStream->codec->height;
    if(pStream->codec->sample_aspect_ratio.num == 0)
      p->m_fAspect = (float)p->m_iWidth / p->m_iHeight;
    else
      p->m_fAspect = av_q2d(pStream->codec->sample_aspect_ratio) * pStream->codec->width / pStream->codec->height;
    p->m_iDuration = m_duration_ms;

    // finally, calculate seconds
    if (p->m_iDuration > 0)
      p->m_iDuration = p->m_iDuration / 1000;

    details.AddStream(p);
    retVal = true;
  }

  for(i = 0; i < m_audio_streams.size(); i++)
  {
    CStreamDetailAudio *p = new CStreamDetailAudio();
    AVStream *pStream = m_audio_streams[i];
    CStdString strLanguage;
    CStdString strCodec;

    p->m_iChannels  = pStream->codec->channels;
    GetAudioStreamLanguage(i, strLanguage);
    p->m_strLanguage = strLanguage;

    GetStreamCodecName(pStream, strCodec);
    p->m_strCodec = strCodec;

    details.AddStream(p);
    retVal = true;
  }

  // TODO: here we would have subtitles

  return retVal;
}

void COMXPlayer::ToFFRW(int iSpeed)
{
  if (m_StopPlaying)
    return;

  if (m_speed != iSpeed)
  {
    //change playback speed here
    m_speed = iSpeed;
  }
}

bool COMXPlayer::GetCurrentSubtitle(CStdString& strSubtitle)
{
  strSubtitle = "";

  //strSubtitle = subtitle_text;
  return !strSubtitle.IsEmpty();
}
  
CStdString COMXPlayer::GetPlayerState()
{
  return "";
}

bool COMXPlayer::SetPlayerState(CStdString state)
{
  return false;
}

CStdString COMXPlayer::GetPlayingTitle()
{
  //return video_title;
  return "";
}

int COMXPlayer::GetCacheLevel() const
{
  return 0;
}

void COMXPlayer::OnStartup()
{
}

void COMXPlayer::OnExit()
{
  m_bStop = true;
  // if we didn't stop playing, advance to the next item in xbmc's playlist
  if(m_options.identify == false)
  {
    if (m_StopPlaying)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }


  m_ready.Set();
}

void COMXPlayer::TvServiceCallback(uint32_t reason, uint32_t param1, uint32_t param2)
{
  printf("tvservice_callback(%d,%d,%d)\n", reason, param1, param2);
  switch(reason)
  {
  case VC_HDMI_UNPLUGGED:
    break;
  case VC_HDMI_STANDBY:
    break;
  case VC_SDTV_NTSC:
  case VC_SDTV_PAL:
  case VC_HDMI_HDMI:
  case VC_HDMI_DVI:    
    //Signal we are ready now
    sem_post(&m_tv_synced);
    break;     
  default: 
     break;
  }
}

void COMXPlayer::CallbackTvServiceCallback(void *userdata, uint32_t reason, uint32_t param1, uint32_t param2)
{
   COMXPlayer *omx = static_cast<COMXPlayer*>(userdata);
   omx->TvServiceCallback(reason, param1, param2);
}


void COMXPlayer::Process()
{
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
    CLog::Log(LOGDEBUG, "COMXPlayer: SetThreadPriority failed");

  int                 result            = -1;
  int                 m_video_index_use = -1;
  int                 m_audio_index_use = -1;

  m_video_index_use = m_video_index;
  m_audio_index_use = m_audio_index;

  m_av_clock->Initialize(m_video_count, m_audio_count);

  if(m_pVideoStream && !OpenVideoDecoder(m_pVideoStream))
    goto do_exit;

  m_dst_rect.SetRect(0, 0, 0, 0);
  //if(m_VideoCodecOpen)
  //  m_video_decoder->SetVideoRect(m_dst_rect, m_dst_rect);

  OpenAudioCodec(m_pAudioStream);

  m_Passthrough = IsPassthrough(m_pAudioStream);
  if(!m_Passthrough && m_use_hw_audio)
    m_HWDecode = COMXAudio::HWDecode(m_hints_audio.codec);

  m_av_clock->StateExecute();
  m_av_clock->UpdateCurrentPTS(m_pFormatContext);

  m_duration_ms = (int)(m_pFormatContext->duration / (AV_TIME_BASE /  1000));

  //CLog::Log(LOGDEBUG, "COMXPlayer: Thread started");
  try
  {
    m_speed = 1;
    m_callback.OnPlayBackSpeedChanged(m_speed);

    // starttime has units of seconds (SeekTime will start playback)
    if (m_options.starttime > 0)
      SeekTime(m_options.starttime * 1000);
    SetVolume(g_settings.m_nVolumeLevel);
    SetAVDelay(m_audio_offset_ms);

    // at this point we should know all info about audio/video stream.
    // we are done initializing now, set the readyevent which will
    if (m_video_count)
    {
      // turn on/off subs
      SetSubtitleVisible(g_settings.m_currentVideoSettings.m_SubtitleOn);
      SetSubTitleDelay(m_subtitle_offset_ms);

      // setup renderer for bypass. This tell renderer to get out of the way as
      // hw decoder will be doing the actual video rendering in a video plane
      // that is under the GUI layer.
      int width  = GetPictureWidth();
      int height = GetPictureHeight();
      double fFrameRate = GetActualFPS();
      unsigned int flags = 0;

      flags |= CONF_FLAGS_FORMAT_BYPASS;
      flags |= CONF_FLAGS_FULLSCREEN;
      CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: BYPASS",
        __FUNCTION__, width, height, fFrameRate);

      if(!g_renderManager.Configure(m_video_width, m_video_height,
        m_video_width, m_video_height, m_video_fps, flags, 0))
      {
        CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
      }
      if (!g_renderManager.IsStarted())
      {
        CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
      }

      HDMI_INTERLACED_T interlaced = HDMI_NONINTERLACED;
      EDID_MODE_MATCH_FLAG_T edid = HDMI_MODE_MATCH_FRAMERATE;
      g_Windowing.Hide();
      sem_init (&m_tv_synced, 0, 0);
      m_BcmHost.vc_tv_register_callback(CallbackTvServiceCallback, this);
      m_BcmHost.vc_tv_hdmi_power_on_best(width, height, (int)(fFrameRate+0.5), interlaced, edid);
      // wait for TV sync complete
      // This can take a second or two, so we should really move this later, and start buffering now.
      sem_wait(&m_tv_synced);
      m_BcmHost.vc_tv_unregister_callback(CallbackTvServiceCallback);
      g_Windowing.Show(true);
    }

    if (m_options.identify == false)
      m_callback.OnPlayBackStarted();

    // drop CGUIDialogBusy, and release the hold in OpenFile
    m_ready.Set();

    m_videoStats.Start();

    while (!m_bStop && !m_StopPlaying)
    {

      if(g_abort)
        goto do_exit;
      
      if(m_paused)
      {
        if(!m_av_clock->IsPaused())
          m_av_clock->Pause();

        OMXSleep(2);
        continue;
      }
      else if(!m_buffer_seek && !m_paused)
      {
        if(m_av_clock->IsPaused())
          m_av_clock->Resume();
      }

      if(m_seek_req)
      {
        /*
        if(m_last_pts == AV_NOPTS_VALUE)
          m_last_pts = 0;
        */

        //int64_t pos = (m_elapsed_ms + m_seek_ms) * 1000;
        int64_t pos = m_seek_ms * 1000;
        if(pos < 0)
          pos = 0;

        //printf("seek %f m_seek_ms %d\n", (double)pos / AV_TIME_BASE, m_seek_ms);

        int     seek_flags  = (m_seek_ms - m_elapsed_ms) < 0 ? AVSEEK_FLAG_BACKWARD : 0;

        if(m_pFormatContext->start_time != (int64_t)AV_NOPTS_VALUE)
          pos += m_pFormatContext->start_time;

        int ret = m_dllAvFormat.av_seek_frame(m_pFormatContext, -1, pos, seek_flags);

        if(ret < 0)
        {
          printf("error while seeking seek_flags %d pos %f\n", seek_flags, (double)pos / AV_TIME_BASE);
          //goto do_exit;
        }
        else
        {
          if(m_AudioCodecOpen)
            m_pAudioCodec->Reset();

          if(m_VideoCodecOpen)
          {
            m_video_decoder->Reset();
          }
          if(m_AudioRenderOpen)
          {
            m_audio_render->Flush();
          }
          m_av_clock->Reset();
          m_av_clock->UpdateCurrentPTS(m_pFormatContext);
        }

        m_buffer_seek = true;

        CSingleLock lock(m_SeekSection);
        m_SeekSection.lock();
        m_seek_req = false;
        m_SeekSection.unlock();
      }

      if(m_pkt_consumed)
      {
        m_pkt.size = 0;
        m_pkt.data = NULL;
        m_pkt.stream_index = MAX_STREAMS;
  
        result = m_dllAvFormat.av_read_frame(m_pFormatContext, &m_pkt);
        if (result < 0)
        {
          //printf("error read packet\n");
          goto do_exit;
        }
        else if (m_pkt.size < 0 || m_pkt.stream_index >= MAX_STREAMS)
        {
          m_dllAvCodec.av_free_packet(&m_pkt);
          goto do_exit;
        }

        m_pkt_consumed = false;        if(m_pkt.dts == 0)
          m_pkt.dts = AV_NOPTS_VALUE;
        if(m_pkt.pts == 0)
          m_pkt.pts = AV_NOPTS_VALUE;

        AVStream *pStream = m_pFormatContext->streams[m_pkt.stream_index];

        if(m_bMatroska && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        { // matroska can store different timestamps
          // for different formats, for native stored
          // stuff it is pts, but for ms compatibility
          // tracks, it is really dts. sadly ffmpeg
          // sets these two timestamps equal all the
          // time, so we select it here instead
          if(pStream->codec->codec_tag == 0)
            m_pkt.dts = AV_NOPTS_VALUE;
          else
            m_pkt.pts = AV_NOPTS_VALUE;
        }
  
        // we need to get duration slightly different for matroska embedded text subtitels
        if(m_bMatroska && pStream->codec->codec_id == CODEC_ID_TEXT && m_pkt.convergence_duration != 0)
          m_pkt.duration = m_pkt.convergence_duration;

        if(m_bAVI && pStream->codec && pStream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
          // AVI's always have borked pts, specially if m_pFormatContext->flags includes
          // AVFMT_FLAG_GENPTS so always use dts
          m_pkt.pts = AV_NOPTS_VALUE;
        }

        // check if stream has passed full duration, needed for live streams
        if(m_pkt.dts != (int64_t)AV_NOPTS_VALUE)
        {
          int64_t duration;
          duration = m_pkt.dts;
          if(pStream->start_time != (int64_t)AV_NOPTS_VALUE)
            duration -= pStream->start_time;

          if(duration > pStream->duration)
          {
            pStream->duration = duration;
            duration = m_dllAvUtil.av_rescale_rnd(pStream->duration, pStream->time_base.num * AV_TIME_BASE, pStream->time_base.den, AV_ROUND_NEAR_INF);
            if ((m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE && m_pFormatContext->file_size > 0)
                ||  (m_pFormatContext->duration != (int64_t)AV_NOPTS_VALUE && duration > m_pFormatContext->duration))
              m_pFormatContext->duration = duration;
          }
        }

        m_pkt.dts = OMXClock::ConvertTimestamp(m_pkt.dts, m_pFormatContext->start_time, &pStream->time_base);
        m_pkt.pts = OMXClock::ConvertTimestamp(m_pkt.pts, m_pFormatContext->start_time, &pStream->time_base);
        m_pkt.duration = DVD_SEC_TO_TIME((double)m_pkt.duration * pStream->time_base.num / pStream->time_base.den);

        // used to guess streamlength
        if ((uint64_t)m_pkt.dts != AV_NOPTS_VALUE && (m_pkt.dts > m_av_clock->GetCurrentPts() || m_av_clock->GetCurrentPts() == AV_NOPTS_VALUE))
          m_av_clock->SetCurrentPts(m_pkt.dts);

        // check if stream seem to have grown since start
        if(m_pFormatContext->file_size > 0 && m_pFormatContext->pb)
        {
          if(m_pFormatContext->pb->pos > m_pFormatContext->file_size)
            m_pFormatContext->file_size = m_pFormatContext->pb->pos;
        }

        // Audio Stream changed
        if(m_audio_count > 0 && (m_audio_index != m_audio_index_use ||
           m_pAudioStream->codec->channels != m_hints_audio.channels ||
           m_pAudioStream->codec->sample_rate != m_hints_audio.samplerate ||
           m_pAudioStream->codec->codec_id != m_hints_audio.codec))
        {
          m_av_clock->Pause();

          CloseAudioDecoder();
          CloseAudioCodec();

          m_audio_index_use = m_audio_index;
          if((unsigned int)m_audio_index > m_audio_streams.size())
          {
            CLog::Log(LOGERROR, "COMXPlayer::Process: Audio stream index error");
            goto do_exit;
          }
          m_pAudioStream = m_audio_streams[m_audio_index];

          m_AudioCodecOpen = OpenAudioCodec(m_pAudioStream);
          if(!m_AudioCodecOpen)
            goto do_exit;

          m_AudioRenderOpen = false;

          m_Passthrough = IsPassthrough(m_pAudioStream);
          if(!m_Passthrough && m_use_hw_audio)
            m_HWDecode = COMXAudio::HWDecode(m_hints_audio.codec);

        }
      }
      
      /* when the audio buffer runns under 0.1 seconds we buffer up */
      if(m_AudioRenderOpen && m_audio_render->GetDelay() < 0.1f)
      {
        m_buffer_seek = true;
        //printf("\nenter buffering %f\n\n", m_audio_render->GetDelay());
      }

      /* buffering once after seek */
      if(m_buffer_seek)
      {
        bool bAudioBufferReady = false;

        if(m_AudioRenderOpen && m_VideoCodecOpen)
        {
          if(m_audio_render->GetDelay() > (AUDIO_BUFFER_SECONDS - 0.25f))
          {
            bAudioBufferReady = true;
          }
          else
          {
            if(!m_av_clock->IsPaused())
              m_av_clock->Pause();
          }
          if(bAudioBufferReady)
          {
            if(m_av_clock->IsPaused())
              m_av_clock->Resume();
            m_buffer_seek = false;
          }
        }
        if(m_VideoCodecOpen)
        {
          if(m_video_decoder->GetFreeSpace() < ((80*1024*VIDEO_BUFFERS) * 0.25))
          {
            if(m_av_clock->IsPaused())
              m_av_clock->Resume();
            m_buffer_seek = false;
          }
        }
      }

      if( ( m_pVideoStream == m_pFormatContext->streams[m_pkt.stream_index] ) && m_VideoCodecOpen )
      {
        if ((uint64_t)m_pkt.dts == AV_NOPTS_VALUE && (uint64_t)m_pkt.pts == AV_NOPTS_VALUE)
          m_videoClock = m_pkt.pts;
        else if ((uint64_t)m_pkt.pts == AV_NOPTS_VALUE)
          m_videoClock = m_pkt.dts;
        else if ((uint64_t)m_pkt.pts != AV_NOPTS_VALUE)
          m_videoClock = m_pkt.pts;

        if(m_bMpeg)
        {
          m_video_decoder->Decode(m_pkt.data, m_pkt.size, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
        }
        else
        {
          m_video_decoder->Decode(m_pkt.data, m_pkt.size, m_videoClock + (m_audio_offset_ms * 1000), 
                                  m_videoClock + (m_audio_offset_ms * 1000));
        }
        m_av_clock->UpdateVideoClock(m_videoClock);

        m_last_pts = m_videoClock;

        if ((uint64_t)m_pkt.dts == AV_NOPTS_VALUE && (uint64_t)m_pkt.pts == AV_NOPTS_VALUE)
          m_videoClock += m_frametime;

        if(m_AudioRenderOpen && m_VideoCodecOpen)
        {
          printf("V : %8.02f %8d %8d A : %8.02f %8.02f                             \r", m_videoClock / DVD_TIME_BASE, 80*1024*VIDEO_BUFFERS,
             m_video_decoder->GetFreeSpace(), m_audioClock / DVD_TIME_BASE, m_audio_render->GetDelay());
        }
        else if(m_VideoCodecOpen)
        {
          printf("V : %8.02f %8d %8d                                               \r", m_videoClock / DVD_TIME_BASE, 80*1024*VIDEO_BUFFERS,
             m_video_decoder->GetFreeSpace());
        }

        m_videoStats.AddSampleBytes(m_pkt.size);

        if(m_audio_count == 0)
          usleep(m_frametime - 2000);

        m_pkt_consumed = true;
      }
      else if( ( m_pAudioStream == m_pFormatContext->streams[m_pkt.stream_index] ) && m_AudioCodecOpen )
      {
        const uint8_t *data_dec = m_pkt.data;
        int           data_len  = m_pkt.size;

        if ((uint64_t)m_pkt.pts != AV_NOPTS_VALUE)
          m_audioClock = m_pkt.pts;
        else if ((uint64_t)m_pkt.dts != AV_NOPTS_VALUE)
          m_audioClock = m_pkt.dts;

        if(!m_Passthrough  && !m_HWDecode)
        {
          while(data_len > 0)
          {
            int len = m_pAudioCodec->Decode((BYTE *)data_dec, data_len);
            if (len < 0)
            {
              printf("reset\n");
              m_pAudioCodec->Reset();
              break;
            }

            if( len >  data_len )
            {
              printf("len >  data_len\n");
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
  
            if(!m_AudioRenderOpen)
            {
              m_AudioRenderOpen = OpenAudioDecoder(m_pAudioStream);
              if(!m_AudioRenderOpen)
                goto do_exit;
              m_av_clock->StateExecute();
              if(m_av_clock->IsPaused())
                m_av_clock->Resume();
            }
  
            if(m_AudioRenderOpen)
            {
              if(m_bMpeg)
              {
                ret = m_audio_render->AddPackets(decoded, decoded_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
              }
              else
              {
                ret = m_audio_render->AddPackets(decoded, decoded_size, m_audioClock, m_audioClock);
              }
              if(ret != decoded_size)
              {
                printf("error ret %d decoded_size %d\n", ret, decoded_size);
              }
            }

            int n = (m_hints_audio.channels * m_hints_audio.bitspersample * m_hints_audio.samplerate)>>3;
            if (n > 0)
            {
              m_audioClock += ((double)decoded_size * DVD_TIME_BASE) / n;
            }
          }
        }
        else
        {
          if(!m_AudioRenderOpen)
          {
            m_AudioRenderOpen = OpenAudioDecoder(m_pAudioStream);
            if(!m_AudioRenderOpen)
              goto do_exit;
            m_av_clock->StateExecute();
            if(m_av_clock->IsPaused())
              m_av_clock->Resume();
          }

          if(m_AudioRenderOpen)
          {
            int ret = 0;
            if(m_bMpeg)
            {
              ret = m_audio_render->AddPackets(m_pkt.data, m_pkt.size, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
            }
            else
            {
              ret = m_audio_render->AddPackets(m_pkt.data, m_pkt.size, m_audioClock, m_audioClock);
            }
            if(ret != m_pkt.size)
            {
              printf("error ret %d decoded_size %d\n", ret, m_pkt.size);
            }
          }
        }

        m_av_clock->UpdateAudioClock(m_audioClock);

        m_last_pts = m_audioClock;

        if(m_AudioRenderOpen && m_VideoCodecOpen)
        {
          printf("V : %8.02f %8d %8d A : %8.02f %8.02f                             \r", m_videoClock / DVD_TIME_BASE, 80*1024*VIDEO_BUFFERS,
             m_video_decoder->GetFreeSpace(), m_audioClock / DVD_TIME_BASE, m_audio_render->GetDelay());
        }
        else if(m_AudioRenderOpen)
        {
          printf("A : %8.02f %8.02f                             \r",
              m_audioClock / DVD_TIME_BASE, m_audio_render->GetDelay());
        }

        m_pkt_consumed = true;
      }
      else
      {
        m_pkt_consumed = true;
      }

      if(m_audioClock != AV_NOPTS_VALUE)
        m_elapsed_ms = m_audioClock / 1000;
      else if(m_videoClock != AV_NOPTS_VALUE)
        m_elapsed_ms = m_videoClock / 1000;
      else
        m_elapsed_ms = 0;

      if(m_pkt_consumed)
        m_dllAvCodec.av_free_packet(&m_pkt);
    }
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "COMXPlayer::Process: Exception thrown");
  }

do_exit:

  m_av_clock->Pause();

  /* nobody tooked care about the av packet */
  if(!m_pkt_consumed)
  {
    m_dllAvCodec.av_free_packet(&m_pkt);
    m_pkt_consumed = true;
  }

  m_bStop = m_StopPlaying = true;
}

#endif
