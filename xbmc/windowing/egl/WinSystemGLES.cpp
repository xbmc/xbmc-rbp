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
#include "xbmc/cores/VideoRenderers/RenderManager.h"
#include "xbmc/linux/DllBCM.h"

#define IS_WIDESCREEN(m) (m==3||m==7||m==9||m==11||m==13||m==15||m==18||m==22||m==24||m==26||m==28||m==30||m==36||m==38||m==43||m==45||m==49||m==51||m==53||m==55||m==57||m==59)

#define MAKEFLAGS(group, mode, interlace, mode3d) (((mode)<<24)|((group)<<16)|((interlace)!=0?D3DPRESENTFLAG_INTERLACED:D3DPRESENTFLAG_PROGRESSIVE)| \
   (((group)==HDMI_RES_GROUP_CEA && IS_WIDESCREEN(mode))?D3DPRESENTFLAG_WIDESCREEN:0)|((mode3d)!=0?D3DPRESENTFLAG_MODE3DSBS:0))
#define GETFLAGS_INTERLACE(f) (((f)&D3DPRESENTFLAG_INTERLACED)!=0)
#define GETFLAGS_WIDESCREEN(f) (((f)&D3DPRESENTFLAG_WIDESCREEN)!=0)
#define GETFLAGS_GROUP(f) ((HDMI_RES_GROUP_T)(((f)>>16)&0xff))
#define GETFLAGS_MODE(f) (((f)>>24)&0xff)
#define GETFLAGS_MODE3D(f) (((f)&D3DPRESENTFLAG_MODE3DSBS)!=0)

////////////////////////////////////////////////////////////////////////////////////////////
CWinSystemGLES::CWinSystemGLES() : CWinSystemBase()
{
  m_window = NULL;
  m_eglBinding = new CWinBindingEGL();
  m_eWindowSystem = WINDOW_SYSTEM_EGL;

  m_dispman_element = DISPMANX_NO_HANDLE;
  m_dispman_element2 = DISPMANX_NO_HANDLE;
  m_dispman_display = DISPMANX_NO_HANDLE;
  m_videoWidth = 0;
  m_videoHeight = 0;
  m_videoFrameRate = 0;
  m_videoMode3dSbs = 0;
  m_found_preferred = false;
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
  if (!m_DllBcmHost.Load())
    return false;

  m_display = EGL_DEFAULT_DISPLAY;
  m_window  = (EGL_DISPMANX_WINDOW_T*)calloc(1, sizeof(EGL_DISPMANX_WINDOW_T));

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

  DestroyWindow();

  if (!m_eglBinding->DestroyWindow())
    return false;

  if(m_DllBcmHostDisplay.IsLoaded())
    m_DllBcmHostDisplay.Unload();

  if(m_DllBcmHost.IsLoaded())
    m_DllBcmHost.Unload();

  return true;
}

bool CWinSystemGLES::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_dispman_display = m_DllBcmHostDisplay.vc_dispmanx_display_open(0);
  OVERSCAN &overscan = res.Overscan;

  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  //m_bFullScreen = fullScreen;
  m_fb_width  = res.iWidth;
  m_fb_height = res.iHeight;

  m_fb_bpp    = 8;

  VC_RECT_T dst_rect;
  VC_RECT_T src_rect;
  dst_rect.x = overscan.left;
  dst_rect.y = overscan.top;
  dst_rect.width = overscan.right-overscan.left;
  dst_rect.height = overscan.bottom-overscan.top;

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
  CLog::Log(LOGDEBUG, "CWinSystemGLES::Show %dx%d->%dx%d->%dx%d\n", m_fb_width, m_fb_height, dst_rect.width, dst_rect.height, m_videoWidth, m_videoHeight);

  // width < height => half SBS
  if (src_rect.width < src_rect.height)
  {
    // right side
    dst_rect.x = res.iWidth;
    dst_rect.width >>= overscan.right-dst_rect.x;
    m_dispman_element2 = m_DllBcmHostDisplay.vc_dispmanx_element_add(dispman_update,
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
      assert(m_dispman_element2 != DISPMANX_NO_HANDLE);
      assert(m_dispman_element2 != (unsigned)DISPMANX_INVALID);
    // left side - fall through
    dst_rect.x = overscan.left;
    dst_rect.width = res.iWidth - dst_rect.x;
  }
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

  CLog::Log(LOGDEBUG, "CWinSystemGLES::CreateNewWindow(%dx%d) (%dx%d)\n", m_window->width, m_window->height, res.iWidth, res.iHeight);
  if (!m_eglBinding->CreateWindow((EGLNativeDisplayType)m_display, (EGLNativeWindowType)m_window))
    return false;

  m_bWindowCreated = true;

  return true;
}

