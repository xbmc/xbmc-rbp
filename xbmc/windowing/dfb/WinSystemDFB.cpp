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

#ifdef HAS_DIRECTFB

#include "WinSystemDFB.h"
#include "utils/log.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/Settings.h"
#include "guilib/Texture.h"
#include "windowing/dfb/WinBindingEGL.h"

#include <vector>
#include <directfb/directfb.h>

CWinSystemDFB::CWinSystemDFB() : CWinSystemBase()
{
  m_dfb = NULL;
  m_dfb_layer   = NULL;
  m_dfb_surface = NULL;
  m_vsync       = false;
  m_buffermode  = DLBM_FRONTONLY;     // no backbuffer ( tearing unless we WaitForSync)
  //m_buffermode  = DLBM_BACKVIDEO;   // backbuffer in video memory (no tearing but gui fps is slower)
  m_eWindowSystem = WINDOW_SYSTEM_DFB;
  m_eglBinding = new CWinBindingEGL();
}

CWinSystemDFB::~CWinSystemDFB()
{
  DestroyWindowSystem();
  delete m_eglBinding;
}

bool CWinSystemDFB::InitWindowSystem()
{
  DirectFBInit(NULL, NULL);
  DirectFBCreate(&m_dfb);
  m_dfb->SetCooperativeLevel(m_dfb, DFSCL_NORMAL);

  m_dfb->GetDisplayLayer(m_dfb, DLID_PRIMARY, &m_dfb_layer);
  m_dfb_layer->SetCooperativeLevel(m_dfb_layer, DLSCL_ADMINISTRATIVE);
  m_dfb_layer->SetBackgroundMode(m_dfb_layer, DLBM_DONTCARE);
  m_dfb_layer->EnableCursor(m_dfb_layer, 0);

  m_dfb_layer->GetSurface(m_dfb_layer, &m_dfb_surface);

  int screenW, screenH;
  m_dfb_surface->GetSize(m_dfb_surface, &screenW, &screenH);
  CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: width(%d), height(%d)",
    screenW, screenH);
  
  DFBDisplayLayerConfig dlcfg;
  dlcfg.flags       = (DFBDisplayLayerConfigFlags)(DLCONF_BUFFERMODE | DLCONF_PIXELFORMAT);
  dlcfg.buffermode  = (DFBDisplayLayerBufferMode)m_buffermode;     
  dlcfg.pixelformat = DSPF_ARGB;
  m_dfb_layer->SetConfiguration(m_dfb_layer, &dlcfg);

  if (!CWinSystemBase::InitWindowSystem())
    return false;

  return true;
}

bool CWinSystemDFB::DestroyWindowSystem()
{
  if (m_dfb_surface)
    m_dfb_surface->Release(m_dfb_surface);
  m_dfb_surface = NULL;
  if (m_dfb_layer)
    m_dfb_layer->Release(m_dfb_layer);
  m_dfb_layer  = NULL;
  if (m_dfb)
    m_dfb->Release(m_dfb);
  m_dfb = NULL;

  return true;
}

bool CWinSystemDFB::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  if (m_eglBinding->CreateWindow((EGLNativeDisplayType)m_dfb, (NativeWindowType)m_dfb_surface))
    return true;

  m_bWindowCreated = true;

  return false;
}

bool CWinSystemDFB::DestroyWindow()
{
  if (!m_eglBinding->DestroyWindow())
    return false;

  return true;
}

bool CWinSystemDFB::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight, true, 0);
  return true;
}

bool CWinSystemDFB::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  CLog::Log(LOGDEBUG, "CWinSystemDFB::SetFullScreen");
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  m_eglBinding->ReleaseSurface();
  CreateNewWindow("", fullScreen, res, NULL);

  CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight, true, 0);

  return true;
}

void CWinSystemDFB::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  int w = 1280;
  int h = 720;
  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, w, h, 0.0);
}

bool CWinSystemDFB::IsExtSupported(const char* extension)
{
  if(strncmp(extension, "EGL_", 4) != 0)
    return CRenderSystemGLES::IsExtSupported(extension);

  return m_eglBinding->IsExtSupported(extension);
}

bool CWinSystemDFB::PresentRenderImpl()
{
  // if we are not running a backbuffer,
  // then we have to handle the vsync ourselfs
  if (m_vsync && (m_buffermode == DLBM_FRONTONLY))
    m_dfb_layer->WaitForSync(m_dfb_layer);

  m_eglBinding->SwapBuffers();

  return true;
}

void CWinSystemDFB::SetVSyncImpl(bool enable)
{
  m_vsync = enable;
  m_eglBinding->SetVSync(enable);
}

void CWinSystemDFB::ShowOSMouse(bool show)
{
  //m_dfb_layer->EnableCursor(m_dfb_layer, show ? 1 : 0);
}

void CWinSystemDFB::NotifyAppActiveChange(bool bActivated)
{
}

bool CWinSystemDFB::Minimize()
{
  return true;
}

bool CWinSystemDFB::Restore()
{
  return false;
}

bool CWinSystemDFB::Hide()
{
  return true;
}

bool CWinSystemDFB::Show(bool raise)
{
  return true;
}

IDirectFB* CWinSystemDFB::GetIDirectFB() const
{
  return m_dfb;
}

#endif
