/*
 * $Id$
 *
 * Copyright � 2003 Eric Anholt
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Eric Anholt not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Eric Anholt makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * ERIC ANHOLT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ERIC ANHOLT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
/* $Header$ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "ati.h"
#include "ati_reg.h"

struct pci_id_entry ati_pci_ids[] = {
	{0x1002, 0x4136, 0x1, "ATI Radeon RS100"},
	{0x1002, 0x4137, 0x3, "ATI Radeon RS200"},
	{0x1002, 0x4237, 0x3, "ATI Radeon RS250"},
	{0x1002, 0x4144, 0x5, "ATI Radeon R300 AD"},
	{0x1002, 0x4145, 0x5, "ATI Radeon R300 AE"},
	{0x1002, 0x4146, 0x5, "ATI Radeon R300 AF"},
	{0x1002, 0x4147, 0x5, "ATI Radeon R300 AG"},
	{0x1002, 0x4148, 0x5, "ATI Radeon R350 AH"},
	{0x1002, 0x4149, 0x5, "ATI Radeon R350 AI"},
	{0x1002, 0x414a, 0x5, "ATI Radeon R350 AJ"},
	{0x1002, 0x414b, 0x5, "ATI Radeon R350 AK"},
	{0x1002, 0x4150, 0x5, "ATI Radeon RV350 AP"},
	{0x1002, 0x4151, 0x5, "ATI Radeon RV350 AQ"},
	{0x1002, 0x4152, 0x5, "ATI Radeon RV350 AR"},
	{0x1002, 0x4153, 0x5, "ATI Radeon RV350 AS"},
	{0x1002, 0x4154, 0x5, "ATI Radeon RV350 AT"},
	{0x1002, 0x4156, 0x5, "ATI Radeon RV350 AV"},
	{0x1002, 0x4242, 0x3, "ATI Radeon R200 BB"},
	{0x1002, 0x4243, 0x3, "ATI Radeon R200 BC"},
	{0x1002, 0x4336, 0x1, "ATI Radeon RS100"},
	{0x1002, 0x4337, 0x3, "ATI Radeon RS200"},
	{0x1002, 0x4437, 0x3, "ATI Radeon RS250"},
	{0x1002, 0x4964, 0x3, "ATI Radeon RV250 Id"},
	{0x1002, 0x4965, 0x3, "ATI Radeon RV250 Ie"},
	{0x1002, 0x4966, 0x3, "ATI Radeon RV250 If"},
	{0x1002, 0x4967, 0x3, "ATI Radeon RV250 Ig"},
	{0x1002, 0x4c45, 0x0, "ATI Rage 128 LE"},
	{0x1002, 0x4c46, 0x0, "ATI Rage 128 LF"},
	{0x1002, 0x4c57, 0x3, "ATI Radeon RV200 LW"},
	{0x1002, 0x4c58, 0x3, "ATI Radeon RV200 LX"},
	{0x1002, 0x4c59, 0x3, "ATI Radeon Mobility M6 LY"},
	{0x1002, 0x4c5a, 0x3, "ATI Radeon Mobility LZ"},
	{0x1002, 0x4c64, 0x3, "ATI Radeon RV250 Ld"},
	{0x1002, 0x4c65, 0x3, "ATI Radeon RV250 Le"},
	{0x1002, 0x4c66, 0x3, "ATI Radeon Mobility M9 RV250 Lf"},
	{0x1002, 0x4c67, 0x3, "ATI Radeon RV250 Lg"},
	{0x1002, 0x4d46, 0x0, "ATI Rage 128 MF"},
	{0x1002, 0x4d46, 0x0, "ATI Rage 128 ML"},
	{0x1002, 0x4e44, 0x5, "ATI Radeon R300 ND"},
	{0x1002, 0x4e45, 0x5, "ATI Radeon R300 NE"},
	{0x1002, 0x4e46, 0x5, "ATI Radeon R300 NF"},
	{0x1002, 0x4e47, 0x5, "ATI Radeon R300 NG"},
	{0x1002, 0x4e48, 0x5, "ATI Radeon R350 NH"},
	{0x1002, 0x4e49, 0x5, "ATI Radeon R350 NI"},
	{0x1002, 0x4e4a, 0x5, "ATI Radeon R350 NJ"},
	{0x1002, 0x4e4b, 0x5, "ATI Radeon R350 NK"},
	{0x1002, 0x4e50, 0x5, "ATI Radeon Mobility RV350 NP"},
	{0x1002, 0x4e51, 0x5, "ATI Radeon Mobility RV350 NQ"},
	{0x1002, 0x4e52, 0x5, "ATI Radeon Mobility RV350 NR"},
	{0x1002, 0x4e53, 0x5, "ATI Radeon Mobility RV350 NS"},
	{0x1002, 0x4e54, 0x5, "ATI Radeon Mobility RV350 NT"},
	{0x1002, 0x4e56, 0x5, "ATI Radeon Mobility RV350 NV"},
	{0x1002, 0x5041, 0x0, "ATI Rage 128 PA"},
	{0x1002, 0x5042, 0x0, "ATI Rage 128 PB"},
	{0x1002, 0x5043, 0x0, "ATI Rage 128 PC"},
	{0x1002, 0x5044, 0x0, "ATI Rage 128 PD"},
	{0x1002, 0x5045, 0x0, "ATI Rage 128 PE"},
	{0x1002, 0x5046, 0x0, "ATI Rage 128 PF"},
	{0x1002, 0x5047, 0x0, "ATI Rage 128 PG"},
	{0x1002, 0x5048, 0x0, "ATI Rage 128 PH"},
	{0x1002, 0x5049, 0x0, "ATI Rage 128 PI"},
	{0x1002, 0x504a, 0x0, "ATI Rage 128 PJ"},
	{0x1002, 0x504b, 0x0, "ATI Rage 128 PK"},
	{0x1002, 0x504c, 0x0, "ATI Rage 128 PL"},
	{0x1002, 0x504d, 0x0, "ATI Rage 128 PM"},
	{0x1002, 0x504e, 0x0, "ATI Rage 128 PN"},
	{0x1002, 0x504f, 0x0, "ATI Rage 128 PO"},
	{0x1002, 0x5050, 0x0, "ATI Rage 128 PP"},
	{0x1002, 0x5051, 0x0, "ATI Rage 128 PQ"},
	{0x1002, 0x5052, 0x0, "ATI Rage 128 PR"},
	{0x1002, 0x5053, 0x0, "ATI Rage 128 PS"},
	{0x1002, 0x5054, 0x0, "ATI Rage 128 PT"},
	{0x1002, 0x5055, 0x0, "ATI Rage 128 PU"},
	{0x1002, 0x5056, 0x0, "ATI Rage 128 PV"},
	{0x1002, 0x5057, 0x0, "ATI Rage 128 PW"},
	{0x1002, 0x5058, 0x0, "ATI Rage 128 PX"},
	{0x1002, 0x5144, 0x1, "ATI Radeon R100 QD"},
	{0x1002, 0x5145, 0x1, "ATI Radeon R100 QE"},
	{0x1002, 0x5146, 0x1, "ATI Radeon R100 QF"},
	{0x1002, 0x5147, 0x1, "ATI Radeon R100 QG"},
	{0x1002, 0x5148, 0x1, "ATI Radeon R200 QH"},
	{0x1002, 0x514c, 0x1, "ATI Radeon R200 QL"},
	{0x1002, 0x514d, 0x1, "ATI Radeon R200 QM"},
	{0x1002, 0x5157, 0x1, "ATI Radeon RV200 QW"},
	{0x1002, 0x5158, 0x1, "ATI Radeon RV200 QX"},
	{0x1002, 0x5159, 0x1, "ATI Radeon RV100 QY"},
	{0x1002, 0x515a, 0x1, "ATI Radeon RV100 QZ"},
	{0x1002, 0x5245, 0x0, "ATI Rage 128 RE"},
	{0x1002, 0x5246, 0x0, "ATI Rage 128 RF"},
	{0x1002, 0x5247, 0x0, "ATI Rage 128 RG"},
	{0x1002, 0x524b, 0x0, "ATI Rage 128 RK"},
	{0x1002, 0x524c, 0x0, "ATI Rage 128 RL"},
	{0x1002, 0x5345, 0x0, "ATI Rage 128 SE"},
	{0x1002, 0x5346, 0x0, "ATI Rage 128 SF"},
	{0x1002, 0x5347, 0x0, "ATI Rage 128 SG"},
	{0x1002, 0x5348, 0x0, "ATI Rage 128 SH"},
	{0x1002, 0x534b, 0x0, "ATI Rage 128 SK"},
	{0x1002, 0x534c, 0x0, "ATI Rage 128 SL"},
	{0x1002, 0x534d, 0x0, "ATI Rage 128 SM"},
	{0x1002, 0x534e, 0x0, "ATI Rage 128 SN"},
	{0x1002, 0x5446, 0x0, "ATI Rage 128 TF"},
	{0x1002, 0x544c, 0x0, "ATI Rage 128 TL"},
	{0x1002, 0x5452, 0x0, "ATI Rage 128 TR"},
	{0x1002, 0x5453, 0x0, "ATI Rage 128 TS"},
	{0x1002, 0x5454, 0x0, "ATI Rage 128 TT"},
	{0x1002, 0x5455, 0x0, "ATI Rage 128 TU"},
	{0x1002, 0x5834, 0x5, "ATI Radeon RS300"},
	{0x1002, 0x5835, 0x5, "ATI Radeon RS300 Mobility"},
	{0x1002, 0x5941, 0x3, "ATI Radeon RV280 (9200)"},
	{0x1002, 0x5961, 0x3, "ATI Radeon RV280 (9200 SE)"},
	{0x1002, 0x5964, 0x3, "ATI Radeon RV280 (9200 SE)"},
	{0x1002, 0x5c60, 0x3, "ATI Radeon RV280"},
	{0x1002, 0x5c61, 0x3, "ATI Radeon RV280 Mobility"},
	{0x1002, 0x5c62, 0x3, "ATI Radeon RV280"},
	{0x1002, 0x5c63, 0x3, "ATI Radeon RV280 Mobility"},
	{0x1002, 0x5c64, 0x3, "ATI Radeon RV280"},
	{0, 0, 0, NULL}
};

static char *
make_busid(KdCardAttr *attr)
{
	char *busid;
	
	busid = xalloc(20);
	if (busid == NULL)
		return NULL;
	snprintf(busid, 20, "pci:%04x:%02x:%02x.%d", attr->domain, attr->bus, 
	    attr->slot, attr->func);
	return busid;
}

static Bool
ATICardInit(KdCardInfo *card)
{
	ATICardInfo *atic;
	int i;
	Bool initialized = FALSE;

	atic = xcalloc(sizeof(ATICardInfo), 1);
	if (atic == NULL)
		return FALSE;

#ifdef KDRIVEFBDEV
	if (!initialized && fbdevInitialize(card, &atic->backend_priv.fbdev)) {
		atic->use_fbdev = TRUE;
		initialized = TRUE;
		atic->backend_funcs.cardfini = fbdevCardFini;
		atic->backend_funcs.scrfini = fbdevScreenFini;
		atic->backend_funcs.initScreen = fbdevInitScreen;
		atic->backend_funcs.finishInitScreen = fbdevFinishInitScreen;
		atic->backend_funcs.createRes = fbdevCreateResources;
		atic->backend_funcs.preserve = fbdevPreserve;
		atic->backend_funcs.restore = fbdevRestore;
		atic->backend_funcs.dpms = fbdevDPMS;
		atic->backend_funcs.enable = fbdevEnable;
		atic->backend_funcs.disable = fbdevDisable;
		atic->backend_funcs.getColors = fbdevGetColors;
		atic->backend_funcs.putColors = fbdevPutColors;
	}
#endif
#ifdef KDRIVEVESA
	if (!initialized && vesaInitialize(card, &atic->backend_priv.vesa)) {
		atic->use_vesa = TRUE;
		initialized = TRUE;
		atic->backend_funcs.cardfini = vesaCardFini;
		atic->backend_funcs.scrfini = vesaScreenFini;
		atic->backend_funcs.initScreen = vesaInitScreen;
		atic->backend_funcs.finishInitScreen = vesaFinishInitScreen;
		atic->backend_funcs.createRes = vesaCreateResources;
		atic->backend_funcs.preserve = vesaPreserve;
		atic->backend_funcs.restore = vesaRestore;
		atic->backend_funcs.dpms = vesaDPMS;
		atic->backend_funcs.enable = vesaEnable;
		atic->backend_funcs.disable = vesaDisable;
		atic->backend_funcs.getColors = vesaGetColors;
		atic->backend_funcs.putColors = vesaPutColors;
	}
#endif

	if (!initialized || !ATIMapReg(card, atic)) {
		xfree(atic);
		return FALSE;
	}

	atic->busid = make_busid(&card->attr);
	if (atic->busid == NULL)
		return FALSE;

#ifdef USE_DRI
	/* We demand identification by busid, not driver name */
	atic->drmFd = drmOpen(NULL, atic->busid);
	if (atic->drmFd < 0)
		ErrorF("Failed to open DRM.  DMA won't be used.\n");
