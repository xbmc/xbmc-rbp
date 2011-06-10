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

using namespace std;

CWinSystemDFB::CWinSystemDFB() : CWinSystemBase()
{
  m_dfb = NULL;
  m_eWindowSystem = WINDOW_SYSTEM_DFB;
  m_eglBinding = new CWinBindingEGL();
}

CWinSystemDFB::~CWinSystemDFB()
{
  DestroyWindowSystem();

  m_dfb_surface_egl->Release(m_dfb_surface_egl);
  m_dfb_window_egl->Release(m_dfb_window_egl);
  m_dfb->Release(m_dfb);

  delete m_eglBinding;
}

bool CWinSystemDFB::InitWindowSystem()
{
  DirectFBInit(NULL, NULL);
  DirectFBCreate(&m_dfb);
  m_dfb->SetCooperativeLevel(m_dfb, DFSCL_NORMAL);

  IDirectFBScreen *screen;
  m_dfb->GetScreen(m_dfb, 0, &screen);

  IDirectFBDisplayLayer *layer;
  m_dfb->GetDisplayLayer(m_dfb, DLID_PRIMARY, &layer);
  layer->SetCooperativeLevel(layer, DLSCL_ADMINISTRATIVE);
  layer->SetBackgroundMode(layer, DLBM_DONTCARE);
  layer->EnableCursor(layer, 0);
  
  DFBDisplayLayerConfig dlcfg;
  dlcfg.flags       = (DFBDisplayLayerConfigFlags)(DLCONF_BUFFERMODE | DLCONF_PIXELFORMAT);
  dlcfg.buffermode  = DLBM_FRONTONLY;
  dlcfg.pixelformat = DSPF_ARGB;
  layer->SetConfiguration(layer, &dlcfg);

  DFBWindowDescription desc;
  desc.flags = (DFBWindowDescriptionFlags)(DWDESC_POSX |
     DWDESC_POSY | DWDESC_WIDTH | DWDESC_HEIGHT | DWDESC_CAPS | DWDESC_SURFACE_CAPS);
  desc.posx   = 0;
  desc.posy   = 0;
  desc.width  = 1280;  // screen_width;
  desc.height = 720;   // screen_height;

  desc.surface_caps = (DFBSurfaceCapabilities)(DSCAPS_VIDEOONLY | DSCAPS_PAGE_ALIGNED);

  layer->CreateWindow(layer, &desc, &m_dfb_window_egl);
  m_dfb_window_egl->GetSurface(m_dfb_window_egl, &m_dfb_surface_egl);
  m_dfb_window_egl->SetOpacity(m_dfb_window_egl, 0xff);
  m_dfb_window_egl->RequestFocus(m_dfb_window_egl);
  m_dfb_window_egl->RaiseToTop(m_dfb_window_egl);

  if (!CWinSystemBase::InitWindowSystem())
    return false;

  return true;
}

bool CWinSystemDFB::DestroyWindowSystem()
{
  // close dfb here
  return true;
}

bool CWinSystemDFB::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  if (m_eglBinding->CreateWindow((EGLNativeDisplayType)m_dfb, (NativeWindowType)m_dfb_surface_egl))
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
  CLog::Log(LOGNONE, "CWinSystemDFB::SetFullScreen");
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
  eglSwapBuffers(m_eglBinding->GetDisplay(), m_eglBinding->GetSurface());
  return true;
}

void CWinSystemDFB::SetVSyncImpl(bool enable)
{
  if (eglSwapInterval(m_eglBinding->GetDisplay(), enable ? 1 : 0) == EGL_FALSE)
  {
    CLog::Log(LOGERROR, "EGL Error: Could not set vsync");
  }
}

void CWinSystemDFB::ShowOSMouse(bool show)
{
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
