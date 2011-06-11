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

#if defined (HAVE_SIGMASMP)
#include "SMPPlayer.h"
#include "Application.h"
#include "FileItem.h"
#include "GUIInfoManager.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "settings/Settings.h"
#include "threads/SingleLock.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/URIUtils.h"

#include "FileIDataSource.h"

#include <directfb/directfb.h>
#include <directfb/iadvancedmediaprovider.h>
#include <cdefs_lpb.h>

union UMSStatus
{
  struct SStatus      generic;
  struct SLPBStatus   lpb;
};
union UMSCommand
{
  struct SCommand     generic;
  struct SLPBCommand  lpb;
};
union UMSResult
{
  struct SResult      generic;
  struct SLPBResult   lpb;
};

struct SIdsData
{
	CFileIDataSource *src;
	void *ch;
};

static const char *mediaType2String(TMediaType type)
{
  const char *text = NULL;

  switch (type)
  {
    case MTYPE_ELEM_NONE:     text = "none";                      break;
    case MTYPE_ELEM_UNKNOWN:  text = "unknown";                   break;
    case MTYPE_ELEM_MPEG1:    text = "MPEG1 video";               break;
    case MTYPE_ELEM_MPEG2:    text = "MPEG2 video";               break;
    case MTYPE_ELEM_MPEG4:    text = "MPEG4 video";               break;
    case MTYPE_ELEM_AVC:      text = "MPEG4 AVC video";           break;
    case MTYPE_ELEM_VC1:      text = "VC1 video";                 break;
    case MTYPE_ELEM_DIVX3:    text = "DIVX 3 video";              break;
    case MTYPE_ELEM_DIVX4:    text = "DIVX 4 video";              break;
    //case MTYPE_ELEM_MPA:    text = "MPEG audio";                break;
    case MTYPE_ELEM_MP1:      text = "MPEG1 Layer 1-2 audio";     break;
    //case MTYPE_ELEM_MP2:    text = "MPEG1 Layer 2 audio";       break;
    case MTYPE_ELEM_MP3:      text = "MPEG1 Layer 3 (MP3) audio"; break;
    case MTYPE_ELEM_MP2MC:    text = "MPEG2 (MC) audio";          break;
    case MTYPE_ELEM_AAC:      text = "MPEG2 AAC audio";           break;
    case MTYPE_ELEM_AACP:     text = "MPEG2 AAC+ audio";          break;
    case MTYPE_ELEM_AC3:      text = "Dolby Digital (AC3) audio"; break;
    case MTYPE_ELEM_DDP:      text = "Dolby Digital Plus audio";  break;
    case MTYPE_ELEM_DLLS:     text = "? audio";                   break;
    case MTYPE_ELEM_DTS:      text = "DTS audio";                 break;
    case MTYPE_ELEM_DTSHD:    text = "DTS-HD audio";              break;
    case MTYPE_ELEM_PCM:      text = "PCM audio";                 break;
    case MTYPE_ELEM_WMA:      text = "WMA";                       break;
    case MTYPE_ELEM_WMAPRO:   text = "WMA-PRO";                   break;
    case MTYPE_ELEM_SPU:      text = "SD graphic subtitles";      break;
    case MTYPE_ELEM_PG:       text = "HD graphic subtitles";      break;
    case MTYPE_ELEM_IG:       text = "Interactive graphics";      break;
    case MTYPE_ELEM_TS:       text = "Text subtitles";            break;
    case MTYPE_ELEM_JPEG:     text = "JPEG graphics";             break;
    case MTYPE_ELEM_GIF:      text = "GIF graphics";              break;
    case MTYPE_ELEM_PNG:      text = "PNG graphics";              break;
    case MTYPE_ELEM_BMP:      text = "BMP graphics";              break;
    case MTYPE_ELEM_TIFF:     text = "TIFF graphics";             break;
    case MTYPE_ELEM_PIXMAP:   text = "RAW graphics";              break;
    case MTYPE_ELEM_ASCII:    text = "ASCII text";                break;
    case MTYPE_ELEM_FONT:     text = "Font data";                 break;
    case MTYPE_ELEM_VIDEO:    text = "video";                     break;
    case MTYPE_ELEM_AUDIO:    text = "audio";                     break;
    case MTYPE_ELEM_GRAPHICS: text = "graphics";                  break;
    case MTYPE_CONT_ASF:      text = "asf";                       break;
    case MTYPE_CONT_AVI:      text = "avi";                       break;
    default:                  text = "invalid value";             break;
  }
  return text;
}

static const char *mediaTypeToCodecName(TMediaType type)
{
  const char *text = NULL;

  switch (type)
  {
    case MTYPE_ELEM_MPEG1:    text = "mpeg1video";  break;
    case MTYPE_ELEM_MPEG2:    text = "mpeg2video";  break;
    case MTYPE_ELEM_MPEG4:    text = "mpeg4";       break;
    case MTYPE_ELEM_AVC:      text = "avc1";        break;
    case MTYPE_ELEM_VC1:      text = "vc-1";        break;
    case MTYPE_ELEM_DIVX3:    text = "divx";        break;
    case MTYPE_ELEM_DIVX4:    text = "divx";        break;
    case MTYPE_ELEM_MP1:      text = "mp1";         break;
    //case MTYPE_ELEM_MP2:    text = "mp2";         break;
    case MTYPE_ELEM_MP3:      text = "mp3";         break;
    case MTYPE_ELEM_MP2MC:    text = "mp2";         break;
    case MTYPE_ELEM_AAC:      text = "aac";         break;
    case MTYPE_ELEM_AACP:     text = "aac";         break;
    case MTYPE_ELEM_AC3:      text = "ac3";         break;
    case MTYPE_ELEM_DDP:      text = "dca";         break;
    case MTYPE_ELEM_DTS:      text = "dts";         break;
    case MTYPE_ELEM_DTSHD:    text = "dtshd-ma";    break;
    case MTYPE_ELEM_PCM:      text = "pcm";         break;
    case MTYPE_ELEM_WMA:      text = "wma";         break;
    case MTYPE_ELEM_WMAPRO:   text = "wmapro";      break;
    default:                  text = "";            break;
  }
  return text;
}

