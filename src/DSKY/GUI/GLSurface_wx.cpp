///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#include "GLSurface_wx.hpp"

#include <wx/glcanvas.h>
#include <wx/window.h>

namespace DSKY
{

GLSurface_wx::GLSurface_wx(wxGLCanvas *canvas, wxGLContext *context) : m_canvas(canvas), m_context(context) {}

bool GLSurface_wx::makeCurrent()
{
    return m_context != nullptr && m_canvas != nullptr && m_canvas->SetCurrent(*m_context);
}

void GLSurface_wx::releaseCurrent()
{
#ifdef _WIN32
    wglMakeCurrent(NULL, NULL);
#endif
}

void GLSurface_wx::swapBuffers()
{
    if (m_canvas != nullptr)
        m_canvas->SwapBuffers();
}

std::pair<int, int> GLSurface_wx::getSize() const
{
    int w = 0, h = 0;
    if (m_canvas != nullptr)
        m_canvas->GetSize(&w, &h);
    return {w, h};
}

float GLSurface_wx::getScaleFactor() const
{
    if (m_canvas != nullptr)
        return static_cast<float>(m_canvas->GetContentScaleFactor());
    return 1.0f;
}

void GLSurface_wx::setCursor(CursorType type)
{
    if (m_canvas == nullptr)
        return;
    switch (type)
    {
    case CursorType::Standard:
        m_canvas->SetCursor(*wxSTANDARD_CURSOR);
        break;
    case CursorType::Cross:
        m_canvas->SetCursor(*wxCROSS_CURSOR);
        break;
    }
}

bool GLSurface_wx::isShownOnScreen() const
{
    return m_canvas != nullptr && m_canvas->IsShownOnScreen();
}

bool GLSurface_wx::isShown() const
{
    return m_canvas != nullptr && m_canvas->IsShown();
}

void GLSurface_wx::setFocus()
{
    if (m_canvas != nullptr)
        m_canvas->SetFocus();
}

bool GLSurface_wx::hasFocus() const
{
    return m_canvas != nullptr && (wxWindow::FindFocus() == m_canvas);
}

bool GLSurface_wx::hasCapture() const
{
    return m_canvas != nullptr && m_canvas->HasCapture();
}

void GLSurface_wx::releaseMouse()
{
    if (m_canvas != nullptr)
        m_canvas->ReleaseMouse();
}

std::pair<int, int> GLSurface_wx::screenToClient(int screen_x, int screen_y) const
{
    if (m_canvas == nullptr)
        return {0, 0};
    wxPoint client = m_canvas->ScreenToClient(wxPoint(screen_x, screen_y));
    return {client.x, client.y};
}

void GLSurface_wx::refresh()
{
    if (m_canvas != nullptr)
        m_canvas->Refresh();
}

} // namespace DSKY
