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
#include "ati_dri.h"
#include "ati_dripriv.h"
#include "sarea.h"
#include "ati_sarea.h"
#include "ati_draw.h"
#include "r128_common.h"
#include "radeon_common.h"

/* ?? HACK - for now, put this here... */
/* ?? Alpha - this may need to be a variable to handle UP1x00 vs TITAN */
#if defined(__alpha__)
# define DRM_PAGE_SIZE 8192
#elif defined(__ia64__)
# define DRM_PAGE_SIZE getpagesize()
#else
# define DRM_PAGE_SIZE 4096
#endif

void XFree86DRIExtensionInit(void);

static Bool ATIDRIFinishScreenInit(ScreenPtr pScreen);

/* Compute log base 2 of val. */
static int
ATILog2(int val)
{
	int bits;

	if (!val)
		return 1;
	for (bits = 0; val != 0; val >>= 1, ++bits)
		;
	return bits;
}

static void
ATIDRIInitGARTValues(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	int s, l;

	atis->gartOffset = 0;
	
	/* Initialize the ring buffer data */
	atis->ringStart       = atis->gartOffset;
	atis->ringMapSize     = atis->ringSize*1024*1024 + DRM_PAGE_SIZE;
	
	atis->ringReadOffset  = atis->ringStart + atis->ringMapSize;
	atis->ringReadMapSize = DRM_PAGE_SIZE;
	
	/* Reserve space for vertex/indirect buffers */
	atis->bufStart        = atis->ringReadOffset + atis->ringReadMapSize;
	atis->bufMapSize      = atis->bufSize*1024*1024;
	
	/* Reserve the rest for GART textures */
	atis->gartTexStart     = atis->bufStart + atis->bufMapSize;
	s = (atis->gartSize*1024*1024 - atis->gartTexStart);
	l = ATILog2((s-1) / ATI_NR_TEX_REGIONS);
	if (l < ATI_LOG_TEX_GRANULARITY) l = ATI_LOG_TEX_GRANULARITY;
	atis->gartTexMapSize   = (s >> l) << l;
	atis->log2GARTTexGran  = l;
}

static int
ATIDRIAddAndMap(int fd, drmHandle offset, drmSize size,
    drmMapType type, drmMapFlags flags, drmHandlePtr handle,
    drmAddressPtr address, char *desc)
{
	char *name;
	
	name = (type == DRM_AGP) ? "agp" : "pci";
	
	if (drmAddMap(fd, offset, size, type, flags, handle) < 0) {
		ErrorF("[%s] Could not add %s mapping\n", name, desc);
		return FALSE;
	}
	ErrorF("[%s] %s handle = 0x%08lx\n", name, desc, *handle);

	if (drmMap(fd, *handle, size, address) < 0) {
		ErrorF("[agp] Could not map %s\n", name, desc);
		return FALSE;
	}
	ErrorF("[%s] %s mapped at 0x%08lx\n", name, desc, address);

	return TRUE;
}

/* Initialize the AGP state.  Request memory for use in AGP space, and
   initialize the Rage 128 registers to point to that memory. */