CSMPPlayer::CSMPPlayer(IPlayerCallback &callback) 
  : IPlayer(callback),
  CThread(),
  m_ready(true)
{
  m_amp = NULL;
  // request video layer
  m_ampID = MAIN_VIDEO_AMP_ID;
  //m_ampID = SECONDARY_VIDEO_AMP_ID;
  m_speed = 0;
  m_paused = false;
  m_StopPlaying = false;

  m_status = malloc(sizeof(union UMSStatus));
  memset(m_status, 0, sizeof(union UMSStatus));
  ((UMSStatus*)m_status)->generic.size = sizeof(union UMSStatus);
  ((UMSStatus*)m_status)->generic.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
}

CSMPPlayer::~CSMPPlayer()
{
  CloseFile();

  free(m_status);
}

bool CSMPPlayer::OpenFile(const CFileItem &file, const CPlayerOptions &options)
{
  try
  {
    CLog::Log(LOGNOTICE, "CSMPPlayer: Opening: %s", file.m_strPath.c_str());
    // if playing a file close it first
    // this has to be changed so we won't have to close it.
    if(ThreadHandle())
      CloseFile();

    m_item = file;
    m_options = options;
    m_elapsed_ms  =  0;
    m_duration_ms =  0;
    m_audio_index = -1;
    m_audio_count =  0;
    m_audio_info  = "none";
    m_video_index = -1;
    m_video_count =  0;
    m_video_info  = "none";
    m_subtitle_index = -1;
    m_subtitle_count =  0;
    m_chapter_index  =  0;
    m_chapter_count  =  0;

    m_video_fps      =  0.0;
    m_video_width    =  0;
    m_video_height   =  0;

    IDirectFB *dfb = g_Windowing.GetIDirectFB();
    DFBResult res = dfb->GetInterface(dfb, "IAdvancedMediaProvider", "EM8630", (void*)m_ampID, (void **)&m_amp);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::OpenFile:Could not get IAdvancedMediaProvider");
      return false;
    }
    // The event buffer must be retrieved BEFORE the OpenMedia() call
    res = m_amp->GetEventBuffer(m_amp, &m_amp_event);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::OpenFile:Could not retrieve the AMP event buffer!!!");
      return false;
    }

    std::string  url;
    SMediaFormat format = { 0 };
#if 0
    CStdString    protocol = m_item.GetProtocol();
    if (protocol == "http")
    {
      format.mediaType = MTYPE_APP_UNKNOWN;
      // strip user agent that we append
      url = m_item.m_strPath;
      url = url.erase(url.rfind('|'), url.size());
    }
    else
