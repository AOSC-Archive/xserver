/*
 * Copyright © 2006 Red Hat, Inc
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat,
 * Inc not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior
 * permission.  Red Hat, Inc makes no representations about the
 * suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * RED HAT, INC DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL RED HAT, INC BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <dlfcn.h>

#include <drm.h>
#include <GL/gl.h>
#include <GL/internal/dri_interface.h>

#include <windowstr.h>
#include <os.h>

#define _XF86DRI_SERVER_
#include <drm_sarea.h>
#include <xf86drm.h>
#include <xf86dristr.h>
#include <xf86str.h>
#include <xf86.h>
#include <dri.h>

#define DRI_NEW_INTERFACE_ONLY
#include "glxserver.h"
#include "glxutil.h"
#include "glcontextmodes.h"

#include "g_disptab.h"
#include "glapitable.h"
#include "glapi.h"
#include "glthread.h"
#include "dispatch.h"
#include "extension_string.h"

#define containerOf(ptr, type, member)			\
    (type *)( (char *)ptr - offsetof(type,member) )

typedef struct __GLXDRIscreen   __GLXDRIscreen;
typedef struct __GLXDRIcontext  __GLXDRIcontext;
typedef struct __GLXDRIdrawable __GLXDRIdrawable;

struct __GLXDRIscreen {
    __GLXscreen		 base;
    __DRIscreen		 driScreen;
    void		*driver;

    xf86EnterVTProc	*enterVT;
    xf86LeaveVTProc	*leaveVT;

    __DRIcopySubBufferExtension *copySubBuffer;
    __DRIswapControlExtension *swapControl;

#ifdef __DRI_TEX_OFFSET
    __DRItexOffsetExtension *texOffset;
    DRITexOffsetStartProcPtr texOffsetStart;
    DRITexOffsetFinishProcPtr texOffsetFinish;
    __GLXDRIdrawable *texOffsetOverride[16];
    GLuint lastTexOffsetOverride;
#endif

    unsigned char glx_enable_bits[__GLX_EXT_BYTES];
};

struct __GLXDRIcontext {
    __GLXcontext base;
    __DRIcontext driContext;
    XID hwContextID;
};

struct __GLXDRIdrawable {
    __GLXdrawable base;
    __DRIdrawable driDrawable;

    /* Pulled in from old __GLXpixmap */
#ifdef __DRI_TEX_OFFSET
    GLint texname;
    __GLXDRIcontext *ctx;
    unsigned long offset;
    DamagePtr pDamage;
#endif
};

static const char CREATE_NEW_SCREEN_FUNC[] = __DRI_CREATE_NEW_SCREEN_STRING;

static void
__glXDRIleaveServer(GLboolean rendering)
{
    int i;

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
	__GLXDRIscreen * const screen =
	    (__GLXDRIscreen *) glxGetScreen(screenInfo.screens[i]);
	GLuint lastOverride = screen->lastTexOffsetOverride;

	if (lastOverride) {
	    __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
	    int j;

	    for (j = 0; j < lastOverride; j++) {
		__GLXDRIdrawable *pGlxPix = texOffsetOverride[j];

		if (pGlxPix && pGlxPix->texname) {
		    pGlxPix->offset =
			screen->texOffsetStart((PixmapPtr)pGlxPix->base.pDraw);
		}
	    }
	}
    }

    DRIBlockHandler(NULL, NULL, NULL);

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
	__GLXDRIscreen * const screen =
	    (__GLXDRIscreen *) glxGetScreen(screenInfo.screens[i]);
	GLuint lastOverride = screen->lastTexOffsetOverride;

	if (lastOverride) {
	    __GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
	    int j;

	    for (j = 0; j < lastOverride; j++) {
		__GLXDRIdrawable *pGlxPix = texOffsetOverride[j];

		if (pGlxPix && pGlxPix->texname) {
		    screen->texOffset->setTexOffset(&pGlxPix->ctx->driContext,
						    pGlxPix->texname,
						    pGlxPix->offset,
						    pGlxPix->base.pDraw->depth,
						    ((PixmapPtr)pGlxPix->base.pDraw)->devKind);
		}
	    }
	}
    }
}
    