#endif /* USE_DRI */

	card->driver = atic;

	for (i = 0; ati_pci_ids[i].name != NULL; i++) {
		struct pci_id_entry *id = &ati_pci_ids[i];
		if (id->device == card->attr.deviceID) {
			if (id->caps & CAP_RADEON) {
				if (id->caps & CAP_R200)
					atic->is_r200 = TRUE;
				atic->is_radeon = TRUE;
			}
		}
	}
	return TRUE;
}

static void
ATICardFini(KdCardInfo *card)
{
	ATICardInfo *atic = (ATICardInfo *)card->driver;

	ATIUnmapReg(card, atic);
	atic->backend_funcs.cardfini(card);
}

static Bool
ATIScreenInit(KdScreenInfo *screen)
{
	ATIScreenInfo *atis;
	ATICardInfo(screen);
	int success = FALSE;

	atis = xcalloc(sizeof(ATIScreenInfo), 1);
	if (atis == NULL)
		return FALSE;

	atis->atic = atic;

	screen->driver = atis;

#ifdef KDRIVEFBDEV
	if (atic->use_fbdev) {
		success = fbdevScreenInitialize(screen,
						&atis->backend_priv.fbdev);
		screen->memory_size = min(atic->backend_priv.fbdev.fix.smem_len,
		    8192 * screen->fb[0].byteStride);
		/*screen->memory_size = atic->backend_priv.fbdev.fix.smem_len;*/
		screen->off_screen_base =
		    atic->backend_priv.fbdev.var.yres_virtual *
		    screen->fb[0].byteStride;
	}
#endif
#ifdef KDRIVEVESA
	if (atic->use_vesa) {
		if (screen->fb[0].depth == 0)
			screen->fb[0].depth = 16;
		success = vesaScreenInitialize(screen,
		    &atis->backend_priv.vesa);
	}
#endif
	if (!success) {
		screen->driver = NULL;
		xfree(atis);
		return FALSE;
	}

	return TRUE;
}

