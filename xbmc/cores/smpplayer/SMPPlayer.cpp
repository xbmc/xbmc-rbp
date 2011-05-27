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
#include "FileItem.h"
#include "cores/VideoRenderers/RenderManager.h"
#include "filesystem/SpecialProtocol.h"
#include "guilib/GUIWindowManager.h"
#include "settings/AdvancedSettings.h"
#include "threads/SingleLock.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/URIUtils.h"

#include "FileIDataSource.h"

#include <directfb/directfb.h>
#include <directfb/iadvancedmediaprovider.h>
#include <cdefs_lpb.h>

//#define HAS_SM_BY_TIME_MS

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
    case MTYPE_ELEM_NONE:
      // 0x0000
      // no elementary data exists or the format is irrelevant
      text = "none";
    break;

    case MTYPE_ELEM_UNKNOWN:
      // 0x00FF
      // elementary data exists, is relevant but the format is unknown
      text = "unknown";
    break;

    case MTYPE_ELEM_MPEG1:
      // 0x0011
      text = "MPEG1 video";
    break;

    case MTYPE_ELEM_MPEG2:
      // 0x0012
      text = "MPEG2 video";
    break;

    case MTYPE_ELEM_MPEG4:
      // 0x0014
      text = "MPEG4 video";
    break;

    case MTYPE_ELEM_AVC:
      // 0x001A
      text = "MPEG4 AVC video";
    break;

    case MTYPE_ELEM_VC1:
      // 0x0020
      text = "VC1 video";
    break;

    case MTYPE_ELEM_DIVX3:
      // 0x0033
      text = "DIVX 3 video";
    break;

    case MTYPE_ELEM_DIVX4:
      // 0x0034
      text = "DIVX 4 video";
    break;
/*
    case MTYPE_ELEM_MPA:
      // 0x0040
      // any MPEG audio
      text = "MPEG audio";
    break;
*/

    case MTYPE_ELEM_MP1:
      // 0x0041
      text = "MPEG1 Layer 1-2 audio";
    break;

