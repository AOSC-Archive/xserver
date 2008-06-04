/*
 *
Copyright 1990, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall not be
used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization from The Open Group.
 *
 * Author:  Keith Packard, MIT X Consortium
 */

/*
 * mieq.c
 *
 * Machine independent event queue
 *
 */

#if HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

# define NEED_EVENTS
# include   <X11/X.h>
# include   <X11/Xmd.h>
# include   <X11/Xproto.h>
# include   "misc.h"
# include   "windowstr.h"
# include   "pixmapstr.h"
# include   "inputstr.h"
# include   "mi.h"
# include   "mipointer.h"
# include   "scrnintstr.h"
# include   <X11/extensions/XI.h>
# include   <X11/extensions/XIproto.h>
# include   <X11/extensions/geproto.h>
# include   "extinit.h"
# include   "exglobals.h"

#ifdef DPMSExtension
# include "dpmsproc.h"
# define DPMS_SERVER
# include <X11/extensions/dpms.h>
#endif

#define QUEUE_SIZE  512

#define EnqueueScreen(dev) dev->spriteInfo->sprite->pEnqueueScreen
#define DequeueScreen(dev) dev->spriteInfo->sprite->pDequeueScreen

typedef struct _Event {
    EventListPtr    events;
    int             nevents;
    ScreenPtr	    pScreen;
    DeviceIntPtr    pDev; /* device this event _originated_ from */
} EventRec, *EventPtr;

typedef struct _EventQueue {
    HWEventQueueType head, tail;         /* long for SetInputCheck */
    CARD32           lastEventTime;      /* to avoid time running backwards */
    int              lastMotion;         /* device ID if last event motion? */
    EventRec         events[QUEUE_SIZE]; /* static allocation for signals */
    mieqHandler      handlers[128];      /* custom event handler */
} EventQueueRec, *EventQueuePtr;

static EventQueueRec miEventQueue;

Bool
mieqInit(void)
{
    int i;

    miEventQueue.head = miEventQueue.tail = 0;
    miEventQueue.lastEventTime = GetTimeInMillis ();
    miEventQueue.lastMotion = FALSE;
    for (i = 0; i < 128; i++)
        miEventQueue.handlers[i] = NULL;
    for (i = 0; i < QUEUE_SIZE; i++)
    {
        EventListPtr evlist = InitEventList(7); /* 1 + MAX_VALUATOR_EVENTS */
        if (!evlist)
            FatalError("Could not allocate event queue.\n");
        miEventQueue.events[i].events = evlist;
    }
    SetInputCheck(&miEventQueue.head, &miEventQueue.tail);
    return TRUE;
}

/* Ensure all events in the EQ are at least size bytes. */
void
mieqResizeEvents(int min_size)
{
    int i;

    for (i = 0; i < QUEUE_SIZE; i++)
        SetMinimumEventSize(miEventQueue.events[i].events, 7, min_size);
}

/*
 * Must be reentrant with ProcessInputEvents.  Assumption: mieqEnqueue
 * will never be interrupted.  If this is called from both signal
 * handlers and regular code, make sure the signal is suspended when
 * called from regular code.
 */