static void
__glXDRIenterServer(GLboolean rendering)
{
    int i;

    for (i = 0; rendering && i < screenInfo.numScreens; i++) {
	__GLXDRIscreen * const screen = (__GLXDRIscreen *)
	    glxGetScreen(screenInfo.screens[i]);

	if (screen->lastTexOffsetOverride) {
	    CALL_Flush(GET_DISPATCH(), ());
	    break;
	}
    }

    DRIWakeupHandler(NULL, 0, NULL);
}

static void
__glXDRIdrawableDestroy(__GLXdrawable *drawable)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) drawable;

    (*private->driDrawable.destroyDrawable)(&private->driDrawable);

    __glXenterServer(GL_FALSE);
    DRIDestroyDrawable(drawable->pDraw->pScreen,
		       serverClient, drawable->pDraw);
    __glXleaveServer(GL_FALSE);

    xfree(private);
}

static GLboolean
__glXDRIdrawableResize(__GLXdrawable *glxPriv)
{
    /* Nothing to do here, the DRI driver asks the server for drawable
     * geometry when it sess the SAREA timestamps change.*/

    return GL_TRUE;
}

static GLboolean
__glXDRIdrawableSwapBuffers(__GLXdrawable *basePrivate)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) basePrivate;

    (*private->driDrawable.swapBuffers)(&private->driDrawable);

    return TRUE;
}


static int
__glXDRIdrawableSwapInterval(__GLXdrawable *baseDrawable, int interval)
{
    __GLXDRIdrawable *draw = (__GLXDRIdrawable *) baseDrawable;
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
	glxGetScreen(baseDrawable->pDraw->pScreen);

    if (screen->swapControl)
	screen->swapControl->setSwapInterval(&draw->driDrawable, interval);

    return 0;
}


static void
__glXDRIdrawableCopySubBuffer(__GLXdrawable *basePrivate,
			       int x, int y, int w, int h)
{
    __GLXDRIdrawable *private = (__GLXDRIdrawable *) basePrivate;
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
	glxGetScreen(basePrivate->pDraw->pScreen);

    if (screen->copySubBuffer)
	screen->copySubBuffer->copySubBuffer(&private->driDrawable,
					     x, y, w, h);
}

static void
__glXDRIcontextDestroy(__GLXcontext *baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    Bool retval;

    context->driContext.destroyContext(&context->driContext);

    __glXenterServer(GL_FALSE);
    retval = DRIDestroyContext(baseContext->pScreen, context->hwContextID);
    __glXleaveServer(GL_FALSE);

    __glXContextDestroy(&context->base);
    xfree(context);
}

static int
__glXDRIcontextMakeCurrent(__GLXcontext *baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    __GLXDRIdrawable *draw = (__GLXDRIdrawable *) baseContext->drawPriv;
    __GLXDRIdrawable *read = (__GLXDRIdrawable *) baseContext->readPriv;

    return (*context->driContext.bindContext)(&context->driContext,
					      &draw->driDrawable,
					      &read->driDrawable);
}					      

static int
__glXDRIcontextLoseCurrent(__GLXcontext *baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;

    return (*context->driContext.unbindContext)(&context->driContext);
}

static int
__glXDRIcontextCopy(__GLXcontext *baseDst, __GLXcontext *baseSrc,
		    unsigned long mask)
{
    __GLXDRIcontext *dst = (__GLXDRIcontext *) baseDst;
    __GLXDRIcontext *src = (__GLXDRIcontext *) baseSrc;

    /* FIXME: We will need to add DRIcontext::copyContext for this. */

    (void) dst;
    (void) src;

    return FALSE;
}

static int
__glXDRIcontextForceCurrent(__GLXcontext *baseContext)
{
    __GLXDRIcontext *context = (__GLXDRIcontext *) baseContext;
    __GLXDRIdrawable *draw = (__GLXDRIdrawable *) baseContext->drawPriv;
    __GLXDRIdrawable *read = (__GLXDRIdrawable *) baseContext->readPriv;

    return (*context->driContext.bindContext)(&context->driContext,
					      &draw->driDrawable,
					      &read->driDrawable);
}

static void
glxFillAlphaChannel (PixmapPtr pixmap, int x, int y, int width, int height)
{
    int i;
    CARD32 *p, *end, *pixels = (CARD32 *)pixmap->devPrivate.ptr;
    CARD32 rowstride = pixmap->devKind / 4;
    
    for (i = y; i < y + height; i++)
    {
	p = &pixels[i * rowstride + x];
	end = p + width;
	while (p < end)
	  *p++ |= 0xFF000000;
    }
}