static Bool
ATIDRIAgpInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	unsigned char *mmio = atic->reg_base;
	unsigned long mode;
	int           ret;
	unsigned long agpBase;
	CARD32        cntl, chunk;

	if (drmAgpAcquire(atic->drmFd) < 0) {
		ErrorF("[agp] AGP not available\n");
		return FALSE;
	}

	ATIDRIInitGARTValues(pScreen);

	/* Modify the mode if the default mode is not appropriate for this
	 * particular combination of graphics card and AGP chipset.
	 */

	/* XXX: Disable fast writes? */

	mode = drmAgpGetMode(atic->drmFd);
	if (mode > 4)
		mode = 4;
	/* Set all mode bits below the chosen one so fallback can happen */
	mode = (mode * 2) - 1;

	if (drmAgpEnable(atic->drmFd, mode) < 0) {
		ErrorF("[agp] AGP not enabled\n");
		drmAgpRelease(atic->drmFd);
		return FALSE;
	}

	/* Workaround for some hardware bugs */
	/* XXX: Magic numbers */
	if (!atic->is_r200) {
		cntl = MMIO_IN32(mmio, RADEON_REG_AGP_CNTL) | 0x000e0000;
		MMIO_OUT32(mmio, RADEON_REG_AGP_CNTL, cntl);
	}

	if ((ret = drmAgpAlloc(atic->drmFd, atis->gartSize*1024*1024, 0, NULL,
	    &atis->agpMemHandle)) < 0) {
		ErrorF("[agp] Out of memory (%d)\n", ret);
		drmAgpRelease(atic->drmFd);
		return FALSE;
	}
	ErrorF("[agp] %d kB allocated with handle 0x%08lx\n",
	    atis->gartSize*1024, (long)atis->agpMemHandle);

	if (drmAgpBind(atic->drmFd, atis->agpMemHandle, atis->gartOffset) < 0) {
		ErrorF("[agp] Could not bind\n");
		drmAgpFree(atic->drmFd, atis->agpMemHandle);
		drmAgpRelease(atic->drmFd);
		return FALSE;
	}

	if (!ATIDRIAddAndMap(atic->drmFd, atis->ringStart, atis->ringMapSize,
	    DRM_AGP, DRM_READ_ONLY, &atis->ringHandle,
	    (drmAddressPtr)&atis->ring, "ring"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->ringReadOffset,
	    atis->ringReadMapSize, DRM_AGP, DRM_READ_ONLY,
	    &atis->ringReadPtrHandle, (drmAddressPtr)&atis->ringReadPtr,
	    "ring read ptr"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->bufStart, atis->bufMapSize,
	    DRM_AGP, 0, &atis->bufHandle, (drmAddressPtr)&atis->buf,
	    "vertex/indirect buffers"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->gartTexStart,
	    atis->gartTexMapSize, DRM_AGP, 0, &atis->gartTexHandle,
	    (drmAddressPtr)&atis->gartTex, "AGP texture map"))
		return FALSE;

	/* Initialize radeon/r128 AGP registers */
	cntl = MMIO_IN32(mmio, RADEON_REG_AGP_CNTL);
	cntl &= ~RADEON_AGP_APER_SIZE_MASK;
	switch (atis->gartSize) {
	case 256: cntl |= RADEON_AGP_APER_SIZE_256MB; break;
	case 128: cntl |= RADEON_AGP_APER_SIZE_128MB; break;
	case  64: cntl |= RADEON_AGP_APER_SIZE_64MB;  break;
	case  32: cntl |= RADEON_AGP_APER_SIZE_32MB;  break;
	case  16: cntl |= RADEON_AGP_APER_SIZE_16MB;  break;
	case   8: cntl |= RADEON_AGP_APER_SIZE_8MB;   break;
	case   4: cntl |= RADEON_AGP_APER_SIZE_4MB;   break;
	default:
		ErrorF("[agp] Illegal aperture size %d kB\n", atis->gartSize*1024);
		return FALSE;
	}
	agpBase = drmAgpBase(atic->drmFd);
	MMIO_OUT32(mmio, RADEON_REG_AGP_BASE, agpBase); 
	MMIO_OUT32(mmio, RADEON_REG_AGP_CNTL, cntl);

	if (!atic->is_radeon) {
		/* Disable Rage 128 PCIGART registers */
		chunk = MMIO_IN32(mmio, R128_REG_BM_CHUNK_0_VAL);
		chunk &= ~(R128_BM_PTR_FORCE_TO_PCI |
		R128_BM_PM4_RD_FORCE_TO_PCI |
		R128_BM_GLOBAL_FORCE_TO_PCI);
		MMIO_OUT32(mmio, R128_REG_BM_CHUNK_0_VAL, chunk);
		
		/* Ensure AGP GART is used (for now) */
		MMIO_OUT32(mmio, R128_REG_PCI_GART_PAGE, 1);
	}

    return TRUE;
}

