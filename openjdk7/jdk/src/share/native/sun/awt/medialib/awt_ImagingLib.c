/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sun_awt_image_ImagingLib.h"
#include "java_awt_Transparency.h"
#include "java_awt_image_AffineTransformOp.h"
#include "java_awt_image_BufferedImage.h"
#include "java_awt_color_ColorSpace.h"
#include "java_awt_image_ConvolveOp.h"
#include "sun_awt_image_IntegerComponentRaster.h"
#include "awt_ImagingLib.h"
#include "awt_parseImage.h"
#include "imageInitIDs.h"
#include <jni.h>
#include <jni_util.h>
#include <assert.h>
#include "awt_Mlib.h"
#include "gdefs.h"
#include "safe_alloc.h"

/***************************************************************************
 *                               Definitions                               *
 ***************************************************************************/
#define jio_fprintf fprintf

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */

#define TYPE_CUSTOM         java_awt_image_BufferedImage_TYPE_CUSTOM
#define TYPE_INT_RGB        java_awt_image_BufferedImage_TYPE_INT_RGB
#define TYPE_INT_ARGB       java_awt_image_BufferedImage_TYPE_INT_ARGB
#define TYPE_INT_ARGB_PRE   java_awt_image_BufferedImage_TYPE_INT_ARGB_PRE
#define TYPE_INT_BGR        java_awt_image_BufferedImage_TYPE_INT_BGR
#define TYPE_4BYTE_ABGR     java_awt_image_BufferedImage_TYPE_4BYTE_ABGR
#define TYPE_4BYTE_ABGR_PRE java_awt_image_BufferedImage_TYPE_4BYTE_ABGR_PRE

/* (alpha*color)>>nbits + alpha>>(nbits-1) */
#define BLEND(color, alpha, alphaNbits) \
    ((((alpha)*(color))>>(alphaNbits)) + ((alpha) >> ((alphaNbits)-1)))

    /* ((color - (alpha>>(nBits-1)))<<nBits)/alpha */
#define UNBLEND(color, alpha, alphaNbits) \
    ((((color)-((alpha)>>((alphaNbits)-1)))<<(alphaNbits))/(alpha))

/* Enumeration of all of the mlib functions used */
typedef enum {
    MLIB_CONVMxN,
    MLIB_AFFINE,
    MLIB_LOOKUP,
    MLIB_CONVKERNCVT
} mlibTypeE_t;

typedef struct {
    int dataType;           /* One of BYTE_DATA_TYPE, SHORT_DATA_TYPE, */
    int needToCopy;
    int cvtSrcToDefault;    /* If TRUE, convert the src to def CM (pre?) */
    int allocDefaultDst;    /* If TRUE, alloc def CM dst buffer */
    int cvtToDst;           /* If TRUE, convert dst buffer to Dst CM */
    int addAlpha;
} mlibHintS_t;

/***************************************************************************
 *                     Static Variables/Structures                         *
 ***************************************************************************/

static mlibSysFnS_t sMlibSysFns = {
    NULL, // placeholder for j2d_mlib_ImageCreate
    NULL, // placeholder for j2d_mlib_ImageCreateStruct
    NULL, // placeholder for j2d_mlib_ImageDelete
};

static mlibFnS_t sMlibFns[] = {
    {NULL, "j2d_mlib_ImageConvMxN"},
    {NULL, "j2d_mlib_ImageAffine"},
    {NULL, "j2d_mlib_ImageLookUp"},
    {NULL, "j2d_mlib_ImageConvKernelConvert"},
    {NULL, NULL},
};

static int s_timeIt = 0;
static int s_printIt = 0;
static int s_startOff = 0;
static int s_nomlib = 0;

/***************************************************************************
 *                          Static Function Prototypes                     *
 ***************************************************************************/

static int
allocateArray(JNIEnv *env, BufImageS_t *imageP,
              mlib_image **mlibImagePP, void **dataPP, int isSrc,
              int cvtToDefault, int addAlpha);
static int
allocateRasterArray(JNIEnv *env, RasterS_t *rasterP,
                    mlib_image **mlibImagePP, void **dataPP, int isSrc);

static void
freeArray(JNIEnv *env, BufImageS_t *srcimageP, mlib_image *srcmlibImP,
          void *srcdataP, BufImageS_t *dstimageP, mlib_image *dstmlibImP,
          void *dstdataP);
static void
freeDataArray(JNIEnv *env, jobject srcJdata, mlib_image *srcmlibImP,
          void *srcdataP, jobject dstJdata, mlib_image *dstmlibImP,
          void *dstdataP);

static int
storeImageArray(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
                mlib_image *mlibImP);

static int
storeRasterArray(JNIEnv *env, RasterS_t *srcP, RasterS_t *dstP,
                mlib_image *mlibImP);

static int
storeICMarray(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
              mlib_image *mlibImP);

static int
colorMatch(int r, int g, int b, int a, unsigned char *argb, int numColors);

static int
setImageHints(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
              int expandICM, int useAlpha,
              int premultiply, mlibHintS_t *hintP);


static int expandICM(JNIEnv *env, BufImageS_t *imageP, unsigned int *mDataP);
static int expandPackedBCR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP);
static int expandPackedSCR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP);
static int expandPackedICR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP);
static int expandPackedBCRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha);
static int expandPackedSCRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha);
static int expandPackedICRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha);
static int setPackedBCR(JNIEnv *env, RasterS_t *rasterP, int component,
                        unsigned char *outDataP);
static int setPackedSCR(JNIEnv *env, RasterS_t *rasterP, int component,
                        unsigned char *outDataP);
static int setPackedICR(JNIEnv *env, RasterS_t *rasterP, int component,
                        unsigned char *outDataP);
static int setPackedBCRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *outDataP,
                               int supportsAlpha);
static int setPackedSCRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *outDataP,
                               int supportsAlpha);
static int setPackedICRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *outDataP,
                               int supportsAlpha);

mlib_start_timer start_timer = NULL;
mlib_stop_timer stop_timer = NULL;

/***************************************************************************
 *                          Debugging Definitions                          *
 ***************************************************************************/
#ifdef DEBUG

static void
printMedialibError(int status) {
    switch(status) {
    case MLIB_FAILURE:
        jio_fprintf(stderr, "failure\n");
        break;
    case MLIB_NULLPOINTER:
        jio_fprintf(stderr, "null pointer\n");
        break;
    case MLIB_OUTOFRANGE:
        jio_fprintf (stderr, "out of range\n");
        break;
    default:
        jio_fprintf (stderr, "medialib error\n");
        break;
    }
}
#else /* ! DEBUG */
#  define printMedialibError(x)

#endif /* ! DEBUG */

static int
getMlibEdgeHint(jint edgeHint) {
    switch (edgeHint) {
    case java_awt_image_ConvolveOp_EDGE_NO_OP:
        return MLIB_EDGE_DST_COPY_SRC;
    case java_awt_image_ConvolveOp_EDGE_ZERO_FILL:
    default:
        return MLIB_EDGE_DST_FILL_ZERO;
    }
}

/***************************************************************************
 *                          External Functions                             *
 ***************************************************************************/
JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_convolveBI(JNIEnv *env, jobject this,
                                         jobject jsrc, jobject jdst,
                                         jobject jkernel, jint edgeHint)
{
    void *sdata, *ddata;
    mlib_image *src;
    mlib_image *dst;
    int i, scale;
    mlib_d64 *dkern;
    mlib_s32 *kdata;
    int klen;
    float kmax;
    mlib_s32 cmask;
    mlib_status status;
    int retStatus = 1;
    float *kern;
    BufImageS_t *srcImageP, *dstImageP;
    jobject jdata;
    int kwidth;
    int kheight;
    int w, h;
    int x, y;
    mlibHintS_t hint;
    int nbands;

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt)     (*start_timer)(3600);

    kwidth  = (*env)->GetIntField(env, jkernel, g_KernelWidthID);
    kheight = (*env)->GetIntField(env, jkernel, g_KernelHeightID);
    jdata = (*env)->GetObjectField(env, jkernel, g_KernelDataID);
    klen  = (*env)->GetArrayLength(env, jdata);
    kern  = (float *) (*env)->GetPrimitiveArrayCritical(env, jdata, NULL);
    if (kern == NULL) {
        /* out of memory exception already thrown */
        return 0;
    }

    if ((kwidth&0x1) == 0) {
        /* Kernel has even width */
        w = kwidth+1;
    }
    else {
        w = kwidth;
    }
    if ((kheight&0x1) == 0) {
        /* Kernel has even height */
        h = kheight+1;
    }
    else {
        h = kheight;
    }

    dkern = NULL;
    if (SAFE_TO_ALLOC_3(w, h, sizeof(mlib_d64))) {
        dkern = (mlib_d64 *)calloc(1, w * h * sizeof(mlib_d64));
    }
    if (dkern == NULL) {
        (*env)->ReleasePrimitiveArrayCritical(env, jdata, kern, JNI_ABORT);
        return 0;
    }

    /* Need to flip and find max value of the kernel.
     * Also, save the kernel values as mlib_d64 values.
     * The flip is to operate correctly with medialib,
     * which doesn't do the mathemetically correct thing,
     * i.e. it doesn't rotate the kernel by 180 degrees.
     * REMIND: This should perhaps be done at the Java
     * level by ConvolveOp.
     * REMIND: Should the max test be looking at absolute
     * values?
     * REMIND: What if klen != kheight * kwidth?
     */
    kmax = kern[klen-1];
    i = klen-1;
    for (y=0; y < kheight; y++) {
        for (x=0; x < kwidth; x++, i--) {
            dkern[y*w+x] = (mlib_d64) kern[i];
            if (kern[i] > kmax) {
                kmax = kern[i];
            }
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jdata, kern, JNI_ABORT);

    if (kmax > 1<<16) {
        /* We can only handle 16 bit max */
        free(dkern);
        return 0;
    }


    /* Parse the source image */
    if ((status = awt_parseImage(env, jsrc, &srcImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        free(dkern);
        return 0;
    }

    /* Parse the destination image */
    if ((status = awt_parseImage(env, jdst, &dstImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        awt_freeParsedImage(srcImageP, TRUE);
        free(dkern);
        return 0;
    }

    nbands = setImageHints(env, srcImageP, dstImageP, TRUE, TRUE,
                        FALSE, &hint);
    if (nbands < 1) {
        /* Can't handle any custom images */
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        free(dkern);
        return 0;
    }
    /* Allocate the arrays */
    if (allocateArray(env, srcImageP, &src, &sdata, TRUE,
                      hint.cvtSrcToDefault, hint.addAlpha) < 0) {
        /* Must be some problem */
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        free(dkern);
        return 0;
    }
    if (allocateArray(env, dstImageP, &dst, &ddata, FALSE,
                      hint.cvtToDst, FALSE) < 0) {
        /* Must be some problem */
        freeArray(env, srcImageP, src, sdata, NULL, NULL, NULL);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        free(dkern);
        return 0;
    }

    kdata = NULL;
    if (SAFE_TO_ALLOC_3(w, h, sizeof(mlib_s32))) {
        kdata = (mlib_s32 *)malloc(w * h * sizeof(mlib_s32));
    }
    if (kdata == NULL) {
        freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        free(dkern);
        return 0;
    }

    if ((*sMlibFns[MLIB_CONVKERNCVT].fptr)(kdata, &scale, dkern, w, h,
                                    mlib_ImageGetType(src)) != MLIB_SUCCESS) {
        freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        free(dkern);
        free(kdata);
        return 0;
    }

    if (s_printIt) {
        fprintf(stderr, "Orig Kernel(len=%d):\n",klen);
        for (y=kheight-1; y >= 0; y--) {
            for (x=kwidth-1; x >= 0; x--) {
                fprintf(stderr, "%g ", dkern[y*w+x]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "New Kernel(scale=%d):\n", scale);
        for (y=kheight-1; y >= 0; y--) {
            for (x=kwidth-1; x >= 0; x--) {
                fprintf(stderr, "%d ", kdata[y*w+x]);
            }
            fprintf(stderr, "\n");
        }
    }

    cmask = (1<<src->channels)-1;
    status = (*sMlibFns[MLIB_CONVMxN].fptr)(dst, src, kdata, w, h,
                               (w-1)/2, (h-1)/2, scale, cmask,
                               getMlibEdgeHint(edgeHint));

    if (status != MLIB_SUCCESS) {
        printMedialibError(status);
        retStatus = 0;
    }

    if (s_printIt) {
        unsigned int *dP;
        if (s_startOff != 0) {
            printf("Starting at %d\n", s_startOff);
        }
        if (sdata == NULL) {
            dP = (unsigned int *) mlib_ImageGetData(src);
        }
        else {
            dP = (unsigned int *) sdata;
        }
        printf("src is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[s_startOff+i]);
        }
        printf("\n");
        if (ddata == NULL) {
            dP = (unsigned int *)mlib_ImageGetData(dst);
        }
        else {
            dP = (unsigned int *) ddata;
        }
        printf("dst is \n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[s_startOff+i]);
        }
        printf("\n");
    }

    /* Means that we couldn't write directly into the destination buffer */
    if (ddata == NULL) {

        /* Need to store it back into the array */
        if (storeImageArray(env, srcImageP, dstImageP, dst) < 0) {
            /* Error */
            retStatus = 0;
        }
    }

    /* Release the pinned memory */
    freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);
    awt_freeParsedImage(srcImageP, TRUE);
    awt_freeParsedImage(dstImageP, TRUE);
    free(dkern);
    free(kdata);

    if (s_timeIt) (*stop_timer)(3600, 1);

    return retStatus;
}

JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_convolveRaster(JNIEnv *env, jobject this,
                                             jobject jsrc, jobject jdst,
                                             jobject jkernel, jint edgeHint)
{
    mlib_image *src;
    mlib_image *dst;
    int i, scale;
    mlib_d64 *dkern;
    mlib_s32 *kdata;
    int klen;
    float kmax;
    int retStatus = 1;
    mlib_status status;
    mlib_s32 cmask;
    void *sdata;
    void *ddata;
    RasterS_t *srcRasterP;
    RasterS_t *dstRasterP;
    int kwidth;
    int kheight;
    int w, h;
    int x, y;
    jobject jdata;
    float *kern;

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt)     (*start_timer)(3600);

    kwidth  = (*env)->GetIntField(env, jkernel, g_KernelWidthID);
    kheight = (*env)->GetIntField(env, jkernel, g_KernelHeightID);
    jdata = (*env)->GetObjectField(env, jkernel, g_KernelDataID);
    klen  = (*env)->GetArrayLength(env, jdata);
    kern  = (float *) (*env)->GetPrimitiveArrayCritical(env, jdata, NULL);
    if (kern == NULL) {
        /* out of memory exception already thrown */
        return 0;
    }

    if ((kwidth&0x1) == 0) {
        /* Kernel has even width */
        w = kwidth+1;
    }
    else {
        w = kwidth;
    }
    if ((kheight&0x1) == 0) {
        /* Kernel has even height */
        h = kheight+1;
    }
    else {
        h = kheight;
    }

    dkern = NULL;
    if (SAFE_TO_ALLOC_3(w, h, sizeof(mlib_d64))) {
        dkern = (mlib_d64 *)calloc(1, w * h * sizeof(mlib_d64));
    }
    if (dkern == NULL) {
        (*env)->ReleasePrimitiveArrayCritical(env, jdata, kern, JNI_ABORT);
        return 0;
    }

    /* Need to flip and find max value of the kernel.
     * Also, save the kernel values as mlib_d64 values.
     * The flip is to operate correctly with medialib,
     * which doesn't do the mathemetically correct thing,
     * i.e. it doesn't rotate the kernel by 180 degrees.
     * REMIND: This should perhaps be done at the Java
     * level by ConvolveOp.
     * REMIND: Should the max test be looking at absolute
     * values?
     * REMIND: What if klen != kheight * kwidth?
     */
    kmax = kern[klen-1];
    i = klen-1;
    for (y=0; y < kheight; y++) {
        for (x=0; x < kwidth; x++, i--) {
            dkern[y*w+x] = (mlib_d64) kern[i];
            if (kern[i] > kmax) {
                kmax = kern[i];
            }
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jdata, kern, JNI_ABORT);

    if (kmax > 1<<16) {
        /* We can only handle 16 bit max */
        free(dkern);
        return 0;
    }

    /* Parse the source image */
    if ((srcRasterP = (RasterS_t *) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        free(dkern);
        return -1;
    }

    if ((dstRasterP = (RasterS_t *) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        free(srcRasterP);
        free(dkern);
        return -1;
    }

    /* Parse the source raster */
    if ((status = awt_parseRaster(env, jsrc, srcRasterP)) <= 0) {
        /* Can't handle any custom rasters */
        free(srcRasterP);
        free(dstRasterP);
        free(dkern);
        return 0;
    }

    /* Parse the destination raster */
    if ((status = awt_parseRaster(env, jdst, dstRasterP)) <= 0) {
        /* Can't handle any custom images */
        awt_freeParsedRaster(srcRasterP, TRUE);
        free(dstRasterP);
        free(dkern);
        return 0;
    }

    /* Allocate the arrays */
    if (allocateRasterArray(env, srcRasterP, &src, &sdata, TRUE) < 0) {
        /* Must be some problem */
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        free(dkern);
        return 0;
    }
    if (allocateRasterArray(env, dstRasterP, &dst, &ddata, FALSE) < 0) {
        /* Must be some problem */
        freeDataArray(env, srcRasterP->jdata, src, sdata, NULL, NULL, NULL);
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        free(dkern);
        return 0;
    }

    kdata = NULL;
    if (SAFE_TO_ALLOC_3(w, h, sizeof(mlib_s32))) {
        kdata = (mlib_s32 *)malloc(w * h * sizeof(mlib_s32));
    }
    if (kdata == NULL) {
        freeDataArray(env, srcRasterP->jdata, src, sdata,
                      dstRasterP->jdata, dst, ddata);
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        free(dkern);
        return 0;
    }

    if ((*sMlibFns[MLIB_CONVKERNCVT].fptr)(kdata, &scale, dkern, w, h,
                                    mlib_ImageGetType(src)) != MLIB_SUCCESS) {
        freeDataArray(env, srcRasterP->jdata, src, sdata,
                      dstRasterP->jdata, dst, ddata);
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        free(dkern);
        free(kdata);
        return 0;
    }

    if (s_printIt) {
        fprintf(stderr, "Orig Kernel(len=%d):\n",klen);
        for (y=kheight-1; y >= 0; y--) {
            for (x=kwidth-1; x >= 0; x--) {
                fprintf(stderr, "%g ", dkern[y*w+x]);
            }
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "New Kernel(scale=%d):\n", scale);
        for (y=kheight-1; y >= 0; y--) {
            for (x=kwidth-1; x >= 0; x--) {
                fprintf(stderr, "%d ", kdata[y*w+x]);
            }
            fprintf(stderr, "\n");
        }
    }

    cmask = (1<<src->channels)-1;
    status = (*sMlibFns[MLIB_CONVMxN].fptr)(dst, src, kdata, w, h,
                               (w-1)/2, (h-1)/2, scale, cmask,
                               getMlibEdgeHint(edgeHint));

    if (status != MLIB_SUCCESS) {
        printMedialibError(status);
        retStatus = 0;
    }

    if (s_printIt) {
        unsigned int *dP;
        if (s_startOff != 0) {
            printf("Starting at %d\n", s_startOff);
        }
        if (sdata == NULL) {
            dP = (unsigned int *) mlib_ImageGetData(src);
        }
        else {
            dP = (unsigned int *) sdata;
        }
        printf("src is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[s_startOff+i]);
        }
        printf("\n");
        if (ddata == NULL) {
            dP = (unsigned int *)mlib_ImageGetData(dst);
        }
        else {
            dP = (unsigned int *) ddata;
        }
        printf("dst is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[s_startOff+i]);
        }
        printf("\n");
    }

    /* Means that we couldn't write directly into the destination buffer */
    if (ddata == NULL) {
        unsigned char *bdataP;
        unsigned short *sdataP;

        /* Punt for now */
        switch (dstRasterP->dataType) {
        case BYTE_DATA_TYPE:
            bdataP  = (unsigned char *) mlib_ImageGetData(dst);
            retStatus = (awt_setPixelByte(env, -1, dstRasterP, bdataP) >= 0) ;
            break;
        case SHORT_DATA_TYPE:
            sdataP  = (unsigned short *) mlib_ImageGetData(dst);
            retStatus = (awt_setPixelShort(env, -1, dstRasterP, sdataP) >= 0) ;
            break;
        default:
            retStatus = 0;
        }
    }

    /* Release the pinned memory */
    freeDataArray(env, srcRasterP->jdata, src, sdata,
                  dstRasterP->jdata, dst, ddata);
    awt_freeParsedRaster(srcRasterP, TRUE);
    awt_freeParsedRaster(dstRasterP, TRUE);
    free(dkern);
    free(kdata);

    if (s_timeIt) (*stop_timer)(3600,1);

    return retStatus;
}


JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_transformBI(JNIEnv *env, jobject this,
                                          jobject jsrc,
                                          jobject jdst,
                                          jdoubleArray jmatrix,
                                          jint interpType)
{
    mlib_image *src;
    mlib_image *dst;
    int i;
    int retStatus = 1;
    mlib_status status;
    double *matrix;
    mlib_d64 mtx[6];
    void *sdata;
    void *ddata;
    BufImageS_t *srcImageP;
    BufImageS_t *dstImageP;
    mlib_filter filter;
    mlibHintS_t hint;
    unsigned int *dP;
    int useIndexed;
    int nbands;

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt) {
        (*start_timer)(3600);
    }

    switch(interpType) {
    case java_awt_image_AffineTransformOp_TYPE_BILINEAR:
        filter = MLIB_BILINEAR;
        break;
    case java_awt_image_AffineTransformOp_TYPE_NEAREST_NEIGHBOR:
        filter = MLIB_NEAREST;
        break;
    case java_awt_image_AffineTransformOp_TYPE_BICUBIC:
        filter = MLIB_BICUBIC;
        break;
    default:
        JNU_ThrowInternalError(env, "Unknown interpolation type");
        return -1;
    }

    if ((*env)->GetArrayLength(env, jmatrix) < 6) {
        /*
         * Very unlikely, however we should check for this:
         * if given matrix array is too short, we can't handle it
         */
        return 0;
    }

    matrix = (*env)->GetPrimitiveArrayCritical(env, jmatrix, NULL);
    if (matrix == NULL) {
        /* out of memory error already thrown */
        return 0;
    }

    if (s_printIt) {
        printf("matrix is %g %g %g %g %g %g\n", matrix[0], matrix[1],
               matrix[2], matrix[3], matrix[4], matrix[5]);
    }

    mtx[0] = matrix[0];
    mtx[1] = matrix[2];
    mtx[2] = matrix[4];
    mtx[3] = matrix[1];
    mtx[4] = matrix[3];
    mtx[5] = matrix[5];

    (*env)->ReleasePrimitiveArrayCritical(env, jmatrix, matrix, JNI_ABORT);

    /* Parse the source image */
    if ((status = awt_parseImage(env, jsrc, &srcImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        return 0;
    }

    /* Parse the destination image */
    if ((status = awt_parseImage(env, jdst, &dstImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        awt_freeParsedImage(srcImageP, TRUE);
        return 0;
    }

    /* REMIND!!  Can't assume that it is the same LUT!! */
    /* Fix 4213160, 4184283 */
    useIndexed = (srcImageP->cmodel.cmType == INDEX_CM_TYPE &&
                  dstImageP->cmodel.cmType == INDEX_CM_TYPE &&
                  srcImageP->raster.rasterType == dstImageP->raster.rasterType &&
                  srcImageP->raster.rasterType == COMPONENT_RASTER_TYPE);

    nbands = setImageHints(env, srcImageP, dstImageP, !useIndexed, TRUE,
                        FALSE, &hint);
    if (nbands < 1) {
        /* Can't handle any custom images */
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }

    /* Allocate the arrays */
    if (allocateArray(env, srcImageP, &src, &sdata, TRUE,
                      hint.cvtSrcToDefault, hint.addAlpha) < 0) {
        /* Must be some problem */
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }
    if (allocateArray(env, dstImageP, &dst, &ddata, FALSE,
                      hint.cvtToDst, FALSE) < 0) {
        /* Must be some problem */
        freeArray(env, srcImageP, src, sdata, NULL, NULL, NULL);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }
#if 0
fprintf(stderr,"Src----------------\n");
fprintf(stderr,"Type : %d\n",src->type);
fprintf(stderr,"Channels: %d\n",src->channels);
fprintf(stderr,"Width   : %d\n",src->width);
fprintf(stderr,"Height  : %d\n",src->height);
fprintf(stderr,"Stride  : %d\n",src->stride);
fprintf(stderr,"Flags   : %d\n",src->flags);

fprintf(stderr,"Dst----------------\n");
fprintf(stderr,"Type : %d\n",dst->type);
fprintf(stderr,"Channels: %d\n",dst->channels);
fprintf(stderr,"Width   : %d\n",dst->width);
fprintf(stderr,"Height  : %d\n",dst->height);
fprintf(stderr,"Stride  : %d\n",dst->stride);
fprintf(stderr,"Flags   : %d\n",dst->flags);
#endif

    if (dstImageP->cmodel.cmType == INDEX_CM_TYPE) {
        /* Need to clear the destination to the transparent pixel */
        unsigned char *cP = (unsigned char *)mlib_ImageGetData(dst);

        memset(cP, dstImageP->cmodel.transIdx,
               mlib_ImageGetWidth(dst)*mlib_ImageGetHeight(dst));
    }
    /* Perform the transformation */
    if ((status = (*sMlibFns[MLIB_AFFINE].fptr)(dst, src, mtx, filter,
                                  MLIB_EDGE_SRC_EXTEND) != MLIB_SUCCESS))
    {
        printMedialibError(status);
        freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);

        return 0;
    }

    if (s_printIt) {
        if (sdata == NULL) {
            dP = (unsigned int *) mlib_ImageGetData(src);
        }
        else {
            dP = (unsigned int *) sdata;
        }
        printf("src is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[i]);
        }
        printf("\n");
        if (ddata == NULL) {
            dP = (unsigned int *)mlib_ImageGetData(dst);
        }
        else {
            dP = (unsigned int *) ddata;
        }
        printf("dst is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[i]);
        }
        printf("\n");
    }

    /* Means that we couldn't write directly into the destination buffer */
    if (ddata == NULL) {
        freeDataArray(env, srcImageP->raster.jdata, src, sdata,
                      NULL, NULL, NULL);
        /* Need to store it back into the array */
        if (storeImageArray(env, srcImageP, dstImageP, dst) < 0) {
            /* Error */
            retStatus = 0;
        }
        freeDataArray(env, NULL, NULL, NULL, dstImageP->raster.jdata,
                      dst, ddata);
    }
    else {
        /* Release the pinned memory */
        freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);
    }

    awt_freeParsedImage(srcImageP, TRUE);
    awt_freeParsedImage(dstImageP, TRUE);

    if (s_timeIt) (*stop_timer)(3600,1);

    return retStatus;
}

JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_transformRaster(JNIEnv *env, jobject this,
                                              jobject jsrc,
                                              jobject jdst,
                                              jdoubleArray jmatrix,
                                              jint interpType)
{
    mlib_image *src;
    mlib_image *dst;
    int i;
    int retStatus = 1;
    mlib_status status;
    double *matrix;
    mlib_d64 mtx[6];
    void *sdata;
    void *ddata;
    RasterS_t *srcRasterP;
    RasterS_t *dstRasterP;
    mlib_filter filter;
    unsigned int *dP;

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt) {
        (*start_timer)(3600);
    }

    switch(interpType) {
    case java_awt_image_AffineTransformOp_TYPE_BILINEAR:
        filter = MLIB_BILINEAR;
        break;
    case java_awt_image_AffineTransformOp_TYPE_NEAREST_NEIGHBOR:
        filter = MLIB_NEAREST;
        break;
    case java_awt_image_AffineTransformOp_TYPE_BICUBIC:
        filter = MLIB_BICUBIC;
        break;
    default:
        JNU_ThrowInternalError(env, "Unknown interpolation type");
        return -1;
    }

    if ((srcRasterP = (RasterS_t *) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        return -1;
    }

    if ((dstRasterP = (RasterS_t *) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        free(srcRasterP);
        return -1;
    }

    if ((*env)->GetArrayLength(env, jmatrix) < 6) {
        /*
         * Very unlikely, however we should check for this:
         * if given matrix array is too short, we can't handle it.
         */
        free(srcRasterP);
        free(dstRasterP);
        return 0;
    }

    matrix = (*env)->GetPrimitiveArrayCritical(env, jmatrix, NULL);
    if (matrix == NULL) {
        /* out of memory error already thrown */
        free(srcRasterP);
        free(dstRasterP);
        return 0;
    }

    if (s_printIt) {
        printf("matrix is %g %g %g %g %g %g\n", matrix[0], matrix[1],
               matrix[2], matrix[3], matrix[4], matrix[5]);
    }

    mtx[0] = matrix[0];
    mtx[1] = matrix[2];
    mtx[2] = matrix[4];
    mtx[3] = matrix[1];
    mtx[4] = matrix[3];
    mtx[5] = matrix[5];

    (*env)->ReleasePrimitiveArrayCritical(env, jmatrix, matrix, JNI_ABORT);

    /* Parse the source raster */
    if ((status = awt_parseRaster(env, jsrc, srcRasterP)) <= 0) {
        /* Can't handle any custom rasters */
        free(srcRasterP);
        free(dstRasterP);
        return 0;
    }

    /* Parse the destination raster */
    if ((status = awt_parseRaster(env, jdst, dstRasterP)) <= 0) {
        /* Can't handle any custom images */
        awt_freeParsedRaster(srcRasterP, TRUE);
        free(dstRasterP);
        return 0;
    }

    /* Allocate the arrays */
    if (allocateRasterArray(env, srcRasterP, &src, &sdata, TRUE) < 0) {
        /* Must be some problem */
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }
    if (allocateRasterArray(env, dstRasterP, &dst, &ddata, FALSE) < 0) {
        /* Must be some problem */
        freeDataArray(env, srcRasterP->jdata, src, sdata, NULL, NULL, NULL);
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }

#if 0
fprintf(stderr,"Src----------------\n");
fprintf(stderr,"Type : %d\n",src->type);
fprintf(stderr,"Channels: %d\n",src->channels);
fprintf(stderr,"Width   : %d\n",src->width);
fprintf(stderr,"Height  : %d\n",src->height);
fprintf(stderr,"Stride  : %d\n",src->stride);
fprintf(stderr,"Flags   : %d\n",src->flags);

fprintf(stderr,"Dst----------------\n");
fprintf(stderr,"Type : %d\n",dst->type);
fprintf(stderr,"Channels: %d\n",dst->channels);
fprintf(stderr,"Width   : %d\n",dst->width);
fprintf(stderr,"Height  : %d\n",dst->height);
fprintf(stderr,"Stride  : %d\n",dst->stride);
fprintf(stderr,"Flags   : %d\n",dst->flags);
#endif

    {
        unsigned char *cP = (unsigned char *)mlib_ImageGetData(dst);

        memset(cP, 0, mlib_ImageGetWidth(dst)*mlib_ImageGetHeight(dst));
    }

    /* Perform the transformation */
    if ((status = (*sMlibFns[MLIB_AFFINE].fptr)(dst, src, mtx, filter,
                                  MLIB_EDGE_SRC_EXTEND) != MLIB_SUCCESS))
    {
        printMedialibError(status);
        /* REMIND: Free the regions */
        return 0;
    }

    if (s_printIt) {
        if (sdata == NULL) {
            dP = (unsigned int *) mlib_ImageGetData(src);
        }
        else {
            dP = (unsigned int *) sdata;
        }
        printf("src is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[i]);
        }
        printf("\n");
        if (ddata == NULL) {
            dP = (unsigned int *)mlib_ImageGetData(dst);
        }
        else {
            dP = (unsigned int *) ddata;
        }
        printf("dst is\n");
        for (i=0; i < 20; i++) {
            printf("%x ",dP[i]);
        }
        printf("\n");
    }

    /* Means that we couldn't write directly into the destination buffer */
    if (ddata == NULL) {
        unsigned char *bdataP;
        unsigned short *sdataP;

        /* Need to store it back into the array */
        if (storeRasterArray(env, srcRasterP, dstRasterP, dst) < 0) {
            /* Punt for now */
            switch (dst->type) {
            case MLIB_BYTE:
                bdataP  = (unsigned char *) mlib_ImageGetData(dst);
                retStatus = (awt_setPixelByte(env, -1, dstRasterP, bdataP) >= 0) ;
                break;
            case MLIB_SHORT:
                sdataP  = (unsigned short *) mlib_ImageGetData(dst);
                retStatus = (awt_setPixelShort(env, -1, dstRasterP, sdataP) >= 0) ;
                break;
            default:
                retStatus = 0;
            }
        }
    }

    /* Release the pinned memory */
    freeDataArray(env, srcRasterP->jdata, src, sdata,
                  dstRasterP->jdata, dst, ddata);

    awt_freeParsedRaster(srcRasterP, TRUE);
    awt_freeParsedRaster(dstRasterP, TRUE);

    if (s_timeIt) (*stop_timer)(3600,1);

    return retStatus;
}

JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_lookupByteBI(JNIEnv *env, jobject this,
                                           jobject jsrc, jobject jdst,
                                           jobjectArray jtableArrays)
{
    mlib_image *src;
    mlib_image *dst;
    void *sdata, *ddata;
    unsigned char **table;
    unsigned char **tbl;
    unsigned char lut[256];
    int retStatus = 1;
    int i;
    mlib_status status;
    int jlen;
    jobject *jtable;
    BufImageS_t *srcImageP, *dstImageP;
    int nbands;
    int ncomponents;
    mlibHintS_t hint;

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt) (*start_timer)(3600);

    /* Parse the source image */
    if ((status = awt_parseImage(env, jsrc, &srcImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        return 0;
    }

    /* Parse the destination image */
    if ((status = awt_parseImage(env, jdst, &dstImageP, FALSE)) <= 0) {
        /* Can't handle any custom images */
        awt_freeParsedImage(srcImageP, TRUE);
        return 0;
    }

    jlen = (*env)->GetArrayLength(env, jtableArrays);

    ncomponents = srcImageP->cmodel.isDefaultCompatCM
        ? 4
        : srcImageP->cmodel.numComponents;

    tbl = NULL;
    if (SAFE_TO_ALLOC_2(ncomponents, sizeof(unsigned char *))) {
        tbl = (unsigned char **)
            calloc(1, ncomponents * sizeof(unsigned char *));
    }

    jtable = NULL;
    if (SAFE_TO_ALLOC_2(jlen, sizeof(jobject *))) {
        jtable = (jobject *)malloc(jlen * sizeof (jobject *));
    }

    table = NULL;
    if (SAFE_TO_ALLOC_2(jlen, sizeof(unsigned char *))) {
        table = (unsigned char **)malloc(jlen * sizeof(unsigned char *));
    }

    if (tbl == NULL || table == NULL || jtable == NULL) {
        if (tbl != NULL) free(tbl);
        if (table != NULL) free(table);
        if (jtable != NULL) free(jtable);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        JNU_ThrowNullPointerException(env, "NULL LUT");
        return 0;
    }
    /* Need to grab these pointers before we lock down arrays */
    for (i=0; i < jlen; i++) {
        jtable[i] = (*env)->GetObjectArrayElement(env, jtableArrays, i);
        if (jtable[i] == NULL) {
            free(tbl);
            free(table);
            free(jtable);
            awt_freeParsedImage(srcImageP, TRUE);
            awt_freeParsedImage(dstImageP, TRUE);
            return 0;
        }
    }

    nbands = setImageHints(env, srcImageP, dstImageP, FALSE, TRUE,
                        FALSE, &hint);
    if (nbands < 1) {
        /* Can't handle any custom images */
        free(tbl);
        free(table);
        free(jtable);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }

    /* Allocate the arrays */
    if (allocateArray(env, srcImageP, &src, &sdata, TRUE, FALSE, FALSE) < 0) {
        /* Must be some problem */
        free(tbl);
        free(table);
        free(jtable);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }
    if (allocateArray(env, dstImageP, &dst, &ddata, FALSE, FALSE, FALSE) < 0) {
        /* Must be some problem */
        free(tbl);
        free(table);
        free(jtable);
        freeArray(env, srcImageP, src, sdata, NULL, NULL, NULL);
        awt_freeParsedImage(srcImageP, TRUE);
        awt_freeParsedImage(dstImageP, TRUE);
        return 0;
    }

    /* Set up a straight lut so we don't mess around with alpha */
    /*
     * NB: medialib lookup routine expects lookup array for each
     * component of source image including alpha.
     * If lookup table we got form the java layer does not contain
     * sufficient number of lookup arrays we add references to identity
     * lookup array to make medialib happier.
     */
    if (jlen < ncomponents) {
        int j;
        /* REMIND: This should be the size of the input lut!! */
        for (j=0; j < 256; j++) {
            lut[j] = j;
        }
        for (j=0; j < ncomponents; j++) {
            tbl[j] = lut;
        }

    }

    for (i=0; i < jlen; i++) {
        table[i] = (unsigned char *)
            (*env)->GetPrimitiveArrayCritical(env, jtable[i], NULL);
        if (table[i] == NULL) {
            /* Free what we've got so far. */
            int j;
            for (j = 0; j < i; j++) {
                (*env)->ReleasePrimitiveArrayCritical(env,
                                                      jtable[j],
                                                      (jbyte *) table[j],
                                                      JNI_ABORT);
            }
            free(tbl);
            free(table);
            free(jtable);
            freeArray(env, srcImageP, src, sdata, NULL, NULL, NULL);
            awt_freeParsedImage(srcImageP, TRUE);
            awt_freeParsedImage(dstImageP, TRUE);
            return 0;
        }
        tbl[srcImageP->hints.colorOrder[i]] = table[i];
    }

    if (jlen == 1) {
        for (i=1; i < nbands -
                 srcImageP->cmodel.supportsAlpha; i++) {
            tbl[srcImageP->hints.colorOrder[i]] = table[0];
        }
    }

    /* Mlib needs 16bit lookuptable and must be signed! */
    if (src->type == MLIB_SHORT) {
        unsigned short *sdataP = (unsigned short *) src->data;
        unsigned short *sP;
        if (dst->type == MLIB_BYTE) {
            unsigned char *cdataP  = (unsigned char *)  dst->data;
            unsigned char *cP;
            if (nbands > 1) {
                retStatus = 0;
            }
            else {
                int x, y;
                for (y=0; y < src->height; y++) {
                    cP = cdataP;
                    sP = sdataP;
                    for (x=0; x < src->width; x++) {
                        *cP++ = table[0][*sP++];
                    }

                    /*
                     * 4554571: increment pointers using the scanline stride
                     * in pixel units (not byte units)
                     */
                    cdataP += dstImageP->raster.scanlineStride;
                    sdataP += srcImageP->raster.scanlineStride;
                }
            }
        }
        /* How about ddata == null? */
    }
    else if ((status = (*sMlibFns[MLIB_LOOKUP].fptr)(dst, src,
                                      (void **)tbl) != MLIB_SUCCESS)) {
        printMedialibError(status);
        retStatus = 0;
    }

    /*
     * Means that we couldn't write directly into
     * the destination buffer
     */
    if (ddata == NULL) {

        /* Need to store it back into the array */
        if (storeImageArray(env, srcImageP, dstImageP, dst) < 0) {
            /* Error */
            retStatus = 0;
        }
    }

    /* Release the LUT */
    for (i=0; i < jlen; i++) {
        (*env)->ReleasePrimitiveArrayCritical(env, jtable[i],
                                              (jbyte *) table[i], JNI_ABORT);
    }
    free ((void *) jtable);
    free ((void *) table);
    free ((void *) tbl);

    /* Release the pinned memory */
    freeArray(env, srcImageP, src, sdata, dstImageP, dst, ddata);

    awt_freeParsedImage(srcImageP, TRUE);
    awt_freeParsedImage(dstImageP, TRUE);

    if (s_timeIt) (*stop_timer)(3600, 1);

    return retStatus;
}


JNIEXPORT jint JNICALL
Java_sun_awt_image_ImagingLib_lookupByteRaster(JNIEnv *env,
                                               jobject this,
                                               jobject jsrc,
                                               jobject jdst,
                                               jobjectArray jtableArrays)
{
    RasterS_t*     srcRasterP;
    RasterS_t*     dstRasterP;
    mlib_image*    src;
    mlib_image*    dst;
    void*          sdata;
    void*          ddata;
    jobject        jtable[4];
    unsigned char* table[4];
    int            i;
    int            retStatus = 1;
    mlib_status    status;
    int            jlen;
    int            lut_nbands;
    int            src_nbands;
    int            dst_nbands;
    unsigned char  ilut[256];

    /* This function requires a lot of local refs ??? Is 64 enough ??? */
    if ((*env)->EnsureLocalCapacity(env, 64) < 0)
        return 0;

    if (s_nomlib) return 0;
    if (s_timeIt) (*start_timer)(3600);

    if ((srcRasterP = (RasterS_t*) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        return -1;
    }

    if ((dstRasterP = (RasterS_t *) calloc(1, sizeof(RasterS_t))) == NULL) {
        JNU_ThrowOutOfMemoryError(env, "Out of memory");
        free(srcRasterP);
        return -1;
    }

    /* Parse the source raster - reject custom images */
    if ((status = awt_parseRaster(env, jsrc, srcRasterP)) <= 0) {
        free(srcRasterP);
        free(dstRasterP);
        return 0;
    }

    /* Parse the destination image - reject custom images */
    if ((status = awt_parseRaster(env, jdst, dstRasterP)) <= 0) {
        awt_freeParsedRaster(srcRasterP, TRUE);
        free(dstRasterP);
        return 0;
    }

    jlen = (*env)->GetArrayLength(env, jtableArrays);

    lut_nbands = jlen;
    src_nbands = srcRasterP->numBands;
    dst_nbands = dstRasterP->numBands;

    /* MediaLib can't do more than 4 bands */
    if (src_nbands <= 0 || src_nbands > 4 ||
        dst_nbands <= 0 || dst_nbands > 4 ||
        lut_nbands <= 0 || lut_nbands > 4 ||
        src_nbands != dst_nbands ||
        ((lut_nbands != 1) && (lut_nbands != src_nbands)))
    {
        // we should free parsed rasters here
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }

    /* Allocate the raster arrays */
    if (allocateRasterArray(env, srcRasterP, &src, &sdata, TRUE) < 0) {
        /* Must be some problem */
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }
    if (allocateRasterArray(env, dstRasterP, &dst, &ddata, FALSE) < 0) {
        /* Must be some problem */
        freeDataArray(env, srcRasterP->jdata, src, sdata, NULL, NULL, NULL);
        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }

    /*
     * Well, until now we have analyzed number of bands in
     * src and dst rasters.
     * However, it is not enough because medialib lookup routine uses
     * number of channels of medialib image. Note that in certain
     * case number of channels may differs form the number of bands.
     * Good example is raster that is used in TYPE_INT_RGB buffered
     * image: it has 3 bands, but their medialib representation has
     * 4 channels.
     *
     * In order to avoid the lookup routine failure, we need:
     *
     * 1. verify that src and dst have same number of channels.
     * 2. provide lookup array for every channel. If we have "extra"
     *    channel (like the raster described above) then we need to
     *    provide identical lookup array.
     */
    if (src->channels != dst->channels) {
        freeDataArray(env, srcRasterP->jdata, src, sdata,
                      dstRasterP->jdata, dst, ddata);

        awt_freeParsedRaster(srcRasterP, TRUE);
        awt_freeParsedRaster(dstRasterP, TRUE);
        return 0;
    }

    if (src_nbands < src->channels) {
        for (i = 0; i < 256; i++) {
            ilut[i] = i;
        }
    }


    /* Get references to the lookup table arrays */
    /* Need to grab these pointers before we lock down arrays */
    for (i=0; i < lut_nbands; i++) {
        jtable[i] = (*env)->GetObjectArrayElement(env, jtableArrays, i);
        if (jtable[i] == NULL) {
            return 0;
        }
    }

    for (i=0; i < lut_nbands; i++) {
        table[i] = (unsigned char *)
            (*env)->GetPrimitiveArrayCritical(env, jtable[i], NULL);
        if (table[i] == NULL) {
            /* Free what we've got so far. */
            int j;
            for (j = 0; j < i; j++) {
                (*env)->ReleasePrimitiveArrayCritical(env,
                                                      jtable[j],
                                                      (jbyte *) table[j],
                                                      JNI_ABORT);
            }
            freeDataArray(env, srcRasterP->jdata, src, sdata,
                          dstRasterP->jdata, dst, ddata);
            awt_freeParsedRaster(srcRasterP, TRUE);
            awt_freeParsedRaster(dstRasterP, TRUE);
            return 0;
        }
    }

    /*
     * Medialib routine expects lookup array for each band of raster.
     * Setup the  rest of lookup arrays if supplied lookup table
     * contains single lookup array.
     */
    for (i = lut_nbands; i < src_nbands; i++) {
        table[i] = table[0];
    }

    /*
     * Setup lookup array for "extra" channels
     */
    for ( ; i < src->channels; i++) {
        table[i] = ilut;
    }

#define NLUT 8
    /* Mlib needs 16bit lookuptable and must be signed! */
    if (src->type == MLIB_SHORT) {
        unsigned short *sdataP = (unsigned short *) src->data;
        unsigned short *sP;
        if (dst->type == MLIB_BYTE) {
            unsigned char *cdataP  = (unsigned char *)  dst->data;
            unsigned char *cP;
            if (lut_nbands > 1) {
                retStatus = 0;
            } else {
                int x, y;
                unsigned int mask = NLUT-1;
                unsigned char* pLut = table[0];
                unsigned int endianTest = 0xff000000;
                for (y=0; y < src->height; y++) {
                    int nloop, nx;
                    unsigned short* srcP;
                    int* dstP;
                    int npix = src->width;
                    cP = cdataP;
                    sP = sdataP;
                    /* Get to 32 bit-aligned point */
                    while(((uintptr_t)cP & 0x3) != 0 && npix>0) {
                          *cP++ = pLut[*sP++];
                          npix--;
                    }

                    /*
                     * Do NLUT pixels per loop iteration.
                     * Pack into ints and write out 2 at a time.
                     */
                    nloop = npix/NLUT;
                    nx = npix%NLUT;
                    srcP = sP;
                    dstP = (int*)cP;

                    if(((char*)(&endianTest))[0] != 0) {
                        /* Big endian loop */
                        for(x=nloop; x!=0; x--) {
                            dstP[0] = (int)
                                    ((pLut[srcP[0]] << 24) |
                                     (pLut[srcP[1]] << 16) |
                                     (pLut[srcP[2]] << 8)  |
                                      pLut[srcP[3]]);
                            dstP[1] = (int)
                                    ((pLut[srcP[4]] << 24) |
                                     (pLut[srcP[5]] << 16) |
                                     (pLut[srcP[6]] << 8)  |
                                      pLut[srcP[7]]);
                            dstP += NLUT/4;
                            srcP += NLUT;
                        }
                    } else {
                        /* Little endian loop */
                        for(x=nloop; x!=0; x--) {
                            dstP[0] = (int)
                                    ((pLut[srcP[3]] << 24) |
                                     (pLut[srcP[2]] << 16) |
                                     (pLut[srcP[1]] << 8)  |
                                      pLut[srcP[0]]);
                            dstP[1] = (int)
                                    ((pLut[srcP[7]] << 24) |
                                     (pLut[srcP[6]] << 16) |
                                     (pLut[srcP[5]] << 8)  |
                                      pLut[srcP[4]]);
                            dstP += NLUT/4;
                            srcP += NLUT;
                        }
                    }
                    /*
                     * Complete any remaining pixels
                     */
                    cP = cP + NLUT * nloop;
                    sP = sP + NLUT * nloop;
                    for(x=nx; x!=0; x--) {
                        *cP++ = pLut[*sP++];
                    }

                    /*
                     * 4554571: increment pointers using the scanline stride
                     * in pixel units (not byte units)
                     */
                    cdataP += dstRasterP->scanlineStride;
                    sdataP += srcRasterP->scanlineStride;
                }
            }
        }
        /* How about ddata == null? */
    } else if ((status = (*sMlibFns[MLIB_LOOKUP].fptr)(dst, src,
                                      (void **)table) != MLIB_SUCCESS)) {
        printMedialibError(status);
        retStatus = 0;
    }

    /*
     * Means that we couldn't write directly into
     * the destination buffer
     */
    if (ddata == NULL) {
        unsigned char*  bdataP;
        unsigned short* sdataP;

        switch (dstRasterP->dataType) {
          case BYTE_DATA_TYPE:
            bdataP  = (unsigned char *) mlib_ImageGetData(dst);
            retStatus = (awt_setPixelByte(env, -1, dstRasterP, bdataP) >= 0) ;
            break;
          case SHORT_DATA_TYPE:
            sdataP  = (unsigned short *) mlib_ImageGetData(dst);
            retStatus = (awt_setPixelShort(env, -1, dstRasterP, sdataP) >= 0) ;
            break;
          default:
            retStatus = 0;
        }
    }

    /* Release the LUT */
    for (i=0; i < lut_nbands; i++) {
        (*env)->ReleasePrimitiveArrayCritical(env, jtable[i],
                                              (jbyte *) table[i], JNI_ABORT);
    }

    /* Release the pinned memory */
    freeDataArray(env, srcRasterP->jdata, src, sdata,
                  dstRasterP->jdata, dst, ddata);

    awt_freeParsedRaster(srcRasterP, TRUE);
    awt_freeParsedRaster(dstRasterP, TRUE);

    if (s_timeIt) (*stop_timer)(3600, 1);

    return retStatus;
}


JNIEXPORT jboolean JNICALL
Java_sun_awt_image_ImagingLib_init(JNIEnv *env, jclass thisClass) {
    char *start;
    if (getenv("IMLIB_DEBUG")) {
        start_timer = awt_setMlibStartTimer();
        stop_timer = awt_setMlibStopTimer();
        if (start_timer && stop_timer) {
            s_timeIt = 1;
        }
    }

    if (getenv("IMLIB_PRINT")) {
        s_printIt = 1;
    }
    if ((start = getenv("IMLIB_START")) != NULL) {
        sscanf(start, "%d", &s_startOff);
    }

    if (getenv ("IMLIB_NOMLIB")) {
        s_nomlib = 1;
        return JNI_FALSE;
    }

    /* This function is platform-dependent and is in awt_mlib.c */
    if (awt_getImagingLib(env, (mlibFnS_t *)&sMlibFns, &sMlibSysFns) !=
        MLIB_SUCCESS)
    {
        s_nomlib = 1;
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

/* REMIND: How to specify border? */
static void extendEdge(JNIEnv *env, BufImageS_t *imageP,
                       int *widthP, int *heightP) {
    RasterS_t *rasterP = &imageP->raster;
    int width;
    int height;
    /* Useful for convolution? */

    jobject jbaseraster = (*env)->GetObjectField(env, rasterP->jraster,
                                                 g_RasterBaseRasterID);
    width = rasterP->width;
    height = rasterP->height;
#ifdef WORKING
    if (! JNU_IsNull(env, jbaseraster) &&
        !(*env)->IsSameObject(env, rasterP->jraster, jbaseraster)) {
        int xOff;
        int yOff;
        int baseWidth;
        int baseHeight;
        int baseXoff;
        int baseYoff;
        /* Not the same object so get the width and height */
        xOff = (*env)->GetIntField(env, rasterP->jraster, g_RasterXOffsetID);
        yOff = (*env)->GetIntField(env, rasterP->jraster, g_RasterYOffsetID);
        baseWidth  = (*env)->GetIntField(env, jbaseraster, g_RasterWidthID);
        baseHeight = (*env)->GetIntField(env, jbaseraster, g_RasterHeightID);
        baseXoff   = (*env)->GetIntField(env, jbaseraster, g_RasterXOffsetID);
        baseYoff   = (*env)->GetIntField(env, jbaseraster, g_RasterYOffsetID);

        if (xOff + rasterP->width < baseXoff + baseWidth) {
            /* Can use edge */
            width++;
        }
        if (yOff + rasterP->height < baseYoff + baseHeight) {
            /* Can use edge */
            height++;
        }

    }
#endif

}

static int
setImageHints(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
              int expandICM, int useAlpha,
              int premultiply, mlibHintS_t *hintP)
{
    ColorModelS_t *srcCMP = &srcP->cmodel;
    ColorModelS_t *dstCMP = &dstP->cmodel;
    int nbands = 0;
    int ncomponents;

    hintP->dataType = srcP->raster.dataType;
    hintP->addAlpha = FALSE;

    /* Are the color spaces the same? */
    if (srcCMP->csType != dstCMP->csType) {
        /* If the src is GRAY and dst RGB, we can handle it */
        if (!(srcCMP->csType == java_awt_color_ColorSpace_TYPE_GRAY &&
              dstCMP->csType == java_awt_color_ColorSpace_TYPE_RGB)) {
            /* Nope, need to handle that in java for now */
            return -1;
        }
        else {
            hintP->cvtSrcToDefault = TRUE;
        }
    }
    else {
        if (srcP->hints.needToExpand) {
            hintP->cvtSrcToDefault = TRUE;
        }
        else {
            /* Need to initialize this */
            hintP->cvtSrcToDefault = FALSE;
        }
    }

    ncomponents = srcCMP->numComponents;
    if ((useAlpha == 0) && srcCMP->supportsAlpha) {
        ncomponents--;  /* ?? */
        /* Not really, more like shrink src to get rid of alpha */
        hintP->cvtSrcToDefault = TRUE;
    }

    hintP->dataType = srcP->raster.dataType;
    if (hintP->cvtSrcToDefault == FALSE) {
        if (srcCMP->cmType == INDEX_CM_TYPE) {
            if (expandICM) {
                nbands = srcCMP->numComponents;
                hintP->cvtSrcToDefault = TRUE;

                if (dstCMP->isDefaultCompatCM) {
                    hintP->allocDefaultDst = FALSE;
                    hintP->cvtToDst = FALSE;
                }
                else if (dstCMP->isDefaultCompatCM) {
                    hintP->allocDefaultDst = FALSE;
                    hintP->cvtToDst = FALSE;
                }
            }
            else {
                nbands = 1;
                hintP->cvtSrcToDefault = FALSE;
            }

        }
        else {
            if (srcP->hints.packing & INTERLEAVED) {
                nbands = srcCMP->numComponents;
            }
            else {
                nbands = 1;
            }

            /* Look at the packing */
            if ((srcP->hints.packing&BYTE_INTERLEAVED)==BYTE_INTERLEAVED ||
                (srcP->hints.packing&SHORT_INTERLEAVED)==SHORT_INTERLEAVED||
                (srcP->hints.packing&BYTE_SINGLE_BAND) == BYTE_SINGLE_BAND||
                (srcP->hints.packing&SHORT_SINGLE_BAND)==SHORT_SINGLE_BAND||
                (srcP->hints.packing&BYTE_BANDED)  == BYTE_BANDED       ||
                (srcP->hints.packing&SHORT_BANDED) == SHORT_BANDED) {
                /* Can use src directly */
                hintP->cvtSrcToDefault = FALSE;
            }
            else {
                /* Must be packed or custom */
                hintP->cvtSrcToDefault = TRUE;
            }
        }
    }
    if (hintP->cvtSrcToDefault) {
        /* By definition */
        nbands = 4;  /* What about alpha? */
        hintP->dataType = BYTE_DATA_TYPE;
        hintP->needToCopy = TRUE;

        if (srcP->imageType == dstP->imageType) {
            hintP->cvtToDst = TRUE;
        }
        else if (dstP->cmodel.isDefaultCM) {
            /* Not necessarily */
            hintP->cvtToDst = FALSE;
        }
        else {
            hintP->cvtToDst = TRUE;
        }
    }
    else {
        int srcImageType = srcP->imageType;
        int dstImageType = dstP->imageType;
        /* Special case where we need to fill in alpha values */
        if (srcCMP->isDefaultCompatCM && dstCMP->isDefaultCompatCM) {
            int i;
            if (!srcCMP->supportsAlpha &&dstCMP->supportsAlpha) {
                hintP->addAlpha = TRUE;
            }
            for (i=0; i < srcCMP->numComponents; i++) {
                if (srcP->hints.colorOrder[i] != dstP->hints.colorOrder[i]){
                    if (!srcCMP->isDefaultCM) {
                        hintP->cvtSrcToDefault = TRUE;
                        srcImageType = java_awt_image_BufferedImage_TYPE_INT_ARGB;
                    }
                    if (!dstCMP->isDefaultCM) {
                        hintP->cvtToDst = TRUE;
                        dstImageType = java_awt_image_BufferedImage_TYPE_INT_ARGB;
                    }

                    break;
                }
            }
        }
        else if (srcCMP->cmType != INDEX_CM_TYPE &&
                 !srcCMP->supportsAlpha && dstCMP->supportsAlpha)
        {
            /* We've already handled the index case.  This is for the rest of the cases */
            srcImageType = java_awt_image_BufferedImage_TYPE_INT_ARGB;
            hintP->cvtSrcToDefault = TRUE;
        }

        hintP->allocDefaultDst = FALSE;
        if (srcImageType == dstImageType) {
            /* Same image type so use it */
            hintP->cvtToDst = FALSE;
        }
        else if (srcImageType == TYPE_INT_RGB &&
                 (dstImageType == TYPE_INT_ARGB ||
                  dstImageType == TYPE_INT_ARGB_PRE)) {
            hintP->cvtToDst = FALSE;
        }
        else if (srcImageType == TYPE_INT_BGR &&
                 (dstImageType == TYPE_4BYTE_ABGR ||
                  dstImageType == TYPE_4BYTE_ABGR_PRE)) {
            hintP->cvtToDst = FALSE;
        }
        else if (srcP->hints.packing == dstP->hints.packing) {
            /* Now what? */

            /* Check color order */

            /* Check if just need to scale the data */

            hintP->cvtToDst = TRUE;
        }
        else {
            /* Don't know what it is so convert it */
            hintP->allocDefaultDst = TRUE;
            hintP->cvtToDst = TRUE;
        }
        hintP->needToCopy = (ncomponents > nbands);
    }

    return nbands;
}


static int
expandPacked(JNIEnv *env, BufImageS_t *img, ColorModelS_t *cmP,
             RasterS_t *rasterP, int component, unsigned char *bdataP) {

    if (rasterP->rasterType == COMPONENT_RASTER_TYPE) {
        switch (rasterP->dataType) {
        case BYTE_DATA_TYPE:
            if (expandPackedBCR(env, rasterP, component, bdataP) < 0) {
                /* Must have been an error */
                return -1;
            }
            break;

        case SHORT_DATA_TYPE:
            if (expandPackedICR(env, rasterP, component, bdataP) < 0) {
                /* Must have been an error */
                return -1;
            }
            break;

        case INT_DATA_TYPE:
            if (expandPackedICR(env, rasterP, component, bdataP) < 0) {
                /* Must have been an error */
                return -1;
            }
            break;

        default:
            /* REMIND: Return some sort of error */
            return -1;
        }
    }
    else {
        /* REMIND: Return some sort of error */
        return -1;
    }

    return 0;
}

static int
cvtCustomToDefault(JNIEnv *env, BufImageS_t *imageP, int component,
                   unsigned char *dataP) {
    ColorModelS_t *cmP = &imageP->cmodel;
    RasterS_t *rasterP = &imageP->raster;
    int y;
    jobject jpixels = NULL;
    jint *pixels;
    unsigned char *dP = dataP;
#define NUM_LINES    10
    int numLines = NUM_LINES;
    int nbytes = rasterP->width*4*NUM_LINES;

    for (y=0; y < rasterP->height; y+=numLines) {
        /* getData, one scanline at a time */
        if (y+numLines > rasterP->height) {
            numLines = rasterP->height - y;
            nbytes = rasterP->width*4*numLines;
        }
        jpixels = (*env)->CallObjectMethod(env, imageP->jimage,
                                           g_BImgGetRGBMID, 0, y,
                                           rasterP->width, numLines,
                                           jpixels,0, rasterP->width);
        if (jpixels == NULL) {
            JNU_ThrowInternalError(env, "Can't retrieve pixels.");
            return -1;
        }

        pixels = (*env)->GetPrimitiveArrayCritical(env, jpixels, NULL);
        memcpy(dP, pixels, nbytes);
        dP += nbytes;
        (*env)->ReleasePrimitiveArrayCritical(env, jpixels, pixels,
                                              JNI_ABORT);
    }
    return 0;
}

static int
cvtDefaultToCustom(JNIEnv *env, BufImageS_t *imageP, int component,
                   unsigned char *dataP) {
    ColorModelS_t *cmP = &imageP->cmodel;
    RasterS_t *rasterP = &imageP->raster;
    int y;
    jint *pixels;
    unsigned char *dP = dataP;
#define NUM_LINES    10
    int numLines = NUM_LINES;
    int nbytes = rasterP->width*4*NUM_LINES;
    jintArray jpixels;

    jpixels = (*env)->NewIntArray(env, nbytes);
    if (JNU_IsNull(env, jpixels)) {
        JNU_ThrowOutOfMemoryError(env, "Out of Memory");
        return -1;
    }

    for (y=0; y < rasterP->height; y+=NUM_LINES) {
        if (y+numLines > rasterP->height) {
            numLines = rasterP->height - y;
            nbytes = rasterP->width*4*numLines;
        }
        pixels = (*env)->GetPrimitiveArrayCritical(env, jpixels, NULL);
        if (pixels == NULL) {
            /* JNI error */
            return -1;
        }

        memcpy(pixels, dP, nbytes);
        dP += nbytes;

       (*env)->ReleasePrimitiveArrayCritical(env, jpixels, pixels, 0);

       /* setData, one scanline at a time */
       /* Fix 4223648, 4184283 */
       (*env)->CallVoidMethod(env, imageP->jimage, g_BImgSetRGBMID, 0, y,
                                rasterP->width, numLines, jpixels, 0,
                                rasterP->width);
       if ((*env)->ExceptionOccurred(env)) {
           return -1;
       }
    }

    /* Need to release the array */
    (*env)->DeleteLocalRef(env, jpixels);

    return 0;
}

static int
allocateArray(JNIEnv *env, BufImageS_t *imageP,
              mlib_image **mlibImagePP, void **dataPP, int isSrc,
              int cvtToDefault, int addAlpha) {
    void *dataP;
    unsigned char *cDataP;
    RasterS_t *rasterP = &imageP->raster;
    ColorModelS_t *cmP = &imageP->cmodel;
    int dataType = BYTE_DATA_TYPE;
    int width;
    int height;
    HintS_t *hintP = &imageP->hints;
    *dataPP = NULL;

    width = rasterP->width;
    height = rasterP->height;

    /* Useful for convolution? */
    /* This code is zero'ed out so that it cannot be called */

    /* To do this correctly, we need to expand src and dst in the     */
    /* same direction up/down/left/right only if both can be expanded */
    /* in that direction.  Expanding right and down is easy -         */
    /* increment width.  Expanding top and left requires bumping      */
    /* around pointers and incrementing the width/height              */

#if 0
    if (0 && useEdges) {
        baseWidth  = rasterP->baseRasterWidth;
        baseHeight = rasterP->baseRasterHeight;
        baseXoff = rasterP->baseOriginX;
        baseYoff = rasterP->baseOriginY;

        if (rasterP->minX + rasterP->width < baseXoff + baseWidth) {
            /* Can use edge */
            width++;
        }
        if (rasterP->minY + rasterP->height < baseYoff + baseHeight) {
            /* Can use edge */
            height++;
        }

        if (rasterP->minX > baseXoff ) {
            /* Can use edge */
            width++;
            /* NEED TO BUMP POINTER BACK A PIXELSTRIDE */
        }
        if (rasterP->minY  > baseYoff) {
            /* Can use edge */
            height++;
            /* NEED TO BUMP POINTER BACK A SCANLINE */
        }


    }
#endif
    if (cvtToDefault) {
        int status = 0;
        *mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, 4, width, height);
        cDataP  = (unsigned char *) mlib_ImageGetData(*mlibImagePP);
        /* Make sure the image is cleared */
        memset(cDataP, 0, width*height*4);

        if (!isSrc) {
            return 0;
        }

        switch(imageP->cmodel.cmType) {
        case INDEX_CM_TYPE:
            /* REMIND: Need to rearrange according to dst cm */
            /* Fix 4213160, 4184283 */
            if (rasterP->rasterType == COMPONENT_RASTER_TYPE) {
                return expandICM(env, imageP, (unsigned int *)cDataP);
            }
            else {
                return cvtCustomToDefault(env, imageP, -1, cDataP);
            }

        case DIRECT_CM_TYPE:
            switch(imageP->raster.dataType) {
            case BYTE_DATA_TYPE:
                return expandPackedBCRdefault(env, rasterP, -1, cDataP,
                                              !imageP->cmodel.supportsAlpha);
            case SHORT_DATA_TYPE:
                return expandPackedSCRdefault(env, rasterP, -1, cDataP,
                                              !imageP->cmodel.supportsAlpha);
            case INT_DATA_TYPE:
                return expandPackedICRdefault(env, rasterP, -1, cDataP,
                                              !imageP->cmodel.supportsAlpha);
            }
        } /* switch(imageP->cmodel.cmType) */

        return cvtCustomToDefault(env, imageP, -1, cDataP);
    }

    /* Interleaved with shared data */
    dataP = (void *) (*env)->GetPrimitiveArrayCritical(env, rasterP->jdata,
                                                       NULL);
    if (dataP == NULL) {
        return -1;
    }

    /* Means we need to fill in alpha */
    if (!cvtToDefault && addAlpha) {
        *mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, 4, width, height);
        if (*mlibImagePP != NULL) {
            unsigned int *dstP  = (unsigned int *)
                mlib_ImageGetData(*mlibImagePP);
            int dstride = (*mlibImagePP)->stride>>2;
            int sstride = hintP->sStride>>2;
            unsigned int *srcP = (unsigned int *)
                ((unsigned char *)dataP + hintP->dataOffset);
            unsigned int *dP, *sP;
            int x, y;
            for (y=0; y < height; y++, srcP += sstride, dstP += dstride){
                sP = srcP;
                dP = dstP;
                for (x=0; x < width; x++) {
                    dP[x] = sP[x] | 0xff000000;
                }
            }
        }
        (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, dataP,
                                              JNI_ABORT);
        return 0;
    }
    else if ((hintP->packing & BYTE_INTERLEAVED) == BYTE_INTERLEAVED) {
        int nChans = (cmP->isDefaultCompatCM ? 4 : hintP->numChans);
        /* Easy case.  It is or is similar to the default CM so use
     * the array.  Must be byte data.
         */
            /* Create the medialib image */
        *mlibImagePP = (*sMlibSysFns.createStructFP)(MLIB_BYTE,
                                              nChans,
                                              width,
                                              height,
                                              hintP->sStride,
                                              (unsigned char *)dataP
                                              + hintP->dataOffset);
    }
    else if ((hintP->packing & SHORT_INTERLEAVED) == SHORT_INTERLEAVED) {
        *mlibImagePP = (*sMlibSysFns.createStructFP)(MLIB_SHORT,
                                              hintP->numChans,
                                              width,
                                              height,
                                              imageP->raster.scanlineStride*2,
                                              (unsigned short *)dataP
                                              + hintP->channelOffset);
    }
    else {
        /* Release the data array */
        (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, dataP,
                                              JNI_ABORT);
        return -1;
    }

    *dataPP = dataP;
    return 0;
}

static int
allocateRasterArray(JNIEnv *env, RasterS_t *rasterP,
                    mlib_image **mlibImagePP, void **dataPP, int isSrc) {
    void *dataP;
    unsigned char *cDataP;
    unsigned short *sdataP;
    int dataType = BYTE_DATA_TYPE;
    int width;
    int height;
    int dataSize;
    int offset;

    *dataPP = NULL;

    width = rasterP->width;
    height = rasterP->height;

    if (rasterP->numBands <= 0 || rasterP->numBands > 4) {
        /* REMIND: Fix this */
        return -1;
    }

    /* Useful for convolution? */
    /* This code is zero'ed out so that it cannot be called */

    /* To do this correctly, we need to expand src and dst in the     */
    /* same direction up/down/left/right only if both can be expanded */
    /* in that direction.  Expanding right and down is easy -         */
    /* increment width.  Expanding top and left requires bumping      */
    /* around pointers and incrementing the width/height              */

#if 0
    if (0 && useEdges) {
        baseWidth  = rasterP->baseRasterWidth;
        baseHeight = rasterP->baseRasterHeight;
        baseXoff = rasterP->baseOriginX;
        baseYoff = rasterP->baseOriginY;

        if (rasterP->minX + rasterP->width < baseXoff + baseWidth) {
            /* Can use edge */
            width++;
        }
        if (rasterP->minY + rasterP->height < baseYoff + baseHeight) {
            /* Can use edge */
            height++;
        }

        if (rasterP->minX > baseXoff ) {
            /* Can use edge */
            width++;
            /* NEED TO BUMP POINTER BACK A PIXELSTRIDE */
        }
        if (rasterP->minY  > baseYoff) {
            /* Can use edge */
            height++;
            /* NEED TO BUMP POINTER BACK A SCANLINE */
        }


    }
#endif
    switch (rasterP->type) {
    case sun_awt_image_IntegerComponentRaster_TYPE_INT_8BIT_SAMPLES:
        if (!((rasterP->chanOffsets[0] == 0 || SAFE_TO_ALLOC_2(rasterP->chanOffsets[0], 4)) &&
              SAFE_TO_ALLOC_2(width, 4) &&
              SAFE_TO_ALLOC_3(height, rasterP->scanlineStride, 4)))
        {
            return -1;
        }
        offset = 4 * rasterP->chanOffsets[0];
        dataSize = 4 * (*env)->GetArrayLength(env, rasterP->jdata);

        if (offset < 0 || offset >= dataSize ||
            width > rasterP->scanlineStride ||
            height * rasterP->scanlineStride * 4 > dataSize - offset)
        {
            // raster data buffer is too short
            return -1;
        }
        dataP = (void *) (*env)->GetPrimitiveArrayCritical(env, rasterP->jdata,
                                                           NULL);
        if (dataP == NULL) {
            return -1;
        }
        *mlibImagePP = (*sMlibSysFns.createStructFP)(MLIB_BYTE, 4,
                                              width, height,
                                              rasterP->scanlineStride*4,
                                              (unsigned char *)dataP + offset);
        *dataPP = dataP;
        return 0;
    case sun_awt_image_IntegerComponentRaster_TYPE_BYTE_SAMPLES:
        if (!(SAFE_TO_ALLOC_2(width, rasterP->numBands) &&
              SAFE_TO_ALLOC_2(height, rasterP->scanlineStride)))
        {
            return -1;
        }
        offset = rasterP->chanOffsets[0];
        dataSize = (*env)->GetArrayLength(env, rasterP->jdata);

        if (offset < 0 || offset >= dataSize ||
            width * rasterP->numBands > rasterP->scanlineStride ||
            height * rasterP->scanlineStride > dataSize - offset)
        {
            // raster data buffer is too short
            return -1;
        }
        dataP = (void *) (*env)->GetPrimitiveArrayCritical(env, rasterP->jdata,
                                                           NULL);
        if (dataP == NULL) {
            return -1;
        }
        *mlibImagePP = (*sMlibSysFns.createStructFP)(MLIB_BYTE, rasterP->numBands,
                                              width, height,
                                              rasterP->scanlineStride,
                                              (unsigned char *)dataP + offset);
        *dataPP = dataP;
        return 0;
    case sun_awt_image_IntegerComponentRaster_TYPE_USHORT_SAMPLES:
        if (!((rasterP->chanOffsets[0] == 0 || SAFE_TO_ALLOC_2(rasterP->chanOffsets[0], 2)) &&
              SAFE_TO_ALLOC_3(width, rasterP->numBands, 2) &&
              SAFE_TO_ALLOC_3(height, rasterP->scanlineStride, 2)))
        {
              return -1;
        }
        offset = rasterP->chanOffsets[0] * 2;
        dataSize = 2 * (*env)->GetArrayLength(env, rasterP->jdata);

        if (offset < 0 || offset >= dataSize ||
            width * rasterP->numBands > rasterP->scanlineStride ||
            height * rasterP->scanlineStride * 2 > dataSize - offset)
        {
            // raster data buffer is too short
             return -1;
        }
        dataP = (void *) (*env)->GetPrimitiveArrayCritical(env, rasterP->jdata,
                                                           NULL);
        if (dataP == NULL) {
            return -1;
        }
        *mlibImagePP = (*sMlibSysFns.createStructFP)(MLIB_SHORT,
                                                     rasterP->numBands,
                                                     width, height,
                                                     rasterP->scanlineStride*2,
                                                     (unsigned char *)dataP + offset);
        *dataPP = dataP;
        return 0;

    case sun_awt_image_IntegerComponentRaster_TYPE_BYTE_PACKED_SAMPLES:
        *mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, rasterP->numBands,
                                        width, height);
        if (!isSrc) return 0;
        cDataP  = (unsigned char *) mlib_ImageGetData(*mlibImagePP);
        return expandPackedBCR(env, rasterP, -1, cDataP);

    case sun_awt_image_IntegerComponentRaster_TYPE_USHORT_PACKED_SAMPLES:
        if (rasterP->sppsm.maxBitSize <= 8) {
            *mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, rasterP->numBands,
                                            width, height);
            if (!isSrc) return 0;
            cDataP  = (unsigned char *) mlib_ImageGetData(*mlibImagePP);
            return expandPackedSCR(env, rasterP, -1, cDataP);
        }
        break;
    case sun_awt_image_IntegerComponentRaster_TYPE_INT_PACKED_SAMPLES:
        if (rasterP->sppsm.maxBitSize <= 8) {
            *mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, rasterP->numBands,
                                            width, height);
            if (!isSrc) return 0;
            cDataP  = (unsigned char *) mlib_ImageGetData(*mlibImagePP);
            return expandPackedICR(env, rasterP, -1, cDataP);
        }
        break;
    }

    /* Just expand it right now */
    switch (rasterP->dataType) {
    case BYTE_DATA_TYPE:
        if ((*mlibImagePP = (*sMlibSysFns.createFP)(MLIB_BYTE, rasterP->numBands,
                                             width, height)) == NULL) {
            return -1;
        }
        if (isSrc) {
            cDataP  = (unsigned char *) mlib_ImageGetData(*mlibImagePP);
            if (awt_getPixelByte(env, -1, rasterP, cDataP) < 0) {
                (*sMlibSysFns.deleteImageFP)(*mlibImagePP);
                return -1;
            }
        }
        break;

    case SHORT_DATA_TYPE:
        if ((*mlibImagePP = (*sMlibSysFns.createFP)(MLIB_SHORT,
                                                    rasterP->numBands,
                                                    width, height)) == NULL) {
            return -1;
        }
        if (isSrc) {
            sdataP  = (unsigned short *) mlib_ImageGetData(*mlibImagePP);
            if (awt_getPixelShort(env, -1, rasterP, sdataP) < 0) {
                (*sMlibSysFns.deleteImageFP)(*mlibImagePP);
                return -1;
            }
        }
        break;

    default:
        return -1;
    }
    return 0;
}

static void
freeArray(JNIEnv *env, BufImageS_t *srcimageP, mlib_image *srcmlibImP,
          void *srcdataP, BufImageS_t *dstimageP, mlib_image *dstmlibImP,
          void *dstdataP) {
    jobject srcJdata = (srcimageP != NULL ? srcimageP->raster.jdata : NULL);
    jobject dstJdata = (dstimageP != NULL ? dstimageP->raster.jdata : NULL);
    freeDataArray(env, srcJdata, srcmlibImP, srcdataP,
                  dstJdata, dstmlibImP, dstdataP);
}
static void
freeDataArray(JNIEnv *env, jobject srcJdata, mlib_image *srcmlibImP,
          void *srcdataP, jobject dstJdata, mlib_image *dstmlibImP,
          void *dstdataP)
{
    /* Free the medialib image */
    if (srcmlibImP) {
        (*sMlibSysFns.deleteImageFP)(srcmlibImP);
    }

    /* Release the array */
    if (srcdataP) {
        (*env)->ReleasePrimitiveArrayCritical(env, srcJdata,
                                              srcdataP, JNI_ABORT);
    }

    /* Free the medialib image */
    if (dstmlibImP) {
        (*sMlibSysFns.deleteImageFP)(dstmlibImP);
    }

    /* Release the array */
    if (dstdataP) {
        (*env)->ReleasePrimitiveArrayCritical(env, dstJdata,
                                              dstdataP, 0);
    }
}

static int
storeDstArray(JNIEnv *env,  BufImageS_t *srcP, BufImageS_t *dstP,
              mlibHintS_t *hintP, mlib_image *mlibImP, void *ddata) {
    RasterS_t *rasterP = &dstP->raster;

    /* Nothing to do since it is the same image type */
    if (srcP->imageType == dstP->imageType
        && srcP->imageType != java_awt_image_BufferedImage_TYPE_CUSTOM
        && srcP->imageType != java_awt_image_BufferedImage_TYPE_BYTE_INDEXED
        && srcP->imageType != java_awt_image_BufferedImage_TYPE_BYTE_BINARY) {
        /* REMIND: Should check the ICM LUTS to see if it is the same */
        return 0;
    }

    /* These types are compatible with TYPE_INT_RGB */
    if (srcP->imageType == java_awt_image_BufferedImage_TYPE_INT_RGB
        && (dstP->imageType == java_awt_image_BufferedImage_TYPE_INT_ARGB ||
           dstP->imageType == java_awt_image_BufferedImage_TYPE_INT_ARGB_PRE)){
        return 0;
    }

    if (hintP->cvtSrcToDefault &&
        (srcP->cmodel.isAlphaPre == dstP->cmodel.isAlphaPre)) {
        if (srcP->cmodel.isAlphaPre) {
            if (dstP->imageType ==
                java_awt_image_BufferedImage_TYPE_INT_ARGB_PRE)
            {
                return 0;
            }
            if (!srcP->cmodel.supportsAlpha &&
                dstP->imageType == java_awt_image_BufferedImage_TYPE_INT_RGB){
                return 0;
            }
        }
        else {
            /* REMIND: */
        }
    }

    if (dstP->cmodel.cmType == DIRECT_CM_TYPE) {
        /* Just need to move bits */
        if (mlibImP->type == MLIB_BYTE) {
            return awt_setPixelByte(env, -1, &dstP->raster,
                                    (unsigned char *) mlibImP->data);
        }
        else if (mlibImP->type == MLIB_SHORT) {
            return awt_setPixelByte(env, -1, &dstP->raster,
                                    (unsigned char *) mlibImP->data);
        }
    }

    return 0;
}

static int
storeImageArray(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
                mlib_image *mlibImP) {
    int mStride;
    unsigned char *cmDataP, *dataP, *cDataP;
    HintS_t *hintP = &dstP->hints;
    RasterS_t *rasterP = &dstP->raster;
    int y;

    /* REMIND: Store mlib data type? */

    /* Check if it is an IndexColorModel */
    if (dstP->cmodel.cmType == INDEX_CM_TYPE) {
        if (dstP->raster.rasterType == COMPONENT_RASTER_TYPE) {
            return storeICMarray(env, srcP, dstP, mlibImP);
        }
        else {
            /* Packed or some other custom raster */
            cmDataP = (unsigned char *) mlib_ImageGetData(mlibImP);
            return cvtDefaultToCustom(env, dstP, -1, cmDataP);
        }
    }

    if (hintP->packing == BYTE_INTERLEAVED) {
        /* Write it back to the destination */
        cmDataP = (unsigned char *) mlib_ImageGetData(mlibImP);
        mStride = mlib_ImageGetStride(mlibImP);
        dataP = (unsigned char *)(*env)->GetPrimitiveArrayCritical(env,
                                                      rasterP->jdata, NULL);
        if (dataP == NULL) return 0;
        cDataP = dataP + hintP->dataOffset;
        for (y=0; y < rasterP->height;
             y++, cmDataP += mStride, cDataP += hintP->sStride)
        {
            memcpy(cDataP, cmDataP, rasterP->width*hintP->numChans);
        }
        (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, dataP,
                                              JNI_ABORT);
    }
    else if (hintP->packing == SHORT_INTERLEAVED) {
        /* Write it back to the destination */
        unsigned short *sdataP, *sDataP;
        unsigned short *smDataP = (unsigned short *)mlib_ImageGetData(mlibImP);
        mStride = mlib_ImageGetStride(mlibImP);
        sdataP = (unsigned short *)(*env)->GetPrimitiveArrayCritical(env,
                                                      rasterP->jdata, NULL);
        if (sdataP == NULL) return -1;
        sDataP = sdataP + hintP->dataOffset;
        for (y=0; y < rasterP->height;
             y++, smDataP += mStride, sDataP += hintP->sStride)
        {
            memcpy(sDataP, smDataP, rasterP->width*hintP->numChans);
        }
        (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, sdataP,
                                              JNI_ABORT);
    }
    else if (dstP->cmodel.cmType == DIRECT_CM_TYPE) {
        /* Just need to move bits */
        if (mlibImP->type == MLIB_BYTE) {
            if (dstP->hints.packing == PACKED_BYTE_INTER) {
                return setPackedBCRdefault(env, rasterP, -1,
                                           (unsigned char *) mlibImP->data,
                                           dstP->cmodel.supportsAlpha);
            } else if (dstP->hints.packing == PACKED_SHORT_INTER) {
                return setPackedSCRdefault(env, rasterP, -1,
                                           (unsigned char *) mlibImP->data,
                                           dstP->cmodel.supportsAlpha);
            } else if (dstP->hints.packing == PACKED_INT_INTER) {
                return setPackedICRdefault(env, rasterP, -1,
                                           (unsigned char *) mlibImP->data,
                                           dstP->cmodel.supportsAlpha);
            }
        }
        else if (mlibImP->type == MLIB_SHORT) {
            return awt_setPixelShort(env, -1, rasterP,
                                    (unsigned short *) mlibImP->data);
        }
    }
    else {
        return cvtDefaultToCustom(env, dstP, -1,
                                  (unsigned char *)mlibImP->data);
    }

    return 0;
}

static int
storeRasterArray(JNIEnv *env, RasterS_t *srcP, RasterS_t *dstP,
                mlib_image *mlibImP) {
    unsigned char *cDataP;

    switch(dstP->type) {
    case sun_awt_image_IntegerComponentRaster_TYPE_BYTE_PACKED_SAMPLES:
        cDataP  = (unsigned char *) mlib_ImageGetData(mlibImP);
        return setPackedBCR(env, dstP, -1, cDataP);

    case sun_awt_image_IntegerComponentRaster_TYPE_USHORT_PACKED_SAMPLES:
        if (dstP->sppsm.maxBitSize <= 8) {
            cDataP  = (unsigned char *) mlib_ImageGetData(mlibImP);
            return setPackedSCR(env, dstP, -1, cDataP);
        }
        break;
    case sun_awt_image_IntegerComponentRaster_TYPE_INT_PACKED_SAMPLES:
        if (dstP->sppsm.maxBitSize <= 8) {
            cDataP  = (unsigned char *) mlib_ImageGetData(mlibImP);
            return setPackedICR(env, dstP, -1, cDataP);
        }
    }

    return -1;
}


static int
storeICMarray(JNIEnv *env, BufImageS_t *srcP, BufImageS_t *dstP,
              mlib_image *mlibImP)
{
    int *argb;
    int x, y;
    unsigned char *dataP, *cDataP, *cP;
    unsigned char *sP;
    int aIdx, rIdx, gIdx, bIdx;
    ColorModelS_t *cmodelP = &dstP->cmodel;
    RasterS_t *rasterP = &dstP->raster;

    /* REMIND: Only works for RGB */
    if (cmodelP->csType != java_awt_color_ColorSpace_TYPE_RGB) {
        JNU_ThrowInternalError(env, "Writing to non-RGB images not implemented yet");
        return -1;
    }

    if (srcP->imageType == java_awt_image_BufferedImage_TYPE_INT_ARGB ||
        srcP->imageType == java_awt_image_BufferedImage_TYPE_INT_ARGB_PRE ||
        srcP->imageType == java_awt_image_BufferedImage_TYPE_INT_RGB)
    {
        aIdx = 0;
        rIdx = 1;
        gIdx = 2;
        bIdx = 3;
    }
    else if (srcP->imageType ==java_awt_image_BufferedImage_TYPE_4BYTE_ABGR||
        srcP->imageType == java_awt_image_BufferedImage_TYPE_4BYTE_ABGR_PRE)
    {
        aIdx = 0;
        rIdx = 3;
        gIdx = 2;
        bIdx = 1;
    }
    else if (srcP->imageType == java_awt_image_BufferedImage_TYPE_3BYTE_BGR){
        rIdx = 2;
        gIdx = 1;
        bIdx = 0;
        aIdx = 0;       /* Ignored */
    }
    else if (srcP->cmodel.cmType == INDEX_CM_TYPE) {
        rIdx = 0;
        gIdx = 1;
        bIdx = 2;
        aIdx = 3;   /* Use supportsAlpha to see if it is really there */
    }
    else {
        return -1;
    }

    /* Lock down the destination raster */
    dataP = (unsigned char *) (*env)->GetPrimitiveArrayCritical(env,
                                                  rasterP->jdata, NULL);
    if (dataP == NULL) {
        return -1;
    }
    argb = (*env)->GetPrimitiveArrayCritical(env, cmodelP->jrgb, NULL);
    if (argb == NULL) {
        (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, dataP,
                                              JNI_ABORT);
        return -1;
    }

    cDataP = dataP + dstP->hints.dataOffset;
    sP = (unsigned char *) mlib_ImageGetData(mlibImP);

    for (y=0; y < rasterP->height; y++, cDataP += rasterP->scanlineStride) {
        cP = cDataP;
        for (x=0; x < rasterP->width; x++, cP += rasterP->pixelStride) {
            *cP = colorMatch(sP[rIdx], sP[gIdx], sP[bIdx], sP[aIdx],
                             (unsigned char *)argb, cmodelP->mapSize);
            sP += cmodelP->numComponents;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, cmodelP->jrgb, argb, JNI_ABORT);
    (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata, dataP,
                                          JNI_ABORT);
    return -1;
}

static int expandICM(JNIEnv *env, BufImageS_t *imageP, unsigned int *mDataP)
{
    ColorModelS_t *cmP = &imageP->cmodel;
    RasterS_t *rasterP = &imageP->raster;
    HintS_t *hintP     = &imageP->hints;
    int *rgb;
    int status = 0;
    unsigned char *dataP, *cP;
    unsigned int *mP;
    int width = rasterP->width;
    int height = rasterP->height;
    int x, y;

    /* Need to grab the lookup tables.  Right now only bytes */
    rgb = (int *) (*env)->GetPrimitiveArrayCritical(env, cmP->jrgb, NULL);

    /* Interleaved with shared data */
    dataP = (void *) (*env)->GetPrimitiveArrayCritical(env,
                                                       rasterP->jdata, NULL);
    if (rgb == NULL || dataP == NULL) {
        /* Release the lookup tables */
        if (rgb) {
            (*env)->ReleasePrimitiveArrayCritical(env, cmP->jrgb, rgb,
                                                  JNI_ABORT);
        }
        if (dataP) {
            (*env)->ReleasePrimitiveArrayCritical(env,
                                                  rasterP->jdata, dataP,
                                                  JNI_ABORT);
        }
        return -1;
    }

    if (rasterP->dataType == BYTE_DATA_TYPE) {
        unsigned char *cDataP = ((unsigned char *)dataP) + hintP->dataOffset;

        for (y=0; y < height; y++) {
            mP = mDataP;
            cP = cDataP;
            for (x=0; x < width; x++, cP += rasterP->pixelStride) {
                *mP++ = rgb[*cP];
            }
            mDataP += width;
            cDataP += rasterP->scanlineStride;
        }
    }
    else if (rasterP->dataType == SHORT_DATA_TYPE) {
        unsigned short *sDataP, *sP;
        sDataP = ((unsigned short *)dataP) + hintP->channelOffset;

        for (y=0; y < height; y++) {
            mP = mDataP;
            sP = sDataP;
            for (x=0; x < width; x++, sP+=rasterP->pixelStride) {
                *mP++ = rgb[*sP];
            }
            mDataP += width;
            sDataP += rasterP->scanlineStride;
        }
    }
    else {
        /* Unknown type */
        status = -1;
    }
    /* Release the lookup table data */
    (*env)->ReleasePrimitiveArrayCritical(env, imageP->cmodel.jrgb,
                                          rgb, JNI_ABORT);
    /* Release the data array */
    (*env)->ReleasePrimitiveArrayCritical(env, rasterP->jdata,
                                          dataP, JNI_ABORT);
    return status;
}
/* This routine is expecting a ByteComponentRaster with a PackedColorModel */
static int expandPackedBCR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned char *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_BCRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned char *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }
        /* Convert the all bands */
        if (rasterP->numBands < 4) {
            /* Need to put in alpha */
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)
                    ((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0];
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ByteComponentRaster with a PackedColorModel */
static int expandPackedBCRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned char *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int numBands = rasterP->numBands - (forceAlpha ? 0 : 1);
    int a = numBands;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_BCRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned char *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }

        /* Need to put in alpha */
        if (forceAlpha) {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = 0xff;
                    for (c=0; c < numBands; c++) {
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = (unsigned char)
                        (((*inP&rasterP->sppsm.maskArray[a]) >> roff[a])
                         <<loff[a]);
                    for (c=0; c < numBands; c++) {
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)
                    ((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0];
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ShortComponentRaster with a PackedColorModel */
static int expandPackedSCR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned short *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_SCRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned short *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }
        /* Convert the all bands */
        if (rasterP->numBands < 4) {
            /* Need to put in alpha */
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        /*
                         *Not correct.  Might need to unpremult,
                         * shift, etc
                         */
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        } else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        /*
                         *Not correct.  Might need to unpremult,
                         * shift, etc
                         */
                        *outP++ = (unsigned char)
                            (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                             <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)
                    ((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0];
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ShortComponentRaster with a PackedColorModel */
static int expandPackedSCRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned short *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int numBands = rasterP->numBands - (forceAlpha ? 0 : 1);
    int a = numBands;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_SCRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned short *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }

        /* Need to put in alpha */
        if (forceAlpha) {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = 0xff;
                    for (c=0; c < numBands; c++) {
                        /*
                         * Not correct.  Might need to unpremult,
                         * shift, etc
                         */
                        *outP++ = (unsigned char)
                                (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = (unsigned char)
                        (((*inP&rasterP->sppsm.maskArray[a]) >> roff[a])
                                   <<loff[a]);
                    for (c=0; c < numBands; c++) {
                        /*
                         * Not correct.  Might need to
                         * unpremult, shift, etc
                         */
                        *outP++ = (unsigned char)
                                (((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)
                        ((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0];
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;

}

/* This routine is expecting a IntegerComponentRaster with a PackedColorModel*/
static int expandPackedICR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *outDataP)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned int *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_ICRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned int *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }
        /* Convert the all bands */
        if (rasterP->numBands < 4) {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        /*
                         * Not correct.  Might need to unpremult,
                         * shift, etc
                         */
                        *outP++ = (unsigned char)(((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    for (c=0; c < rasterP->numBands; c++) {
                        /*
                         * Not correct.  Might need to
                         * unpremult, shift, etc
                         */
                        *outP++ = (unsigned char)(((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)(((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0]);
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a IntegerComponentRaster with a PackedColorModel*/
static int expandPackedICRdefault(JNIEnv *env, RasterS_t *rasterP,
                                  int component, unsigned char *outDataP,
                                  int forceAlpha)
{
    int x, y, c;
    unsigned char *outP = outDataP;
    unsigned int *lineInP, *inP;
    jarray jInDataP;
    jint   *inDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int numBands = rasterP->numBands - (forceAlpha ? 0 : 1);
    int a = numBands;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jInDataP = (*env)->GetObjectField(env, rasterP->jraster, g_ICRdataID);
    inDataP = (*env)->GetPrimitiveArrayCritical(env, jInDataP, 0);
    if (inDataP == NULL) {
        return -1;
    }
    lineInP =  (unsigned int *)inDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            roff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (roff[c] < 0) {
                loff[c] = -roff[c];
                roff[c] = 0;
            }
            else loff[c] = 0;
        }

        /* Need to put in alpha */
        if (forceAlpha) {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = 0xff;
                    for (c=0; c < numBands; c++) {
                        /*
                         * Not correct.  Might need to unpremult,
                         * shift, etc
                         */
                        *outP++ = (unsigned char)(((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                inP = lineInP;
                for (x=0; x < rasterP->width; x++) {
                    *outP++ = (unsigned char)(((*inP&rasterP->sppsm.maskArray[a]) >> roff[a])
                                   <<loff[a]);
                    for (c=0; c < numBands; c++) {
                        /*
                         * Not correct.  Might need to
                         * unpremult, shift, etc
                         */
                        *outP++ = (unsigned char)(((*inP&rasterP->sppsm.maskArray[c]) >> roff[c])
                                   <<loff[c]);
                    }
                    inP++;
                }
                lineInP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        roff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (roff[0] < 0) {
            loff[0] = -roff[0];
            roff[0] = 0;
        }
        else loff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            inP = lineInP;
            for (x=0; x < rasterP->width; x++) {
                *outP++ = (unsigned char)(((*inP & rasterP->sppsm.maskArray[c])>>roff[0])<<loff[0]);
                inP++;
            }
            lineInP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jInDataP, inDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ByteComponentRaster with a PackedColorModel */
static int setPackedBCR(JNIEnv *env, RasterS_t *rasterP, int component,
                        unsigned char *inDataP)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned char *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_BCRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned char *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            *outP = 0;
            for (x=0; x < rasterP->width; x++) {
                for (c=0; c < rasterP->numBands; c++, inP++) {
                    *outP |= (*inP<<loff[c]>>roff[c])&rasterP->sppsm.maskArray[c];
                }
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ShortComponentRaster with a PackedColorModel */
static int setPackedSCR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *inDataP)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned short *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_SCRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned short *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++) {
                for (c=0; c < rasterP->numBands; c++, inP++) {
                    /* Not correct.  Might need to unpremult, shift, etc */
                    *outP |= (*inP<<loff[c]>>roff[c])&rasterP->sppsm.maskArray[c];
                }
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a IntegerComponentRaster with a PackedColorModel*/
static int setPackedICR(JNIEnv *env, RasterS_t *rasterP, int component,
                           unsigned char *inDataP)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned int *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_ICRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned int *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++) {
                for (c=0; c < rasterP->numBands; c++, inP++) {
                    /* Not correct.  Might need to unpremult, shift, etc */
                    *outP |= (*inP<<loff[c]>>roff[c])&rasterP->sppsm.maskArray[c];
                }
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;

        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ByteComponentRaster with a PackedColorModel */
static int setPackedBCRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *inDataP,
                               int supportsAlpha)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned char *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int a = rasterP->numBands - 1;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_BCRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned char *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        if (supportsAlpha) {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                *outP = 0;
                for (x=0; x < rasterP->width; x++) {
                    *outP |= (*inP<<loff[a]>>roff[a])&
                        rasterP->sppsm.maskArray[a];
                    inP++;
                    for (c=0; c < rasterP->numBands-1; c++, inP++) {
                        *outP |= (*inP<<loff[c]>>roff[c])&
                            rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                *outP = 0;
                for (x=0; x < rasterP->width; x++) {
                    inP++;
                    for (c=0; c < rasterP->numBands; c++, inP++) {
                        *outP |= (*inP<<loff[c]>>roff[c])&rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a ShortComponentRaster with a PackedColorModel */
static int setPackedSCRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *inDataP,
                               int supportsAlpha)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned short *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int a = rasterP->numBands - 1;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_SCRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned short *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        if (supportsAlpha) {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                for (x=0; x < rasterP->width; x++) {
                    *outP |= (*inP<<loff[a]>>roff[a])&
                        rasterP->sppsm.maskArray[a];
                    inP++;
                    for (c=0; c < rasterP->numBands-1; c++, inP++) {
                        /* Not correct.  Might need to unpremult, shift, etc */
                        *outP |= (*inP<<loff[c]>>roff[c])&
                            rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                for (x=0; x < rasterP->width; x++) {
                    inP++;
                    for (c=0; c < rasterP->numBands; c++, inP++) {
                        /* Not correct.  Might need to unpremult, shift, etc */
                        *outP |= (*inP<<loff[c]>>roff[c])&rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;
        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This routine is expecting a IntegerComponentRaster with a PackedColorModel*/
static int setPackedICRdefault(JNIEnv *env, RasterS_t *rasterP,
                               int component, unsigned char *inDataP,
                               int supportsAlpha)
{
    int x, y, c;
    unsigned char *inP = inDataP;
    unsigned int *lineOutP, *outP;
    jarray jOutDataP;
    jint   *outDataP;
    int loff[MAX_NUMBANDS], roff[MAX_NUMBANDS];
    int a = rasterP->numBands - 1;

    if (rasterP->numBands > MAX_NUMBANDS) {
        return -1;
    }

    /* Grab data ptr, strides, offsets from raster */
    jOutDataP = (*env)->GetObjectField(env, rasterP->jraster, g_ICRdataID);
    outDataP = (*env)->GetPrimitiveArrayCritical(env, jOutDataP, 0);
    if (outDataP == NULL) {
        return -1;
    }
    lineOutP =  (unsigned int *)outDataP + rasterP->chanOffsets[0];

    if (component < 0) {
        for (c=0; c < rasterP->numBands; c++) {
            loff[c] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
            if (loff[c] < 0) {
                roff[c] = -loff[c];
                loff[c] = 0;
            }
            else roff[c] = 0;
        }
        /* Convert the all bands */
        if (supportsAlpha) {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                for (x=0; x < rasterP->width; x++) {
                    *outP |= (*inP<<loff[a]>>roff[a])&
                        rasterP->sppsm.maskArray[a];
                    inP++;
                    for (c=0; c < rasterP->numBands-1; c++, inP++) {
                        /* Not correct.  Might need to unpremult, shift, etc */
                        *outP |= (*inP<<loff[c]>>roff[c])&
                            rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
        else {
            for (y=0; y < rasterP->height; y++) {
                outP = lineOutP;
                for (x=0; x < rasterP->width; x++) {
                    inP++;
                    for (c=0; c < rasterP->numBands; c++, inP++) {
                        /* Not correct.  Might need to unpremult, shift, etc */
                        *outP |= (*inP<<loff[c]>>roff[c])&
                            rasterP->sppsm.maskArray[c];
                    }
                    outP++;
                }
                lineOutP += rasterP->scanlineStride;
            }
        }
    }
    else {
        c = component;
        loff[0] = rasterP->sppsm.offsets[c] + (rasterP->sppsm.nBits[c]-8);
        if (loff[0] < 0) {
            roff[0] = -loff[0];
            loff[0] = 0;
        }
        else roff[c] = 0;

        for (y=0; y < rasterP->height; y++) {
            outP = lineOutP;
            for (x=0; x < rasterP->width; x++, inP++) {
                *outP |= (*inP<<loff[0]>>roff[0])&rasterP->sppsm.maskArray[c];
                outP++;
            }
            lineOutP += rasterP->scanlineStride;
        }
    }

    (*env)->ReleasePrimitiveArrayCritical(env, jOutDataP, outDataP, JNI_ABORT);

    return 0;
}

/* This is temporary code.  Should go away when there is better color
 * conversion code available.
 * REMIND:  Ignoring alpha
 */
/* returns the absolute value x */
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define CLIP(val,min,max)       ((val < min) ? min : ((val > max) ? max : val))

static int
colorMatch(int r, int g, int b, int a, unsigned char *argb, int numColors) {
    int besti = 0;
    int mindist, i, t, d;
    unsigned char red, green, blue;

    r = CLIP(r, 0, 255);
    g = CLIP(g, 0, 255);
    b = CLIP(b, 0, 255);

    /* look for pure gray match */
    if ((r == g) && (g == b)) {
        mindist = 256;
        for (i = 0 ; i < numColors ; i++, argb+=4) {
            red = argb[1];
            green = argb[2];
            blue = argb[3];
            if (! ((red == green) && (green == blue)) ) {
                continue;
            }
            d = ABS(red - r);
            if (d == 0)
                return i;
            if (d < mindist) {
                besti = i;
                mindist = d;
            }
        }
        return besti;
    }

    /* look for non-pure gray match */
    mindist = 256 * 256 * 256;
    for (i = 0 ; i < numColors ; i++, argb+=4) {
        red = argb[1];
        green = argb[2];
        blue = argb[3];
        t = red - r;
        d = t * t;
        if (d >= mindist) {
            continue;
        }
        t = green - g;
        d += t * t;
        if (d >= mindist) {
            continue;
        }
        t = blue - b;
        d += t * t;
        if (d >= mindist) {
            continue;
        }
        if (d == 0)
            return i;
        if (d < mindist) {
            besti = i;
            mindist = d;
        }
    }

    return besti;
}