/*
    case MTYPE_ELEM_MP2:
      // 0x0042
      text = "MPEG1 Layer 2 audio";
    break;
*/

    case MTYPE_ELEM_MP3:
      // 0x0043
      text = "MPEG1 Layer 3 (MP3) audio";
    break;

    case MTYPE_ELEM_MP2MC:
      // 0x0044
      text = "MPEG2 (MC) audio";
    break;

    case MTYPE_ELEM_AAC:
      // 0x0045
      text = "MPEG2 AAC audio";
    break;

    case MTYPE_ELEM_AACP:
      // 0x0046
      text = "MPEG2 AAC+ audio";
    break;

    case MTYPE_ELEM_AC3:
      // 0x0053
      text = "Dolby Digital (AC3) audio";
    break;

    case MTYPE_ELEM_DDP:
      // 0x0054
      text = "Dolby Digital Plus audio";
    break;

    case MTYPE_ELEM_DLLS:
      // 0x0055
      text = "? audio";
    break;

    case MTYPE_ELEM_DTS:
      // 0x0060
      text = "DTS audio";
    break;

    case MTYPE_ELEM_DTSHD:
      // 0x0061
      text = "DTS-HD audio";
    break;

    case MTYPE_ELEM_PCM:
      // 0x0070
      text = "PCM audio";
    break;

    case MTYPE_ELEM_WMA:
      // 0x0080
      text = "WMA";
    break;

    case MTYPE_ELEM_WMAPRO:
      // 0x0081
      text = "WMA-PRO";
    break;

    case MTYPE_ELEM_SPU:
      // 0x0090
      text = "SD graphic subtitles";
    break;

    case MTYPE_ELEM_PG:
      // 0x0091
      text = "HD graphic subtitles";
    break;

    case MTYPE_ELEM_IG:
      // 0x00A0
      text = "Interactive graphics";
    break;

    case MTYPE_ELEM_TS:
      // 0x00A2
      text = "Text subtitles";
    break;

    case MTYPE_ELEM_JPEG:
      // 0x00B0
      text = "JPEG graphics";
    break;

    case MTYPE_ELEM_GIF:
      // 0x00B1
      text = "GIF graphics";
    break;

    case MTYPE_ELEM_PNG:
      // 0x00B2
      text = "PNG graphics";
    break;

    case MTYPE_ELEM_BMP:
      // 0x00B3
      text = "BMP graphics";
    break;

    case MTYPE_ELEM_TIFF:
      // 0x00B4
      text = "TIFF graphics";
    break;

    case MTYPE_ELEM_PIXMAP:
      // 0x00BF  ///< uncompressed pixel map
      text = "RAW graphics";
    break;

    case MTYPE_ELEM_ASCII:
      // 0x00C1
      text = "ASCII text";
    break;

    case MTYPE_ELEM_FONT:
      // 0x00C2  ///< TrueType font data
      text = "Font data";
    break;

    case MTYPE_ELEM_VIDEO:
      // 0x00F1  ///< any video type
      text = "video";
    break;

    case MTYPE_ELEM_AUDIO:
      // 0x00F2  ///< any audio type
      text = "audio";
    break;

    case MTYPE_ELEM_GRAPHICS:
      // 0x00F3  ///< any graphics type
      text = "graphics";
    break;

    case MTYPE_CONT_ASF:
      text = "asf";
    break;

    case MTYPE_CONT_AVI:
      text = "avi";
    break;

  default:
      text = "invalid value";
      break;
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
  m_speed = 1;
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
    m_subtitle_info  = "none";
    m_video_fps = 0.0;
    m_video_width  = 0;
    m_video_height = 0;

    IDirectFB *dfb = g_Windowing.GetIDirectFB();
    DFBResult res = dfb->GetInterface(dfb, "IAdvancedMediaProvider", "EM8630", (void*)m_ampID, (void **)&m_amp);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG, "Could not instantiate the AMP interface");
      return false;
    }
    // The event buffer must be retrieved BEFORE the OpenMedia() call
    res = m_amp->GetEventBuffer(m_amp, &m_amp_event);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG, "Could not retrieve the AMP event buffer!!!");
      return false;
    }

    DFBAdvancedMediaProviderDescription desc;
    memset(&desc, 0, sizeof(desc));
		m_amp->GetDescription(m_amp, &desc);
    //SGlobals *ampGlobals = (SGlobals*)desc.privateInfo;

    m_StopPlaying = false;
    m_ready.Reset();
    Create();
    if(!m_ready.WaitMSec(100))
    {
      CGUIDialogBusy* dialog = (CGUIDialogBusy*)g_windowManager.GetWindow(WINDOW_DIALOG_BUSY);
      dialog->Show();
      while(!m_ready.WaitMSec(1))
        g_windowManager.Process(false);
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

bool CSMPPlayer::CloseFile()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::CloseFile");
  m_bStop = true;
  m_StopPlaying = true;

  CLog::Log(LOGNOTICE, "CSMPPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  if (m_amp)
    m_amp->Release(m_amp);
  m_amp = NULL;
  //  The event buffer must be released after the CloseMedia()/Release call
  if (m_amp_event)
    m_amp_event->Release(m_amp_event);
  m_amp_event = NULL;

  CLog::Log(LOGNOTICE, "CSMPPlayer: finished waiting");
  g_renderManager.UnInit();

  return true;
}

bool CSMPPlayer::IsPlaying() const
{
  return !m_bStop;
}

void CSMPPlayer::Pause()
{
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

  CSingleLock lock(m_StateSection);
  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
    CLog::Log(LOGDEBUG, "CSMPPlayer::Pause:AMP command failed!");
}

bool CSMPPlayer::IsPaused() const
{
  return m_paused;
}

bool CSMPPlayer::HasVideo() const
{
  return true;
}

bool CSMPPlayer::HasAudio() const
{
  return true;
}

void CSMPPlayer::ToggleFrameDrop()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::ToggleFrameDrop");
}

bool CSMPPlayer::CanSeek()
{
  return true;
}

void CSMPPlayer::Seek(bool bPlus, bool bLargeStep)
{
  int step = 10;

  if (bLargeStep)
    step = 5 * 60;

  if (!bPlus)
    step *= -1;

  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.param1.seekMode = SM_BY_TIME;
  memset(&cmd.param2.time, 0, sizeof(cmd.param2.time));
  cmd.param2.time.Second = (m_elapsed_ms/1000) + step;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
}

