#ifndef WINDOW_SYSTEM_EGLGLES_H
#define WINDOW_SYSTEM_EGLGLES_H

#pragma once

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

#include "rendering/gles/RenderSystemGLES.h"
#include "utils/GlobalsHandling.h"
#include "windowing/WinSystem.h"

#include <EGL/eglplatform.h>
#include <semaphore.h>

// TODO: remove after we have it in configure
#ifndef HAVE_LIBBCM_HOST
#define HAVE_LIBBCM_HOST
#endif

#include "linux/DllBCM.h"

typedef void *EGLDisplay;
typedef void *EGLContext;
class CWinBindingEGL;

class CWinSystemGLES : public CWinSystemBase, public CRenderSystemGLES
{
public:
  CWinSystemGLES();
  virtual ~CWinSystemGLES();

  virtual bool  InitWindowSystem();
  virtual bool  DestroyWindowSystem();
  virtual bool  CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction);
  virtual bool  DestroyWindow();
  virtual bool  ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop);
  virtual bool  SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays);
  virtual void  UpdateResolutions();
  virtual bool  IsExtSupported(const char* extension);

  virtual void  ShowOSMouse(bool show);

  virtual void  NotifyAppActiveChange(bool bActivated);

  virtual bool  Minimize();
  virtual bool  Restore() ;
  virtual bool  Hide();
  virtual bool  Show(bool raise = true);
  virtual bool  InformVideoInfo(int width, int height, int frame_rate, bool mode3d_sbs = false);
  virtual RESOLUTION GetResolution();

  EGLNativeWindowType   GetEGLGetNativeWindow() const;
  EGLNativeDisplayType  GetEGLNativeDispla() const;
  EGLContext            GetEGLContext() const;
  EGLDisplay            GetEGLSurface() const;
  EGLDisplay            GetEGLDisplay() const;
protected:
  virtual bool  PresentRenderImpl(const CDirtyRegionList &dirty);
  virtual void  SetVSyncImpl(bool enable);
  void AddResolution(const RESOLUTION_INFO &res);
  void                  *m_display;
  EGL_DISPMANX_WINDOW_T *m_window;
  CWinBindingEGL        *m_eglBinding;
  int                   m_fb_width;
  int                   m_fb_height;
  int                   m_fb_bpp;
  DllBcmHostDisplay     m_DllBcmHostDisplay;
  DllBcmHost            m_DllBcmHost;
  DISPMANX_ELEMENT_HANDLE_T m_dispman_element;
  DISPMANX_ELEMENT_HANDLE_T m_dispman_element2;
  DISPMANX_DISPLAY_HANDLE_T m_dispman_display;
  sem_t                 m_tv_synced;
  int                   m_videoWidth;
  int                   m_videoHeight;
  int                   m_videoFrameRate;
  bool                  m_videoMode3dSbs;
  bool                  m_found_preferred;
  void GetSupportedModes(HDMI_RES_GROUP_T group);
  void MapGpuToXbmcMode(TV_SUPPORTED_MODE_T *supported_modes, int num_modes, HDMI_RES_GROUP_T group, RESOLUTION xbmc_res, int want_mode);
  void TvServiceCallback(uint32_t reason, uint32_t param1, uint32_t param2);
  static void CallbackTvServiceCallback(void *userdata, uint32_t reason, uint32_t param1, uint32_t param2);
};

XBMC_GLOBAL_REF(CWinSystemGLES,g_Windowing);
#define g_Windowing XBMC_GLOBAL_USE(CWinSystemGLES)

#endif // WINDOW_SYSTEM_EGLGLES_H
