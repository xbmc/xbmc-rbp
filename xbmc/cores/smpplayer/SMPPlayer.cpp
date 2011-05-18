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
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#include "FileIDataSource.h"

#include <SDL/SDL_syswm.h>

#include <directfb/directfb.h>
#include <directfb/advancedmediaprovider.h>
#include <cdefs_lpb.h>

union UMSStatus
{
  struct SStatus      generic;
  struct SLPBStatus   lpb;
  uint8_t             pad[8192];
};
union UMSCommand
{
  struct SCommand     generic;
  struct SLPBCommand  lpb;
  uint8_t             pad[8192];
};
union UMSResult
{
  struct SResult      generic;
  struct SLPBResult   lpb;
  uint8_t             pad[8192];
};

struct SIdsData
{
	CFileIDataSource *src;
	void *ch;
};

CSMPPlayer::CSMPPlayer(IPlayerCallback &callback) 
  : IPlayer(callback),
  CThread(),
  m_ready(true)
{
  m_amp = NULL;
  // request video layer
  m_ampID = MAIN_VIDEO_AMP_ID;
  m_speed = 1;
  m_paused = false;
  m_StopPlaying = false;
}

CSMPPlayer::~CSMPPlayer()
{
  CloseFile();
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

    IDirectFB *dfb = (IDirectFB*)SDL_DirectFB_GetIDirectFB();
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
  m_bStop = true;
  m_StopPlaying = true;

  CLog::Log(LOGNOTICE, "CSMPPlayer: waiting for threads to exit");
  // wait for the main thread to finish up
  // since this main thread cleans up all other resources and threads
  // we are done after the StopThread call
  StopThread();

  CLog::Log(LOGDEBUG, "m_amp->Release(m_amp)");
  if (m_amp)
    m_amp->Release(m_amp);
  m_amp = NULL;
  CLog::Log(LOGDEBUG, "m_amp_event->Release(m_amp_event)");
  //  The event buffer must be released after the CloseMedia()/Release call
  if (m_amp_event)
    m_amp_event->Release(m_amp_event);

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
  fprintf(stderr, "CSMPPlayer::Pause\n");
  if (!m_amp)
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
  if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    CLog::Log(LOGDEBUG, "CSMPPlayer::Pause:AMP command succeeded\n");
  else
    CLog::Log(LOGDEBUG, "CSMPPlayer::Pause:AMP command failed!\n");
}

bool CSMPPlayer::IsPaused() const
{
  return m_paused;
}

void CSMPPlayer::SetVolume(long nVolume)
{
  //short vol = (1.0f + (nVolume / 6000.0f)) * 256.0f;
  //m_qtFile->SetVolume(vol);
}

void CSMPPlayer::GetAudioInfo(CStdString &strAudioInfo)
{
  //CSingleLock lock(m_StateSection);
}

void CSMPPlayer::GetVideoInfo(CStdString &strVideoInfo)
{
  //CSingleLock lock(m_StateSection);
}