static Bool
ATIDRIPciInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	unsigned char *mmio = atic->reg_base;
	CARD32 chunk;
	int ret;

	ATIDRIInitGARTValues(pScreen);

	ret = drmScatterGatherAlloc(atic->drmFd, atis->gartSize*1024*1024,
	    &atis->pciMemHandle);
	if (ret < 0) {
		ErrorF("[pci] Out of memory (%d)\n", ret);
		return FALSE;
	}
	ErrorF("[pci] %d kB allocated with handle 0x%08lx\n",
	    atis->gartSize*1024, (long)atis->pciMemHandle);


	if (!ATIDRIAddAndMap(atic->drmFd, atis->ringStart, atis->ringMapSize,
	    DRM_SCATTER_GATHER, DRM_READ_ONLY | DRM_LOCKED | DRM_KERNEL,
	    &atis->ringHandle, (drmAddressPtr)&atis->ring, "ring"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->ringReadOffset,
	    atis->ringReadMapSize, DRM_SCATTER_GATHER,
	    DRM_READ_ONLY | DRM_LOCKED | DRM_KERNEL,
	    &atis->ringReadPtrHandle, (drmAddressPtr)&atis->ringReadPtr,
	    "ring read ptr"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->bufStart, atis->bufMapSize,
	    DRM_SCATTER_GATHER, 0, &atis->bufHandle, (drmAddressPtr)&atis->buf,
	    "vertex/indirect buffers"))
		return FALSE;

	if (!ATIDRIAddAndMap(atic->drmFd, atis->gartTexStart,
	    atis->gartTexMapSize, DRM_SCATTER_GATHER, 0, &atis->gartTexHandle,
	    (drmAddressPtr)&atis->gartTex, "PCI texture map"))
		return FALSE;

	if (!atic->is_radeon) {
		/* Force PCI GART mode */
		chunk = MMIO_IN32(mmio, R128_REG_BM_CHUNK_0_VAL);
		chunk |= (R128_BM_PTR_FORCE_TO_PCI |
		    R128_BM_PM4_RD_FORCE_TO_PCI | R128_BM_GLOBAL_FORCE_TO_PCI);
		MMIO_OUT32(mmio, R128_REG_BM_CHUNK_0_VAL, chunk);
		MMIO_OUT32(mmio, R128_REG_PCI_GART_PAGE, 0); /* Ensure PCI GART is used */
	}
	return TRUE;
}


/* Initialize the kernel data structures. */
static int
R128DRIKernelInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	drmR128Init drmInfo;

	memset(&drmInfo, 0, sizeof(drmR128Init) );

	drmInfo.func                = DRM_R128_INIT_CCE;
	drmInfo.sarea_priv_offset   = sizeof(XF86DRISAREARec);
	drmInfo.is_pci              = !atis->IsAGP;
	drmInfo.cce_mode            = atis->CCEMode;
	drmInfo.cce_secure          = TRUE;
	drmInfo.ring_size           = atis->ringSize*1024*1024;
	drmInfo.usec_timeout        = atis->DMAusecTimeout;

	drmInfo.fb_bpp              = pScreenPriv->screen->fb[0].depth;
	drmInfo.depth_bpp           = pScreenPriv->screen->fb[0].depth;

	/* XXX: pitches are in pixels on r128. */
	drmInfo.front_offset        = atis->frontOffset;
	drmInfo.front_pitch         = atis->frontPitch;

	drmInfo.back_offset         = atis->backOffset;
	drmInfo.back_pitch          = atis->backPitch;

	drmInfo.depth_offset        = atis->depthOffset;
	drmInfo.depth_pitch         = atis->depthPitch;
	drmInfo.span_offset         = atis->spanOffset;

	drmInfo.fb_offset           = atis->fbHandle;
	drmInfo.mmio_offset         = atis->registerHandle;
	drmInfo.ring_offset         = atis->ringHandle;
	drmInfo.ring_rptr_offset    = atis->ringReadPtrHandle;
	drmInfo.buffers_offset      = atis->bufHandle;
	drmInfo.agp_textures_offset = atis->gartTexHandle;

	if (drmCommandWrite(atic->drmFd, DRM_R128_INIT, &drmInfo,
	    sizeof(drmR128Init)) < 0)
		return FALSE;

	return TRUE;
}

