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

#ifndef _OMX_PLAYERSUBTITLE_H_
#define _OMX_PLAYERSUBTITLE_H_

#include "utils/StdString.h"

#include "OMXClock.h"
#include "DVDStreamInfo.h"
#include "OMXVideo.h"
//#ifdef STANDALONE
//#include "OMXThread.h"
//#else
#include "threads/Thread.h"
//#endif

#include <deque>
#include <sys/types.h>

#include "DVDSubtitles/DVDSubtitleParser.h"
#include "DVDCodecs/DVDFactoryCodec.h"
#include "DVDInputStreams/DVDInputStream.h"
#include "DVDSubtitles/DVDFactorySubtitle.h"
#include "DVDInputStreams/DVDFactoryInputStream.h"
#include "DVDCodecs/Overlay/DVDOverlayText.h"
#include "DVDCodecs/Overlay/DVDOverlayCodec.h"
#include "DVDDemuxers/DVDDemux.h"
#include "DVDStreamInfo.h"
#include "DVDOverlayContainer.h"
#include "DVDMessageQueue.h"

using namespace std;

//#ifdef STANDALONE
//class OMXPlayerSubtitle : public OMXThread
//#else
class OMXPlayerSubtitle
//#endif
{
protected:
  bool                      m_open;
  CDVDStreamInfo            m_hints;
  OMXClock                  *m_av_clock;
  double                    m_lastPts;
  CDVDOverlayCodec          *m_pSubtitleCodec;
  CDVDSubtitleParser*       m_pSubtitleFileParser;
  std::deque<CDVDOverlay *> m_overlays;
  double                    m_iSubtitleDelay;
  bool                      m_bRenderSubs;

  void Lock();
  void UnLock();
  void LockDecoder();
  void UnLockDecoder();

  CDVDOverlayContainer  *m_pOverlayContainer;
  CDVDMessageQueue      m_messageQueue;

private:
public:
  OMXPlayerSubtitle(OMXClock *av_clock, CDVDOverlayContainer* pOverlayContainer);
  ~OMXPlayerSubtitle();
  bool Open(CDVDStreamInfo &hints, std::string filename);
  void SendMessage(CDVDMsg* pMsg, int priority = 0);
  bool Close(bool flush);
  void ClearSubtitles();
  void Flush();
  void GetCurrentSubtitle(CStdString& strSubtitle, double pts);
  void Process(double pts);
  double GetSubtitleDelay()                         { return m_iSubtitleDelay; }
  void SetSubtitleDelay(double delay)               { m_iSubtitleDelay = delay; }
  void EnableSubtitle(bool bEnable)                 { m_bRenderSubs = bEnable; }
  bool IsSubtitleEnabled()                          { return m_bRenderSubs; }
  bool IsStalled()                                  { return m_overlays.size() == 0; }
  bool AcceptsData();
};
#endif
