#pragma once

#include "WinTab/msgpack.h"
#include "WinTab/wintab.h"
#define PACKETDATA (PK_X | PK_Y | PK_BUTTONS | PK_NORMAL_PRESSURE | PK_STATUS)
#define PACKETMODE PK_BUTTONS
#include "WinTab/pktdef.h"
#include "WinTab/Utils.h"

class WacomTablet
{
public:
    HCTX g_hCtx = NULL;
    AXIS TabletPressure = { 0 };
    LOGCONTEXTA glogContext = { 0 };
    glm::vec2 m_pen_pos{ 0 };
    float m_pen_pres{ 1 };
    bool m_pen_down = false;
    int m_pen_idle = 0;
    bool m_mouse_down = false;
    bool m_stylus = false;
    bool m_eraser = false;
    bool m_ink_pen = false;
    bool m_ink_touch = false;

    HCTX TabletInit(HWND hWnd);
    
    static WacomTablet I;
    bool init(HWND hWnd);
    void terminate();
    void handle_message(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam);
    void set_focus(int activate);
    float get_pressure() const;
    void reset_pressure();
};
