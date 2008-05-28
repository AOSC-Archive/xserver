/*
 * Copyright 2007-2008 Peter Hutterer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Peter Hutterer, University of South Australia, NICTA
 */

#define	 NEED_EVENTS
#define	 NEED_REPLIES
#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <X11/X.h>	/* for inputstr.h    */
#include <X11/Xproto.h>	/* Request macro     */
#include "inputstr.h"	/* DeviceIntPtr      */
#include "windowstr.h"	/* window structure  */
#include "scrnintstr.h"	/* screen structure  */
#include <X11/extensions/XI.h>
#include <X11/extensions/XIproto.h>
#include "extnsionst.h"
#include "exevents.h"
#include "exglobals.h"

#include "chaccess.h"

/***********************************************************************
 * This procedure allows a client to change window access control.
 */

int
SProcXChangeWindowAccess(ClientPtr client)
{
    char n;
    REQUEST(xChangeWindowAccessReq);

    swaps(&stuff->length, n);
    swapl(&stuff->win, n);
    return ProcXChangeWindowAccess(client);
}

int
ProcXChangeWindowAccess(ClientPtr client)
{
    int padding, rc, i;
    XID* deviceids = NULL;
    WindowPtr win;
    DeviceIntPtr* perm_devices = NULL;
    DeviceIntPtr* deny_devices = NULL;
    REQUEST(xChangeWindowAccessReq);
    REQUEST_AT_LEAST_SIZE(xChangeWindowAccessReq);


    padding = (4 - (((stuff->npermit + stuff->ndeny) * sizeof(XID)) % 4)) % 4;

    if (stuff->length != ((sizeof(xChangeWindowAccessReq)  +
            (((stuff->npermit + stuff->ndeny) * sizeof(XID)) + padding)) >> 2))
    {
        return BadLength;
    }


    rc = dixLookupWindow(&win, stuff->win, client, DixWriteAccess);
    if (rc != Success)
    {
        return rc;
    }

    /* Are we clearing? if so, ignore the rest */
    if (stuff->clear)
    {
        rc = ACClearWindowAccess(client, win, stuff->clear);
        return rc;
    }

    if (stuff->npermit || stuff->ndeny)
        deviceids = (XID*)&stuff[1];

    if (stuff->npermit)
    {
        perm_devices =
            (DeviceIntPtr*)xalloc(stuff->npermit * sizeof(DeviceIntPtr));
        if (!perm_devices)
        {
            ErrorF("[Xi] ProcXChangeWindowAccess: alloc failure.\n");
            return BadImplementation;
        }

        /* if one of the devices cannot be accessed, we don't do anything.*/
        for (i = 0; i < stuff->npermit; i++)
        {
            rc = dixLookupDevice(&perm_devices[i], deviceids[i], client,
                                  DixWriteAccess);
            if (rc != Success)
            {
                xfree(perm_devices);
                return rc;
            }
        }
    }

    if (stuff->ndeny)
    {
        deny_devices =
            (DeviceIntPtr*)xalloc(stuff->ndeny * sizeof(DeviceIntPtr));
        if (!deny_devices)
        {
            ErrorF("[Xi] ProcXChangeWindowAccecss: alloc failure.\n");
            xfree(perm_devices);
            return BadImplementation;
        }

        for (i = 0; i < stuff->ndeny; i++)
        {
            rc = dixLookupDevice(&deny_devices[i],
                                  deviceids[i+stuff->npermit],
                                  client,
                                  DixWriteAccess);
            if (rc != Success)
            {
                xfree(perm_devices);
                xfree(deny_devices);
                return rc;
            }
        }
    }

    rc = ACChangeWindowAccess(client, win, stuff->defaultRule,
                               perm_devices, stuff->npermit,
                               deny_devices, stuff->ndeny);
    if (rc != Success)
    {
        return rc;
    }

    xfree(perm_devices);
    xfree(deny_devices);
    return Success;
}

