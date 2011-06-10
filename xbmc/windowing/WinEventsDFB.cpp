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

#include "system.h"
#if defined(HAVE_SIGMASMP)

#include "WinEvents.h"
#include "Application.h"
#include "WindowingFactory.h"


PHANDLE_EVENT_FUNC CWinEventsBase::m_pEventFunc = NULL;

IDirectFB* dfb;
IDirectFBEventBuffer* buffer=NULL;
bool m_initialized = false;

bool CWinEventsDFB::MessagePump()
{
  if (!m_initialized)
  {
    dfb = g_Windowing.GetIDirectFB();
    if (dfb->CreateInputEventBuffer(dfb, DICAPS_ALL, DFB_TRUE, &buffer) != DFB_OK)
    {
      fprintf(stderr, "Could not create input event buffer!!!\n");
      return false;
    }
  m_initialized=true;
  }

  DFBEvent event;
  bool ret = false;
  XBMC_Event newEvent;

  while (buffer->HasEvent(buffer) == DFB_OK)
  {
    buffer->GetEvent(buffer, &event);
    if (event.clazz != DFEC_INPUT) 
      return false;

    XBMC_Event newEvent;
    switch(event.input.type)
    {
      case DIET_KEYPRESS:
        newEvent.type = XBMC_KEYDOWN;
        break;
      case DIET_KEYRELEASE:
        newEvent.type = XBMC_KEYUP;
        break;
      default:
        return false;
    }

    newEvent.key.keysym.scancode = event.input.key_id;
    newEvent.key.keysym.unicode = event.input.key_symbol;
    newEvent.key.keysym.mod =(XBMCMod) event.input.modifiers;
    if (event.input.key_symbol < 128)
      newEvent.key.keysym.sym = (XBMCKey) event.input.key_symbol;
    else
    {
      switch (event.input.key_symbol)
      {
        case DIKS_CURSOR_UP:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_UP;
          break;
        case DIKS_CURSOR_DOWN:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_DOWN;
          break;
        case DIKS_CURSOR_LEFT:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_LEFT;
          break;
        case DIKS_CURSOR_RIGHT:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_RIGHT;
          break;
        case DIKS_INSERT:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_INSERT;
          break;
        case DIKS_HOME:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_HOME;
          break;
        case DIKS_END:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_END;
          break;
        case DIKS_PAGE_UP:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_PAGEUP;
          break;
        case DIKS_PAGE_DOWN:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_PAGEDOWN;
          break;
        case DIKS_VOLUME_UP:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_VOLUME_UP;
          break;
        case DIKS_MUTE:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_VOLUME_MUTE;
          break;
        case DIKS_PREVIOUS:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_BACKSPACE;
          break;
        case DIKS_PLAY:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_MEDIA_PLAY_PAUSE;
          break;
        case DIKS_REWIND:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_r;
          break;
        case DIKS_STOP:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_MEDIA_STOP;
          break;
        case DIKS_FASTFORWARD:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_f;
          break;
        case DIKS_PAUSE:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_PAUSE;
          break;
        case DIKS_INFO:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_i;
          break;
        default:
          newEvent.key.keysym.sym=(XBMCKey) XBMCK_UNKNOWN;
          break;
      }
    }
    ret |= g_application.OnEvent(newEvent);
  }
  return ret;
}
#endif