/* Initialize the kernel data structures */
static int
RadeonDRIKernelInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	drmRadeonInit drmInfo;

	memset(&drmInfo, 0, sizeof(drmRadeonInit));

	if (atic->is_r200)
	    drmInfo.func             = DRM_RADEON_INIT_R200_CP;
	else
	    drmInfo.func             = DRM_RADEON_INIT_CP;

	drmInfo.sarea_priv_offset   = sizeof(XF86DRISAREARec);
	drmInfo.is_pci              = !atis->IsAGP;
	drmInfo.cp_mode             = atis->CPMode;
	drmInfo.gart_size           = atis->gartSize*1024*1024;
	drmInfo.ring_size           = atis->ringSize*1024*1024;
	drmInfo.usec_timeout        = atis->DMAusecTimeout;

	drmInfo.fb_bpp              = pScreenPriv->screen->fb[0].depth;
	drmInfo.depth_bpp           = pScreenPriv->screen->fb[0].depth;

	drmInfo.front_offset        = atis->frontOffset;
	drmInfo.front_pitch         = atis->frontPitch;
	drmInfo.back_offset         = atis->backOffset;
	drmInfo.back_pitch          = atis->backPitch;
	drmInfo.depth_offset        = atis->depthOffset;
	drmInfo.depth_pitch         = atis->depthPitch;

	drmInfo.fb_offset           = atis->fbHandle;
	drmInfo.mmio_offset         = atis->registerHandle;
	drmInfo.ring_offset         = atis->ringHandle;
	drmInfo.ring_rptr_offset    = atis->ringReadPtrHandle;
	drmInfo.buffers_offset      = atis->bufHandle;
	drmInfo.gart_textures_offset = atis->gartTexHandle;

	if (drmCommandWrite(atic->drmFd, DRM_RADEON_CP_INIT,
	    &drmInfo, sizeof(drmRadeonInit)) < 0)
		return FALSE;

	return TRUE;
}

/* Add a map for the vertex buffers that will be accessed by any
   DRI-based clients. */
static Bool
ATIDRIBufInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	int type, size;
	
	if (atic->is_radeon)
		size = RADEON_BUFFER_SIZE;
	else
		size = R128_BUFFER_SIZE;

	if (atis->IsAGP)
		type = DRM_AGP_BUFFER;
	else
		type = DRM_SG_BUFFER;

	/* Initialize vertex buffers */
	atis->bufNumBufs = drmAddBufs(atic->drmFd, atis->bufMapSize / size,
	    size, type, atis->bufStart);

	if (atis->bufNumBufs <= 0) {
		ErrorF("[drm] Could not create vertex/indirect buffers list\n");
		return FALSE;
	}
	ErrorF("[drm] Added %d %d byte vertex/indirect buffers\n",
	    atis->bufNumBufs, size);

	atis->buffers = drmMapBufs(atic->drmFd);
	if (atis->buffers == NULL) {
		ErrorF("[drm] Failed to map vertex/indirect buffers list\n");
		return FALSE;
	}
	ErrorF("[drm] Mapped %d vertex/indirect buffers\n",
	    atis->buffers->count);

	return TRUE;
}

static int 
ATIDRIIrqInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);

	if (atis->irqEnabled)
		return FALSE;

	atis->irqEnabled = drmCtlInstHandler(atic->drmFd, 0);

	if (!atis->irqEnabled)
		return FALSE;

	return TRUE;
}

static void ATIDRISwapContext(ScreenPtr pScreen, DRISyncType syncType,
    DRIContextType oldContextType, void *oldContext,
    DRIContextType newContextType, void *newContext)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);

	if ((syncType==DRI_3D_SYNC) && (oldContextType==DRI_2D_CONTEXT) &&
	    (newContextType==DRI_2D_CONTEXT)) {
		/* Entering from Wakeup */
		/* XXX: XFree86 sets NeedToSync */
		
	}
	if ((syncType==DRI_2D_SYNC) && (oldContextType==DRI_NO_CONTEXT) &&
	    (newContextType==DRI_2D_CONTEXT)) {
		/* Exiting from Block Handler */
		if (atis->using_dma)
			ATIDMAFlushIndirect(1);
	}
}

/* Initialize the screen-specific data structures for the DRI and the
   Rage 128.  This is the main entry point to the device-specific
   initialization code.  It calls device-independent DRI functions to
   create the DRI data structures and initialize the DRI state. */