/*
 * (sticking this here for lack of a better place)
 * Known issues with the GLX_EXT_texture_from_pixmap implementation:
 * - In general we ignore the fbconfig, lots of examples follow
 * - No fbconfig handling for multiple mipmap levels
 * - No fbconfig handling for 1D textures
 * - No fbconfig handling for TEXTURE_TARGET
 * - No fbconfig exposure of Y inversion state
 * - No GenerateMipmapEXT support (due to no FBO support)
 * - No support for anything but 16bpp and 32bpp-sparse pixmaps
 */

static int
__glXDRIbindTexImage(__GLXcontext *baseContext,
		     int buffer,
		     __GLXdrawable *glxPixmap)
{
    RegionPtr	pRegion = NULL;
    PixmapPtr	pixmap;
    int		bpp, override = 0, texname;
    GLenum	format, type;
    ScreenPtr pScreen = glxPixmap->pDraw->pScreen;
    __GLXDRIdrawable *driDraw =
	    containerOf(glxPixmap, __GLXDRIdrawable, base);
    __GLXDRIscreen * const screen =
	    (__GLXDRIscreen *) glxGetScreen(pScreen);

    CALL_GetIntegerv(GET_DISPATCH(), (glxPixmap->target == GL_TEXTURE_2D ?
				      GL_TEXTURE_BINDING_2D :
				      GL_TEXTURE_BINDING_RECTANGLE_NV,
				      &texname));

    if (!texname)
	return __glXError(GLXBadContextState);

    pixmap = (PixmapPtr) glxPixmap->pDraw;

    if (screen->texOffsetStart && screen->texOffset) {
	__GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
	int i, firstEmpty = 16;

	for (i = 0; i < 16; i++) {
	    if (texOffsetOverride[i] == driDraw)
		goto alreadyin; 

	    if (firstEmpty == 16 && !texOffsetOverride[i])
		firstEmpty = i;
	}

	if (firstEmpty == 16) {
	    ErrorF("%s: Failed to register texture offset override\n", __func__);
	    goto nooverride;
	}

	if (firstEmpty >= screen->lastTexOffsetOverride)
	    screen->lastTexOffsetOverride = firstEmpty + 1;

	texOffsetOverride[firstEmpty] = driDraw;

alreadyin:
	override = 1;

	driDraw->ctx = (__GLXDRIcontext*)baseContext;

	if (texname == driDraw->texname)
	    return Success;

	driDraw->texname = texname;

	screen->texOffset->setTexOffset(&driDraw->ctx->driContext, texname, 0,
					pixmap->drawable.depth,
					pixmap->devKind);
    }
nooverride:

    if (!driDraw->pDamage) {
	if (!override) {
	    driDraw->pDamage = DamageCreate(NULL, NULL, DamageReportNone,
					    TRUE, pScreen, NULL);
	    if (!driDraw->pDamage)
		return BadAlloc;

	    DamageRegister ((DrawablePtr) pixmap, driDraw->pDamage);
	}

	pRegion = NULL;
    } else {
	pRegion = DamageRegion(driDraw->pDamage);
	if (REGION_NIL(pRegion))
	    return Success;
    }

    /* XXX 24bpp packed, 8, etc */
    if (pixmap->drawable.depth >= 24) {
	bpp = 4;
	format = GL_BGRA;
	type =
#if X_BYTE_ORDER == X_BIG_ENDIAN
	    !override ? GL_UNSIGNED_INT_8_8_8_8_REV :
#endif
	    GL_UNSIGNED_BYTE;
    } else {
	bpp = 2;
	format = GL_RGB;
	type = GL_UNSIGNED_SHORT_5_6_5;
    }

    CALL_PixelStorei( GET_DISPATCH(), (GL_UNPACK_ROW_LENGTH,
				       pixmap->devKind / bpp) );

    if (pRegion == NULL)
    {
	if (!override && pixmap->drawable.depth == 24)
	    glxFillAlphaChannel(pixmap,
				pixmap->drawable.x,
				pixmap->drawable.y,
				pixmap->drawable.width,
				pixmap->drawable.height);

        CALL_PixelStorei( GET_DISPATCH(), (GL_UNPACK_SKIP_PIXELS,
					   pixmap->drawable.x) );
	CALL_PixelStorei( GET_DISPATCH(), (GL_UNPACK_SKIP_ROWS,
					   pixmap->drawable.y) );

	CALL_TexImage2D( GET_DISPATCH(),
			 (glxPixmap->target,
			  0,
			  bpp == 4 ? 4 : 3,
			  pixmap->drawable.width,
			  pixmap->drawable.height,
			  0,
			  format,
			  type,
			  override ? NULL : pixmap->devPrivate.ptr) );
    } else if (!override) {
        int i, numRects;
	BoxPtr p;

	numRects = REGION_NUM_RECTS (pRegion);
	p = REGION_RECTS (pRegion);
	for (i = 0; i < numRects; i++)
	{
	    if (pixmap->drawable.depth == 24)
		glxFillAlphaChannel(pixmap,
				    pixmap->drawable.x + p[i].x1,
				    pixmap->drawable.y + p[i].y1,
				    p[i].x2 - p[i].x1,
				    p[i].y2 - p[i].y1);

	    CALL_PixelStorei( GET_DISPATCH(), (GL_UNPACK_SKIP_PIXELS,
					       pixmap->drawable.x + p[i].x1) );
	    CALL_PixelStorei( GET_DISPATCH(), (GL_UNPACK_SKIP_ROWS,
					       pixmap->drawable.y + p[i].y1) );

	    CALL_TexSubImage2D( GET_DISPATCH(),
				(glxPixmap->target,
				 0,
				 p[i].x1, p[i].y1,
				 p[i].x2 - p[i].x1, p[i].y2 - p[i].y1,
				 format,
				 type,
				 pixmap->devPrivate.ptr) );
	}
    }

    if (!override)
	DamageEmpty(driDraw->pDamage);

    return Success;
}