#endif
    {
      // default to hinting container type
      CStdString extension;
      extension = URIUtils::GetExtension(m_item.m_strPath);
      if (extension.Equals(".wmv"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_ASF;
      else if (extension.Equals(".avi"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_AVI;
      else if (extension.Equals(".mkv"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_MKV;
      else if (extension.Equals(".mp4"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_MP4;
      else if (extension.Equals(".mov"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_MP4;
      else if (extension.Equals(".mpg"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
      else if (extension.Equals(".vob"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
      else if (extension.Equals(".ts"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
      else if (extension.Equals(".m2ts"))
        format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
      else
        format.mediaType = MTYPE_APP_UNKNOWN;

      // local source only for now, smb is failing to read
      SIdsData ids;
      char     c_str[64];
      // setup the IDataSource cookie, CloseMedia will delete it
      ids.src = new CFileIDataSource(m_item.m_strPath.c_str());
      snprintf(c_str, sizeof(c_str)/sizeof(char), "ids://0x%08lx", (long unsigned int)&ids);
      url = c_str;
    }

    // Setup open parameters
    struct SLPBOpenParams parameters = {0, };
    parameters.zero = 0;
    // magic cookie to indicate this is binary params
    parameters.a1b2c3d4 = 0xa1b2c3d4;
    // audio offset time from video in milliseconds 
    parameters.audioOffset = 0;
    // system time clock offset in milliseconds.
    // negative values will delay start of presentation.
    parameters.stcOffset = -200;
    // max prebuffersize in bytes, 0 for default
    parameters.maxPrebufferSize = 100 * 1024;

#if 0
//#if defined(SLPBPARAMS_MAX_TEXT_SUBS)
    // SRT, SSA, ASS, SUB/IDX or SMI
    // find any available external subtitles
    std::vector<CStdString> filenames;
    CUtil::ScanForExternalSubtitles( m_item.m_strPath, filenames );
    for(unsigned int i=0;i<filenames.size();i++)
    {
      if (URIUtils::GetExtension(filenames[i]) == ".idx")
      {
        CStdString strSubFile;
        if ( CUtil::FindVobSubPair( filenames, filenames[i], strSubFile ) )
          AddSubtitleFile(filenames[i], strSubFile);
      }
      if (URIUtils::GetExtension(filenames[i]) == ".sub")
      {
        CStdString strSubFile;
        if ( CUtil::FindVobSubPair( filenames, filenames[i], strSubFile ) )
          AddSubtitleFile(filenames[i], strSubFile);
      }
    } // end loop over all subtitle files    
#endif

    // open the media using the IAdvancedMediaProvider
    res = m_amp->OpenMedia(m_amp, (char*)url.c_str(), &format, &parameters);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::OpenFile:OpenMedia() failed");
      throw;
    }


    // create the playing thread
    m_StopPlaying = false;
    Create();
    // spin the busy dialog until we are playing
    m_ready.Reset();
    if(!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy *dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while(!m_ready.WaitMSec(1))
        g_windowManager.Process(false);
      dialog->Close();
    }

    // Playback might have been stopped due to some error.
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

bool CSMPPlayer::CloseFile()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::CloseFile");
  m_bStop = true;
  m_StopPlaying = true;

  CLog::Log(LOGDEBUG, "CSMPPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  if (m_amp)
    m_amp->Release(m_amp);
  m_amp = NULL;
  //  The event buffer must be released after the Release call
  if (m_amp_event)
    m_amp_event->Release(m_amp_event);
  m_amp_event = NULL;

  CLog::Log(LOGDEBUG, "CSMPPlayer: finished waiting");
  g_renderManager.UnInit();

  return true;
}

bool CSMPPlayer::IsPlaying() const
{
  return !m_bStop;
}

void CSMPPlayer::Pause()
{
  CSingleLock lock(m_amp_command_csection);

  if (!m_amp && m_StopPlaying)
    return;

  SLPBCommand cmd;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  if (m_paused)
  {
    cmd.cmd = LPBCmd_PAUSE_OFF;
    m_callback.OnPlayBackResumed();
  }
  else
  {
    cmd.cmd = LPBCmd_PAUSE_ON;
    m_callback.OnPlayBackPaused();
  }
  m_paused = !m_paused;

  SLPBResult  res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
    CLog::Log(LOGERROR, "CSMPPlayer::Pause:AMP command failed!");
  CLog::Log(LOGDEBUG, "CSMPPlayer::Pause");
}

bool CSMPPlayer::IsPaused() const
{
  return m_paused;
}

bool CSMPPlayer::HasVideo() const
{
  return m_video_count > 0;
}

bool CSMPPlayer::HasAudio() const
{
  return m_audio_count > 0;
}

void CSMPPlayer::ToggleFrameDrop()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::ToggleFrameDrop");
}

bool CSMPPlayer::CanSeek()
{
  return GetTotalTime() > 0;
}

void CSMPPlayer::Seek(bool bPlus, bool bLargeStep)
{
  CSingleLock lock(m_amp_command_csection);

  // try chapter seeking first, chapter_index is ones based.
  int chapter_index = GetChapter();
  if (bLargeStep)
  {
    // seek to next chapter
    if (bPlus && (chapter_index < GetChapterCount()))
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

  // force updated to m_elapsed_ms, m_duration_ms.
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
    CLog::Log(LOGDEBUG, "CSMPPlayer::Seek: In mystery code, what did I do");
    g_application.SeekTime((seek_ms - m_elapsed_ms) * 0.001 + g_application.GetTime());
    // warning, don't access any object variables here as
    // the object may have been destroyed
    return;
  }

  // bugfix, dcchd takes forever to seek to 0 and play
  //  seek to 1 second and play is immediate.
  if (seek_ms <= 0)
    seek_ms = 1000;
  
  if (seek_ms > m_duration_ms)
    seek_ms = m_duration_ms;

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.param1.seekMode    = SM_BY_TIME;
  cmd.param2.time.Hour   = (seek_ms / 3600000);
  cmd.param2.time.Minute = (seek_ms / 60000) % 60;
  cmd.param2.time.Second = (seek_ms / 1000)  % 60;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::Seek:to Hour(%lu), Minute(%lu), Second(%lu)",
  //  cmd.param2.time.Hour, cmd.param2.time.Minute, cmd.param2.time.Second);

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  // This will popup seek info dialog if skin supports it
  //g_infoManager.SetDisplayAfterSeek(100000);
  //m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - m_elapsed_ms));
  //g_windowManager.Process(false);

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
    CLog::Log(LOGERROR, "CSMPPlayer::SeekTime:AMP command failed!");

  //g_infoManager.SetDisplayAfterSeek();
}

bool CSMPPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::SeekScene");
  return false;
}

void CSMPPlayer::SeekPercentage(float fPercent)
{
  CSingleLock lock(m_amp_command_csection);

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  cmd.param1.seekMode   = SM_BY_PERCENTAGE;
  cmd.param2.percentage = fPercent;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
    CLog::Log(LOGERROR, "CSMPPlayer::SeekTime:AMP command failed!");
}

float CSMPPlayer::GetPercentage()
{
  GetTotalTime();
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

void CSMPPlayer::SetVolume(long nVolume)
{
  CSingleLock lock(m_amp_command_csection);
  // nVolume is a milliBels from -6000 (-60dB or mute) to 0 (0dB or full volume)
  // 0db is represented by Volume = 0x10000000
  // bit shifts adjust by 6db.
  // Maximum gain is 0xFFFFFFFF ~=24db
  uint32_t volume = (1.0f + (nVolume / 6000.0f)) * (float)0x10000000;

  SAdjustment adjustment;
  // use ADJ_MIXER_VOLUME_MAIN and not ADJ_VOLUME as
  // ADJ_VOLUME will cause a complete re-init of audio decoder.
  adjustment.type = ADJ_MIXER_VOLUME_MAIN;
  adjustment.value.volume	= volume;

  SLPBCommand cmd;
  cmd.cmd = (ELPBCmd)Cmd_Adjust;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  cmd.control.adjustment = &adjustment;

  if (m_amp->PostPresentationCmd(m_amp, (SCommand*)&cmd) != DFB_OK)
    CLog::Log(LOGDEBUG, "CSMPPlayer::SetVolume:AMP command failed!");
}

void CSMPPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioInfo");
  if (GetAmpStatus())
  {
    m_audio_info.Format("Audio stream (%d) [%s] of type %s",
      ((UMSStatus*)m_status)->lpb.audio.index,
      ((UMSStatus*)m_status)->lpb.audio.name,
      mediaType2String(((UMSStatus*)m_status)->lpb.audio.format.mediaType));
  }
  strAudioInfo = m_audio_info;
}

void CSMPPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetVideoInfo");
  if (GetAmpStatus())
  {
    m_video_info.Format("Video stream (%d) [%s] of type %s",
      ((UMSStatus*)m_status)->lpb.video.index,
      ((UMSStatus*)m_status)->lpb.video.name,
      mediaType2String(((UMSStatus*)m_status)->lpb.video.format.mediaType));
  }
  strVideoInfo = m_video_info;
}

int CSMPPlayer::GetAudioStreamCount()
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioStreamCount");
  if (GetAmpStatus())
    m_audio_count = ((UMSStatus*)m_status)->lpb.media.audio_streams;

  return m_audio_count;
}

int CSMPPlayer::GetAudioStream()
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioStream");
  if (GetAmpStatus())
    m_audio_index = ((UMSStatus*)m_status)->lpb.audio.index;

	return m_audio_index;
}

void CSMPPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  CSingleLock lock(m_amp_command_csection);

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_GET_AUDIO_STREAM_INFO;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    strStreamName.Format("%s", res.value.streamInfo.name);
  else
    strStreamName.Format("Undefined");
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioStreamName");
}
 
void CSMPPlayer::SetAudioStream(int SetAudioStream)
{
  CSingleLock lock(m_amp_command_csection);

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SELECT_AUDIO_STREAM;
  cmd.param1.streamIndex = SetAudioStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
  //CLog::Log(LOGDEBUG, "CSMPPlayer::SetAudioStream");
}

int CSMPPlayer::GetSubtitleCount()
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetSubtitleCount");
  if (GetAmpStatus())
    m_subtitle_count = ((UMSStatus*)m_status)->lpb.media.subtitle_streams;

	return m_subtitle_count;
}

int CSMPPlayer::GetSubtitle()
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetSubtitle");
  if (GetAmpStatus())
    m_subtitle_index = ((UMSStatus*)m_status)->lpb.subtitle.index;

	return m_subtitle_index;
}

void CSMPPlayer::GetSubtitleName(int iStream, CStdString &strStreamName)
{
  CSingleLock lock(m_amp_command_csection);

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_GET_SUBTITLE_STREAM_INFO;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    strStreamName.Format("%s", res.value.streamInfo.name);
  else
    strStreamName.Format("Undefined");
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetSubtitleName");
}
 
void CSMPPlayer::SetSubtitle(int iStream)
{
  CSingleLock lock(m_amp_command_csection);

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SELECT_SUBTITLE_STREAM;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
  //CLog::Log(LOGDEBUG, "CSMPPlayer::SetSubtitle");
}

bool CSMPPlayer::GetSubtitleVisible()
{
  return m_subtitle_show;
}

void CSMPPlayer::SetSubtitleVisible(bool bVisible)
{
  CSingleLock lock(m_amp_command_csection);

  m_subtitle_show = bVisible;
  g_settings.m_currentVideoSettings.m_SubtitleOn = bVisible;

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SELECT_SUBTITLE_STREAM;
  if (bVisible)
    cmd.param1.streamIndex = GetSubtitle();
  else
    cmd.param1.streamIndex = -1;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
}

int CSMPPlayer::AddSubtitle(const CStdString& strSubPath)
{
  // this waits until we can stop/restart amp stream.
  return -1;
}

void CSMPPlayer::Update(bool bPauseDrawing)
{
  g_renderManager.Update(bPauseDrawing);
}

void CSMPPlayer::GetVideoRect(CRect& SrcRect, CRect& DestRect)
{
  g_renderManager.GetVideoRect(SrcRect, DestRect);
}

void CSMPPlayer::GetVideoAspectRatio(float &fAR)
{
  fAR = g_renderManager.GetAspectRatio();
}

int CSMPPlayer::GetChapterCount()
{
  return m_chapter_count;
}

int CSMPPlayer::GetChapter()
{
  // returns a one based value.
  int chapter_index = m_chapter_index + 1;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetChapter:chapter_index(%d)", chapter_index);
  return chapter_index;
}

void CSMPPlayer::GetChapterName(CStdString& strChapterName)
{
  if (m_chapter_count)
    strChapterName = m_chapters[m_chapter_index].name;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetChapterName:strChapterName(%s)", strChapterName.c_str());
}

int CSMPPlayer::SeekChapter(int chapter_index)
{
  CSingleLock lock(m_amp_command_csection);

  GetAmpStatus();
  // chapter_index is a one based value.
  int chapter_count = GetChapterCount();
  if (chapter_count > 0)
  {
    if (chapter_index < 1)
      chapter_index = 1;
    if (chapter_index > chapter_count)
      chapter_index = chapter_count;

    // amp time units are seconds,
    // so we add 1000ms to get into the chapter.
    int64_t seek_ms = m_chapters[chapter_index - 1].seekto_ms + 1000;

    // bugfix: dcchd takes forever to seek to 0 and play
    //  seek to 1 second and play is immediate.
    if (seek_ms <= 0)
      seek_ms = 1000;

    SLPBCommand cmd;
    cmd.cmd = LPBCmd_SEEK;
    cmd.dataSize = sizeof(cmd);
    cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
    cmd.param1.seekMode    = SM_BY_TIME;
    cmd.param2.time.Hour   = (seek_ms / 3600000);
    cmd.param2.time.Minute = (seek_ms / 60000) % 60;
    cmd.param2.time.Second = (seek_ms / 1000)  % 60;
    //CLog::Log(LOGDEBUG, "CSMPPlayer::SeekChapter:to Hour(%lu), Minute(%lu), Second(%lu)",
    //  cmd.param2.time.Hour, cmd.param2.time.Minute, cmd.param2.time.Second);

    SLPBResult res;
    res.dataSize = sizeof(res);
    res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

    // This will popup seek info dialog if skin supports it.
    //g_infoManager.SetDisplayAfterSeek(100000);
    //m_callback.OnPlayBackSeekChapter(GetChapter());
    //g_windowManager.Process(false);

    if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
      CLog::Log(LOGERROR, "CSMPPlayer::SeekChapter:ExecutePresentationCmd failed!");

    if (IS_SUCCESS( ((SResult*)&res)->value ))
    {
      for (int timeout = 10; timeout > 0; timeout--)
      {
        usleep(100*1000);
        // wait until this chapter starts playing
        GetAmpStatus();
        if (chapter_index == GetChapter())
          break;
        //g_windowManager.Process(false);
      }
    }
    else
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::SeekChapter:LPBCmd_SEEK failed");
    }
    //g_infoManager.SetDisplayAfterSeek();
  }
  else
  {
    // we do not have a chapter list so do a regular big jump.
    if (chapter_index > GetChapter())
      Seek(true,  true);
    else
      Seek(false, true);
  }
  return 0;
}

float CSMPPlayer::GetActualFPS()
{
  if (GetAmpStatus())
  {
    float rateN = ((UMSStatus*)m_status)->lpb.video.format.format.image.rateN;
    if (rateN > 0.0)
    {
      float rateM = ((UMSStatus*)m_status)->lpb.video.format.format.image.rateM;
      m_video_fps = rateM / rateN;
    }
  }
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetActualFPS:m_video_fps(%f)", m_video_fps);
  return m_video_fps;
}

void CSMPPlayer::SeekTime(__int64 seek_ms)
{
  CSingleLock lock(m_amp_command_csection);
  // bugfix, dcchd takes forever to seek to 0 and play
  //  seek to 1 second and play is immediate.
  if (seek_ms <= 0)
    seek_ms = 1000;

  // iTime units are milliseconds.
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  cmd.param1.seekMode    = SM_BY_TIME;
  cmd.param2.time.Hour   = (seek_ms / 3600000);
  cmd.param2.time.Minute = (seek_ms / 60000) % 60;
  cmd.param2.time.Second = (seek_ms / 1000)  % 60;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::SeekTime:to Hour(%lu), Minute(%lu), Second(%lu)",
  //  cmd.param2.time.Hour, cmd.param2.time.Minute, cmd.param2.time.Second);

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  // This will popup seek info dialog if skin supports it.
  //g_infoManager.SetDisplayAfterSeek(100000);
  //m_callback.OnPlayBackSeek((int)seek_ms, (int)(seek_ms - GetTime()));
  //g_windowManager.Process(false);

  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
    CLog::Log(LOGERROR, "CSMPPlayer::SeekTime:AMP command failed!");

  //g_infoManager.SetDisplayAfterSeek();
}

__int64 CSMPPlayer::GetTime()
{
  return m_elapsed_ms;
}

int CSMPPlayer::GetTotalTime()
{
  if (GetAmpStatus())
    m_duration_ms = 1000 * ((UMSStatus*)m_status)->lpb.media.duration;
	return m_duration_ms / 1000;
}

int CSMPPlayer::GetAudioBitrate()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioBitrate");
  return 0;
}
int CSMPPlayer::GetVideoBitrate()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetVideoBitrate");
  return 0;
}

int CSMPPlayer::GetSourceBitrate()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetSourceBitrate");
  return 0;
}

int CSMPPlayer::GetChannels()
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetActualFPS");
  // returns number of audio channels (ie 5.1 = 6)
  if (GetAmpStatus())
  {
    m_audio_channels  = ((UMSStatus*)m_status)->lpb.audio.format.format.sound.channels;
    m_audio_channels += ((UMSStatus*)m_status)->lpb.audio.format.format.sound.lfe;
  }

  return m_audio_channels;
}

int CSMPPlayer::GetBitsPerSample()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetBitsPerSample");
  return 0;
}

int CSMPPlayer::GetSampleRate()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetSampleRate");
  return 0;
}

