///|/ Copyright (c) preFlight 2025+ oozeBot, LLC
///|/
///|/ Released under AGPLv3 or higher
///|/
#pragma once

#include "GLSurface.hpp"

class wxGLCanvas;
class wxGLContext;

namespace DSKY
{

class GLSurface_wx : public IGLSurface
{
public:
    GLSurface_wx(wxGLCanvas *canvas, wxGLContext *context);

    bool makeCurrent() override;
    void releaseCurrent() override;
    void swapBuffers() override;
    std::pair<int, int> getSize() const override;
    float getScaleFactor() const override;
    void setCursor(CursorType type) override;
    bool isShownOnScreen() const override;
    bool isShown() const override;
    void setFocus() override;
    bool hasFocus() const override;
    bool hasCapture() const override;
    void releaseMouse() override;
    std::pair<int, int> screenToClient(int screen_x, int screen_y) const override;
    void refresh() override;

    wxGLCanvas *getCanvas() { return m_canvas; }
    wxGLContext *getContext() { return m_context; }

private:
    wxGLCanvas *m_canvas;
    wxGLContext *m_context;
};

} // namespace DSKY

