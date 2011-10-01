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
}

COMXPlayer::~COMXPlayer()
{
  CloseFile();
}

bool COMXPlayer::Initialize(TiXmlElement* pConfig)
{
  return true;
}

bool COMXPlayer::OpenFile(const CFileItem &file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "COMXPlayer: Opening: %s", file.m_strPath.c_str());
    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if(ThreadHandle())
      CloseFile();

    std::string url;

    m_item = file;
    m_options = options;
    m_StopPlaying = false;

    m_elapsed_ms  = 0;
    m_duration_ms = 0;

    m_audio_index = 0;
    m_audio_count = 0;
    m_audio_bits  = 0;
    m_audio_channels = 0;
    m_audio_samplerate = 0;
    m_audio_offset_ms = g_settings.m_currentVideoSettings.m_AudioDelay;

    m_video_index = 0;
    m_video_count = 0;
    m_video_fps   = 0.0;
    m_video_width = 0;
    m_video_height= 0;

    m_subtitle_index = 0;
    m_subtitle_count = 0;
    m_subtitle_show  = g_settings.m_currentVideoSettings.m_SubtitleOn;
    m_chapter_count  = 0;
    m_subtitle_offset_ms = g_settings.m_currentVideoSettings.m_SubtitleDelay;

    // open file and start playing here.
    
    m_ready.Reset();
    Create();
    if (!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy* dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while(!m_ready.WaitMSec(1))
        g_windowManager.Process(false);
      dialog->Close();
    }
    // just in case process thread throws.
    m_ready.Set();

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
  return GetTotalTime() > 0;
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
    if (!bPlus && chapter_index > 1)
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

  SeekTime(seek_ms);
}

bool COMXPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "COMXPlayer::SeekScene");
  return false;
}

void COMXPlayer::SeekPercentage(float fPercent)
{
  // update m_elapsed_ms and m_duration_ms.
  GetTime();
  GetTotalTime();

  fPercent /= 100.0f;
  fPercent += (float)m_elapsed_ms/(float)m_duration_ms;
  // convert to milliseconds
  int64_t seek_ms = m_duration_ms * fPercent;
  SeekTime(seek_ms);
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
  return m_audio_offset_ms;
}

void COMXPlayer::SetSubTitleDelay(float fValue)
{
  // time offset in seconds of subtitle with respect to playback
  m_subtitle_offset_ms = fValue * 1e3;
  // set sub offset here
}

float COMXPlayer::GetSubTitleDelay()
{
  return m_subtitle_offset_ms;
}

void COMXPlayer::SetVolume(long nVolume)
{
  // nVolume is a milliBels from -6000 (-60dB or mute) to 0 (0dB or full volume)
  CSingleLock lock(m_csection);

  float volume;
  if (nVolume == -6000) {
    // We are muted
    volume = 0.0f;
  } else {
    volume = (double)nVolume / -10000.0f;
    // Convert what XBMC gives into what omx needs
  }
  //set volume here
}

void COMXPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  /*
  m_audio_info.Format("Audio stream (%d) [%s-%s]",
    m_audio_index, acodec_name[m_audio_index], acodec_language[m_audio_index]);
  */
  strAudioInfo = m_audio_info;
}

void COMXPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  /*
  m_video_info.Format("Video stream (%d) [%s-%s]",
    m_video_index, vcodec_name[m_video_index], vcodec_language[m_video_index]);
  */
  strVideoInfo = m_video_info;
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

void COMXPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  //strStreamName = acodec_name[iStream];
}
 
void COMXPlayer::SetAudioStream(int SetAudioStream)
{
  m_audio_index = SetAudioStream;
  // set audio index here
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
  // set audio index here
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
  display.SetRect(0, 0, g_settings.m_ResInfo[res].iScreenWidth, g_settings.m_ResInfo[res].iScreenHeight);
  
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
  // here you would set dest rect of video that is running on separate video plane
}

void COMXPlayer::GetVideoAspectRatio(float &fAR)
{
  fAR = g_renderManager.GetAspectRatio();
}

int COMXPlayer::GetChapterCount()
{
  // check for avi/mkv chapters.
  if (m_chapter_count == 0)
  {
  }
  //CLog::Log(LOGDEBUG, "COMXPlayer::GetChapterCount:m_chapter_count(%d)", m_chapter_count);
  return m_chapter_count;
}

int COMXPlayer::GetChapter()
{
  // returns a one based value.
  // if we have a chapter list, we need to figure out which chapter we are in.
  int chapter_index = 0;

  //CLog::Log(LOGDEBUG, "COMXPlayer::GetChapter:chapter_index(%d)", chapter_index);
  return chapter_index + 1;
}

void COMXPlayer::GetChapterName(CStdString& strChapterName)
{
  if (m_chapter_count)
    strChapterName = m_chapters[GetChapter() - 1].name;
  //CLog::Log(LOGDEBUG, "COMXPlayer::GetChapterName:strChapterName(%s)", strChapterName.c_str());
}

