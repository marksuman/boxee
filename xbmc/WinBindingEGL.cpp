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

#ifdef HAS_EGL

#include "WinBindingEGL.h"
#include "log.h"

CWinBindingEGL::CWinBindingEGL()
{
  m_surface = EGL_NO_SURFACE;
  m_context = EGL_NO_CONTEXT;
  m_display = EGL_NO_DISPLAY;
}

CWinBindingEGL::~CWinBindingEGL()
{
  DestroyWindow();
}

bool CWinBindingEGL::ReleaseSurface()
{
  EGLBoolean eglStatus;

  if (m_surface == EGL_NO_SURFACE)
  {
    return true;
  }

  eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  eglStatus = eglDestroySurface(m_display, m_surface);
  if (!eglStatus)
  {
    CLog::Log(LOGERROR, "Error destroying EGL surface");
    return false;
  }

  m_surface = EGL_NO_SURFACE;

  return true;
}

bool CWinBindingEGL::CreateWindow(EGLNativeDisplayType nativeDisplay, EGLNativeWindowType nativeWindow)
{
  EGLBoolean eglStatus;
  EGLint     configCount;
  EGLConfig* configList = NULL;  

  m_nativeDisplay = nativeDisplay;
  m_nativeWindow = nativeWindow;

  m_display = eglGetDisplay(nativeDisplay);
  if (m_display == EGL_NO_DISPLAY) 
  {
    CLog::Log(LOGERROR, "EGL failed to obtain display");
    return false;
  }
   
  if (!eglInitialize(m_display, 0, 0)) 
  {
    CLog::Log(LOGERROR, "EGL failed to initialize");
    return false;
  } 
  
  EGLint configAttrs[] = {
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_DEPTH_SIZE,      0,
        EGL_STENCIL_SIZE,    0,
        EGL_SAMPLE_BUFFERS,  0,
        EGL_SAMPLES,         0,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
  };

  // Find out how many configurations suit our needs  
  eglStatus = eglChooseConfig(m_display, configAttrs, NULL, 0, &configCount);
  if (!eglStatus || !configCount) 
  {
    CLog::Log(LOGERROR, "EGL failed to return any matching configurations: %d", eglStatus);
    return false;
  }
    
  // Allocate room for the list of matching configurations
  configList = (EGLConfig*)malloc(configCount * sizeof(EGLConfig));
  if (!configList) 
  {
    CLog::Log(LOGERROR, "kdMalloc failure obtaining configuration list");
    return false;
  }

  // Obtain the configuration list from EGL
  eglStatus = eglChooseConfig(m_display, configAttrs,
                                configList, configCount, &configCount);
  if (!eglStatus || !configCount) 
  {
    CLog::Log(LOGERROR, "EGL failed to populate configuration list: %d", eglStatus);
    return false;
  }
  
  // Select an EGL configuration that matches the native window
  m_config = configList[0];

  EGLint* attribList = NULL;
#ifdef EMPOWER
  EGLint windowAttrs[3];
  int windowIndex = 0;
  windowAttrs[windowIndex++] = EGL_RENDER_BUFFER;
  windowAttrs[windowIndex++] = EGL_BACK_BUFFER;
  windowAttrs[windowIndex++] = EGL_NONE;
  attribList = windowAttrs;
#endif

  if (m_surface != EGL_NO_SURFACE)
  {
    ReleaseSurface();
  }

  m_surface = eglCreateWindowSurface(m_display, m_config, m_nativeWindow, attribList);
  if (!m_surface)
  { 
    CLog::Log(LOGERROR, "EGL couldn't create window surface");
    return false;
  }

  eglStatus = eglBindAPI(EGL_OPENGL_ES_API);
  if (!eglStatus) 
  {
    CLog::Log(LOGERROR, "EGL failed to bind API: %d", eglStatus);
    return false;
  }

  EGLint contextAttrs[] = 
  {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  // Create an EGL context
  if (m_context == EGL_NO_CONTEXT)
  {
    m_context = eglCreateContext(m_display, m_config, NULL, contextAttrs);
    if (!m_context)
    {
      CLog::Log(LOGERROR, "EGL couldn't create context");
      return false;
    }
  }

  // Make the context and surface current for rendering
  eglStatus = eglMakeCurrent(m_display, m_surface, m_surface, m_context);
  if (!eglStatus) 
  {
    CLog::Log(LOGERROR, "EGL couldn't make context/surface current: %d", eglStatus);
    return false;
  }
 
  free(configList);

  eglSwapInterval(m_display, 0);

  CLog::Log(LOGINFO, "EGL window and context creation complete");

  return true;
}

bool CWinBindingEGL::DestroyWindow()
{
  EGLBoolean eglStatus;
  if (m_context != EGL_NO_CONTEXT)
  {
    eglStatus = eglDestroyContext(m_display, m_context);
    if (!eglStatus)
      CLog::Log(LOGERROR, "Error destroying EGL context");
    m_context = EGL_NO_CONTEXT;
  }

  if (m_surface != EGL_NO_SURFACE)
  {
    eglStatus = eglDestroySurface(m_display, m_surface);
    if (!eglStatus)
      CLog::Log(LOGERROR, "Error destroying EGL surface");
    m_surface = EGL_NO_SURFACE;
  }

  if (m_display != EGL_NO_DISPLAY)
  {
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    eglStatus = eglTerminate(m_display);
    if (!eglStatus)
      CLog::Log(LOGERROR, "Error terminating EGL");
    m_display = EGL_NO_DISPLAY;
  }

  return true;
}

#endif