Bool
ATIDRIScreenInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	void *scratch_ptr;
	int scratch_int;
	DRIInfoPtr pDRIInfo;
	int devSareaSize;
	drmSetVersion sv;
	
	/* XXX: Disable DRI clients for unsupported depths */

	if (atic->is_radeon) {
		atis->CPMode = RADEON_CSQ_PRIBM_INDBM;
	}
	else {
		atis->CCEMode = R128_PM4_64BM_64VCBM_64INDBM;
		atis->CCEFifoSize = 64;
	}

	atis->IsAGP = FALSE;	/* XXX */
	atis->agpMode = 1;
	atis->gartSize = 8;
	atis->ringSize = 1;
	atis->bufSize = 2;
	atis->gartTexSize = 1;
	atis->DMAusecTimeout = 10000;

	atis->frontOffset = 0;
	atis->frontPitch = pScreenPriv->screen->fb[0].byteStride;
	atis->backOffset = 0; /* XXX */
	atis->backPitch = pScreenPriv->screen->fb[0].byteStride;
	atis->depthOffset = 0; /* XXX */
	atis->depthPitch = 0; /* XXX */
	atis->spanOffset = 0; /* XXX */

	if (atic->drmFd < 0)
		return FALSE;

	sv.drm_di_major = -1;
	sv.drm_dd_major = -1;
	drmSetInterfaceVersion(atic->drmFd, &sv);
	if (atic->is_radeon) {
		if (sv.drm_dd_major != 1 || sv.drm_dd_minor < 6) {
			ErrorF("[dri] radeon kernel module version is %d.%d "
			    "but version 1.6 or greater is needed.\n",
			    sv.drm_dd_major, sv.drm_dd_minor);
			return FALSE;
		}
	} else {
		if (sv.drm_dd_major != 2 || sv.drm_dd_minor < 2) {
			ErrorF("[dri] r128 kernel module version is %d.%d "
			    "but version 2.2 or greater is needed.\n",
			    sv.drm_dd_major, sv.drm_dd_minor);
			return FALSE;
		}
	}

	/* Create the DRI data structure, and fill it in before calling the
	 * DRIScreenInit().
	 */
	pDRIInfo = DRICreateInfoRec();
	if (!pDRIInfo)
		return FALSE;

	atis->pDRIInfo = pDRIInfo;
	pDRIInfo->busIdString = atic->busid;
	if (atic->is_radeon) {
		pDRIInfo->drmDriverName = "radeon";
		if (atic->is_r200)
			pDRIInfo->clientDriverName = "radeon";
		else
			pDRIInfo->clientDriverName = "r200";
	} else {
		pDRIInfo->drmDriverName = "r128";
		pDRIInfo->clientDriverName = "r128";
	}
	pDRIInfo->ddxDriverMajorVersion = 4;
	pDRIInfo->ddxDriverMinorVersion = 0;
	pDRIInfo->ddxDriverPatchVersion = 0;
	pDRIInfo->frameBufferPhysicalAddress =
	    (unsigned long)pScreenPriv->screen->memory_base;
	pDRIInfo->frameBufferSize = pScreenPriv->screen->memory_size;
	pDRIInfo->frameBufferStride = pScreenPriv->screen->fb[0].byteStride;
	pDRIInfo->ddxDrawableTableEntry = SAREA_MAX_DRAWABLES;
	pDRIInfo->maxDrawableTableEntry = SAREA_MAX_DRAWABLES;

	/* For now the mapping works by using a fixed size defined
	 * in the SAREA header
	 */
	pDRIInfo->SAREASize = SAREA_MAX;

	if (atic->is_radeon) {
		pDRIInfo->devPrivateSize = sizeof(R128DRIRec);
		devSareaSize = sizeof(R128SAREAPriv);
	} else {
		pDRIInfo->devPrivateSize = sizeof(RADEONDRIRec);
		devSareaSize = sizeof(RADEONSAREAPriv);
	}

	if (sizeof(XF86DRISAREARec) + devSareaSize > SAREA_MAX) {
		ErrorF("[dri] Data does not fit in SAREA.  Disabling DRI.\n");
		return FALSE;
	}

	pDRIInfo->devPrivate = xcalloc(pDRIInfo->devPrivateSize, 1);
	if (pDRIInfo->devPrivate == NULL) {
		DRIDestroyInfoRec(atis->pDRIInfo);
		atis->pDRIInfo = NULL;
		return FALSE;
	}

	pDRIInfo->contextSize    = sizeof(ATIDRIContextRec);

	pDRIInfo->SwapContext    = ATIDRISwapContext;
	/*pDRIInfo->InitBuffers    = R128DRIInitBuffers;*/ /* XXX Appears unnecessary */
	/*pDRIInfo->MoveBuffers    = R128DRIMoveBuffers;*/ /* XXX Badness */
	pDRIInfo->bufferRequests = DRI_ALL_WINDOWS;
	/*pDRIInfo->TransitionTo2d = R128DRITransitionTo2d;
	pDRIInfo->TransitionTo3d = R128DRITransitionTo3d;
	pDRIInfo->TransitionSingleToMulti3D = R128DRITransitionSingleToMulti3d;
	pDRIInfo->TransitionMultiToSingle3D = R128DRITransitionMultiToSingle3d;*/

	pDRIInfo->createDummyCtx     = TRUE;
	pDRIInfo->createDummyCtxPriv = FALSE;

	if (!DRIScreenInit(pScreen, pDRIInfo, &atic->drmFd)) {
		ErrorF("[dri] DRIScreenInit failed.  Disabling DRI.\n");
		xfree(pDRIInfo->devPrivate);
		pDRIInfo->devPrivate = NULL;
		DRIDestroyInfoRec(pDRIInfo);
		pDRIInfo = NULL;
		return FALSE;
	}

	/* Add a map for the MMIO registers that will be accessed by any
	* DRI-based clients.
	*/
	atis->registerSize = RADEON_REG_SIZE(atic);
	if (drmAddMap(atic->drmFd, RADEON_REG_BASE(pScreenPriv->screen->card),
	    atis->registerSize, DRM_REGISTERS, DRM_READ_ONLY,
	    &atis->registerHandle) < 0) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}
	ErrorF("[drm] register handle = 0x%08lx\n", atis->registerHandle);

	/* DRIScreenInit adds the frame buffer map, but we need it as well */
	DRIGetDeviceInfo(pScreen, &atis->fbHandle, &scratch_int, &scratch_int,
	    &scratch_int, &scratch_int, &scratch_ptr);

	/* Initialize AGP */
	if (atis->IsAGP && !ATIDRIAgpInit(pScreen)) {
		atis->IsAGP = FALSE;
		ErrorF("[agp] AGP failed to initialize; falling back to PCI mode.\n");
		ErrorF("[agp] Make sure your kernel's AGP support is loaded and functioning.");
	}

	/* Initialize PCIGART */
	if (!atis->IsAGP && !ATIDRIPciInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

#ifdef GLXEXT
	if (!R128InitVisualConfigs(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}
	ErrorF("[dri] Visual configs initialized\n");
#endif

	atis->serverContext = DRIGetContext(pScreen);

	return ATIDRIFinishScreenInit(pScreen);
}

/* Finish initializing the device-dependent DRI state, and call
   DRIFinishScreenInit() to complete the device-independent DRI
   initialization. */
static Bool
R128DRIFinishScreenInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	R128SAREAPrivPtr pSAREAPriv;
	R128DRIPtr       pR128DRI;

	/* Initialize the kernel data structures */
	if (!R128DRIKernelInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

	/* Initialize the vertex buffers list */
	if (!ATIDRIBufInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

	/* Initialize IRQ */
	ATIDRIIrqInit(pScreen);

	/* Initialize and start the CCE if required */
	ATIDMAStart(pScreen);

	pSAREAPriv = (R128SAREAPrivPtr)DRIGetSAREAPrivate(pScreen);
	memset(pSAREAPriv, 0, sizeof(*pSAREAPriv));

	pR128DRI                    = (R128DRIPtr)atis->pDRIInfo->devPrivate;

	pR128DRI->deviceID          = pScreenPriv->screen->card->attr.deviceID;
	pR128DRI->width             = pScreenPriv->screen->width;
	pR128DRI->height            = pScreenPriv->screen->height;
	pR128DRI->depth             = pScreenPriv->screen->fb[0].depth;
	pR128DRI->bpp               = pScreenPriv->screen->fb[0].bitsPerPixel;

	pR128DRI->IsPCI             = !atis->IsAGP;
	pR128DRI->AGPMode           = atis->agpMode;

	pR128DRI->frontOffset       = atis->frontOffset;
	pR128DRI->frontPitch        = atis->frontPitch;
	pR128DRI->backOffset        = atis->backOffset;
	pR128DRI->backPitch         = atis->backPitch;
	pR128DRI->depthOffset       = atis->depthOffset;
	pR128DRI->depthPitch        = atis->depthPitch;
	pR128DRI->spanOffset        = atis->spanOffset;
	pR128DRI->textureOffset     = atis->textureOffset;
	pR128DRI->textureSize       = atis->textureSize;
	pR128DRI->log2TexGran       = atis->log2TexGran;

	pR128DRI->registerHandle    = atis->registerHandle;
	pR128DRI->registerSize      = atis->registerSize;

	pR128DRI->gartTexHandle     = atis->gartTexHandle;
	pR128DRI->gartTexMapSize    = atis->gartTexMapSize;
	pR128DRI->log2AGPTexGran    = atis->log2GARTTexGran;
	pR128DRI->gartTexOffset     = atis->gartTexStart;
	pR128DRI->sarea_priv_offset = sizeof(XF86DRISAREARec);

	return TRUE;
}

/* Finish initializing the device-dependent DRI state, and call
 * DRIFinishScreenInit() to complete the device-independent DRI
 * initialization.
 */
static Bool
RadeonDRIFinishScreenInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo(pScreenPriv);
	RADEONSAREAPrivPtr  pSAREAPriv;
	RADEONDRIPtr        pRADEONDRI;
	drmRadeonMemInitHeap drmHeap;

	/* Initialize the kernel data structures */
	if (!RadeonDRIKernelInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

	/* Initialize the vertex buffers list */
	if (!ATIDRIBufInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

	/* Initialize IRQ */
	ATIDRIIrqInit(pScreen);

	drmHeap.region = RADEON_MEM_REGION_GART;
	drmHeap.start  = 0;
	drmHeap.size   = atis->gartTexMapSize;
    
	if (drmCommandWrite(atic->drmFd, DRM_RADEON_INIT_HEAP, &drmHeap,
	    sizeof(drmHeap))) {
		ErrorF("[drm] Failed to initialize GART heap manager\n");
	}

	ATIDMAStart(pScreen);

	/* Initialize the SAREA private data structure */
	pSAREAPriv = (RADEONSAREAPrivPtr)DRIGetSAREAPrivate(pScreen);
	memset(pSAREAPriv, 0, sizeof(*pSAREAPriv));

	pRADEONDRI = (RADEONDRIPtr)atis->pDRIInfo->devPrivate;

	pRADEONDRI->deviceID          = pScreenPriv->screen->card->attr.deviceID;
	pRADEONDRI->width             = pScreenPriv->screen->width;
	pRADEONDRI->height            = pScreenPriv->screen->height;
	pRADEONDRI->depth             = pScreenPriv->screen->fb[0].depth;
	pRADEONDRI->bpp               = pScreenPriv->screen->fb[0].bitsPerPixel;

	pRADEONDRI->IsPCI             = !atis->IsAGP;
	pRADEONDRI->AGPMode           = atis->agpMode;
	
	pRADEONDRI->frontOffset       = atis->frontOffset;
	pRADEONDRI->frontPitch        = atis->frontPitch;
	pRADEONDRI->backOffset        = atis->backOffset;
	pRADEONDRI->backPitch         = atis->backPitch;
	pRADEONDRI->depthOffset       = atis->depthOffset;
	pRADEONDRI->depthPitch        = atis->depthPitch;
	pRADEONDRI->textureOffset     = atis->textureOffset;
	pRADEONDRI->textureSize       = atis->textureSize;
	pRADEONDRI->log2TexGran       = atis->log2TexGran;
	
	pRADEONDRI->registerHandle    = atis->registerHandle;
	pRADEONDRI->registerSize      = atis->registerSize;
	
	pRADEONDRI->statusHandle      = atis->ringReadPtrHandle;
	pRADEONDRI->statusSize        = atis->ringReadMapSize;
	
	pRADEONDRI->gartTexHandle     = atis->gartTexHandle;
	pRADEONDRI->gartTexMapSize    = atis->gartTexMapSize;
	pRADEONDRI->log2GARTTexGran   = atis->log2GARTTexGran;
	pRADEONDRI->gartTexOffset     = atis->gartTexStart;
	
	pRADEONDRI->sarea_priv_offset = sizeof(XF86DRISAREARec);

	return TRUE;
}

static Bool
ATIDRIFinishScreenInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	ATIScreenInfo(pScreenPriv);
	ATICardInfo (pScreenPriv);

	atis->pDRIInfo->driverSwapMethod = DRI_HIDE_X_CONTEXT;

	/* NOTE: DRIFinishScreenInit must be called before *DRIKernelInit
	 * because *DRIKernelInit requires that the hardware lock is held by
	 * the X server, and the first time the hardware lock is grabbed is
	 * in DRIFinishScreenInit.
	 */
	if (!DRIFinishScreenInit(pScreen)) {
		ATIDRICloseScreen(pScreen);
		return FALSE;
	}

	if (atic->is_radeon) {
		if (!RadeonDRIFinishScreenInit(pScreen)) {
			ATIDRICloseScreen(pScreen);
			return FALSE;
		}
	} else {
		if (!R128DRIFinishScreenInit(pScreen)) {
			ATIDRICloseScreen(pScreen);
			return FALSE;
		}
	}

	XFree86DRIExtensionInit();

	atis->using_dri = TRUE;

	return TRUE;
}

/* The screen is being closed, so clean up any state and free any
   resources used by the DRI. */
void
ATIDRICloseScreen(ScreenPtr pScreen)
{
	KdScreenPriv (pScreen);
	ATIScreenInfo (pScreenPriv);
	ATICardInfo (pScreenPriv);
	drmR128Init drmR128Info;
	drmRadeonInit drmRadeonInfo;

	if (atis->indirectBuffer != NULL) {
		ATIDMADispatchIndirect(1);
		atis->indirectBuffer = NULL;
		atis->indirectStart = 0;
	}
	ATIDMAStop(pScreen);

	if (atis->irqEnabled) {
		drmCtlUninstHandler(atic->drmFd);
		atis->irqEnabled = FALSE;
	}

	/* De-allocate vertex buffers */
	if (atis->buffers) {
		drmUnmapBufs(atis->buffers);
		atis->buffers = NULL;
	}

	/* De-allocate all kernel resources */
	if (atic->is_radeon) {
		memset(&drmR128Info, 0, sizeof(drmR128Init));
		drmR128Info.func = DRM_R128_CLEANUP_CCE;
		drmCommandWrite(atic->drmFd, DRM_R128_INIT, &drmR128Info,
		    sizeof(drmR128Init));
	} else {
		memset(&drmRadeonInfo, 0, sizeof(drmRadeonInfo));
		drmRadeonInfo.func = DRM_RADEON_CLEANUP_CP;
		drmCommandWrite(atic->drmFd, DRM_RADEON_CP_INIT, &drmRadeonInfo,
		    sizeof(drmR128Init));
	}

	/* De-allocate all AGP resources */
	if (atis->gartTex) {
		drmUnmap(atis->gartTex, atis->gartTexMapSize);
		atis->gartTex = NULL;
	}
	if (atis->buf) {
		drmUnmap(atis->buf, atis->bufMapSize);
		atis->buf = NULL;
	}
	if (atis->ringReadPtr) {
		drmUnmap(atis->ringReadPtr, atis->ringReadMapSize);
		atis->ringReadPtr = NULL;
	}
	if (atis->ring) {
		drmUnmap(atis->ring, atis->ringMapSize);
		atis->ring = NULL;
	}
	if (atis->agpMemHandle != DRM_AGP_NO_HANDLE) {
		drmAgpUnbind(atic->drmFd, atis->agpMemHandle);
		drmAgpFree(atic->drmFd, atis->agpMemHandle);
		atis->agpMemHandle = DRM_AGP_NO_HANDLE;
		drmAgpRelease(atic->drmFd);
	}
	if (atis->pciMemHandle) {
		drmScatterGatherFree(atic->drmFd, atis->pciMemHandle);
		atis->pciMemHandle = 0;
	}

	/* De-allocate all DRI resources */
	DRICloseScreen(pScreen);

	/* De-allocate all DRI data structures */
	if (atis->pDRIInfo) {
		if (atis->pDRIInfo->devPrivate) {
			xfree(atis->pDRIInfo->devPrivate);
			atis->pDRIInfo->devPrivate = NULL;
		}
		DRIDestroyInfoRec(atis->pDRIInfo);
		atis->pDRIInfo = NULL;
	}
	atis->using_dri = FALSE;
#ifdef GLXEXT
	if (atis->pVisualConfigs) {
		xfree(atis->pVisualConfigs);
		atis->pVisualConfigs = NULL;
	}
	if (atis->pVisualConfigsPriv) {
		xfree(atis->pVisualConfigsPriv);
		atis->pVisualConfigsPriv = NULL;
	}
#endif /* GLXEXT */
	atic->drmFd = -1;
}