static int
__glXDRIreleaseTexImage(__GLXcontext *baseContext,
			int buffer,
			__GLXdrawable *pixmap)
{
    ScreenPtr pScreen = pixmap->pDraw->pScreen;
    __GLXDRIdrawable *driDraw =
	    containerOf(pixmap, __GLXDRIdrawable, base);
    __GLXDRIscreen * const screen =
	(__GLXDRIscreen *) glxGetScreen(pScreen);
    GLuint lastOverride = screen->lastTexOffsetOverride;

    if (lastOverride) {
	__GLXDRIdrawable **texOffsetOverride = screen->texOffsetOverride;
	int i;

	for (i = 0; i < lastOverride; i++) {
	    if (texOffsetOverride[i] == driDraw) {
		if (screen->texOffsetFinish)
		    screen->texOffsetFinish((PixmapPtr)pixmap->pDraw);

		texOffsetOverride[i] = NULL;

		if (i + 1 == lastOverride) {
		    lastOverride = 0;

		    while (i--) {
			if (texOffsetOverride[i]) {
			    lastOverride = i + 1;
			    break;
			}
		    }

		    screen->lastTexOffsetOverride = lastOverride;

		    break;
		}
	    }
	}
    }

    return Success;
}

static __GLXtextureFromPixmap __glXDRItextureFromPixmap = {
    __glXDRIbindTexImage,
    __glXDRIreleaseTexImage
};

static void
__glXDRIscreenDestroy(__GLXscreen *baseScreen)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *) baseScreen;

    screen->driScreen.destroyScreen(&screen->driScreen);

    dlclose(screen->driver);

    __glXScreenDestroy(baseScreen);

    xfree(screen);
}

static __GLXcontext *
__glXDRIscreenCreateContext(__GLXscreen *baseScreen,
			    __GLcontextModes *modes,
			    __GLXcontext *baseShareContext)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *) baseScreen;
    __GLXDRIcontext *context, *shareContext;
    VisualPtr visual;
    int i;
    GLboolean retval;
    __DRIcontext *driShare;
    drm_context_t hwContext;
    ScreenPtr pScreen = baseScreen->pScreen;

    shareContext = (__GLXDRIcontext *) baseShareContext;
    if (shareContext)
	driShare = &shareContext->driContext;
    else
	driShare = NULL;

    context = xalloc(sizeof *context);
    if (context == NULL)
	return NULL;

    memset(context, 0, sizeof *context);
    context->base.destroy           = __glXDRIcontextDestroy;
    context->base.makeCurrent       = __glXDRIcontextMakeCurrent;
    context->base.loseCurrent       = __glXDRIcontextLoseCurrent;
    context->base.copy              = __glXDRIcontextCopy;
    context->base.forceCurrent      = __glXDRIcontextForceCurrent;
    context->base.pScreen           = screen->base.pScreen;

    context->base.textureFromPixmap = &__glXDRItextureFromPixmap;
    /* Find the requested X visual */
    visual = pScreen->visuals;
    for (i = 0; i < pScreen->numVisuals; i++, visual++)
	if (visual->vid == modes->visualID)
	    break;
    if (i == pScreen->numVisuals)
	return GL_FALSE;

    context->hwContextID = FakeClientID(0);

    __glXenterServer(GL_FALSE);
    retval = DRICreateContext(baseScreen->pScreen, visual,
			      context->hwContextID, &hwContext);
    __glXleaveServer(GL_FALSE);

    context->driContext.private =
	screen->driScreen.createNewContext(&screen->driScreen,
					   modes,
					   0, /* render type */
					   driShare,
					   hwContext,
					   &context->driContext);

    return &context->base;
}

