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
#include "guilib/LocalizeStrings.h"
#include "utils/StreamDetails.h"

#include <sstream>
#include <iomanip>

#include "BitstreamConverter.h"

using namespace XFILE;

// ****************************************************************
// ****************************************************************
COMXPlayer::COMXPlayer(IPlayerCallback &callback) 
  : IPlayer(callback),
  CThread("COMXPlayer"),
  m_ready(true)
{
  m_speed           = 1;
  m_paused          = false;
  m_StopPlaying     = false;
  m_mode3d_sbs      = false;
  m_hdmi_clock_sync = false;
  m_av_clock        = NULL;

  m_OMX.Initialize();
}

COMXPlayer::~COMXPlayer()
{
  CloseFile();

  m_OMX.Deinitialize();
}

bool COMXPlayer::Initialize(TiXmlElement* pConfig)
{
  return true;
}

void COMXPlayer::FlushStreams()
{
  if(m_av_clock)
    m_av_clock->OMXPause();

  if(m_video_count)
    m_player_video.Flush();

  if(m_audio_count)
    m_player_audio.Flush();

  m_flush = true;

  m_last_subtitle_pts = DVD_NOPTS_VALUE;

  if(m_av_clock)
  {
    m_av_clock->OMXReset();
    m_av_clock->OMXResume();
  }
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

    std::string url;

    m_item        = file;
    m_options     = options;
    m_StopPlaying = false;

    m_elapsed_ms  = 0;
    m_duration_ms = 0;

    m_audio_index = 0;
    m_audio_count = 0;

    m_video_index = 0;
    m_video_count = 0;
    m_video_fps   = 0.0;

    m_subtitle_index = 0;
    m_subtitle_count = 0;
    m_chapter_count  = 0;
    m_lastSub        = "";

    m_subtitle_show  = g_settings.m_currentVideoSettings.m_SubtitleOn;

    m_hints_audio.Clear();
    m_hints_video.Clear();

    m_startpts        = 0;

    // open file and start playing here.

    m_buffer_empty    = true;

    m_use_passthrough = (g_guiSettings.GetInt("audiooutput.mode") == IAudioRenderer::ENCODED_NONE) ? false : true ;
    m_use_hw_audio    = g_advancedSettings.m_omHWAudioDecode;

    m_current_volume  = 0;
    m_change_volume   = true;

    m_flush           = false;

    m_dst_rect.SetRect(0, 0, 0, 0);

    m_filename = file.GetPath();
    
    m_speed           = DVD_PLAYSPEED_NORMAL;
    m_last_subtitle_pts = DVD_NOPTS_VALUE;

    if (!m_BcmHostDisplay.Load() || !m_BcmHost.Load())
      return false;

    memset(&m_tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
    m_BcmHost.vc_tv_get_state(&m_tv_state);

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
  
  m_omx_reader.Close();

  g_Windowing.InformVideoInfo(m_tv_state.width, m_tv_state.height, m_tv_state.frame_rate);

  m_BcmHostDisplay.Unload();
  m_BcmHost.Unload();

  if(m_av_clock)
    delete m_av_clock;
  m_av_clock = NULL;

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
    // unpause here
    m_callback.OnPlayBackResumed();
    if(m_av_clock->OMXIsPaused())
      m_av_clock->OMXResume();
  }
  else
  {
    // pause here
    m_callback.OnPlayBackPaused();
    if(!m_av_clock->OMXIsPaused())
      m_av_clock->OMXPause();
  }
  m_paused = !m_paused;
  m_buffer_empty = false;
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
}