static void
ATIScreenFini(KdScreenInfo *screen)
{
	ATIScreenInfo *atis = (ATIScreenInfo *)screen->driver;
	ATICardInfo *atic = screen->card->driver;

	atic->backend_funcs.scrfini(screen);
	xfree(atis);
	screen->driver = 0;
}

Bool
ATIMapReg(KdCardInfo *card, ATICardInfo *atic)
{
	atic->reg_base = (CARD8 *)KdMapDevice(RADEON_REG_BASE(card),
	    RADEON_REG_SIZE(card));

	if (atic->reg_base == NULL)
		return FALSE;

	KdSetMappedMode(RADEON_REG_BASE(card), RADEON_REG_SIZE(card),
	    KD_MAPPED_MODE_REGISTERS);

	return TRUE;
}

void
ATIUnmapReg(KdCardInfo *card, ATICardInfo *atic)
{
	if (atic->reg_base) {
		KdResetMappedMode(RADEON_REG_BASE(card), RADEON_REG_SIZE(card),
		    KD_MAPPED_MODE_REGISTERS);
		KdUnmapDevice((void *)atic->reg_base, RADEON_REG_SIZE(card));
		atic->reg_base = 0;
	}
}

static Bool
ATIInitScreen(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	return atic->backend_funcs.initScreen(pScreen);
}