static __GLXdrawable *
__glXDRIscreenCreateDrawable(__GLXscreen *screen,
			     DrawablePtr pDraw,
			     int type,
			     XID drawId,
			     __GLcontextModes *modes)
{
    __GLXDRIscreen *driScreen = (__GLXDRIscreen *) screen;
    __GLXDRIdrawable *private;
    GLboolean retval;
    drm_drawable_t hwDrawable;

    private = xalloc(sizeof *private);
    if (private == NULL)
	return NULL;

    memset(private, 0, sizeof *private);

    if (!__glXDrawableInit(&private->base, screen,
			   pDraw, type, drawId, modes)) {
        xfree(private);
	return NULL;
    }

    private->base.destroy       = __glXDRIdrawableDestroy;
    private->base.resize        = __glXDRIdrawableResize;
    private->base.swapBuffers   = __glXDRIdrawableSwapBuffers;
    private->base.copySubBuffer = __glXDRIdrawableCopySubBuffer;

    __glXenterServer(GL_FALSE);
    retval = DRICreateDrawable(screen->pScreen, serverClient,
			       pDraw, &hwDrawable);
    __glXleaveServer(GL_FALSE);

    /* The last argument is 'attrs', which is used with pbuffers which
     * we currently don't support. */

    private->driDrawable.private =
	(driScreen->driScreen.createNewDrawable)(&driScreen->driScreen,
						 modes,
						 &private->driDrawable,
						 hwDrawable, 0, NULL);

    return &private->base;
}

static GLboolean
getDrawableInfo(__DRIdrawable *driDrawable,
		unsigned int *index, unsigned int *stamp,
		int *x, int *y, int *width, int *height,
		int *numClipRects, drm_clip_rect_t **ppClipRects,
		int *backX, int *backY,
		int *numBackClipRects, drm_clip_rect_t **ppBackClipRects)
{
    __GLXDRIdrawable *drawable = containerOf(driDrawable,
					     __GLXDRIdrawable, driDrawable);
    ScreenPtr pScreen = drawable->base.pDraw->pScreen;
    drm_clip_rect_t *pClipRects, *pBackClipRects;
    GLboolean retval;
    size_t size;

    __glXenterServer(GL_FALSE);
    retval = DRIGetDrawableInfo(pScreen, drawable->base.pDraw, index, stamp,
				x, y, width, height,
				numClipRects, &pClipRects,
				backX, backY,
				numBackClipRects, &pBackClipRects);
    __glXleaveServer(GL_FALSE);

    if (*numClipRects > 0) {
	size = sizeof (drm_clip_rect_t) * *numClipRects;
	*ppClipRects = xalloc (size);

	/* Clip cliprects to screen dimensions (redirected windows) */
	if (*ppClipRects != NULL) {
	    int i, j;

	    for (i = 0, j = 0; i < *numClipRects; i++) {
	        (*ppClipRects)[j].x1 = max(pClipRects[i].x1, 0);
		(*ppClipRects)[j].y1 = max(pClipRects[i].y1, 0);
		(*ppClipRects)[j].x2 = min(pClipRects[i].x2, pScreen->width);
		(*ppClipRects)[j].y2 = min(pClipRects[i].y2, pScreen->height);

		if ((*ppClipRects)[j].x1 < (*ppClipRects)[j].x2 &&
		    (*ppClipRects)[j].y1 < (*ppClipRects)[j].y2) {
		    j++;
		}
	    }

	    if (*numClipRects != j) {
		*numClipRects = j;
		*ppClipRects = xrealloc (*ppClipRects,
					 sizeof (drm_clip_rect_t) *
					 *numClipRects);
	    }
	} else
	    *numClipRects = 0;
    }
    else {
      *ppClipRects = NULL;
    }
      
    if (*numBackClipRects > 0) {
	size = sizeof (drm_clip_rect_t) * *numBackClipRects;
	*ppBackClipRects = xalloc (size);
	if (*ppBackClipRects != NULL)
	    memcpy (*ppBackClipRects, pBackClipRects, size);
    }
    else {
      *ppBackClipRects = NULL;
    }

    return retval;
}