bool CSMPPlayer::SeekScene(bool bPlus)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::SeekScene");
  return false;
}

void CSMPPlayer::SeekPercentage(float fPercent)
{
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  cmd.param1.seekMode   = SM_BY_PERCENTAGE;
  cmd.param2.percentage = fPercent;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
}

float CSMPPlayer::GetPercentage()
{
  if (m_duration_ms)
    return 100.0f * (float)m_elapsed_ms/(float)m_duration_ms;
  else
    return 0.0f;
}

void CSMPPlayer::SetVolume(long nVolume)
{
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

  CSingleLock lock(m_StateSection);
  if (m_amp->PostPresentationCmd(m_amp, (SCommand*)&cmd) != DFB_OK)
    CLog::Log(LOGDEBUG, "CSMPPlayer::SetVolume:AMP command failed!");
}

void CSMPPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  strAudioInfo = m_audio_info;
}

void CSMPPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  strVideoInfo = m_video_info;
}

int CSMPPlayer::GetAudioStreamCount()
{
  return m_audio_count;
}

int CSMPPlayer::GetAudioStream()
{
	return m_audio_index;
}

void CSMPPlayer::GetAudioStreamName(int iStream, CStdString &strStreamName)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioStreamName");
  
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_GET_AUDIO_STREAM_INFO;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    strStreamName.Format("%s", res.value.streamInfo.name);
  else
    strStreamName.Format("Undefined");
}
 
void CSMPPlayer::SetAudioStream(int SetAudioStream)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::SetAudioStream");
  
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SELECT_AUDIO_STREAM;
  cmd.param1.streamIndex = SetAudioStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
}

int CSMPPlayer::GetSubtitleCount()
{
	return m_subtitle_count;
}

int CSMPPlayer::GetSubtitle()
{
	return m_subtitle_index;
}

void CSMPPlayer::GetSubtitleName(int iStream, CStdString &strStreamName)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetSubtitleName");
  
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_GET_SUBTITLE_STREAM_INFO;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    strStreamName.Format("%s", res.value.streamInfo.name);
  else
    strStreamName.Format("Undefined");
}
 
void CSMPPlayer::SetSubtitle(int iStream)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::SetSubtitle");
  
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SELECT_SUBTITLE_STREAM;
  cmd.param1.streamIndex = iStream;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);
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

float CSMPPlayer::GetActualFPS()
{
  return m_video_fps;
}

void CSMPPlayer::SeekTime(__int64 iTime)
{
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_SEEK;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  #ifdef HAS_SM_BY_TIME_MS
    cmd.param1.seekMode    = SM_BY_TIME_MS;
    cmd.param2.timems      = (uint32_t)iTime;
  #else
    cmd.param1.seekMode    = SM_BY_TIME;
    cmd.param2.time.Hour   = 0;
    cmd.param2.time.Minute = 0;
    cmd.param2.time.Second = (uint32_t)iTime / 1000;
    cmd.param2.time.Frame  = 0;
  #endif

  SLPBResult res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;

  CSingleLock lock(m_StateSection);
  m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res);

  int seekOffset = (int)(iTime - m_elapsed_ms);
  m_callback.OnPlayBackSeek((int)iTime, seekOffset);
}

__int64 CSMPPlayer::GetTime()
{
  return m_elapsed_ms;
}

int CSMPPlayer::GetTotalTime()
{
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
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetChannels");
  // returns number of audio channels
  return 6;
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
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetAudioCodecName");
  return "";
}

CStdString CSMPPlayer::GetVideoCodecName()
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetVideoCodecName");
 return "";
}

int CSMPPlayer::GetPictureWidth()
{
  return m_video_width;
}

int CSMPPlayer::GetPictureHeight()
{
  return m_video_height;
}

bool CSMPPlayer::GetStreamDetails(CStreamDetails &details)
{
  CLog::Log(LOGDEBUG, "CSMPPlayer::GetStreamDetails");
  return false;
}

