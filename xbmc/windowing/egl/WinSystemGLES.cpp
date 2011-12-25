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

#ifdef HAS_EGLGLES

#ifndef __VIDEOCORE4__
#define __VIDEOCORE4__
#endif

#define __VCCOREVER__ 0x04000000

#include "WinSystemGLES.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/Settings.h"
#include "guilib/Texture.h"
#include "utils/log.h"
#include "WinBindingEGL.h"

#include <vector>

////////////////////////////////////////////////////////////////////////////////////////////
CWinSystemGLES::CWinSystemGLES() : CWinSystemBase()
{
  m_window = NULL;
  m_eglBinding = new CWinBindingEGL();
  // m_vendorBindings = new CDispmanx();
  m_eWindowSystem = WINDOW_SYSTEM_EGL;

  m_dispman_element = DISPMANX_NO_HANDLE;
  m_dispman_display = DISPMANX_NO_HANDLE;
}

CWinSystemGLES::~CWinSystemGLES()
{
  DestroyWindowSystem();
  delete m_eglBinding;
}

bool CWinSystemGLES::InitWindowSystem()
{
  if(!m_DllBcmHostDisplay.Load())
    return false;

  m_display = EGL_DEFAULT_DISPLAY;
  m_window  = (EGL_DISPMANX_WINDOW_T*)calloc(1, sizeof(EGL_DISPMANX_WINDOW_T));

  Show(true);

  if (!CWinSystemBase::InitWindowSystem())
    return false;

  CLog::Log(LOGDEBUG, "Video mode: %dx%d with %d bits per pixel.",
    m_fb_width, m_fb_height, m_fb_bpp);

  return true;
}

bool CWinSystemGLES::DestroyWindowSystem()
{
  free(m_window);
  m_window = NULL;

  if(m_dispman_element != DISPMANX_NO_HANDLE)
  {
     Hide();
  }

  if(m_DllBcmHostDisplay.IsLoaded())
    m_DllBcmHostDisplay.Unload();


  return true;
}

bool CWinSystemGLES::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  if (!m_eglBinding->CreateWindow((EGLNativeDisplayType)m_display, (EGLNativeWindowType)m_window))
    return false;

  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, m_nWidth, m_nHeight, 0.0);

  m_bWindowCreated = true;

  return true;
}

bool CWinSystemGLES::DestroyWindow()
{
  if (!m_eglBinding->DestroyWindow())
    return false;

  m_bWindowCreated = false;

  return true;
}

bool CWinSystemGLES::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight, true, 0);

  return true;
}

bool CWinSystemGLES::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
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

void CWinSystemGLES::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  // here is where we would probe the avaliable display resolutions from display.
  // hard code now to what we get from vc_dispmanx_display_get_info
  int w = m_fb_width;
  int h = m_fb_height;
  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, w, h, 0.0);
}

bool CWinSystemGLES::IsExtSupported(const char* extension)
{
  if(strncmp(extension, "EGL_", 4) != 0)
    return CRenderSystemGLES::IsExtSupported(extension);

  return m_eglBinding->IsExtSupported(extension);
}

bool CWinSystemGLES::PresentRenderImpl(const CDirtyRegionList &dirty)
{
  m_eglBinding->SwapBuffers();

  return true;
}

void CWinSystemGLES::SetVSyncImpl(bool enable)
{
  m_iVSyncMode = enable ? 10 : 0;
  if (m_eglBinding->SetVSync(enable) == FALSE)
    CLog::Log(LOGERROR, "CWinSystemDFB::SetVSyncImpl: Could not set egl vsync");
}

void CWinSystemGLES::ShowOSMouse(bool show)
{
}

void CWinSystemGLES::NotifyAppActiveChange(bool bActivated)
{
}

bool CWinSystemGLES::Minimize()
{
  Hide();
  return true;
}

bool CWinSystemGLES::Restore()
{
  Show(true);
  return false;
}