static int
getUST(int64_t *ust)
{
    struct timeval  tv;
    
    if (ust == NULL)
	return -EFAULT;

    if (gettimeofday(&tv, NULL) == 0) {
	ust[0] = (tv.tv_sec * 1000000) + tv.tv_usec;
	return 0;
    } else {
	return -errno;
    }
}

static void __glXReportDamage(__DRIdrawable *driDraw,
			      int x, int y,
			      drm_clip_rect_t *rects, int num_rects,
			      GLboolean front_buffer)
{
    __GLXDRIdrawable *drawable =
	    containerOf(driDraw, __GLXDRIdrawable, driDrawable);
    DrawablePtr pDraw = drawable->base.pDraw;
    RegionRec region;

    REGION_INIT(pDraw->pScreen, &region, (BoxPtr) rects, num_rects);
    REGION_TRANSLATE(pScreen, &region, pDraw->x, pDraw->y);
    DamageDamageRegion(pDraw, &region);
    REGION_UNINIT(pDraw->pScreen, &region);
}

/* Table of functions that we export to the driver. */
static const __DRIinterfaceMethods interface_methods = {
    _gl_context_modes_create,
    _gl_context_modes_destroy,

    getDrawableInfo,

    getUST,
    NULL, /* glXGetMscRateOML, */

    __glXReportDamage,
};

static const char dri_driver_path[] = DRI_DRIVER_PATH;

static Bool
glxDRIEnterVT (int index, int flags)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *) 
	glxGetScreen(screenInfo.screens[index]);

    LogMessage(X_INFO, "AIGLX: Resuming AIGLX clients after VT switch\n");

    if (!(*screen->enterVT) (index, flags))
	return FALSE;
    
    glxResumeClients();

    return TRUE;
}

static void
glxDRILeaveVT (int index, int flags)
{
    __GLXDRIscreen *screen = (__GLXDRIscreen *)
	glxGetScreen(screenInfo.screens[index]);

    LogMessage(X_INFO, "AIGLX: Suspending AIGLX clients for VT switch\n");

    glxSuspendClients();

    return (*screen->leaveVT) (index, flags);
}

static void
initializeExtensions(__GLXDRIscreen *screen)
{
    const __DRIextension **extensions;
    int i;

    extensions = screen->driScreen.getExtensions(&screen->driScreen);
    for (i = 0; extensions[i]; i++) {
#ifdef __DRI_COPY_SUB_BUFFER
	if (strcmp(extensions[i]->name, __DRI_COPY_SUB_BUFFER) == 0) {
	    screen->copySubBuffer = (__DRIcopySubBufferExtension *) extensions[i];
	    __glXEnableExtension(screen->glx_enable_bits,
				 "GLX_MESA_copy_sub_buffer");
	    
	    LogMessage(X_INFO, "AIGLX: enabled GLX_MESA_copy_sub_buffer\n");
	}
#endif

#ifdef __DRI_SWAP_CONTROL
	if (strcmp(extensions[i]->name, __DRI_SWAP_CONTROL) == 0) {
	    screen->swapControl = (__DRIswapControlExtension *) extensions[i];
	    __glXEnableExtension(screen->glx_enable_bits,
				 "GLX_SGI_swap_control");
	    __glXEnableExtension(screen->glx_enable_bits,
				 "GLX_MESA_swap_control");
	    
	    LogMessage(X_INFO, "AIGLX: enabled GLX_SGI_swap_control and GLX_MESA_swap_control\n");
	}
#endif

#ifdef __DRI_TEX_OFFSET
	if (strcmp(extensions[i]->name, __DRI_TEX_OFFSET) == 0) {
	    screen->texOffset = (__DRItexOffsetExtension *) extensions[i];
	    LogMessage(X_INFO, "AIGLX: enabled GLX_texture_from_pixmap with driver support\n");
	}
#endif
	/* Ignore unknown extensions */
    }
}
    