void CSMPPlayer::ToFFRW(int iSpeed)
{
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

    CSingleLock lock(m_StateSection);
    if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) != DFB_OK)
      CLog::Log(LOGDEBUG, "CSMPPlayer::ToFFRW:AMP command failed!");

    m_speed = iSpeed;
  }
}

void CSMPPlayer::OnStartup()
{
  CThread::SetName("CSMPPlayer");

  g_renderManager.PreInit();
}

void CSMPPlayer::OnExit()
{
  CLog::Log(LOGNOTICE, "CSMPPlayer::OnExit()");
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
  DFBResult     res;
  SMediaFormat  format = { 0 };
  CStdString    url;

  CLog::Log(LOGDEBUG, "CSMPPlayer: Thread started");

  // default to hinting container type
  CStdString extension;
  extension = URIUtils::GetExtension(m_item.m_strPath);
  if (extension.Equals(".mkv"))
    format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_MKV;
  else if (extension.Equals(".avi"))
    format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_AVI;
  else if (extension.Equals(".mov"))
    format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_MP4;
  else if (extension.Equals(".mpg"))
    format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
  else if (extension.Equals(".m2ts"))
    format.mediaType = MTYPE_APP_NONE | MTYPE_CONT_M2TS;
  else
    format.mediaType = MTYPE_APP_UNKNOWN;

/*
  if (m_item.m_strPath.Left(7).Equals("http://"))
  {
    url = m_item.m_strPath;
  }
  else
*/
  {
    // local source only for now, smb is failing to read
    SIdsData      ids;
    char          c_str[64];
    // setup the IDataSource cookie, CloseMedia will delete it
    ids.src = new CFileIDataSource(m_item.m_strPath.c_str());
    snprintf(c_str, sizeof(c_str)/sizeof(char), "ids://0x%08lx", (long unsigned int)&ids);
    url = c_str;
  }
  
  // Setup open parameters
  struct SLPBOpenParams parameters = {0, };
  parameters.zero = 0;
  parameters.a1b2c3d4 = 0xa1b2c3d4;
  //
  parameters.maxPrebufferSize = 1;
  parameters.stcOffset = -200;
  
  // open the media using the IAdvancedMediaProvider
  res = m_amp->OpenMedia(m_amp, (char*)url.c_str(), &format, &parameters);
  if (res != DFB_OK)
  {
    CLog::Log(LOGDEBUG, "OpenMedia() failed");
    m_ready.Set();
    goto _exit;
  }
  
  // wait 10 seconds and check the confirmation event
  if ((m_amp_event->WaitForEventWithTimeout(m_amp_event, 40, 0) == DFB_OK) &&
      (m_amp->UploadStatusChanges(m_amp, (SStatus*)m_status, DFB_TRUE) == DFB_OK) &&
      (((UMSStatus*)m_status)->generic.flags & SSTATUS_COMMAND) &&
         IS_SUCCESS(((UMSStatus*)m_status)->generic.lastCmd.result))
  {
    // eat the event
    DFBEvent event;
    m_amp_event->GetEvent(m_amp_event, &event);

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
      __FUNCTION__, width, height, fFrameRate, formatstr.c_str());
    g_renderManager.IsConfigured();
    if(!g_renderManager.Configure(width, height, displayWidth, displayHeight, fFrameRate, flags))
    {
      CLog::Log(LOGERROR, "%s - failed to configure renderer", __FUNCTION__);
    }
    if (!g_renderManager.IsStarted()) {
      CLog::Log(LOGERROR, "%s - renderer not started", __FUNCTION__);
    }

    // start the playback
    res = m_amp->StartPresentation(m_amp, DFB_TRUE);
    if (res != DFB_OK)
    {
      CLog::Log(LOGDEBUG,"Could not issue StartPresentation()");
      m_ready.Set();
      goto _exit;
    }

    // we are done initializing now, set the readyevent,
    // drop CGUIDialogBusy, and release the hold in OpenFile
    m_ready.Set();

    // wait for playback to start with 2 second timeout
    if (WaitForAmpPlaying(20000))
    {
      m_callback.OnPlayBackStarted();

      while (!m_bStop && !m_StopPlaying)
      {
        // AMP monitoring loop for automatic playback termination (100ms wait)
        if (m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK)
        {
          // eat the event
          DFBEvent event;
          m_amp_event->GetEvent(m_amp_event, &event);

          if (m_amp->UploadStatusChanges(m_amp, (SStatus*)m_status, DFB_TRUE) == DFB_OK)
          {
            if ((((UMSStatus*)m_status)->generic.flags & SSTATUS_MODE) &&
                (((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_STOPPED))
            {
              m_StopPlaying = true;
            }
              
            #ifdef HAS_SM_BY_TIME_MS
              m_elapsed_ms= ((UMSStatus*)m_status)->generic.elapsedTimeMs;
            #else
              m_elapsed_ms= 1000 * ((UMSStatus*)m_status)->generic.elapsedTime;
            #endif
            m_duration_ms = 1000 * ((UMSStatus*)m_status)->lpb.media.duration;

            m_audio_index = ((UMSStatus*)m_status)->lpb.audio.index;
            m_audio_count = ((UMSStatus*)m_status)->lpb.media.audio_streams;
            m_audio_info.Format("Audio stream (%d) [%s] of type %s",
              ((UMSStatus*)m_status)->lpb.audio.index, ((UMSStatus*)m_status)->lpb.audio.name,
              mediaType2String(((UMSStatus*)m_status)->lpb.audio.format.mediaType));

            m_video_index = ((UMSStatus*)m_status)->lpb.video.index;
            m_video_count = ((UMSStatus*)m_status)->lpb.media.video_streams;
            m_video_info.Format("Video stream (%d) [%s] of type %s",
              ((UMSStatus*)m_status)->lpb.video.index, ((UMSStatus*)m_status)->lpb.video.name,
              mediaType2String(((UMSStatus*)m_status)->lpb.video.format.mediaType));
            m_video_fps   = ((UMSStatus*)m_status)->lpb.video.format.format.image.rateN;
            if (m_video_fps > 0.0)
              m_video_fps = (float)((UMSStatus*)m_status)->lpb.video.format.format.image.rateM / m_video_fps;
            m_video_width = ((UMSStatus*)m_status)->lpb.video.format.format.image.width;
            m_video_height= ((UMSStatus*)m_status)->lpb.video.format.format.image.height;

            m_subtitle_index = ((UMSStatus*)m_status)->lpb.subtitle.index;
            m_subtitle_count = ((UMSStatus*)m_status)->lpb.media.subtitle_streams;
            m_subtitle_info.Format("Subtitle stream (%d) [%s] of type %s",
              ((UMSStatus*)m_status)->lpb.subtitle.index, ((UMSStatus*)m_status)->lpb.subtitle.name,
              mediaType2String(((UMSStatus*)m_status)->lpb.subtitle.format.mediaType));
          }
          else
          {
            usleep(100*1000);
          }
        }
      }
      m_callback.OnPlayBackEnded();
    }
    else
    {
      CLog::Log(LOGDEBUG, "StartPresentation() failed, m_status.flags(0x%08lx), m_status.mode.flags(0x%08lx)",
        (long unsigned int)((UMSStatus*)m_status)->generic.flags,
        (long unsigned int)((UMSStatus*)m_status)->generic.mode.flags);
    }
  }
 
_exit:
  if (m_amp)
    m_amp->CloseMedia(m_amp);
  
  CLog::Log(LOGDEBUG, "CSMPPlayer: Thread end");
}

bool CSMPPlayer::WaitForAmpPlaying(int timeout)
{
  bool        rtn = false;

  while (!m_bStop && timeout > 0)
  {
    if ((m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK) &&
        (m_amp->UploadStatusChanges(m_amp, (SStatus*)m_status, DFB_TRUE) == DFB_OK))
    {
      // eat the event
      DFBEvent event;
      m_amp_event->GetEvent(m_amp_event, &event);

      if ((((UMSStatus*)m_status)->generic.flags & SSTATUS_MODE) && 
          (((UMSStatus*)m_status)->generic.mode.flags & SSTATUS_MODE_PLAYING))
      {
        rtn = true;
        break;
      }
    }
    timeout -= 100;
  }
  
  return rtn;
}

#endif
