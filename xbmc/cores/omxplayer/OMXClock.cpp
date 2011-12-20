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

  Reset();
}

OMXClock::~OMXClock()
{
  Deinitialize();

  m_dllAvFormat.Unload();
}

bool OMXClock::Reset()
{
  m_video_clock = 0;
  m_audio_clock = 0;
  m_has_video   = false;
  m_has_audio   = false;
  m_play_speed  = 1;
  m_pause       = false;
  m_iCurrentPts = AV_NOPTS_VALUE;

  if(m_omx_clock.GetComponent() != NULL)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
    OMX_INIT_STRUCTURE(clock);

    omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::Reset error getting OMX_IndexConfigTimeClockState\n");
      return false;
    }

    clock.eState = OMX_TIME_ClockStateStopped;
    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::Reset error setting OMX_IndexConfigTimeClockState\n");
      return false;
    }

#ifdef OMX_SKIP64BIT
    clock.nStartTime.nLowPart   = 0;
    clock.nStartTime.nHighPart  = 0;
#else
    clock.nStartTime = 0;
#endif
    clock.eState    = OMX_TIME_ClockStateWaitingForStartTime;
    if(m_has_audio)
      clock.nWaitMask |= OMX_CLOCKPORT0;
    if(m_has_video)
      clock.nWaitMask |= OMX_CLOCKPORT1;
#ifdef OMX_SKIP64BIT
    clock.nOffset.nLowPart   = 0;
    clock.nOffset.nHighPart  = 0;
#else
    clock.nOffset   = 0;
#endif
    omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "OMXClock::Reset error setting OMX_IndexConfigTimeClockState\n");
      return false;
    }
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

  omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Initialize error getting OMX_IndexConfigTimeClockState\n");
    return false;
  }

#ifdef OMX_SKIP64BIT
  clock.nStartTime.nLowPart   = 0;
  clock.nStartTime.nHighPart  = 0;
#else
  clock.nStartTime = 0;
#endif
  clock.eState = OMX_TIME_ClockStateWaitingForStartTime;

  clock.nWaitMask = 0;
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

  /*
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Pause error getting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  clock.eState = OMX_TIME_ClockStateStopped;

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Pause error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }
  */

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

  /*
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Resume error getting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  clock.eState = OMX_TIME_ClockStateRunning;

  uint64_t val = 0;
  if(m_audio_clock != AV_NOPTS_VALUE)
  {
    val = m_audio_clock;
  }
  else if(m_video_clock != AV_NOPTS_VALUE)
  {
    val = m_video_clock;
  }

#ifdef OMX_SKIP64BIT
  clock.nStartTime.nLowPart = val & 0x00000000FFFFFFFF;
  clock.nStartTime.nHighPart = (val & 0xFFFFFFFF00000000) >> 32;
#else
  clock.nStartTime = val; // in microseconds
#endif

  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::Resume error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }
  */

  m_pause = false;

  return true;
}

bool OMXClock::WaitStart(uint64_t pts)
{
  if(m_omx_clock.GetComponent() == NULL)
    return false;

  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clock;
  OMX_INIT_STRUCTURE(clock);

  omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::WaitStart error getting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  clock.eState = OMX_TIME_ClockStateStopped;
  omx_err = OMX_SetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::WaitStart error setting OMX_IndexConfigTimeClockState\n");
    return false;
  }

  omx_err = OMX_GetConfig(m_omx_clock.GetComponent(), OMX_IndexConfigTimeClockState, &clock);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "OMXClock::WaitStart error getting OMX_IndexConfigTimeClockState\n");
    return false;
  }

#ifdef OMX_SKIP64BIT
  clock.nStartTime.nLowPart   = pts & 0x00000000FFFFFFFF;
  clock.nStartTime.nHighPart  = (pts & 0xFFFFFFFF00000000) >> 32;
#else
  clock.nStartTime = pts;
#endif

  clock.eState = OMX_TIME_ClockStateWaitingForStartTime;

  clock.nWaitMask = 0;
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

double OMXClock::NormalizeFrameduration(double frameduration)
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

double OMXClock::ConvertTimestamp(int64_t pts, int64_t start_time, AVRational *time_base)
{
  if (pts == (int64_t)AV_NOPTS_VALUE)
    return AV_NOPTS_VALUE;

  // do calculations in floats as they can easily overflow otherwise
  // we don't care for having a completly exact timestamp anyway
  double timestamp = (double)pts * time_base->num  / time_base->den;
  double starttime = 0.0f;

  if (start_time != (int64_t)AV_NOPTS_VALUE)
    starttime = (double)start_time / AV_TIME_BASE;

  if(timestamp > starttime)
    timestamp -= starttime;
  else if( timestamp + 0.1f > starttime )
    timestamp = 0;

  return timestamp * DVD_TIME_BASE;
}

void OMXClock::UpdateCurrentPTS(AVFormatContext *ctx)
{
  if(!ctx)
    return;

  m_iCurrentPts = AV_NOPTS_VALUE;
  for(unsigned int i = 0; i < ctx->nb_streams; i++)
  {
    AVStream *stream = ctx->streams[i];
    if(stream && stream->cur_dts != (int64_t)AV_NOPTS_VALUE)
    {
      double ts = ConvertTimestamp(stream->cur_dts, ctx->start_time, &stream->time_base);
      if(m_iCurrentPts == AV_NOPTS_VALUE || m_iCurrentPts > ts )
        m_iCurrentPts = ts;
    }
  }
}