bool CWinSystemGLES::DestroyWindow()
{
  CLog::Log(LOGDEBUG, "CWinSystemGLES::DestroyWindow()\n");

  DISPMANX_UPDATE_HANDLE_T dispman_update = m_DllBcmHostDisplay.vc_dispmanx_update_start(0);

  if (m_dispman_element != DISPMANX_NO_HANDLE)
  {
    m_DllBcmHostDisplay.vc_dispmanx_element_remove(dispman_update, m_dispman_element);
    m_dispman_element = DISPMANX_NO_HANDLE;
  }
  if (m_dispman_element2 != DISPMANX_NO_HANDLE)
  {
    m_DllBcmHostDisplay.vc_dispmanx_element_remove(dispman_update, m_dispman_element2);
    m_dispman_element2 = DISPMANX_NO_HANDLE;
  }
  m_DllBcmHostDisplay.vc_dispmanx_update_submit_sync(dispman_update);

  if (m_dispman_display != DISPMANX_NO_HANDLE)
  {
    m_DllBcmHostDisplay.vc_dispmanx_display_close(m_dispman_display);
    m_dispman_display = DISPMANX_NO_HANDLE;
  }

  m_eglBinding->ReleaseSurface();

  m_bWindowCreated = false;

  return true;
}

bool CWinSystemGLES::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight, true, 0);

  return true;
}

void CWinSystemGLES::TvServiceCallback(uint32_t reason, uint32_t param1, uint32_t param2)
{
  CLog::Log(LOGDEBUG, "tvservice_callback(%d,%d,%d)\n", reason, param1, param2);
  switch(reason)
  {
  case VC_HDMI_UNPLUGGED:
    break;
  case VC_HDMI_STANDBY:
    break;
  case VC_SDTV_NTSC:
  case VC_SDTV_PAL:
  case VC_HDMI_HDMI:
  case VC_HDMI_DVI:
    //Signal we are ready now
    sem_post(&m_tv_synced);
    break;
  default:
     break;
  }
}

void CWinSystemGLES::CallbackTvServiceCallback(void *userdata, uint32_t reason, uint32_t param1, uint32_t param2)
{
   CWinSystemGLES *omx = static_cast<CWinSystemGLES*>(userdata);
   omx->TvServiceCallback(reason, param1, param2);
}


bool CWinSystemGLES::InformVideoInfo(int width, int height, int frame_rate, bool mode3d_sbs)
{
  CLog::Log(LOGDEBUG, "CWinSystemGLES::InformVideoInfo(%dx%d@%d) 3d=%d\n", width, height, frame_rate, mode3d_sbs);
  if (width > 1280)
    m_videoWidth = 1920, m_videoHeight = 1080;
  else if (width > 720)
    m_videoWidth = 1280, m_videoHeight = 720;
  else if (width > 640)
    m_videoWidth = 720, m_videoHeight = 480;
  else
    m_videoWidth = 640, m_videoHeight = 480;
  m_videoFrameRate = frame_rate;
  m_videoMode3dSbs = mode3d_sbs;
}

RESOLUTION CWinSystemGLES::GetResolution()
{
  return g_renderManager.GetResolution();
}