float COMXPlayer::GetPercentage()
{
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

float COMXPlayer::GetCachePercentage()
{
  return std::min(100.0, (double)(GetPercentage() + GetCacheLevel()));
}

void COMXPlayer::SetAVDelay(float fValue)
{
  // time offset in seconds of audio with respect to video
  m_player_video.SetDelay(fValue * DVD_TIME_BASE);
  // set a/v offset here
}

float COMXPlayer::GetAVDelay()
{
  return m_player_video.GetDelay() / (float)DVD_TIME_BASE;
}

void COMXPlayer::SetSubTitleDelay(float fValue)
{
  m_player_video.SetSubtitleDelay(-fValue * DVD_TIME_BASE);
}

float COMXPlayer::GetSubTitleDelay()
{
  return -m_player_video.GetSubtitleDelay() / DVD_TIME_BASE;
}

void COMXPlayer::SetVolume(long nVolume)
{
  // nVolume is a milliBels from -6000 (-60dB or mute) to 0 (0dB or full volume)
  CSingleLock lock(m_csection);

  m_current_volume = nVolume;
  m_change_volume = true;
}

void COMXPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  std::ostringstream s;
    s << "kB/s:" << fixed << setprecision(2) << (double)m_hints_audio.bitrate / 1024.0;

  strAudioInfo.Format("Audio stream (%s) [%s]", GetAudioCodecName().c_str(), s.str());
}

void COMXPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  std::ostringstream s;
    s << "fr:"     << fixed << setprecision(3) << m_video_fps;
    s << ", Mb/s:" << fixed << setprecision(2) << (double)GetVideoBitrate() / (1024.0*1024.0);

  strVideoInfo.Format("Video stream (%s) [%s]", GetVideoCodecName().c_str(), s.str());
}

void COMXPlayer::GetGeneralInfo(CStdString& strGeneralInfo)
{
  if (!m_bStop && m_av_clock)
  {
    double apts = m_av_clock->GetAudioClock();
    double vpts = m_av_clock->GetVideoClock();
    double dDiff = 0;

    if( apts != DVD_NOPTS_VALUE && vpts != DVD_NOPTS_VALUE )
      dDiff = (apts - vpts) / DVD_TIME_BASE;

    strGeneralInfo.Format("C( a/v:% 6.3f, dcpu:%2i%% acpu:%2i%% vcpu:%2i%% )"
                         , dDiff
                         , (int)(CThread::GetRelativeUsage()*100)
                         , (int)(m_player_audio.GetRelativeUsage()*100)
                         , (int)(m_player_video.GetRelativeUsage()*100));
  }
}

int COMXPlayer::GetAudioStreamCount()
{
  return m_audio_count;
}

int COMXPlayer::GetAudioStream()
{
	return m_audio_index;
}

void COMXPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  CStdString name;

  strStreamName = "";

  /*
  name = m_omx_reader.GetStreamName(OMXSTREAM_AUDIO, iStream);
  if(name.length() > 0)
  {
    strStreamName += name;
  }
  else
  {
  */
    CStdString language = m_omx_reader.GetStreamLanguage(OMXSTREAM_AUDIO, iStream);
    if(language.length() > 0)
    {
      if (!g_LangCodeExpander.Lookup(strStreamName, language))
        strStreamName += g_localizeStrings.Get(13205); // Unknown
    }
    else
    {
      strStreamName += g_localizeStrings.Get(13205); // Unknown
    }
    
    CStdString strType = m_omx_reader.GetStreamType(OMXSTREAM_AUDIO, iStream);
    if(strType.length() > 0)
    {
      strStreamName = strStreamName + " - " + strType;
    }
  /*
  }
  */
}
 
void COMXPlayer::SetAudioStream(int SetAudioStream)
{
  CSingleLock lock(m_csection);

  m_omx_reader.SetActiveStream(OMXSTREAM_AUDIO, SetAudioStream);

  m_audio_index = SetAudioStream;
}