bool CWinSystemGLES::Hide()
{
  DISPMANX_UPDATE_HANDLE_T dispman_update = m_DllBcmHostDisplay.vc_dispmanx_update_start(0);

  m_DllBcmHostDisplay.vc_dispmanx_element_remove(dispman_update, m_dispman_element);
  m_DllBcmHostDisplay.vc_dispmanx_update_submit_sync(dispman_update);
  m_dispman_element = DISPMANX_NO_HANDLE;

  if (m_dispman_display != DISPMANX_NO_HANDLE)
  {
    m_DllBcmHostDisplay.vc_dispmanx_display_close(m_dispman_display);
    m_dispman_display = DISPMANX_NO_HANDLE;
  }

  return true;
}

bool CWinSystemGLES::Show(bool raise)
{
  DISPMANX_MODEINFO_T mode_info;
  memset(&mode_info, 0x0, sizeof(DISPMANX_MODEINFO_T));
  m_dispman_display = m_DllBcmHostDisplay.vc_dispmanx_display_open(0);
  m_DllBcmHostDisplay.vc_dispmanx_display_get_info(m_dispman_display, &mode_info);

  if (!m_fb_width)
    m_fb_width  = mode_info.width;
  if (!m_fb_height)
    m_fb_height = mode_info.height;
  m_fb_bpp    = 8;

  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.width = mode_info.width;
  dst_rect.height = mode_info.height;

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.width = m_fb_width << 16;
  src_rect.height = m_fb_height << 16;

  VC_DISPMANX_ALPHA_T alpha;
  memset(&alpha, 0x0, sizeof(VC_DISPMANX_ALPHA_T));
  alpha.flags = DISPMANX_FLAGS_ALPHA_FROM_SOURCE;

  DISPMANX_CLAMP_T clamp;
  memset(&clamp, 0x0, sizeof(DISPMANX_CLAMP_T));

  DISPMANX_TRANSFORM_T transform = DISPMANX_NO_ROTATE;
  DISPMANX_UPDATE_HANDLE_T dispman_update = m_DllBcmHostDisplay.vc_dispmanx_update_start(0);

  m_dispman_element = m_DllBcmHostDisplay.vc_dispmanx_element_add(dispman_update,
    m_dispman_display,
    1,                              // layer
    &dst_rect,
    (DISPMANX_RESOURCE_HANDLE_T)0,  // src
    &src_rect,
    DISPMANX_PROTECTION_NONE,
    //(VC_DISPMANX_ALPHA_T*)0,        // alpha
    &alpha,
    //(DISPMANX_CLAMP_T*)0,           // clamp
    &clamp,
    //(DISPMANX_TRANSFORM_T)0);       // transform
    transform);       // transform

  assert(m_dispman_element != DISPMANX_NO_HANDLE);
  assert(m_dispman_element != (unsigned)DISPMANX_INVALID);
  m_window->element = m_dispman_element;

  m_window->width   = m_fb_width;
  m_window->height  = m_fb_height;
  m_DllBcmHostDisplay.vc_dispmanx_display_set_background(dispman_update, m_dispman_display, 0x00, 0x00, 0x00);
  m_DllBcmHostDisplay.vc_dispmanx_update_submit_sync(dispman_update);

  return true;
}

EGLNativeWindowType CWinSystemGLES::GetEGLGetNativeWindow() const
{
  return m_eglBinding->GetNativeWindow();
}

EGLNativeDisplayType CWinSystemGLES::GetEGLNativeDispla() const
{
  return m_eglBinding->GetNativeDisplay();
}

EGLContext CWinSystemGLES::GetEGLContext() const
{
  return m_eglBinding->GetContext();
}

EGLContext CWinSystemGLES::GetEGLSurface() const
{
  return m_eglBinding->GetSurface();
}

EGLDisplay CWinSystemGLES::GetEGLDisplay() const
{
  return m_eglBinding->GetDisplay();
}
#endif