bool CWinSystemGLES::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  CLog::Log(LOGDEBUG, "CWinSystemDFB::SetFullScreen");
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;
  CLog::Log(LOGDEBUG, "CWinSystemGLES::SetFullScreen(%d %dx%d@%d) %x,%d omx\n", m_bFullScreen, m_videoWidth, m_videoHeight, m_videoFrameRate, res.dwFlags, m_videoMode3dSbs);
  CLog::Log(LOGDEBUG, "CWinSystemGLES::SetFullScreen(%d %dx%d@%f) xbmc\n", m_bFullScreen, res.iWidth, res.iHeight, res.fRefreshRate);

  DestroyWindow();

  sem_init (&m_tv_synced, 0, 0);
  m_DllBcmHost.vc_tv_register_callback(CallbackTvServiceCallback, this);

  CLog::Log(LOGDEBUG, "vc_tv_hdmi_power_on_explicit(%d,%d,%d) xbmc\n", GETFLAGS_MODE3D(res.dwFlags) ? HDMI_MODE_3D:HDMI_MODE_HDMI, GETFLAGS_GROUP(res.dwFlags), GETFLAGS_MODE(res.dwFlags));
  int success = m_DllBcmHost.vc_tv_hdmi_power_on_explicit(HDMI_MODE_HDMI, GETFLAGS_GROUP(res.dwFlags), GETFLAGS_MODE(res.dwFlags));
  if (success == 0) {
    sem_wait(&m_tv_synced);
  } else {
    CLog::Log(LOGERROR, " CWinSystemGLES::SetFullScreen failed to set HDMI mode (%d,%d,%d)=%d\n", GETFLAGS_MODE3D(res.dwFlags) ? HDMI_MODE_3D:HDMI_MODE_HDMI, GETFLAGS_GROUP(res.dwFlags), GETFLAGS_MODE(res.dwFlags), success);
  }

  m_DllBcmHost.vc_tv_unregister_callback(CallbackTvServiceCallback);

  CreateNewWindow("", fullScreen, res, NULL);
  CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight, true, 0);
  return true;
}

static int find_mode(TV_SUPPORTED_MODE_T *supported_modes, int num_modes, int want_mode)
{
  int i;
  int prefer_index = -1;
  for (i=0; i<num_modes; i++)
    if (supported_modes[i].code == want_mode)
      prefer_index = i;
  return prefer_index;
}

void CWinSystemGLES::MapGpuToXbmcMode(TV_SUPPORTED_MODE_T *supported_modes, int num_modes, HDMI_RES_GROUP_T group, RESOLUTION xbmc_res, int want_mode)
{
  int index = find_mode(supported_modes, num_modes, want_mode);
  if (index >= 0 && index < num_modes)
  {
    TV_SUPPORTED_MODE_T *tv = supported_modes + index;
    UpdateDesktopResolution(g_settings.m_ResInfo[xbmc_res], 0 /* iScreen */, tv->width, tv->height,
        (float)tv->frame_rate, MAKEFLAGS(group, want_mode, tv->scan_mode, 0) /* dwFlags */);
  }
}

void CWinSystemGLES::GetSupportedModes(HDMI_RES_GROUP_T group)
{
  //Supported HDMI CEA/DMT resolutions, first one will be preferred resolution
  #define TV_MAX_SUPPORTED_MODES 60
  TV_SUPPORTED_MODE_T supported_modes[TV_MAX_SUPPORTED_MODES];
  int32_t num_modes;
  HDMI_RES_GROUP_T prefer_group;
  uint32_t prefer_mode;
  int i;

  num_modes = m_DllBcmHost.vc_tv_hdmi_get_supported_modes(group,
                                           supported_modes,
                                           TV_MAX_SUPPORTED_MODES,
                                           &prefer_group,
                                           &prefer_mode);
  CLog::Log(LOGDEBUG, "vc_tv_hdmi_get_supported_modes(%d) = %d, prefer_group=%x, prefer_mode=%x\n", group, num_modes, prefer_group, prefer_mode);
  // replace the native xbcm displays with our ones
  if (!m_found_preferred && num_modes > 0)
  {
#if 0
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_HDTV_1080i, HDMI_CEA_1080i60);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_HDTV_720p, HDMI_CEA_720p60);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_HDTV_480p_4x3, HDMI_CEA_480p60);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_HDTV_480p_16x9, HDMI_CEA_480p60H);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_NTSC_4x3, HDMI_CEA_480p60);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_NTSC_16x9, HDMI_CEA_480p60H);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_PAL_4x3, HDMI_CEA_576p50);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_PAL_16x9, HDMI_CEA_576p50H);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_PAL60_4x3, HDMI_CEA_576p50);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_PAL60_16x9, HDMI_CEA_576p50H);
#endif
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_AUTORES, prefer_mode);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_WINDOW, prefer_mode);
    MapGpuToXbmcMode(supported_modes, num_modes, group, RES_DESKTOP, prefer_mode);
    m_found_preferred = true;
  }
  if (num_modes > 0 && prefer_group != HDMI_RES_GROUP_INVALID)
  {
    TV_SUPPORTED_MODE_T *tv = supported_modes;
    for (i=0; i<num_modes; i++, tv++)
    {
      // treat 3D modes as half-width SBS
      int width = group==HDMI_RES_GROUP_CEA_3D ? tv->width>>1:tv->width;
      RESOLUTION_INFO res;
      CLog::Log(LOGDEBUG, "%d: %dx%d@%d %s%s:%x\n", i, width, tv->height, tv->frame_rate, tv->native?"N":"", tv->scan_mode?"I":"", tv->code);
      UpdateDesktopResolution(res, 0 /* iScreen */, width, tv->height, (float)tv->frame_rate, MAKEFLAGS(group, supported_modes[i].code, tv->scan_mode, group==HDMI_RES_GROUP_CEA_3D) /* dwFlags */);
      AddResolution(res);
    }
  }
}