CStdString CSMPPlayer::GetAudioCodecName()
{
  CStdString strAudioCodec;
  if (GetAmpStatus())
    strAudioCodec = mediaTypeToCodecName(((UMSStatus*)m_status)->lpb.audio.format.mediaType);
  return strAudioCodec;
}

CStdString CSMPPlayer::GetVideoCodecName()
{
  CStdString strVideoCodec;
  if (GetAmpStatus())
    strVideoCodec = mediaTypeToCodecName(((UMSStatus*)m_status)->lpb.audio.format.mediaType);
  return strVideoCodec;
}

int CSMPPlayer::GetPictureWidth()
{
  if (GetAmpStatus())
    m_video_width = ((UMSStatus*)m_status)->lpb.video.format.format.image.width;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetPictureWidth(%d)", m_video_width);
  return m_video_width;
}

int CSMPPlayer::GetPictureHeight()
{
  if (GetAmpStatus())
    m_video_height= ((UMSStatus*)m_status)->lpb.video.format.format.image.height;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetPictureHeight(%)", m_video_height);
  return m_video_height;
}

bool CSMPPlayer::GetStreamDetails(CStreamDetails &details)
{
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetStreamDetails");
  return false;
}

void CSMPPlayer::ToFFRW(int iSpeed)
{
  CSingleLock lock(m_amp_command_csection);

  if (!m_amp && m_StopPlaying)
    return;

  if (m_speed != iSpeed)
  {
    SLPBCommand cmd;
    cmd.dataSize = sizeof(cmd);
    cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

    // recover power of two value
    int ipower = 0;
    int ispeed = abs(iSpeed);
    while (ispeed >>= 1) ipower++;

    switch(ipower)
    {
      // regular playback
      case  0:
        cmd.cmd = LPBCmd_PLAY;
        cmd.param2.speed = 1 * 1024;
      break;
      default:
        // N x fast forward/rewind (I-frames)
        if (iSpeed > 0)
          cmd.cmd = LPBCmd_FAST_FORWARD;
        else
          cmd.cmd = LPBCmd_SCAN_BACKWARD;
        cmd.param2.speed = ipower * 1024;
      break;
    }

    SLPBResult res;
    res.dataSize = sizeof(res);
    res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

    if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
      CLog::Log(LOGERROR, "CSMPPlayer::ToFFRW:AMP command failed!");

    m_speed = iSpeed;
  }
}