void CSMPPlayer::GetGeneralInfo(CStdString &strVideoInfo)
{
  //CSingleLock lock(m_StateSection);
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

void CSMPPlayer::ToFFRW(int iSpeed)
{
  if (!m_amp)
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
    fprintf(stderr, "CSMPPlayer::ToFFRW, iSpeed(%d), ipower(%d)\n", iSpeed, ipower);

    SLPBResult res;
    res.dataSize = sizeof(res);
    res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
    if (m_amp->ExecutePresentationCmd(m_amp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
      CLog::Log(LOGDEBUG, "CSMPPlayer::ToFFRW:AMP command succeeded");
    else
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
  UMSStatus     status;
  SMediaFormat  format;
  CStdString    url;

  CLog::Log(LOGDEBUG, "CSMPPlayer: Thread started");

  // default to autodetection
  memset(&format, 0, sizeof(format));
  format.mediaType = MTYPE_APP_UNKNOWN;

  if (m_item.m_strPath.Left(7).Equals("http://"))
  {
    url = m_item.m_strPath;
  }
  else
  {
    // local source only for now, smb is failing to read
    SIdsData      ids;
    char          c_str[64];
    // setup the IDataSource cookie, CloseMedia will delete it
    ids.src = new CFileIDataSource(m_item.m_strPath.c_str());
    snprintf(c_str, sizeof(c_str)/sizeof(char), "ids://0x%08lx", (long unsigned int)&ids);
    url = c_str;
  }
  // open the media using the IAdvancedMediaProvider
  res = m_amp->OpenMedia(m_amp, (char*)url.c_str(), &format, NULL);
  if (res != DFB_OK)
  {
    CLog::Log(LOGDEBUG, "OpenMedia() failed");
    m_ready.Set();
    goto _exit;
  }
  
  memset(&status, 0, sizeof(status));
  status.generic.size = sizeof(SStatus);
  status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;
  // wait 10 seconds and check the confirmation event
  if ((m_amp_event->WaitForEventWithTimeout(m_amp_event, 10, 0) == DFB_OK) &&
      (m_amp->UploadStatusChanges(m_amp, (SStatus*)&status, DFB_TRUE)   == DFB_OK) &&
      (status.generic.flags & SSTATUS_COMMAND) && IS_SUCCESS(status.generic.lastCmd.result))
  {
    // eat the event
    DFBEvent event;
    m_amp_event->GetEvent(m_amp_event, &event);

    int m_width = g_graphicsContext.GetWidth();
    int m_height= g_graphicsContext.GetHeight();
    int m_displayWidth = m_width;
    int m_displayHeight= m_height;
    double m_fFrameRate = 24;
    
    unsigned int flags = 0;
    flags |= CONF_FLAGS_FORMAT_BYPASS;
    flags |= CONF_FLAGS_FULLSCREEN;
    CStdString formatstr = "BYPASS";
    CLog::Log(LOGDEBUG,"%s - change configuration. %dx%d. framerate: %4.2f. format: %s",
      __FUNCTION__, m_width, m_height, m_fFrameRate, formatstr.c_str());
    g_renderManager.IsConfigured();
    if(!g_renderManager.Configure(m_width, m_height, m_displayWidth, m_displayHeight, m_fFrameRate, flags))
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
      fprintf(stderr, "Could not issue StartPresentation()\n");
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

          memset(&status, 0, sizeof(status));
          status.generic.size = sizeof(status);
          status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;
          if ((m_amp->UploadStatusChanges(m_amp, (SStatus*)&status, DFB_TRUE) == DFB_OK) &&
              (status.generic.flags & SSTATUS_MODE) && (status.generic.mode.flags & SSTATUS_MODE_STOPPED))
          {
            CLog::Log(LOGINFO, "CSMPPlayer: End of playback reached");
            m_StopPlaying = true;
          }
        }
      }
      m_callback.OnPlayBackEnded();
    }
    else
    {
      CLog::Log(LOGDEBUG, "StartPresentation() failed, status.flags(0x%08lx), status.mode.flags(0x%08lx)",
        (long unsigned int)status.generic.flags, (long unsigned int)status.generic.mode.flags);
    }
  }

_exit:
  CLog::Log(LOGDEBUG, "m_amp->CloseMedia(m_amp)");
  if (m_amp)
    m_amp->CloseMedia(m_amp);
  
  CLog::Log(LOGDEBUG, "CSMPPlayer: Thread end");
}

bool CSMPPlayer::WaitForAmpPlaying(int timeout)
{
  bool        rtn = false;
  UMSStatus   status;

  while (!m_bStop && timeout > 0)
  {
    memset(&status, 0, sizeof(status));
    status.generic.size = sizeof(status);
    status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;
    if ((m_amp_event->WaitForEventWithTimeout(m_amp_event, 0, 100) == DFB_OK) &&
        (m_amp->UploadStatusChanges(m_amp, (SStatus*)&status, DFB_TRUE) == DFB_OK))
    {
      // eat the event
      DFBEvent event;
      m_amp_event->GetEvent(m_amp_event, &event);
      if ((status.generic.flags & SSTATUS_MODE) && 
          (status.generic.mode.flags & SSTATUS_MODE_PLAYING))
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
