#include "pch.h"
#include "log.h"
#include "wacom.h"

WacomTablet WacomTablet::I;

void WacomTablet::set_focus(int activate)
{
    /* if switching in the middle, disable the region */
    if (g_hCtx)
    {
        gpWTEnable(g_hCtx, activate);
        if (g_hCtx && activate)
        {
            gpWTOverlap(g_hCtx, TRUE);
        }
    }
}

HCTX WacomTablet::TabletInit(HWND hWnd)
{
    HCTX hctx = NULL;
    UINT wDevice = 0;
    UINT wExtX = 0;
    UINT wExtY = 0;
    UINT wWTInfoRetVal = 0;
    AXIS TabletX = { 0 };
    AXIS TabletY = { 0 };

    // Set option to move system cursor before getting default system context.
    glogContext.lcOptions |= CXO_SYSTEM;

    // Open default system context so that we can get tablet data
    // in screen coordinates (not tablet coordinates).
    wWTInfoRetVal = gpWTInfoA(WTI_DEFSYSCTX, 0, &glogContext);
    assert(wWTInfoRetVal == sizeof(LOGCONTEXTA));

    assert(glogContext.lcOptions & CXO_SYSTEM);

    // modify the digitizing region
    sprintf_s(glogContext.lcName, "PanoPainter Digitizing %x", (unsigned int)GetModuleHandle(0));

    // We process WT_PACKET (CXO_MESSAGES) messages.
    glogContext.lcOptions |= CXO_MESSAGES;

    // What data items we want to be included in the tablet packets
    glogContext.lcPktData = PACKETDATA;

    // Which packet items should show change in value since the last
    // packet (referred to as 'relative' data) and which items
    // should be 'absolute'.
    glogContext.lcPktMode = PACKETMODE;

    // This bitfield determines whether or not this context will receive
    // a packet when a value for each packet field changes.  This is not
    // supported by the Intuos Wintab.  Your context will always receive
    // packets, even if there has been no change in the data.
    glogContext.lcMoveMask = PACKETDATA;

    // Which buttons events will be handled by this context.  lcBtnMask
    // is a bitfield with one bit per button.
    glogContext.lcBtnUpMask = glogContext.lcBtnDnMask;

    // Set the entire tablet as active
    wWTInfoRetVal = gpWTInfoA(WTI_DEVICES, DVC_X, &TabletX);
    assert(wWTInfoRetVal == sizeof(AXIS));

    wWTInfoRetVal = gpWTInfoA(WTI_DEVICES, DVC_Y, &TabletY);
    assert(wWTInfoRetVal == sizeof(AXIS));

    // Pressure resolution
    wWTInfoRetVal = gpWTInfoA(WTI_DEVICES, DVC_NPRESSURE, &TabletPressure);
    assert(wWTInfoRetVal == sizeof(AXIS));
    LOG("Tablet pressure: %d", TabletPressure.axMin);

    wWTInfoRetVal = gpWTInfoA(WTI_DEVICES, DVC_NAME, 0);
    if (wWTInfoRetVal > 0)
    {
        CHAR* TabletName = new CHAR[wWTInfoRetVal];
        wWTInfoRetVal = gpWTInfoA(WTI_DEVICES, DVC_NAME, TabletName);
        LOG("Tablet: %s", TabletName);
        delete[] TabletName;
    }


    /*
    glogContext.lcInOrgX = 0;
    glogContext.lcInOrgY = 0;
    glogContext.lcInExtX = TabletX.axMax;
    glogContext.lcInExtY = TabletY.axMax;

    // Guarantee the output coordinate space to be in screen coordinates.
    glogContext.lcOutOrgX = GetSystemMetrics( SM_XVIRTUALSCREEN );
    glogContext.lcOutOrgY = GetSystemMetrics( SM_YVIRTUALSCREEN );
    glogContext.lcOutExtX = GetSystemMetrics( SM_CXSCREEN / *SM_CXVIRTUALSCREEN* / ); //SM_CXSCREEN );

    // In Wintab, the tablet origin is lower left.  Move origin to upper left
    // so that it coincides with screen origin.
    glogContext.lcOutExtY = -GetSystemMetrics( SM_CYSCREEN / *SM_CYVIRTUALSCREEN* / );	//SM_CYSCREEN );

    */
    // Leave the system origin and extents as received:
    // lcSysOrgX, lcSysOrgY, lcSysExtX, lcSysExtY

    // open the region
    // The Wintab spec says we must open the context disabled if we are 
    // using cursor masks.  
    hctx = gpWTOpenA(hWnd, &glogContext, FALSE);

    LOG("HCTX: %i", (int)hctx);

    return hctx;
}

bool WacomTablet::init(HWND hWnd)
{
    // Init WinTab
    if (LoadWintab())
    {
        /* check if WinTab available. */
        gpWTInfoA(0, 0, 0);
        if (UINT ret = gpWTInfoA(WTI_DEFSYSCTX, 0, &glogContext))
        {
#ifdef _DEBUG
            // this should just avoid errors in debug mode
            if (ret != sizeof(LOGCONTEXTA))
                return false;
#endif // _DEBUG
            LOG("TabletInit");
            g_hCtx = TabletInit(hWnd);
            if (!g_hCtx)
            {
                LOG("Could Not Open Tablet Context.");
                return false;
            }
            else
            {
                set_focus(true);
                return true;
            }
        }
        else
        {
            LOG("WinTab Services Not Available.");
            return false;
        }
    }
    else
    {
        LOG("Unable to initialize Wacom WinTab");
        return false;
    }
}

void WacomTablet::terminate()
{
    if (gpWTClose)
    {
        gpWTClose(g_hCtx);
        UnloadWintab();
    }
}

void WacomTablet::handle_message(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PACKET pkt;
    static POINT ptOld, ptNew;
    static UINT prsOld, prsNew;

    if (!gpWTPacket)
        return;

    if (gpWTPacket((HCTX)lParam, (UINT)wParam, &pkt))
    {
        if (HIWORD(pkt.pkButtons) == TBN_DOWN)
            m_pen_down = true;
        if (HIWORD(pkt.pkButtons) == TBN_UP)
            m_pen_down = false;

        ptOld = ptNew;
        prsOld = prsNew;

        ptNew.x = pkt.pkX;
        ptNew.y = pkt.pkY;

        prsNew = pkt.pkNormalPressure;

        m_pen_pos = { pkt.pkX, pkt.pkX };
        m_pen_pres = (float)pkt.pkNormalPressure / (float)TabletPressure.axMax;
        m_stylus = true;
        m_eraser = (pkt.pkStatus == 0x10);
        //LOG("packet %x", pkt.pkStatus);
    }
}

float WacomTablet::get_pressure() const
{
    return m_pen_pres;
}
void WacomTablet::reset_pressure()
{
    m_pen_pres = 1.f;
}