void COMXPlayer::GetAudioStreamLanguage(int iStream, CStdString &strLanguage)
{
  CStdString language;

  strLanguage = "";

  language = m_omx_reader.GetStreamLanguage(OMXSTREAM_AUDIO, iStream);

  if(language.length() > 0)
    strLanguage = language;
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
  /*
  CStdString name = m_omx_reader.GetStreamName(OMXSTREAM_SUBTITLE, iStream);

  strStreamName = "";
  if(name.length() > 0)
  {
    strStreamName += name;
  }
  else
  {
  */
    CStdString language = m_omx_reader.GetStreamLanguage(OMXSTREAM_SUBTITLE, iStream);
    if(language.length() > 0)
    {
      if (!g_LangCodeExpander.Lookup(strStreamName, language))
        strStreamName += g_localizeStrings.Get(13205); // Unknown
    }
    else
    {
      strStreamName += g_localizeStrings.Get(13205); // Unknown
    }
  /*
  }
  */
}

void COMXPlayer::GetSubtitleLanguage(int iStream, CStdString &strStreamLang)
{
  CStdString language;
  language = m_omx_reader.GetStreamLanguage(OMXSTREAM_SUBTITLE, iStream);

  if (!g_LangCodeExpander.Lookup(strStreamLang, language))
    strStreamLang = g_localizeStrings.Get(13205); // Unknown
}

void COMXPlayer::SetSubtitle(int iStream)
{
  if(m_omx_reader.SetActiveStream(OMXSTREAM_SUBTITLE, iStream))
    m_player_video.FlushSubtitles();

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

  /*
  if(m_video_decoder)
  {
    //xxx m_video_decoder->SetVideoRect(SrcRect, m_dst_rect);
  }
  */
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

  return m_omx_reader.GetChapter();
}

void COMXPlayer::GetChapterName(CStdString& strChapterName)
{
  m_omx_reader.GetChapterName(strChapterName);
}