void
mieqEnqueue(DeviceIntPtr pDev, xEvent *e)
{
    unsigned int           oldtail = miEventQueue.tail, newtail;
    EventListPtr           evt;
    int                    isMotion = 0;
    int                    evlen;


    /* avoid merging events from different devices */
    if (e->u.u.type == MotionNotify)
        isMotion = pDev->id;
    else if (e->u.u.type == DeviceMotionNotify)
        isMotion = pDev->id | (1 << 8); /* flag to indicate DeviceMotion */

    /* We silently steal valuator events: just tack them on to the last
     * motion event they need to be attached to.  Sigh. */
    if (e->u.u.type == DeviceValuator) {
        deviceValuator         *v = (deviceValuator *) e;
        EventPtr               laste;
        deviceKeyButtonPointer *lastkbp;

        laste = &miEventQueue.events[(oldtail - 1) % QUEUE_SIZE];
        lastkbp = (deviceKeyButtonPointer *) laste->events->event;

        if (laste->nevents > 6) {
            ErrorF("[mi] mieqEnqueue: more than six valuator events; dropping.\n");
            return;
        }
        if (oldtail == miEventQueue.head ||
            !(lastkbp->type == DeviceMotionNotify ||
              lastkbp->type == DeviceButtonPress ||
              lastkbp->type == DeviceButtonRelease ||
              lastkbp->type == ProximityIn ||
              lastkbp->type == ProximityOut) ||
            ((lastkbp->deviceid & DEVICE_BITS) !=
             (v->deviceid & DEVICE_BITS))) {
            ErrorF("[mi] mieqEnequeue: out-of-order valuator event; dropping.\n");
            return;
        }

        memcpy((laste->events[laste->nevents++].event), e, sizeof(xEvent));
        return;
    }

    if (isMotion && isMotion == miEventQueue.lastMotion &&
        oldtail != miEventQueue.head) {
	oldtail = (oldtail - 1) % QUEUE_SIZE;
    }
    else {
	newtail = (oldtail + 1) % QUEUE_SIZE;
	/* Toss events which come in late.  Usually this means your server's
         * stuck in an infinite loop somewhere, but SIGIO is still getting
         * handled. */
	if (newtail == miEventQueue.head) {
            ErrorF("[mi] EQ overflowing. The server is probably stuck "
                   "in an infinite loop.\n");
	    return;
        }
	miEventQueue.tail = newtail;
    }

    evlen = sizeof(xEvent);
    if (e->u.u.type == GenericEvent)
        evlen += ((xGenericEvent*)e)->length * 4;

    evt = miEventQueue.events[oldtail].events;
    if (evt->evlen < evlen)
    {
        evt->evlen = evlen;
        evt->event = xrealloc(evt->event, evt->evlen);
        if (!evt->event)
        {
            ErrorF("[mi] Running out of memory. Tossing event.\n");
            return;
        }
    }

    memcpy(evt->event, e, evlen);
    miEventQueue.events[oldtail].nevents = 1;

    /* Make sure that event times don't go backwards - this
     * is "unnecessary", but very useful. */
    if (e->u.u.type != GenericEvent &&
        e->u.keyButtonPointer.time < miEventQueue.lastEventTime &&
            miEventQueue.lastEventTime - e->u.keyButtonPointer.time < 10000)
        evt->event->u.keyButtonPointer.time = miEventQueue.lastEventTime;

    miEventQueue.lastEventTime = evt->event->u.keyButtonPointer.time;
    miEventQueue.events[oldtail].pScreen = EnqueueScreen(pDev);
    miEventQueue.events[oldtail].pDev = pDev;

    miEventQueue.lastMotion = isMotion;
}

void
mieqSwitchScreen(DeviceIntPtr pDev, ScreenPtr pScreen, Bool fromDIX)
{
    EnqueueScreen(pDev) = pScreen;
    if (fromDIX)
	DequeueScreen(pDev) = pScreen;
}

void
mieqSetHandler(int event, mieqHandler handler)
{
    if (handler && miEventQueue.handlers[event])
        ErrorF("[mi] mieq: warning: overriding existing handler %p with %p for "
               "event %d\n", miEventQueue.handlers[event], handler, event);

    miEventQueue.handlers[event] = handler;
}

/**
 * Change the device id of the given event to the given device's id.
 */
static void
ChangeDeviceID(DeviceIntPtr dev, xEvent* event)
{
    int type = event->u.u.type;

    if (type == DeviceKeyPress || type == DeviceKeyRelease ||
            type == DeviceButtonPress || type == DeviceButtonRelease ||
            type == DeviceMotionNotify || type == ProximityIn ||
            type == ProximityOut)
        ((deviceKeyButtonPointer*)event)->deviceid = dev->id;
    else if (type == DeviceValuator)
        ((deviceValuator*)event)->deviceid = dev->id;
    else if (type == GenericEvent)
    {
        /* FIXME: need to put something into XGE to make this saner */
        if (GEIsType(event, IReqCode, XI_DeviceClassesChangedNotify))
        {
            // do nothing or drink a beer. your choice.
        } else
            DebugF("[mi] Unknown generic event (%d/%d), cannot change id.\n",
                    ((xGenericEvent*)ev)->extension,
                    ((xGenericEvent*)ev)->evtype);
    } else
        DebugF("[mi] Unknown event type (%d), cannot change id.\n", type);
}