void CWinSystemGLES::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();
  int i;
  // here is where we probe the avaliable display resolutions from display.

  GetSupportedModes(HDMI_RES_GROUP_CEA);
  GetSupportedModes(HDMI_RES_GROUP_DMT);
  GetSupportedModes(HDMI_RES_GROUP_CEA_3D);

  // no preferred - probably SDTV
  if (!m_found_preferred)
  {
    TV_GET_STATE_RESP_T m_tv_state;
    memset(&m_tv_state, 0, sizeof(TV_GET_STATE_RESP_T));
    m_DllBcmHost.vc_tv_get_state(&m_tv_state);
    UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0 /* iScreen */, m_tv_state.width, m_tv_state.height, (float)m_tv_state.frame_rate, 0 /* dwFlags */);
  }

  for (i=0; i<g_settings.m_ResInfo.size(); i++) {
    CLog::Log(LOGDEBUG, "%d: %dx%d%s@%f %x (%dx%d) [%s]\n", i, g_settings.m_ResInfo[i].iWidth, g_settings.m_ResInfo[i].iHeight,
      GETFLAGS_INTERLACE(g_settings.m_ResInfo[i].dwFlags) ? "i":"", g_settings.m_ResInfo[i].fRefreshRate, g_settings.m_ResInfo[i].dwFlags, 
      g_settings.m_ResInfo[i].Overscan.right, g_settings.m_ResInfo[i].Overscan.bottom, g_settings.m_ResInfo[i].strMode.c_str());
  }
}

void CWinSystemGLES::AddResolution(const RESOLUTION_INFO &res)
{
  for (unsigned int i = (int)RES_DESKTOP; i < g_settings.m_ResInfo.size(); i++)
    if (g_settings.m_ResInfo[i].iScreen      == res.iScreen &&
        g_settings.m_ResInfo[i].iWidth       == res.iWidth &&
        g_settings.m_ResInfo[i].iHeight      == res.iHeight &&
        g_settings.m_ResInfo[i].fRefreshRate == res.fRefreshRate /*&&
        (g_settings.m_ResInfo[i].dwFlags & 0xffff) == (res.dwFlags & 0xffff)*/)
    {
      // replace interlaced resolution with non-interlaced
      if (GETFLAGS_INTERLACE(g_settings.m_ResInfo[i].dwFlags) && !GETFLAGS_INTERLACE(res.dwFlags))
        UpdateDesktopResolution(g_settings.m_ResInfo[i], res.iScreen, res.iWidth, res.iHeight, res.fRefreshRate, res.dwFlags);
      return; // already have this resolution
    }

  g_settings.m_ResInfo.push_back(res);
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
  return true;
}

bool CWinSystemGLES::Show(bool raise)
{
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