#define COPY_SUB_BUFFER_INTERNAL_VERSION 20060314

static __GLXscreen *
__glXDRIscreenProbe(ScreenPtr pScreen)
{
    PFNCREATENEWSCREENFUNC createNewScreen;
    drm_handle_t hSAREA;
    drmAddress pSAREA = NULL;
    char *BusID;
    __DRIversion   ddx_version;
    __DRIversion   dri_version;
    __DRIversion   drm_version;
    __DRIframebuffer  framebuffer;
    int   fd = -1;
    int   status;
    int api_ver = 20070121;
    drm_magic_t magic;
    drmVersionPtr version;
    int newlyopened;
    char *driverName;
    drm_handle_t  hFB;
    int        junk;
    __GLXDRIscreen *screen;
    void *dev_priv = NULL;
    char filename[128];
    Bool isCapable;
    size_t buffer_size;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if (!xf86LoaderCheckSymbol("DRIQueryDirectRenderingCapable") ||
	!DRIQueryDirectRenderingCapable(pScreen, &isCapable) ||
	!isCapable) {
	LogMessage(X_INFO,
		   "AIGLX: Screen %d is not DRI capable\n", pScreen->myNum);
	return NULL;
    }

    screen = xalloc(sizeof *screen);
    if (screen == NULL)
      return NULL;
    memset(screen, 0, sizeof *screen);

    screen->base.destroy        = __glXDRIscreenDestroy;
    screen->base.createContext  = __glXDRIscreenCreateContext;
    screen->base.createDrawable = __glXDRIscreenCreateDrawable;
    screen->base.swapInterval   = __glXDRIdrawableSwapInterval;
    screen->base.pScreen       = pScreen;

    __glXInitExtensionEnableBits(screen->glx_enable_bits);

    /* DRI protocol version. */
    dri_version.major = XF86DRI_MAJOR_VERSION;
    dri_version.minor = XF86DRI_MINOR_VERSION;
    dri_version.patch = XF86DRI_PATCH_VERSION;

    framebuffer.base = NULL;
    framebuffer.dev_priv = NULL;

    if (!DRIOpenConnection(pScreen, &hSAREA, &BusID)) {
	LogMessage(X_ERROR, "AIGLX error: DRIOpenConnection failed\n");
	goto handle_error;
    }

    fd = drmOpenOnce(NULL, BusID, &newlyopened);

    if (fd < 0) {
	LogMessage(X_ERROR, "AIGLX error: drmOpenOnce failed (%s)\n",
		   strerror(-fd));
	goto handle_error;
    }

    if (drmGetMagic(fd, &magic)) {
	LogMessage(X_ERROR, "AIGLX error: drmGetMagic failed\n");
	goto handle_error;
    }

    version = drmGetVersion(fd);
    if (version) {
	drm_version.major = version->version_major;
	drm_version.minor = version->version_minor;
	drm_version.patch = version->version_patchlevel;
	drmFreeVersion(version);
    }
    else {
	drm_version.major = -1;
	drm_version.minor = -1;
	drm_version.patch = -1;
    }

    if (newlyopened && !DRIAuthConnection(pScreen, magic)) {
	LogMessage(X_ERROR, "AIGLX error: DRIAuthConnection failed\n");
	goto handle_error;
    }

    /* Get device name (like "tdfx") and the ddx version numbers.
     * We'll check the version in each DRI driver's "createNewScreen"
     * function. */
    if (!DRIGetClientDriverName(pScreen,
				&ddx_version.major,
				&ddx_version.minor,
				&ddx_version.patch,
				&driverName)) {
	LogMessage(X_ERROR, "AIGLX error: DRIGetClientDriverName failed\n");
	goto handle_error;
    }

    snprintf(filename, sizeof filename, "%s/%s_dri.so",
             dri_driver_path, driverName);

    screen->driver = dlopen(filename, RTLD_LAZY | RTLD_LOCAL);
    if (screen->driver == NULL) {
	LogMessage(X_ERROR, "AIGLX error: dlopen of %s failed (%s)\n",
		   filename, dlerror());
        goto handle_error;
    }

    createNewScreen = dlsym(screen->driver, CREATE_NEW_SCREEN_FUNC);
    if (createNewScreen == NULL) {
	LogMessage(X_ERROR, "AIGLX error: dlsym for %s failed (%s)\n",
		   CREATE_NEW_SCREEN_FUNC, dlerror());
      goto handle_error;
    }

    /*
     * Get device-specific info.  pDevPriv will point to a struct
     * (such as DRIRADEONRec in xfree86/driver/ati/radeon_dri.h) that
     * has information about the screen size, depth, pitch, ancilliary
     * buffers, DRM mmap handles, etc.
     */
    if (!DRIGetDeviceInfo(pScreen, &hFB, &junk,
			  &framebuffer.size, &framebuffer.stride,
			  &framebuffer.dev_priv_size, &framebuffer.dev_priv)) {
	LogMessage(X_ERROR, "AIGLX error: XF86DRIGetDeviceInfo failed");
	goto handle_error;
    }

    /* Sigh... the DRI interface is broken; the DRI driver will free
     * the dev private pointer using _mesa_free() on screen destroy,
     * but we can't use _mesa_malloc() here.  In fact, the DRI driver
     * shouldn't free data it didn't allocate itself, but what can you
     * do... */
    dev_priv = xalloc(framebuffer.dev_priv_size);
    if (dev_priv == NULL) {
	LogMessage(X_ERROR, "AIGLX error: dev_priv allocation failed");
	goto handle_error;
    }
    memcpy(dev_priv, framebuffer.dev_priv, framebuffer.dev_priv_size);
    framebuffer.dev_priv = dev_priv;

    framebuffer.width = pScreen->width;
    framebuffer.height = pScreen->height;

    /* Map the framebuffer region. */
    status = drmMap(fd, hFB, framebuffer.size, 
		    (drmAddressPtr)&framebuffer.base);
    if (status != 0) {
	LogMessage(X_ERROR, "AIGLX error: drmMap of framebuffer failed (%s)",
		   strerror(-status));
	goto handle_error;
    }

    /* Map the SAREA region.  Further mmap regions may be setup in
     * each DRI driver's "createNewScreen" function.
     */
    status = drmMap(fd, hSAREA, SAREA_MAX, &pSAREA);
    if (status != 0) {
	LogMessage(X_ERROR, "AIGLX error: drmMap of SAREA failed (%s)",
		   strerror(-status));
	goto handle_error;
    }
    
    screen->driScreen.private =
	(*createNewScreen)(pScreen->myNum,
			   &screen->driScreen,
			   &ddx_version,
			   &dri_version,
			   &drm_version,
			   &framebuffer,
			   pSAREA,
			   fd,
			   api_ver,
			   &interface_methods,
			   &screen->base.fbconfigs);

    if (screen->driScreen.private == NULL) {
	LogMessage(X_ERROR, "AIGLX error: Calling driver entry point failed");
	goto handle_error;
    }

    DRIGetTexOffsetFuncs(pScreen, &screen->texOffsetStart,
			 &screen->texOffsetFinish);

    initializeExtensions(screen);

    __glXScreenInit(&screen->base, pScreen);

    buffer_size = __glXGetExtensionString(screen->glx_enable_bits, NULL);
    if (buffer_size > 0) {
	if (screen->base.GLXextensions != NULL) {
	    xfree(screen->base.GLXextensions);
	}

	screen->base.GLXextensions = xnfalloc(buffer_size);
	(void) __glXGetExtensionString(screen->glx_enable_bits, 
				       screen->base.GLXextensions);
    }

    __glXsetEnterLeaveServerFuncs(__glXDRIenterServer, __glXDRIleaveServer);

    screen->enterVT = pScrn->EnterVT;
    pScrn->EnterVT = glxDRIEnterVT; 
    screen->leaveVT = pScrn->LeaveVT;
    pScrn->LeaveVT = glxDRILeaveVT;

    LogMessage(X_INFO,
	       "AIGLX: Loaded and initialized %s\n", filename);

    return &screen->base;

 handle_error:
    if (pSAREA != NULL)
	drmUnmap(pSAREA, SAREA_MAX);

    if (framebuffer.base != NULL)
	drmUnmap((drmAddress)framebuffer.base, framebuffer.size);

    if (dev_priv != NULL)
	xfree(dev_priv);

    if (fd >= 0)
	drmCloseOnce(fd);

    DRICloseConnection(pScreen);

    if (screen->driver)
        dlclose(screen->driver);

    xfree(screen);

    LogMessage(X_ERROR, "AIGLX: reverting to software rendering\n");

    return NULL;
}

__GLXprovider __glXDRIProvider = {
    __glXDRIscreenProbe,
    "DRI",
    NULL
};
