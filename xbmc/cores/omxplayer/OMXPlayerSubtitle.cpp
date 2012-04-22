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

#include "OMXPlayerSubtitle.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#ifndef STANDALONE
#include "FileItem.h"
#endif

#include "linux/XMemUtils.h"
#ifndef STANDALONE
#include "utils/BitstreamStats.h"
#endif

#include "DVDCodecs/DVDCodecs.h"
#include "DVDDemuxers/DVDDemuxUtils.h"
#include "DVDCodecs/Overlay/DVDOverlayCodecText.h"

OMXPlayerSubtitle::OMXPlayerSubtitle(OMXClock *av_clock, 
                                     CDVDOverlayContainer* pOverlayContainer)
: m_messageQueue("subtitle")
{
  m_av_clock              = av_clock;
  m_pOverlayContainer     = pOverlayContainer;
  m_open                  = false;
  m_pSubtitleCodec        = NULL;
  m_pSubtitleFileParser   = NULL;
  m_lastPts               = DVD_NOPTS_VALUE;
  m_iSubtitleDelay        = 0;
  m_bRenderSubs           = false;
}

OMXPlayerSubtitle::~OMXPlayerSubtitle()
{
  Close(false);
}

bool OMXPlayerSubtitle::Open(CDVDStreamInfo &hints, std::string filename)
{
  Close(false);

  // dvd's use special subtitle decoder
  if(hints.codec == CODEC_ID_DVD_SUBTITLE && filename == "dvd")
    return false;

  /*
  if(hints.codec != CODEC_ID_TEXT &&
     hints.codec != CODEC_ID_SSA )
    return false;
  */

  m_hints       = hints;

  CDVDCodecOptions options;

  // okey check if this is a filesubtitle
  if(filename.size() && filename != "dvd" )
  {
    m_pSubtitleFileParser = CDVDFactorySubtitle::CreateParser(filename);
    if (!m_pSubtitleFileParser)
    {
      CLog::Log(LOGERROR, "%s - Unable to create subtitle parser", __FUNCTION__);
      Close(false);
      return false;
    }

    if (!m_pSubtitleFileParser->Open(hints))
    {
      CLog::Log(LOGERROR, "%s - Unable to init subtitle parser", __FUNCTION__);
      Close(false);
      return false;
    }
    return true;
  }

  m_pSubtitleCodec = CDVDFactoryCodec::CreateOverlayCodec(hints);
  if(!m_pSubtitleCodec->Open( m_hints, options ))
  {
    CLog::Log(LOGERROR, "%s - Unable to init overlay codec", __FUNCTION__);
    Close(false);
    return false;
  }

  m_open        = true;

  CLog::Log(LOGDEBUG, "%s - Subtitle open", __FUNCTION__);
  return true;
}

void OMXPlayerSubtitle::ClearSubtitles()
{
  while (!m_overlays.empty())
  {
    CDVDOverlay *overlay = m_overlays.front();
    m_overlays.pop_front();
    overlay->Release();
  }
  m_overlays.clear();
}

bool OMXPlayerSubtitle::Close(bool flush)
{
  m_open            = false;

  if(m_pSubtitleCodec)
    delete m_pSubtitleCodec;
  m_pSubtitleCodec = NULL;
  if(m_pSubtitleFileParser)
    delete m_pSubtitleFileParser;
  m_pSubtitleFileParser = NULL;

  ClearSubtitles();

  m_pOverlayContainer->Clear();

  return true;
}