static Bool
ATIFinishInitScreen(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	return atic->backend_funcs.finishInitScreen(pScreen);
}

static Bool
ATICreateResources(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	return atic->backend_funcs.createRes(pScreen);
}

static void
ATIPreserve(KdCardInfo *card)
{
	ATICardInfo *atic = card->driver;

	atic->backend_funcs.preserve(card);
}

static void
ATIRestore(KdCardInfo *card)
{
	ATICardInfo *atic = card->driver;

	ATIUnmapReg(card, atic);

	atic->backend_funcs.restore(card);
}

static Bool
ATIDPMS(ScreenPtr pScreen, int mode)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	return atic->backend_funcs.dpms(pScreen, mode);
}

static Bool
ATIEnable(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	if (!atic->backend_funcs.enable(pScreen))
		return FALSE;

	if ((atic->reg_base == NULL) && !ATIMapReg(pScreenPriv->screen->card,
	    atic))
		return FALSE;

	ATIDPMS(pScreen, KD_DPMS_NORMAL);

	return TRUE;
}

static void
ATIDisable(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	ATIUnmapReg(pScreenPriv->card, atic);

	atic->backend_funcs.disable(pScreen);
}

static void
ATIGetColors(ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	atic->backend_funcs.getColors(pScreen, fb, n, pdefs);
}