int COMXPlayer::SeekChapter(int chapter_index)
{
  CSingleLock lock(m_csection);

  // chapter_index is a one based value.
  CLog::Log(LOGDEBUG, "COMXPlayer::SeekChapter:chapter_index(%d)", chapter_index);
  if(m_chapter_count > 1)
  {
    // Seek to the chapter.
    g_infoManager.SetDisplayAfterSeek(100000);

    m_omx_reader.SeekChapter(chapter_index, &m_startpts);
    FlushStreams();

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
  CSingleLock lock(m_csection);

  int seek_flags = (seek_ms - m_elapsed_ms) < 0 ? AVSEEK_FLAG_BACKWARD : 0;

  if(m_omx_reader.SeekTime(seek_ms, seek_flags, &m_startpts))
    FlushStreams();
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
  return m_omx_reader.GetSourceBitrate();
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
  return m_omx_reader.GetCodecName(OMXSTREAM_AUDIO);
}

CStdString COMXPlayer::GetVideoCodecName()
{
  return m_omx_reader.GetCodecName(OMXSTREAM_VIDEO);
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
  
  for(i = 0; i < (unsigned int)m_video_count; i++)
  {
    CStreamDetailVideo *p = new CStreamDetailVideo();
    COMXStreamInfo hints;
   
    m_omx_reader.GetHints(OMXSTREAM_VIDEO, i, hints);

    p->m_iWidth     = hints.width;
    p->m_iHeight    = hints.height;
    p->m_fAspect    = hints.aspect;
    p->m_iDuration  = m_duration_ms;
    p->m_strCodec   = m_omx_reader.GetCodecName(OMXSTREAM_VIDEO, i);

    // finally, calculate seconds
    if (p->m_iDuration > 0)
      p->m_iDuration = p->m_iDuration / 1000;

    details.AddStream(p);
    retVal = true;
  }

  for(i = 0; i < (unsigned int)m_audio_count; i++)
  {
    CStreamDetailAudio *p = new CStreamDetailAudio();
    COMXStreamInfo hints;

    m_omx_reader.GetHints(OMXSTREAM_AUDIO, i, hints);

    p->m_iChannels    = hints.channels;
    p->m_strLanguage  = m_omx_reader.GetStreamLanguage(OMXSTREAM_AUDIO, i);
    p->m_strCodec     = m_omx_reader.GetCodecName(OMXSTREAM_AUDIO, i);

    retVal = true;
  }

  for(i = 0; i < (unsigned int)m_subtitle_count; i++)
  {
    CStreamDetailSubtitle *p = new CStreamDetailSubtitle();

    p->m_strLanguage  = m_omx_reader.GetStreamLanguage(OMXSTREAM_SUBTITLE, i);

    details.AddStream(p);
    retVal = true;
  }

  return retVal;
}

void COMXPlayer::ToFFRW(int iSpeed)
{
  if (m_StopPlaying)
    return;

  if(!m_av_clock)
    return;

  if(iSpeed < OMX_PLAYSPEED_PAUSE)
    return;

  m_omx_reader.SetSpeed(iSpeed);

  if(m_av_clock->OMXPlaySpeed() != OMX_PLAYSPEED_PAUSE && iSpeed == OMX_PLAYSPEED_PAUSE)
    m_paused = true;
  else if(m_av_clock->OMXPlaySpeed() == OMX_PLAYSPEED_PAUSE && iSpeed != OMX_PLAYSPEED_PAUSE)
    m_paused = false;

  m_av_clock->OMXSpeed(iSpeed);
}

#define SUBTITLE_DELAY 3000000

bool COMXPlayer::GetCurrentSubtitle(CStdString& strSubtitle)
{
  strSubtitle = "";
  bool nSub = true;

  if(!m_av_clock)
    return nSub;

  // In case we stalled, don't output any subs
  if (m_buffer_empty)
  {
    strSubtitle = m_lastSub;
    nSub = true;
  }
  else
  {
    strSubtitle = m_player_video.GetText();
   
    if(strSubtitle.length())
      nSub = true; 
  }

  return nSub;
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

void COMXPlayer::Process()
{
  if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
    CLog::Log(LOGDEBUG, "COMXPlayer: SetThreadPriority failed");

  struct  timespec starttime, endtime;
  bool    got_eof = false;
  OMXPacket *omx_pkt = NULL;

  //CLog::Log(LOGDEBUG, "COMXPlayer: Thread started");
  try
  {
    if(m_filename.find("3DSBS") != string::npos) {
      CLog::Log(LOGNOTICE, "3DSBS movie found");
      m_mode3d_sbs = true;
    }

    m_hdmi_clock_sync   = g_guiSettings.GetBool("videoplayer.adjustrefreshrate");
    m_thread_player     = true;
    m_stats             = false;

    if(!m_omx_reader.Open(m_filename, false))
      goto do_exit;

    m_video_count     = m_omx_reader.VideoStreamCount();
    m_audio_count     = m_omx_reader.AudioStreamCount();
    m_subtitle_count  = m_omx_reader.SubtitleStreamCount();
    m_chapter_count   = m_omx_reader.GetChapterCount();

    m_omx_reader.GetHints(OMXSTREAM_AUDIO, m_hints_audio);
    m_omx_reader.GetHints(OMXSTREAM_VIDEO, m_hints_video);

    m_bMpeg = m_omx_reader.IsMpegVideo();

    m_av_clock = new OMXClock();
    if(!m_av_clock->OMXInitialize(m_video_count, m_audio_count))
      goto do_exit;

    if(m_hdmi_clock_sync && !m_av_clock->HDMIClockSync())
      goto do_exit;

    bool deinterlace = ( g_settings.m_currentVideoSettings.m_DeinterlaceMode == VS_DEINTERLACEMODE_OFF ) ? false : true;

    if(m_video_count && !m_player_video.Open(m_hints_video, m_av_clock, deinterlace,
                                             m_bMpeg, m_audio_count, m_hdmi_clock_sync, m_thread_player))
      goto do_exit;

    CStdString deviceString;
    if(m_use_passthrough)
      deviceString = g_guiSettings.GetString("audiooutput.passthroughdevice");
    else
      deviceString = g_guiSettings.GetString("audiooutput.audiodevice");

    if(m_audio_count && !m_player_audio.Open(m_hints_audio, m_av_clock, &m_omx_reader, deviceString,
                                m_use_passthrough, m_use_hw_audio, m_thread_player))
      goto do_exit;

    m_av_clock->OMXStateExecute();
    m_av_clock->OMXReset();
    m_av_clock->OMXResume();

    m_video_fps         = m_player_video.GetFPS();

    RESOLUTION res      = g_graphicsContext.GetVideoResolution();
    int video_width     = g_settings.m_ResInfo[res].iWidth;
    int video_height    = g_settings.m_ResInfo[res].iHeight;

    m_dst_rect.SetRect(0, 0, 0, 0);
    //if(m_video_decoder)
    //  m_video_decoder->SetVideoRect(m_dst_rect, m_dst_rect);

    m_av_clock->OMXStateExecute();
    m_av_clock->SetSpeed(DVD_PLAYSPEED_NORMAL);

    m_duration_ms = m_omx_reader.GetDuration();

    m_speed = DVD_PLAYSPEED_NORMAL;
    m_callback.OnPlayBackSpeedChanged(m_speed);

    // starttime has units of seconds (SeekTime will start playback)
    if (m_options.starttime > 0)
      SeekTime(m_options.starttime * 1000);
    SetVolume(g_settings.m_nVolumeLevel);
    SetAVDelay(g_settings.m_currentVideoSettings.m_AudioDelay);
    SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

    // at this point we should know all info about audio/video stream.
    // we are done initializing now, set the readyevent which will
    if (m_video_count)
    {
      // turn on/off subs
      SetSubtitleVisible(g_settings.m_currentVideoSettings.m_SubtitleOn);
      SetSubTitleDelay(g_settings.m_currentVideoSettings.m_SubtitleDelay);

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

      g_Windowing.InformVideoInfo(width, height, (int)(fFrameRate+0.5), m_mode3d_sbs);

      if(!g_renderManager.Configure(video_width, video_height,
        video_width, video_height, m_video_fps, flags, 0))
      {
        CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
      }
      if (!g_renderManager.IsStarted())
      {
        CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
      }
    }

    if (m_options.identify == false)
      m_callback.OnPlayBackStarted();

    // drop CGUIDialogBusy, and release the hold in OpenFile
    m_ready.Set();

    m_videoStats.Start();

    CSingleLock lock(m_csection);
    m_csection.unlock();

    while (!m_bStop && !m_StopPlaying)
    {
      if(m_paused)
      {
        OMXClock::OMXSleep(10);
        continue;
      }

      m_csection.lock();

      if(m_flush && omx_pkt)
      {
        OMXReader::FreePacket(omx_pkt);
        omx_pkt = NULL;
        m_flush = false;
      }

      if(m_change_volume)
      {
        m_player_audio.SetCurrentVolume(m_current_volume);
        m_change_volume = false;
      }
      
      /* when the audio buffer runs under 0.1 seconds we buffer up */
      if(m_audio_count)
      {
        if(m_player_audio.GetDelay() < 0.1f && !m_buffer_empty)
        {
          if(!m_av_clock->OMXIsPaused())
          {
            m_av_clock->OMXPause();
            //printf("buffering start\n");
            m_buffer_empty = true;
            clock_gettime(CLOCK_REALTIME, &starttime);
          }
        }
        if(m_player_audio.GetDelay() > (AUDIO_BUFFER_SECONDS * 0.75f) && m_buffer_empty)
        {
          if(m_av_clock->OMXIsPaused())
          {
            m_av_clock->OMXResume();
            //printf("buffering end\n");
            m_buffer_empty = false;
          }
        }
        if(m_buffer_empty)
        {
          clock_gettime(CLOCK_REALTIME, &endtime);
          if((endtime.tv_sec - starttime.tv_sec) > 1)
          {
            m_buffer_empty = false;
            m_av_clock->OMXResume();
            //printf("buffering timed out\n");
          }
        }
      }

      if(!omx_pkt)
        omx_pkt = m_omx_reader.Read();

      if(omx_pkt && m_omx_reader.IsActive(OMXSTREAM_VIDEO, omx_pkt->stream_index))
      {
        if(m_player_video.AddPacket(omx_pkt))
        {
          m_videoStats.AddSampleBytes(omx_pkt->size);
          omx_pkt = NULL;
        }
        else
        {
          OMXClock::OMXSleep(10);
        }
      }
      else if(omx_pkt && omx_pkt->codec_type == AVMEDIA_TYPE_AUDIO)
      {
        if(m_player_audio.AddPacket(omx_pkt))
          omx_pkt = NULL;
        else
          OMXClock::OMXSleep(10);
      }
      else if(omx_pkt && m_omx_reader.IsActive(OMXSTREAM_SUBTITLE, omx_pkt->stream_index))
      {
        if((omx_pkt->size && omx_pkt->hints.codec == CODEC_ID_TEXT) ||
           (omx_pkt->size && omx_pkt->hints.codec == CODEC_ID_SSA) )
        {
          if(m_player_video.AddPacket(omx_pkt))
            omx_pkt = NULL;
          else
            OMXClock::OMXSleep(10);
        }
      }
      else
      {
        if(omx_pkt)
        {
          OMXReader::FreePacket(omx_pkt);
          omx_pkt = NULL;
        }
      }

      /* player emergency exit */
      if(m_player_audio.Error())
        goto do_exit;

      if(m_stats)
      {
        printf("V : %8.02f %8d %8d A : %8.02f %8.02f Cv : %8d Ca : %8d                            \r",
             m_player_video.GetCurrentPTS() / DVD_TIME_BASE, m_player_video.GetDecoderBufferSize(),
             m_player_video.GetDecoderFreeSpace(), m_player_audio.GetCurrentPTS() / DVD_TIME_BASE,
             m_player_audio.GetDelay(), m_player_video.GetCached(), m_player_audio.GetCached());
      }

      if(m_player_audio.GetCurrentPTS() != AV_NOPTS_VALUE)
        m_elapsed_ms = m_player_audio.GetCurrentPTS() / 1000;
      else if(m_player_video.GetCurrentPTS() != AV_NOPTS_VALUE)
        m_elapsed_ms = m_player_video.GetCurrentPTS() / 1000;
      else
        m_elapsed_ms = 0;

      m_csection.unlock();

      if(m_omx_reader.IsEof())
      {
        got_eof = true; 
        break;
      }
    }
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "COMXPlayer::Process: Exception thrown");
  }

do_exit:

  try
  {
    if(got_eof)
    {
      if(m_audio_count)
        m_player_audio.WaitCompletion();
      else if(m_video_count)
        m_player_video.WaitCompletion();
    }

    m_av_clock->OMXStop();
    m_av_clock->OMXStateIdle();
  
    m_player_video.Close();
    m_player_audio.Close();

    if(omx_pkt)
    {
      OMXReader::FreePacket(omx_pkt);
      omx_pkt = NULL;
    }

    m_bStop = m_StopPlaying = true;
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "COMXPlayer::Process: Exception thrown");
  }
}

void COMXPlayer::RegisterAudioCallback(IAudioCallback* pCallback)
{
  m_player_audio.RegisterAudioCallback(pCallback);
}

void COMXPlayer::UnRegisterAudioCallback()
{
  m_player_audio.UnRegisterAudioCallback();
}

void COMXPlayer::DoAudioWork()
{
  m_player_audio.DoAudioWork();
}

#endif
