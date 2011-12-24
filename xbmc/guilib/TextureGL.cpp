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
#include "TextureGL.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/GLUtils.h"

#if defined(HAS_GL) || defined(HAS_GLES)

using namespace std;

/************************************************************************/
/*    CGLTexture                                                       */
/************************************************************************/
CGLTexture::CGLTexture(unsigned int width, unsigned int height, unsigned int format)
: CBaseTexture(width, height, format)
{
}

CGLTexture::~CGLTexture()
{
  DestroyTextureObject();
}

void CGLTexture::CreateTextureObject()
{
  glGenTextures(1, (GLuint*) &m_texture);

  if (m_accelerated && !m_egl_image && m_texture)
  {
    EGLDisplay egl_display = g_Windowing.GetEGLDisplay();
    EGLContext egl_context = g_Windowing.GetEGLContext();

    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_textureWidth, m_textureHeight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0);
    m_egl_image = eglCreateImageKHR(egl_display, egl_context, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)m_texture, NULL);
  }
}

void CGLTexture::DestroyTextureObject()
{
#ifdef HAVE_LIBBCM_HOST
  if (m_accelerated)
  {
    if (m_egl_image)
    {
      EGLDisplay egl_display = g_Windowing.GetEGLDisplay();
      eglDestroyImageKHR(egl_display, m_egl_image);
      m_egl_image = 0;
    }
  }
  if(m_omx_texture)
    delete m_omx_texture;
  m_omx_texture = NULL;
  if(m_omx_image)
    delete m_omx_image;
  m_omx_image = NULL;
#endif
  if (m_texture)
    glDeleteTextures(1, (GLuint*) &m_texture);
}

void CGLTexture::LoadToGPU()
{
#ifdef HAVE_LIBBCM_HOST
  if (m_accelerated && m_omx_image)
  {
    if (m_loadedToGPU)
    {
      // nothing to load - probably same image (no change)
      return;
    }
    if(m_egl_image == 0 && m_texture == 0)
    {
      CreateTextureObject();
      
      if(m_egl_image && m_texture)
      {
        m_omx_texture = new COMXTexture();

        EGLDisplay egl_display = g_Windowing.GetEGLDisplay();
        if(!m_omx_texture->Open() || !m_omx_texture->Decode(m_omx_image, m_egl_image, egl_display, m_textureWidth, m_textureHeight))
        {
          CLog::Log(LOGERROR, "GL: OMX Error decode Image into Texture");
        }

        delete m_omx_texture;
        m_omx_texture = NULL;
        delete m_omx_image;
        m_omx_image = NULL;
      }
    }

    if(m_egl_image == 0 || m_texture == 0)
    {
      CLog::Log(LOGERROR, "GL: OMX Image Error");
      return;
    }

    // Bind the texture object
    glBindTexture(GL_TEXTURE_2D, m_texture);

    // Set the texture's stretching properties
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    { GLenum err = glGetError(); CLog::Log(LOGERROR, "GL ERROR: %i", (int)err);}

    VerifyGLState();
    m_loadedToGPU = true;
    return;
  }
#endif
  if (!m_pixels)
  {
    // nothing to load - probably same image (no change)
    return;
  }
  if (m_texture == 0)
  {
    // Have OpenGL generate a texture object handle for us
    // this happens only one time - the first time the texture is loaded
    CreateTextureObject();
  }

  // Bind the texture object
  glBindTexture(GL_TEXTURE_2D, m_texture);

  // Set the texture's stretching properties
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  unsigned int maxSize = g_Windowing.GetMaxTextureSize();
  if (m_textureHeight > maxSize)
  {
    CLog::Log(LOGERROR, "GL: Image height %d too big to fit into single texture unit, truncating to %u", m_textureHeight, maxSize);
    m_textureHeight = maxSize;
  }
  if (m_textureWidth > maxSize)
  {
    CLog::Log(LOGERROR, "GL: Image width %d too big to fit into single texture unit, truncating to %u", m_textureWidth, maxSize);
#ifndef HAS_GLES
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_textureWidth);
    m_textureWidth = maxSize;
  }

  GLenum format = GL_BGRA;
  GLint numcomponents = GL_RGBA;

  switch (m_format)
  {
  case XB_FMT_DXT1:
    format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    break;
  case XB_FMT_DXT3:
    format = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
    break;
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    break;
  case XB_FMT_RGB8:
    format = GL_RGB;
    numcomponents = GL_RGB;
    break;
  case XB_FMT_A8R8G8B8:
  default:
    break;
  }

  if ((m_format & XB_FMT_DXT_MASK) == 0)
  {
    glTexImage2D(GL_TEXTURE_2D, 0, numcomponents, m_textureWidth, m_textureHeight, 0,
      format, GL_UNSIGNED_BYTE, m_pixels);
  }
  else
  {
    // changed from glCompressedTexImage2D to support GL < 1.3
    glCompressedTexImage2DARB(GL_TEXTURE_2D, 0, format,
      m_textureWidth, m_textureHeight, 0, GetPitch() * GetRows(), m_pixels);
  }

  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#else	// GLES version
    m_textureWidth = maxSize;
  }

  // All incoming textures are BGRA, which GLES does not necessarily support.
  // Some (most?) hardware supports BGRA textures via an extension.
  // If not, we convert to RGBA first to avoid having to swizzle in shaders.
  // Explicitly define GL_BGRA_EXT here in the case that it's not defined by
  // system headers, and trust the extension list instead.
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

  GLint internalformat;
  GLenum pixelformat;

  switch (m_format)
  {
    default:
    case XB_FMT_RGBA8:
      internalformat = pixelformat = GL_RGBA;
      break;
    case XB_FMT_RGB8:
      internalformat = pixelformat = GL_RGB;
      break;
    case XB_FMT_A8R8G8B8:
      if (g_Windowing.SupportsBGRA())
      {
        internalformat = pixelformat = GL_BGRA_EXT;
      }
      else if (g_Windowing.SupportsBGRAApple())
      {
        // Apple's implementation does not conform to spec. Instead, they require
        // differing format/internalformat, more like GL.
        internalformat = GL_RGBA;
        pixelformat = GL_BGRA_EXT;
      }
      else
      {
        SwapBlueRed(m_pixels, m_textureHeight, GetPitch());
        internalformat = pixelformat = GL_RGBA;
      }
      break;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, internalformat, m_textureWidth, m_textureHeight, 0,
    pixelformat, GL_UNSIGNED_BYTE, m_pixels);

  { GLenum err = glGetError(); CLog::Log(LOGERROR, "GL ERROR: %i", (int)err);}
#endif
  VerifyGLState();

  delete [] m_pixels;
  m_pixels = NULL;

  m_loadedToGPU = true;
}
#endif // HAS_GL