int COMXPlayer::SeekChapter(int chapter_index)
{
  // chapter_index is a one based value.
  CLog::Log(LOGDEBUG, "COMXPlayer::SeekChapter:chapter_index(%d)", chapter_index);
  /*
  {
    if (chapter_index < 0)
      chapter_index = 0;
    if (chapter_index > m_chapter_count)
      return 0;

    // Seek to the chapter.
    SeekTime(m_chapters[chapter_index - 1].seekto_ms);
  }
  else
  {
    // we do not have a chapter list so do a regular big jump.
    if (chapter_index > 0)
      Seek(true,  true);
    else
      Seek(false, true);
  }
  */
  return 0;
}

float COMXPlayer::GetActualFPS()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::GetActualFPS:m_video_fps(%f)", m_video_fps);
  return m_video_fps;
}

void COMXPlayer::SeekTime(__int64 seek_ms)
{
  // seek here
  m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - m_elapsed_ms));
}

__int64 COMXPlayer::GetTime()
{
  // get elapsed ms here
  return m_elapsed_ms;
}

int COMXPlayer::GetTotalTime()
{
  // get total ms here
	return m_duration_ms / 1000;
}

int COMXPlayer::GetAudioBitrate()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::GetAudioBitrate");
  return 0;
}
int COMXPlayer::GetVideoBitrate()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::GetVideoBitrate");
  return 0;
}

int COMXPlayer::GetSourceBitrate()
{
  CLog::Log(LOGDEBUG, "COMXPlayer::GetSourceBitrate");
  return 0;
}

int COMXPlayer::GetChannels()
{
  // returns number of audio channels (ie 5.1 = 6)
  return m_audio_channels;
}

int COMXPlayer::GetBitsPerSample()
{
  return m_audio_bits;
}

int COMXPlayer::GetSampleRate()
{
  return m_audio_samplerate;
}

CStdString COMXPlayer::GetAudioCodecName()
{
  if (m_audio_count && m_audio_index >= 0)
  {
    return acodec_name[m_audio_index];
  }
  else
  {
    return "";
  }
}

CStdString COMXPlayer::GetVideoCodecName()
{
  if (m_video_count && m_video_index >= 0)
  {
    return vcodec_name[m_video_index];
  }
  else
  {
    return "";
  }
}

int COMXPlayer::GetPictureWidth()
{
  return m_video_width;
}

int COMXPlayer::GetPictureHeight()
{
  return m_video_height;
}

bool COMXPlayer::GetStreamDetails(CStreamDetails &details)
{
  CLog::Log(LOGDEBUG, "COMXPlayer::GetStreamDetails");
  return false;
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
  return video_title;
}

int COMXPlayer::GetCacheLevel() const
{
  return 0;
}

void COMXPlayer::OnStartup()
{
  g_renderManager.PreInit();
}

void COMXPlayer::OnExit()
{
  //CLog::Log(LOGNOTICE, "COMXPlayer::OnExit()");
  usleep(100000);
  
  m_bStop = true;
  // if we didn't stop playing, advance to the next item in xbmc's playlist
  if(m_options.identify == false)
  {
    if (m_StopPlaying)
      m_callback.OnPlayBackStopped();
    else
      m_callback.OnPlayBackEnded();
  }
}

void COMXPlayer::Process()
{
  //CLog::Log(LOGDEBUG, "COMXPlayer: Thread started");
  try
  {
    // wait for playing

    if (waitplaying(4000))
    {
      // starttime has units of seconds (SeekTime will start playback)
      if (m_options.starttime > 0)
        SeekTime(m_options.starttime * 1000);
      SetVolume(g_settings.m_nVolumeLevel);
      SetAVDelay(m_audio_offset_ms);
      SetSubTitleDelay(m_subtitle_offset_ms);
        
      // we are done initializing now, set the readyevent which will
      // drop CGUIDialogBusy, and release the hold in OpenFile
      m_ready.Set();

      // at this point we should know all info about audio/video stream.

      if (m_video_count)
      {
        unsigned int flags = 0;
        flags |= CONF_FLAGS_FORMAT_BYPASS;
        flags |= CONF_FLAGS_FULLSCREEN;
        CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: BYPASS",
          __FUNCTION__, m_video_width, m_video_height, m_video_fps);

        if(!g_renderManager.Configure(m_video_width, m_video_height,
          m_video_width, m_video_height, m_video_fps, flags))
        {
          CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
        }
        if (!g_renderManager.IsStarted())
        {
          CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
        }
      }
    }
    else
    {
      CLog::Log(LOGERROR, "COMXPlayer::Process: WaitFoPlaying() failed");
      goto do_exit;
    }

    if (m_options.identify == false)
      m_callback.OnPlayBackStarted();

    while (!m_bStop && !m_StopPlaying)
    {
      usleep(50*1000);
    }
  }
  catch(...)
  {
    CLog::Log(LOGERROR, "COMXPlayer::Process: Exception thrown");
  }

do_exit:
  m_ready.Set();

}

#endif
