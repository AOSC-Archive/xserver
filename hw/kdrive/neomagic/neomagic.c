/*
 *
 * Copyright © 2004 Franco Catrin
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Franco Catrin not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Franco Catrin makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * FRANCO CATRIN DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL FRANCO CATRIN BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "neomagic.h"
#include <sys/io.h>

struct NeoChipInfo neoChips[] = {
    {NEO_VENDOR, 0x0001, CAP_NM2070, "MagicGraph 128(NM2070)",
     896, 65000, 2048, 0x100, 1024, 1024, 1024},
    {NEO_VENDOR, 0x0002, CAP_NM2090, "MagicGraph 128V(NM2090)",
     1152, 80000, 2048, 0x100, 2048, 1024, 1024},
    {NEO_VENDOR, 0x0003, CAP_NM2090, "MagicGraph 128ZV(NM2093)",
     1152, 80000, 2048, 0x100, 2048, 1024, 1024},
    {NEO_VENDOR, 0x0083, CAP_NM2097, "MagicGraph 128ZV+(NM2097)",
     1152, 80000, 1024, 0x100, 2048, 1024, 1024},
    {NEO_VENDOR, 0x0004, CAP_NM2097, "MagicGraph 128XD(NM2160)",
     2048, 90000, 1024, 0x100, 2048, 1024, 1024},
    {NEO_VENDOR, 0x0005, CAP_NM2200, "MagicGraph 256AV(NM2200)",
     2560, 110000, 1024, 0x1000, 4096, 1280, 1024},
    {NEO_VENDOR, 0x0025, CAP_NM2200, "MagicGraph 256AV+(NM2230)",
     3008, 110000, 1024, 0x1000, 4096, 1280, 1024},
    {NEO_VENDOR, 0x0006, CAP_NM2200, "MagicGraph 256ZX(NM2360)",
     4096, 110000, 1024, 0x1000, 4096, 1280, 1024},
    {NEO_VENDOR, 0x0016, CAP_NM2200, "MagicGraph 256XL+(NM2380)",
     6144, 110000, 1024, 0x1000, 8192, 1280, 1024},
    {0, 0, 0, NULL},
};

static Bool
neoCardInit(KdCardInfo *card)
{
    ENTER();
    NeoCardInfo    *neoc;
    struct NeoChipInfo *chip;

    neoc =(NeoCardInfo *) xalloc(sizeof(NeoCardInfo));
    if(!neoc) {
        return FALSE;
    }

    if(!backendInitialize(card, &neoc->backendCard)) {
        xfree(neoc);
        return FALSE;
    }

    for(chip = neoChips; chip->name != NULL; ++chip) {
        if(chip->device == card->attr.deviceID) {
            neoc->chip = chip;
            break;
        }
    }

    ErrorF("Using Neomagic card: %s\n", neoc->chip->name);

    iopl(3);

    if (neoc->chip->caps != CAP_NONE) {
        neoMapReg(card, neoc);
    }

    card->driver = neoc;

    LEAVE();
    return TRUE;
}

static Bool
neoScreenInit(KdScreenInfo *screen)
{
    ENTER();
    NeoScreenInfo *neos;
    neoCardInfo(screen);
    int screen_size, memory;

    neos = xcalloc(sizeof(NeoScreenInfo), 1);
    if(neos == NULL) {
        return FALSE;
    }

    if(!backendScreenInitialize(screen, &neos->backendScreen, &neoc->backendCard)) {
        xfree(neos);
        return FALSE;
    }

    screen->softCursor = TRUE;    // no hardware color cursor available

    switch(neoc->backendCard.type) {
#ifdef KDRIVEFBDEV
        case FBDEV:
            neos->screen = neoc->backendCard.priv.fbdev.fb;
            break;
#endif
#ifdef KDRIVEVESA
        case VESA:
            neos->screen = neos->backendScreen.vesa.fb;
            break;
#endif
        default:
            ErrorF("Unhandled backend, we should never get here.\n");
            xfree(neos);
            return FALSE;
    }

    memory = neoc->chip->linearSize * 1024;
    screen_size = screen->fb[0].byteStride * screen->height;
    memory -= screen_size;

    if(memory > screen->fb[0].byteStride) {
        neos->off_screen = neos->screen + screen_size;
        neos->off_screen_size = memory;
    } else {
        neos->off_screen = 0;
        neos->off_screen_size = 0;
    }

    screen->driver = neos;

    LEAVE();
    return TRUE;
}

static Bool
neoInitScreen(ScreenPtr pScreen)
{
    ENTER();
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);
    
    return neoc->backendCard.initScreen(pScreen);
    LEAVE();
}

static Bool
neoFinishInitScreen(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);
    
    return neoc->backendCard.finishInitScreen(pScreen);
}

static Bool
neoCreateResources(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    return neoc->backendCard.createRes(pScreen);
}

void
neoPreserve(KdCardInfo *card)
{
    NeoCardInfo *neoc = card->driver;
    neoc->backendCard.preserve(card);
}

CARD8
neoGetIndex(NeoCardInfo *nvidiac, CARD16 addr,  CARD8 index)
{
    outb(index, addr);
    
    return inb(addr+1);
}

void
neoSetIndex(NeoCardInfo *nvidiac, CARD16 addr,  CARD8 index, CARD8 val)
{
    outb(index, addr);
    outb(val, addr+1);
}

static void neoLock(NeoCardInfo *neoc){
    CARD8 cr11;
    neoSetIndex(neoc, 0x3ce,  0x09, 0x00);
    cr11 = neoGetIndex(neoc, 0x3d4, 0x11);
    neoSetIndex(neoc, 0x3d4, 0x11, cr11 | 0x80);
}

static void neoUnlock(NeoCardInfo *neoc){
    CARD8 cr11;
    cr11 = neoGetIndex(neoc, 0x3d4, 0x11);
    neoSetIndex(neoc, 0x3d4, 0x11, cr11 & 0x7F);
    neoSetIndex(neoc, 0x3ce,  0x09, 0x26);
}


Bool
neoMapReg(KdCardInfo *card, NeoCardInfo *neoc)
{
    ENTER();
    neoc->reg_base = card->attr.address[1] & 0xFFF80000;
    if(!neoc->reg_base) {
        return FALSE;
    }

    neoc->mmio = KdMapDevice(neoc->reg_base, NEO_REG_SIZE(card));
    if(!neoc->mmio) {
        return FALSE;
    }

    KdSetMappedMode(neoc->reg_base, NEO_REG_SIZE(card), KD_MAPPED_MODE_REGISTERS);

    // if you see the cursor sprite them MMIO is working

    *(((CARD32 *)neoc->mmio)+0x400) =(CARD32)8;
    //neoSetIndex(neoc, 0x3ce, 0x82,8);
    LEAVE();
    return TRUE;
}

void
neoUnmapReg(KdCardInfo *card, NeoCardInfo *neoc)
{
    ENTER();
    if(neoc->reg_base)
    {
        neoSetIndex(neoc, 0x3ce, 0x82,0);
        KdResetMappedMode(neoc->reg_base, NEO_REG_SIZE(card), KD_MAPPED_MODE_REGISTERS);
        KdUnmapDevice((void *)neoc->mmio, NEO_REG_SIZE(card));
        neoc->reg_base = 0;
    }
    LEAVE();
}

static void
neoSetMMIO(KdCardInfo *card, NeoCardInfo *neoc)
{
    if(!neoc->reg_base)
        neoMapReg(card, neoc);
        neoUnlock(neoc);
}

static void
neoResetMMIO(KdCardInfo *card, NeoCardInfo *neoc)
{
    neoUnmapReg(card, neoc);
    neoLock(neoc);
}


Bool
neoEnable(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    if(!neoc->backendCard.enable(pScreen)) {
        return FALSE;
    }

    if (neoc->chip->caps != CAP_NONE) {
        neoSetMMIO(pScreenPriv->card, neoc);
    }

    return TRUE;
}

void
neoDisable(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    if (neoc->chip->caps != CAP_NONE) {
        neoResetMMIO(pScreenPriv->card, neoc);
    }

    neoc->backendCard.disable(pScreen);
}

static void
neoGetColors(ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    neoc->backendCard.getColors(pScreen, fb, n, pdefs);
}

static void
neoPutColors(ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    neoc->backendCard.putColors(pScreen, fb, n, pdefs);
}

static Bool
neoDPMS(ScreenPtr pScreen, int mode)
{
    KdScreenPriv(pScreen);
    neoCardInfo(pScreenPriv);

    return neoc->backendCard.dpms(pScreen, mode);
}

static void
neoRestore(KdCardInfo *card)
{
    NeoCardInfo *neoc = card->driver;

    if (neoc->chip->caps != CAP_NONE) {
        neoResetMMIO(card, neoc);
    }
    neoc->backendCard.restore(card);
}

static void
neoScreenFini(KdScreenInfo *screen)
{
    NeoScreenInfo *neos =(NeoScreenInfo *) screen->driver;
    NeoCardInfo *neoc = screen->card->driver;

    neoc->backendCard.scrfini(screen);
    xfree(neos);
    screen->driver = 0;
}

static void
neoCardFini(KdCardInfo *card)
{
    NeoCardInfo *neoc = card->driver;

    if (neoc->chip->caps != CAP_NONE) {
        neoUnmapReg(card, neoc);
    }
    neoc->backendCard.cardfini(card);
}

#define neoCursorInit 0       // initCursor
#define neoCursorEnable 0     // enableCursor
#define neoCursorDisable 0    // disableCursor
#define neoCursorFini 0       // finiCursor */
#define neoRecolorCursor 0    // recolorCursor */

KdCardFuncs neoFuncs = {
    neoCardInit,              // cardinit
    neoScreenInit,            // scrinit
    neoInitScreen,            // initScreen
    neoFinishInitScreen,      // finishInitScreen
    neoCreateResources,       // createRes
    neoPreserve,              // preserve
    neoEnable,                // enable
    neoDPMS,                  // dpms
    neoDisable,               // disable
    neoRestore,               // restore
    neoScreenFini,            // scrfini
    neoCardFini,              // cardfini

    neoCursorInit,            // initCursor
    neoCursorEnable,          // enableCursor
    neoCursorDisable,         // disableCursor
    neoCursorFini,            // finiCursor
    neoRecolorCursor,         // recolorCursor

    neoDrawInit,              // initAccel
    neoDrawEnable,            // enableAccel
    neoDrawSync,              // syncAccel
    neoDrawDisable,           // disableAccel
    neoDrawFini,              // finiAccel

    neoGetColors,             // getColors
    neoPutColors,             // putColors
};