static void
ATIPutColors(ScreenPtr pScreen, int fb, int n, xColorItem *pdefs)
{
	KdScreenPriv(pScreen);
	ATICardInfo(pScreenPriv);

	atic->backend_funcs.putColors(pScreen, fb, n, pdefs);
}

KdCardFuncs ATIFuncs = {
	ATICardInit,		/* cardinit */
	ATIScreenInit,		/* scrinit */
	ATIInitScreen,		/* initScreen */
	ATIFinishInitScreen,	/* finishInitScreen */
	ATICreateResources,	/* createRes */
	ATIPreserve,		/* preserve */
	ATIEnable,		/* enable */
	ATIDPMS,		/* dpms */
	ATIDisable,		/* disable */
	ATIRestore,		/* restore */
	ATIScreenFini,		/* scrfini */
	ATICardFini,		/* cardfini */

	0,			/* initCursor */
	0,			/* enableCursor */
	0,			/* disableCursor */
	0,			/* finiCursor */
	0,			/* recolorCursor */

	ATIDrawInit,		/* initAccel */
	ATIDrawEnable,		/* enableAccel */
	ATIDrawSync,		/* syncAccel */
	ATIDrawDisable,		/* disableAccel */
	ATIDrawFini,		/* finiAccel */

	ATIGetColors,		/* getColors */
	ATIPutColors,		/* putColors */
};