void CSMPPlayer::OnStartup()
{
  //CThread::SetName("CSMPPlayer");
  g_renderManager.PreInit();
}

void CSMPPlayer::OnExit()
{
  //CLog::Log(LOGNOTICE, "CSMPPlayer::OnExit()");
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

void CSMPPlayer::Process()
{
  try
  {
    // wait for media to open with 20 second timeout.
    if (WaitForAmpOpenMedia(20000))
    {
      DFBResult res;
      // start the playback.
      res = m_amp->StartPresentation(m_amp, DFB_TRUE);
      if (res != DFB_OK)
      {
        CLog::Log(LOGDEBUG,"CSMPPlayer::Process:StartPresentation() failed");
        throw;
      }
    }
    else
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::Process:WaitForAmpOpenMedia timeout");
      throw;
    }
/*
    // wait for amp startup to stopped with 2 second timeout
    if (WaitForAmpStopped(2000))
    {
      SLPBCommand cmd;
      cmd.dataSize = sizeof(cmd);
      cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
      cmd.cmd = LPBCmd_PLAY;
      cmd.param2.speed = 1 * 1024;

      if (m_options.starttime > 0)
      {
        int seek_s = m_options.starttime;
        // starttime has units of seconds
        cmd.cmd = LPBCmd_PLAY_TIME;
        cmd.param2.speed       = 1 * 1024;
        cmd.param2.time.Hour   = (seek_s / 3600);
        cmd.param2.time.Minute = (seek_s / 60) % 60;
        cmd.param2.time.Second = (seek_s % 60);
      }

      SLPBResult res;
      res.dataSize = sizeof(res);
      res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

      if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
        CLog::Log(LOGERROR, "CSMPPlayer::ToFFRW:AMP command failed!");
    }
    else
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::Process:WaitFor AmpStartup timeout");
      throw;
    }
*/
    // wait for playback to start with 2 second timeout
    if (WaitForAmpPlaying(2000))
    {
      // hide the video layer so we can get stream info
      // first, then do a nice transition away from gui.
      ShowAmpVideoLayer(false);

      // drop CGUIDialogBusy dialog and release the hold in OpenFile.
      m_ready.Set();

      // get our initial status.
      GetAmpStatus();

      // starttime has units of seconds
      if (m_options.starttime > 0)
      {
        // BUGFIX: if we try to seek before amp renders 1st frame,
        // bad things happen.
        usleep(100*1000);
        SeekTime(m_options.starttime * 1000);
        WaitForAmpPlaying(1000);
      }

      // wait until video.format.formatValid or audio.format.formatValid
      WaitForAmpFormatValid(2000);

      // we are playing but hidden and all stream fields are valid.
      // check for video in media content
      if (GetVideoStreamCount() > 0)
      {
        // turn on/off subs
        SetSubtitleVisible(g_settings.m_currentVideoSettings.m_SubtitleOn);

        // big fake out here, we do not know the video width, height yet
        // so setup renderer to full display size and tell it we are doing
        // bypass. This tell it to get out of the way as amp will be doing
        // the actual video rendering in a video plane that is under the GUI
        // layer.
        int width = g_graphicsContext.GetWidth();
        int height= g_graphicsContext.GetHeight();
        int displayWidth  = width;
        int displayHeight = height;
        double fFrameRate = 24;
        unsigned int flags = 0;

        flags |= CONF_FLAGS_FORMAT_BYPASS;
        flags |= CONF_FLAGS_FULLSCREEN;
        CStdString formatstr = "BYPASS";
        CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: %s",
          __FUNCTION__, GetPictureWidth(), GetPictureHeight(), GetActualFPS(), formatstr.c_str());
        g_renderManager.IsConfigured();
        if(!g_renderManager.Configure(width, height, displayWidth, displayHeight, fFrameRate, flags))
        {
          CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
        }
        if (!g_renderManager.IsStarted())
        {
          CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
        }
      }

      m_speed = 1;
      m_callback.OnPlayBackStarted();
      WaitForWindowFullScreenVideo(2000);
      // now we can show the video playback layer.
      ShowAmpVideoLayer(true);

      while (!m_bStop && !m_StopPlaying)
      {
        // AMP monitoring loop for automatic playback termination (250ms wait)
        if (m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 250) == DFB_OK)
        {
          // eat the event
          DFBEvent event;
          m_amp_event->GetEvent(m_amp_event, &event);

          if (GetAmpStatus() && (((UMSStatus*)m_status)->generic.flags & SSTATUS_MODE) &&
            (((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_STOPPED))
          {
            m_StopPlaying = true;
            break;
          }
        }
        else
        {
          // we should never get here but just in case.
          usleep(250*1000);
        }
      }
      m_callback.OnPlayBackEnded();
      
      // have to stop if playing before CloseMedia or bad things happen.
      if ((((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_STOPPED) != SSTATUS_MODE_STOPPED)
      {
        SLPBCommand cmd;
        cmd.dataSize = sizeof(cmd);
        cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

        cmd.cmd = LPBCmd_STOP;

        SLPBResult res;
        res.dataSize = sizeof(res);
        res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
        if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
          CLog::Log(LOGERROR, "CSMPPlayer::Process:AMP stop command failed!");
        if (!IS_SUCCESS( ((SResult*)&res)->value ))
        {
          CLog::Log(LOGERROR, "CSMPPlayer::Process:AMP stop SResult(%d)", ((SResult*)&res)->value);
        }
      }
    }
    else
    {
      m_ready.Set();
      m_StopPlaying = true;
      CLog::Log(LOGERROR, "CSMPPlayer::Process: WaitForAmpPlaying() failed, m_status.flags(0x%08lx), m_status.mode.flags(0x%08lx)",
        (long unsigned int)((UMSStatus*)m_status)->generic.flags,
        (long unsigned int)((UMSStatus*)m_status)->generic.mode.flags);
      throw;
    }

  }
  catch(...)
  {
    CLog::Log(LOGERROR, "CSMPPlayer::Process Exception thrown");
  }

  if (m_amp)
    m_amp->CloseMedia(m_amp);
}

int CSMPPlayer::GetVideoStreamCount()
{
  if (GetAmpStatus())
    m_video_count = ((UMSStatus*)m_status)->lpb.media.video_streams;
  //CLog::Log(LOGDEBUG, "CSMPPlayer::GetVideoStreamCount(%d)", m_video_count);
  return m_video_count;
}

void CSMPPlayer::ShowAmpVideoLayer(bool show)
{
  IDirectFB *dfb = g_Windowing.GetIDirectFB();
  // enable background layer to hide video playback layer while we start up
  IDirectFBScreen *screen;
  if (dfb->GetScreen(dfb, 0, &screen) == DFB_OK)
  {
    DFBScreenMixerConfig mixcfg;
    screen->GetMixerConfiguration(screen, 0, &mixcfg);
    mixcfg.flags = DSMCONF_LAYERS;
    // yes this is correct, to hide video we show background.
    if (show)
      //DFB_DISPLAYLAYER_IDS_ADD(mixcfg.layers, EM86LAYER_MAINVIDEO);
      DFB_DISPLAYLAYER_IDS_REMOVE(mixcfg.layers, EM86LAYER_BKGND);
    else
      //DFB_DISPLAYLAYER_IDS_REMOVE(mixcfg.layers, EM86LAYER_MAINVIDEO);
      DFB_DISPLAYLAYER_IDS_ADD(mixcfg.layers, EM86LAYER_BKGND);
    screen->SetMixerConfiguration(screen, 0, &mixcfg);
  }
  //CLog::Log(LOGDEBUG,"CSMPPlayer::ShowAmpVideoLayer: show(%d)", show);
}

bool CSMPPlayer::WaitForAmpStopped(int timeout_ms)
{
  bool rtn = false;

  while (!m_bStop && (timeout_ms > 0))
  {
    if (m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK)
    {
      // eat the event
      DFBEvent event;
      m_amp_event->GetEvent(m_amp_event, &event);

      if (GetAmpStatus())
      {
        if ((((UMSStatus*)m_status)->generic.flags & SSTATUS_MODE) && 
            (((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_STOPPED))
        {
          rtn = true;
          break;
        }
      }
    }
    else
    {
      // we should never get here but just in case.
      usleep(100*1000);
    }
    timeout_ms -= 100;
  }

  return rtn;
}

bool CSMPPlayer::WaitForAmpPlaying(int timeout_ms)
{
  bool rtn = false;

  while (!m_bStop && (timeout_ms > 0))
  {
    if (m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK)
    {
      // eat the event
      DFBEvent event;
      m_amp_event->GetEvent(m_amp_event, &event);

      if (GetAmpStatus())
      {
        if ((((UMSStatus*)m_status)->generic.flags & SSTATUS_MODE) && 
            (((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_PLAYING))
        {
          rtn = true;
          break;
        }
      }
    }
    else
    {
      // we should never get here but just in case.
      usleep(100*1000);
    }
    timeout_ms -= 100;
  }

  return rtn;
}

bool CSMPPlayer::WaitForAmpOpenMedia(int timeout_ms)
{
  bool rtn = false;

  while (!m_bStop && (timeout_ms > 0))
  {
    if (m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK)
    {
      // eat the event
      DFBEvent event;
      m_amp_event->GetEvent(m_amp_event, &event);

      if (GetAmpStatus())
      {
        if ((((UMSStatus*)m_status)->generic.flags & SSTATUS_COMMAND) &&
           IS_SUCCESS(((UMSStatus*)m_status)->generic.lastCmd.result))
        {
          rtn = true;
          break;
        }
      }
      g_windowManager.Process(false);
    }
    else
    {
      // we should never get here but just in case.
      g_windowManager.Process(false);
      usleep(100*1000);
    }
    timeout_ms -= 100;
  }

  return rtn;
}

bool CSMPPlayer::WaitForAmpFormatValid(int timeout_ms)
{
  bool rtn = false;

  while (!m_bStop && (timeout_ms > 0))
  {
    usleep(100*1000);
    if (GetAmpStatus())
    {
      if (((UMSStatus*)m_status)->lpb.video.format.formatValid ||
          ((UMSStatus*)m_status)->lpb.audio.format.formatValid)
      {
        rtn = true;
        break;
      }
    }
    timeout_ms -= 100;
  }

  return rtn;
}

bool CSMPPlayer::WaitForWindowFullScreenVideo(int timeout_ms)
{
  bool rtn = false;

  while (!m_bStop && (timeout_ms > 0))
  {
    usleep(100*1000);
    if (g_windowManager.GetActiveWindow() == WINDOW_FULLSCREEN_VIDEO)
    {
      rtn = true;
      break;
    }
    timeout_ms -= 100;
  }
  //usleep(500*1000);

  return rtn;
}


bool CSMPPlayer::GetAmpStatus()
{
  static uint32_t flags = 0;
  CSingleLock lock(m_amp_status_csection);

  // get status, only update what has changed (DFB_FALSE).
  if (m_amp->UploadStatusChanges(m_amp, (SStatus*)m_status, DFB_FALSE) == DFB_OK)
  {
    int elapsed_ms    = 1000 * ((UMSStatus*)m_status)->generic.elapsedTime;
    int chapter_count = 0;
#if defined(SLPBSTATUS_CHAPTER_LIST_SIZE)
    chapter_count = ((UMSStatus*)m_status)->lpb.media.nb_chapters;
#endif
    if ((elapsed_ms != m_elapsed_ms) || (chapter_count != m_chapter_count))
    {
#if defined(SLPBSTATUS_CHAPTER_LIST_SIZE)
      if (m_chapter_count != chapter_count)
      {
        // update avi/mkv chapters.
        for (int i = 0; i < chapter_count; i++)
        {
          m_chapters[i].name = ((UMSStatus*)m_status)->lpb.media.chapterList[i].pName;
          m_chapters[i].seekto_ms = ((UMSStatus*)m_status)->lpb.media.chapterList[i].time_ms;
        }
        // update internal AFTER we update names/seekto_ms.
        m_chapter_count = chapter_count;
      }
#endif

      int chapter_index = 0;
      // check for a chapter list and figure out which chapter we are in.
      // chapter_index is zero based here.
      for (int chapter_num = m_chapter_count; chapter_num > 0; chapter_num--)
      {
        chapter_index = chapter_num - 1;
        // potential problem here, elapsedTime is seconds so
        // it might take one second+ for us to see any chapter seeks.
        if ((elapsed_ms/1000) >= (m_chapters[chapter_index].seekto_ms/1000))
          break;
      }
      m_elapsed_ms = elapsed_ms;
      m_chapter_index = chapter_index;
    }

    m_video_index = ((UMSStatus*)m_status)->lpb.video.index;
    m_video_count = ((UMSStatus*)m_status)->lpb.media.video_streams;

    if (flags != ((UMSStatus*)m_status)->generic.mode.flags)
    {
      CLog::Log(LOGDEBUG, "CSMPPlayer::GetAmpStatus: flags changed, old(%d), new(%d)",
        flags, ((UMSStatus*)m_status)->generic.mode.flags);
      flags = ((UMSStatus*)m_status)->generic.mode.flags;
    }
    
      
    return true;
  }
  else
  {
    CLog::Log(LOGDEBUG, "CSMPPlayer::GetAmpStatus:UploadStatusChanges return is not DFB_OK");
    return false;
  }
}
#endif
