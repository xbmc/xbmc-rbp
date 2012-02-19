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

#include "OMXClock.h"

OMXClock::OMXClock()
{
  m_dllAvFormat.Load();

  m_video_clock = 0;
  m_audio_clock = 0;
  m_has_video   = false;
  m_has_audio   = false;
  m_play_speed  = 1;
  m_pause       = false;
  m_iCurrentPts = AV_NOPTS_VALUE;

  pthread_mutex_init(&m_lock, NULL);

  Reset();
}

OMXClock::~OMXClock()
{
  Deinitialize();

  m_dllAvFormat.Unload();
  pthread_mutex_destroy(&m_lock);
}

void OMXClock::Lock()
{
  pthread_mutex_lock(&m_lock);
}

void OMXClock::UnLock()
{
  pthread_mutex_unlock(&m_lock);
}

bool OMXClock::Reset()
{
  m_iCurrentPts = AV_NOPTS_VALUE;

  if(m_omx_clock.GetComponent() != NULL)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
    OMX_INIT_STRUCTURE(clock);

    Stop();

    clock.eState    = OMX_TIME_ClockStateWaitingForStartTime;
    if(m_has_audio)
      clock.nWaitMask |= OMX_CLOCKPORT0;
    if(m_has_video)
      clock.nWaitMask |= OMX_CLOCKPORT1;

    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::Reset error setting OMX_IndexConfigTimeClockState\n");
      return false;
    }

    Start();
  }

  return true;
}

bool OMXClock::Initialize(bool has_video, bool has_audio)
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  CStdString componentName = "";

  m_has_video = has_video;
  m_has_audio = has_audio;

  componentName = "OMX.broadcom.clock";
  if(!m_omx_clock.Initialize((const CStdString)componentName, OMX_IndexParamOtherInit))
    return false;

  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateWaitingForStartTime;

  if(m_has_audio)
    clock.nWaitMask |= OMX_CLOCKPORT0;
  if(m_has_video)
    clock.nWaitMask |= OMX_CLOCKPORT1;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);

  if(m_has_audio)
    refClock.eClock = OMX_TIME_RefClockAudio;
  else
    refClock.eClock = OMX_TIME_RefClockVideo;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeActiveRefClock, &refClock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeCurrentAudioReference\n");
    return false;
  }

  return true;
}

void OMXClock::Deinitialize()
{
  m_omx_clock.Deinitialize();
}

bool OMXClock::StatePause()
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(m_omx_clock.GetState() != OMX_StatePause)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    omx_err = m_omx_clock.SetStateForComponent(OMX_StatePause);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::StatePause m_omx_clock.SetStateForComponent\n");
      return false;
    }
  }

  return true;
}

bool OMXClock::StateExecute()
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  if(m_omx_clock.GetState() != OMX_StateExecuting)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    omx_err = m_omx_clock.SetStateForComponent(OMX_StateExecuting);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::StateExecute m_omx_clock.SetStateForComponent\n");
      return false;
    }
  }

  return true;
}

COMXCoreComponent *OMXClock::GetOMXClock()
{
  if(!m_omx_clock.GetComponent())
    return NULL;

  return &m_omx_clock;
}

bool  OMXClock::Stop()
{
  if(m_omx_clock.GetComponent() == NULL)
  {
    return false;
  }

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateStopped;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Stop error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  return true;
}

bool  OMXClock::Start()
{
  if(m_omx_clock.GetComponent() == NULL)
  {
    return false;
  }

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  clock.eState = OMX_TIME_ClockStateRunning;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Start error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  return true;
}

bool  OMXClock::Pause()
{
  if(m_omx_clock.GetComponent() == NULL)
  {
    return false;
  }

  if(m_pause)
    return true;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  scaleType.xScale = 0; // pause

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Pause error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  m_pause = true;

  return true;
}

bool  OMXClock::Resume()
{
  if(m_omx_clock.GetComponent() == NULL)
  {
    return false;
  }

  if(!m_pause)
    return true;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  scaleType.xScale = (1<<16); // normal speed

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Resume error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  m_pause = false;

  return true;
}

bool OMXClock::WaitStart(double pts)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  if(pts == DVD_NOPTS_VALUE)
    pts = 0;

  clock.nStartTime = ToOMXTime((uint64_t)pts);

  if(pts == AV_NOPTS_VALUE)
  {
    clock.eState = OMX_TIME_ClockStateRunning;
    clock.nWaitMask = 0;
  }
  else
  {
    clock.eState = OMX_TIME_ClockStateWaitingForStartTime;

    if(m_has_audio)
      clock.nWaitMask |= OMX_CLOCKPORT0;
    if(m_has_video)
      clock.nWaitMask |= OMX_CLOCKPORT1;
  }

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }
  return true;
}

bool OMXClock::Speed(int speed)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_SCALETYPE scaleType;
  OMX_INIT_STRUCTURE(scaleType);

  scaleType.xScale = (speed << 16);

  m_play_speed = speed;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeScale, &scaleType);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Speed error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  return true;
}

void OMXClock::AddTimespecs(struct timespec &time, long millisecs)
{
   time.tv_sec  += millisecs / 1000;
   time.tv_nsec += (millisecs % 1000) * 1000000;
   if (time.tv_nsec > 1000000000)
   {
      time.tv_sec  += 1;
      time.tv_nsec -= 1000000000;
   }
}

double OMXClock::GetPTS() 
{ 
  Lock();
  double pts = m_iCurrentPts;
  UnLock();
  return pts;
}

void OMXClock::SetPTS(double pts) 
{ 
  Lock();
  m_iCurrentPts = pts; 
  UnLock();
};

bool OMXClock::HDMIClockSync()
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;


  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_CONFIG_LATENCYTARGETTYPE latencyTarget;
  OMX_INIT_STRUCTURE(latencyTarget);

  latencyTarget.nPortIndex = OMX_ALL;
  latencyTarget.bEnabled = OMX_TRUE;
  latencyTarget.nFilter = 10;
  latencyTarget.nTarget = 0;
  latencyTarget.nShift = 3;
  latencyTarget.nSpeedFactor = -200;
  latencyTarget.nInterFactor = 100;
  latencyTarget.nAdjCap = 100;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigLatencyTarget, &latencyTarget);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Speed error setting OMX_IndexConfigLatencyTarget\n");
    return false;
  }

  return true;
}