void OMXPlayerSubtitle::SendMessage(CDVDMsg* pMsg, int priority)
{
  if (pMsg->IsType(CDVDMsg::DEMUXER_PACKET) && m_pSubtitleCodec)
  {
    
    CDVDMsgDemuxerPacket* pMsgDemuxerPacket = (CDVDMsgDemuxerPacket*)pMsg;
    DemuxPacket* pPacket = pMsgDemuxerPacket->GetPacket();
    double duration = pPacket->duration;
    double pts = pPacket->dts != DVD_NOPTS_VALUE ? pPacket->dts : pPacket->pts;
    int result = m_pSubtitleCodec->Decode(pPacket->pData, pPacket->iSize, pts, duration);

    if(result == OC_OVERLAY)
    {

      CDVDOverlay* overlay;
      while((overlay = m_pSubtitleCodec->GetOverlay()) != NULL)
      {
        overlay->iGroupId = pPacket->iGroupId;

        if(overlay->iPTSStopTime > overlay->iPTSStartTime)
          duration = overlay->iPTSStopTime - overlay->iPTSStartTime;
        else if(pPacket->duration != DVD_NOPTS_VALUE)
          duration = pPacket->duration;
        else
          duration = 0.0;
  
        if     (pPacket->pts != DVD_NOPTS_VALUE)
          pts = pPacket->pts;
        else if(pPacket->dts != DVD_NOPTS_VALUE)
          pts = pPacket->dts;
        else
          pts = overlay->iPTSStartTime;
  
        overlay->iPTSStartTime = pts;
        if(duration)
          overlay->iPTSStopTime = pts + duration;
        else
        {
          overlay->iPTSStopTime = 0;
          overlay->replace = true;
        }
  
        m_overlays.push_back(overlay);
      }
    }
  }
  else if( pMsg->IsType(CDVDMsg::GENERAL_FLUSH)
        || pMsg->IsType(CDVDMsg::GENERAL_RESET) )
  {
    if (m_pSubtitleFileParser)
      m_pSubtitleFileParser->Reset();

    if (m_pSubtitleCodec)
      m_pSubtitleCodec->Flush();

    ClearSubtitles();

    m_lastPts = DVD_NOPTS_VALUE;
  }
  pMsg->Release();
}

void OMXPlayerSubtitle::Flush()
{
  SendMessage(new CDVDMsg(CDVDMsg::GENERAL_FLUSH));
}

void OMXPlayerSubtitle::Process(double pts)
{
  if (m_pSubtitleFileParser)
  {
    if(pts == DVD_NOPTS_VALUE)
      return;

    /*
    if (pts < m_lastPts)
    {
      ClearSubtitles();
      //m_pOverlayContainer->Clear();
    }
    */

    if(m_overlays.size() >= 5)
      return;

    /*
    if(m_pOverlayContainer->GetSize() >= 5)
      return;
    */

    CDVDOverlay* pOverlay = m_pSubtitleFileParser->Parse(pts);
    if (pOverlay)
      m_overlays.push_back(pOverlay);
      //m_pOverlayContainer->Add(pOverlay);

    m_lastPts = pts;
  }
}

bool OMXPlayerSubtitle::AcceptsData()
{
  // FIXME : This may still be causing problems + magic number :(
  return m_overlays.size() < 5;
}

void OMXPlayerSubtitle::GetCurrentSubtitle(CStdString& strSubtitle, double pts)
{
  strSubtitle = "";

  Process(pts);

  if(!m_overlays.empty())
  {
    CDVDOverlay* pOverlay = m_overlays.front();

    double iPTSStartTime = pOverlay->iPTSStartTime;
    double iPTSStopTime = (pOverlay->iPTSStartTime > 0) ? iPTSStartTime + (pOverlay->iPTSStopTime - pOverlay->iPTSStartTime) : 0LL;

    if(pOverlay->IsOverlayType(DVDOVERLAY_TYPE_TEXT)
      && (iPTSStartTime <= pts)
      && (iPTSStopTime >= pts || iPTSStopTime == 0LL))
    {
      CDVDOverlayText::CElement* e = ((CDVDOverlayText*)pOverlay)->m_pHead;
      while (e)
      {
        if (e->IsElementType(CDVDOverlayText::ELEMENT_TYPE_TEXT))
        {
          CDVDOverlayText::CElementText* t = (CDVDOverlayText::CElementText*)e;
          strSubtitle += t->m_text;
          strSubtitle += "\n";
        }
        e = e->pNext;
      }
    }
    else if(iPTSStopTime < pts)
    {
      m_overlays.pop_front();
      pOverlay->Release();
    }
  }

  strSubtitle.TrimRight('\n');
}