/**
 * Copy the given event into master.
 * @param mdev The master device
 * @param original The event as it came from the EQ
 * @param master The event after being copied
 * @param count Number of events in original.
 */
void
CopyGetMasterEvent(DeviceIntPtr mdev, xEvent* original,
                   xEvent** master, int count)
{
    if (count > 1) {
        *master = xcalloc(count, sizeof(xEvent));
        if (!*master)
            FatalError("[mi] No memory left for master event.\n");
        while(count--)
        {
            memcpy(&(*master)[count], &original[count], sizeof(xEvent));
            ChangeDeviceID(mdev, &(*master)[count]);
        }
    } else
    {
        int len = sizeof(xEvent);
        if (original->u.u.type == GenericEvent)
            len += GEV(original)->length * 4;
        *master = xalloc(len);
        if (!*master)
            FatalError("[mi] No memory left for master event.\n");
        memcpy(*master, original, len);
        ChangeDeviceID(mdev, *master);
    }
}

/* Call this from ProcessInputEvents(). */
void
mieqProcessInputEvents(void)
{
    EventRec *e = NULL;
    int x = 0, y = 0;
    xEvent* event,
            *master_event = NULL;

    while (miEventQueue.head != miEventQueue.tail) {
        if (screenIsSaved == SCREEN_SAVER_ON)
            dixSaveScreens (serverClient, SCREEN_SAVER_OFF, ScreenSaverReset);
#ifdef DPMSExtension
        else if (DPMSPowerLevel != DPMSModeOn)
            SetScreenSaverTimer();

        if (DPMSPowerLevel != DPMSModeOn)
            DPMSSet(serverClient, DPMSModeOn);
#endif

        e = &miEventQueue.events[miEventQueue.head];
        /* Assumption - screen switching can only occur on motion events. */
        miEventQueue.head = (miEventQueue.head + 1) % QUEUE_SIZE;

        if (e->pScreen != DequeueScreen(e->pDev)) {
            DequeueScreen(e->pDev) = e->pScreen;
            x = e->events[0].event->u.keyButtonPointer.rootX;
            y = e->events[0].event->u.keyButtonPointer.rootY;
            NewCurrentScreen (e->pDev, DequeueScreen(e->pDev), x, y);
        }
        else {
            mieqHandler handler;

            /* FIXME: Bad hack. The only event where we actually get multiple
             * events at once is a DeviceMotionNotify followed by
             * DeviceValuators. For now it's safe enough to just take the
             * event directly or copy the bunch of events and pass in the
             * copy. Eventually the interface for the processInputProc needs
             * to be changed. (whot)
             */
            if (e->nevents > 1)
            {
                int i;
                event = xcalloc(e->nevents, sizeof(xEvent));
                if (!event)
                    FatalError("[mi] No memory left for event processing.\n");
                for (i = 0; i < e->nevents; i++)
                {
                    memcpy(&event[i], e->events[i].event, sizeof(xEvent));
                }
            } else
                event = e->events->event;
            if (!e->pDev->isMaster && e->pDev->u.master)
            {
                CopyGetMasterEvent(e->pDev->u.master, event,
                                   &master_event, e->nevents);
            } else
                master_event = NULL;

            /* If someone's registered a custom event handler, let them
             * steal it. */
            if ((handler = miEventQueue.handlers[e->events->event->u.u.type]))
            {
                handler(DequeueScreen(e->pDev)->myNum, e->events->event,
                        e->pDev, e->nevents);
                if (!e->pDev->isMaster && e->pDev->u.master)
                {
                    handler(DequeueScreen(e->pDev->u.master)->myNum,
                            e->events->event, e->pDev->u.master, e->nevents);
                }
            } else
            {
                /* process slave first, then master */
                e->pDev->public.processInputProc(event, e->pDev, e->nevents);

                if (!e->pDev->isMaster && e->pDev->u.master)
                {
                    e->pDev->u.master->public.processInputProc(master_event,
                            e->pDev->u.master, e->nevents);
                }
            }

            if (e->nevents > 1)
                xfree(event);
            xfree(master_event);
        }

        /* Update the sprite now. Next event may be from different device. */
        if (e->events->event[0].u.u.type == DeviceMotionNotify
                && e->pDev->coreEvents)
        {
            miPointerUpdateSprite(e->pDev);
        }
    }
}

