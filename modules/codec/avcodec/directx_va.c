/*****************************************************************************
 * directx_va.c: DirectX Generic Video Acceleration helpers
 *****************************************************************************
 * Copyright (C) 2009 Geoffroy Couprie
 * Copyright (C) 2009 Laurent Aimar
 * Copyright (C) 2015 Steve Lhomme
 * $Id$
 *
 * Authors: Geoffroy Couprie <geal@videolan.org>
 *          Laurent Aimar <fenrir _AT_ videolan _DOT_ org>
 *          Steve Lhomme <robux4@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <vlc_common.h>
#include <vlc_codecs.h>
#include <vlc_codec.h>

#define COBJMACROS

#include "directx_va.h"

#include "avcodec.h"
#include "../../packetizer/h264_nal.h"
#include "../../packetizer/hevc_nal.h"

static const int PROF_MPEG2_SIMPLE[] = { FF_PROFILE_MPEG2_SIMPLE, 0 };
static const int PROF_MPEG2_MAIN[]   = { FF_PROFILE_MPEG2_SIMPLE,
                                         FF_PROFILE_MPEG2_MAIN, 0 };
static const int PROF_H264_HIGH[]    = { FF_PROFILE_H264_CONSTRAINED_BASELINE,
                                         FF_PROFILE_H264_MAIN,
                                         FF_PROFILE_H264_HIGH, 0 };
static const int PROF_HEVC_MAIN[]    = { FF_PROFILE_HEVC_MAIN, 0 };
static const int PROF_HEVC_MAIN10[]  = { FF_PROFILE_HEVC_MAIN,
                                         FF_PROFILE_HEVC_MAIN_10, 0 };

#include <d3d9.h>
#include <dxva2api.h>

#include <initguid.h> /* must be last included to not redefine existing GUIDs */

/* dxva2api.h GUIDs: http://msdn.microsoft.com/en-us/library/windows/desktop/ms697067(v=vs100).aspx
 * assume that they are declared in dxva2api.h */
#define MS_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)

#ifdef __MINGW32__
# include <_mingw.h>

# if !defined(__MINGW64_VERSION_MAJOR)
#  undef MS_GUID
#  define MS_GUID DEFINE_GUID /* dxva2api.h fails to declare those, redefine as static */
#  define DXVA2_E_NEW_VIDEO_DEVICE MAKE_HRESULT(1, 4, 4097)
# else
#  include <dxva.h>
# endif

#endif /* __MINGW32__ */

/* Codec capabilities GUID, sorted by codec */
MS_GUID    (DXVA2_ModeMPEG2_MoComp,                 0xe6a9f44b, 0x61b0, 0x4563, 0x9e, 0xa4, 0x63, 0xd2, 0xa3, 0xc6, 0xfe, 0x66);
MS_GUID    (DXVA2_ModeMPEG2_IDCT,                   0xbf22ad00, 0x03ea, 0x4690, 0x80, 0x77, 0x47, 0x33, 0x46, 0x20, 0x9b, 0x7e);
MS_GUID    (DXVA2_ModeMPEG2_VLD,                    0xee27417f, 0x5e28, 0x4e65, 0xbe, 0xea, 0x1d, 0x26, 0xb5, 0x08, 0xad, 0xc9);
DEFINE_GUID(DXVA_ModeMPEG1_A,                       0x1b81be09, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeMPEG2_A,                       0x1b81be0A, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeMPEG2_B,                       0x1b81be0B, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeMPEG2_C,                       0x1b81be0C, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeMPEG2_D,                       0x1b81be0D, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA2_ModeMPEG2and1_VLD,                0x86695f12, 0x340e, 0x4f04, 0x9f, 0xd3, 0x92, 0x53, 0xdd, 0x32, 0x74, 0x60);
DEFINE_GUID(DXVA2_ModeMPEG1_VLD,                    0x6f3ec719, 0x3735, 0x42cc, 0x80, 0x63, 0x65, 0xcc, 0x3c, 0xb3, 0x66, 0x16);

MS_GUID    (DXVA2_ModeH264_A,                       0x1b81be64, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeH264_B,                       0x1b81be65, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeH264_C,                       0x1b81be66, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeH264_D,                       0x1b81be67, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeH264_E,                       0x1b81be68, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeH264_F,                       0x1b81be69, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH264_VLD_Multiview,            0x9901CCD3, 0xca12, 0x4b7e, 0x86, 0x7a, 0xe2, 0x22, 0x3d, 0x92, 0x55, 0xc3); // MVC
DEFINE_GUID(DXVA_ModeH264_VLD_WithFMOASO_NoFGT,     0xd5f04ff9, 0x3418, 0x45d8, 0x95, 0x61, 0x32, 0xa7, 0x6a, 0xae, 0x2d, 0xdd);
DEFINE_GUID(DXVADDI_Intel_ModeH264_A,               0x604F8E64, 0x4951, 0x4c54, 0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6);
DEFINE_GUID(DXVADDI_Intel_ModeH264_C,               0x604F8E66, 0x4951, 0x4c54, 0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6);
DEFINE_GUID(DXVA_Intel_H264_NoFGT_ClearVideo,       0x604F8E68, 0x4951, 0x4c54, 0x88, 0xFE, 0xAB, 0xD2, 0x5C, 0x15, 0xB3, 0xD6);
DEFINE_GUID(DXVA_ModeH264_VLD_NoFGT_Flash,          0x4245F676, 0x2BBC, 0x4166, 0xa0, 0xBB, 0x54, 0xE7, 0xB8, 0x49, 0xC3, 0x80);

MS_GUID    (DXVA2_ModeWMV8_A,                       0x1b81be80, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeWMV8_B,                       0x1b81be81, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

MS_GUID    (DXVA2_ModeWMV9_A,                       0x1b81be90, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeWMV9_B,                       0x1b81be91, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeWMV9_C,                       0x1b81be94, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

MS_GUID    (DXVA2_ModeVC1_A,                        0x1b81beA0, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeVC1_B,                        0x1b81beA1, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeVC1_C,                        0x1b81beA2, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
MS_GUID    (DXVA2_ModeVC1_D,                        0x1b81beA3, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA2_ModeVC1_D2010,                    0x1b81beA4, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5); // August 2010 update
DEFINE_GUID(DXVA_Intel_VC1_ClearVideo,              0xBCC5DB6D, 0xA2B6, 0x4AF0, 0xAC, 0xE4, 0xAD, 0xB1, 0xF7, 0x87, 0xBC, 0x89);
DEFINE_GUID(DXVA_Intel_VC1_ClearVideo_2,            0xE07EC519, 0xE651, 0x4CD6, 0xAC, 0x84, 0x13, 0x70, 0xCC, 0xEE, 0xC8, 0x51);

DEFINE_GUID(DXVA_nVidia_MPEG4_ASP,                  0x9947EC6F, 0x689B, 0x11DC, 0xA3, 0x20, 0x00, 0x19, 0xDB, 0xBC, 0x41, 0x84);
DEFINE_GUID(DXVA_ModeMPEG4pt2_VLD_Simple,           0xefd64d74, 0xc9e8, 0x41d7, 0xa5, 0xe9, 0xe9, 0xb0, 0xe3, 0x9f, 0xa3, 0x19);
DEFINE_GUID(DXVA_ModeMPEG4pt2_VLD_AdvSimple_NoGMC,  0xed418a9f, 0x010d, 0x4eda, 0x9a, 0xe3, 0x9a, 0x65, 0x35, 0x8d, 0x8d, 0x2e);
DEFINE_GUID(DXVA_ModeMPEG4pt2_VLD_AdvSimple_GMC,    0xab998b5b, 0x4258, 0x44a9, 0x9f, 0xeb, 0x94, 0xe5, 0x97, 0xa6, 0xba, 0xae);
DEFINE_GUID(DXVA_ModeMPEG4pt2_VLD_AdvSimple_Avivo,  0x7C74ADC6, 0xe2ba, 0x4ade, 0x86, 0xde, 0x30, 0xbe, 0xab, 0xb4, 0x0c, 0xc1);

DEFINE_GUID(DXVA_ModeHEVC_VLD_Main,                 0x5b11d51b, 0x2f4c, 0x4452,0xbc,0xc3,0x09,0xf2,0xa1,0x16,0x0c,0xc0);
DEFINE_GUID(DXVA_ModeHEVC_VLD_Main10,               0x107af0e0, 0xef1a, 0x4d19,0xab,0xa8,0x67,0xa1,0x63,0x07,0x3d,0x13);

DEFINE_GUID(DXVA_ModeH264_VLD_Stereo_Progressive_NoFGT,     0xd79be8da, 0x0cf1, 0x4c81,0xb8,0x2a,0x69,0xa4,0xe2,0x36,0xf4,0x3d);
DEFINE_GUID(DXVA_ModeH264_VLD_Stereo_NoFGT,                 0xf9aaccbb, 0xc2b6, 0x4cfc,0x87,0x79,0x57,0x07,0xb1,0x76,0x05,0x52);
DEFINE_GUID(DXVA_ModeH264_VLD_Multiview_NoFGT,              0x705b9d82, 0x76cf, 0x49d6,0xb7,0xe6,0xac,0x88,0x72,0xdb,0x01,0x3c);

DEFINE_GUID(DXVA_ModeH264_VLD_SVC_Scalable_Baseline,                    0xc30700c4, 0xe384, 0x43e0, 0xb9, 0x82, 0x2d, 0x89, 0xee, 0x7f, 0x77, 0xc4);
DEFINE_GUID(DXVA_ModeH264_VLD_SVC_Restricted_Scalable_Baseline,         0x9b8175d4, 0xd670, 0x4cf2, 0xa9, 0xf0, 0xfa, 0x56, 0xdf, 0x71, 0xa1, 0xae);
DEFINE_GUID(DXVA_ModeH264_VLD_SVC_Scalable_High,                        0x728012c9, 0x66a8, 0x422f, 0x97, 0xe9, 0xb5, 0xe3, 0x9b, 0x51, 0xc0, 0x53);
DEFINE_GUID(DXVA_ModeH264_VLD_SVC_Restricted_Scalable_High_Progressive, 0x8efa5926, 0xbd9e, 0x4b04, 0x8b, 0x72, 0x8f, 0x97, 0x7d, 0xc4, 0x4c, 0x36);

DEFINE_GUID(DXVA_ModeH261_A,                        0x1b81be01, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH261_B,                        0x1b81be02, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVA_ModeH263_A,                        0x1b81be03, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH263_B,                        0x1b81be04, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH263_C,                        0x1b81be05, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH263_D,                        0x1b81be06, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH263_E,                        0x1b81be07, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);
DEFINE_GUID(DXVA_ModeH263_F,                        0x1b81be08, 0xa0c7, 0x11d3, 0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5);

DEFINE_GUID(DXVA_ModeVP8_VLD,                       0x90b899ea, 0x3a62, 0x4705, 0x88, 0xb3, 0x8d, 0xf0, 0x4b, 0x27, 0x44, 0xe7);
DEFINE_GUID(DXVA_ModeVP9_VLD_Profile0,              0x463707f8, 0xa1d0, 0x4585, 0x87, 0x6d, 0x83, 0xaa, 0x6d, 0x60, 0xb8, 0x9e);

typedef struct {
    const char   *name;
    const GUID   *guid;
    int          codec;
    const int    *p_profiles; // NULL or ends with 0
} directx_va_mode_t;

/* XXX Prefered modes must come first */
static const directx_va_mode_t DXVA_MODES[] = {
    /* MPEG-1/2 */
    { "MPEG-1 decoder, restricted profile A",                                         &DXVA_ModeMPEG1_A,                      0, NULL },
    { "MPEG-2 decoder, restricted profile A",                                         &DXVA_ModeMPEG2_A,                      0, NULL },
    { "MPEG-2 decoder, restricted profile B",                                         &DXVA_ModeMPEG2_B,                      0, NULL },
    { "MPEG-2 decoder, restricted profile C",                                         &DXVA_ModeMPEG2_C,                      0, NULL },
    { "MPEG-2 decoder, restricted profile D",                                         &DXVA_ModeMPEG2_D,                      0, NULL },

    { "MPEG-2 variable-length decoder",                                               &DXVA2_ModeMPEG2_VLD,                   AV_CODEC_ID_MPEG2VIDEO, PROF_MPEG2_SIMPLE },
    { "MPEG-2 & MPEG-1 variable-length decoder",                                      &DXVA2_ModeMPEG2and1_VLD,               AV_CODEC_ID_MPEG2VIDEO, PROF_MPEG2_MAIN },
    { "MPEG-2 & MPEG-1 variable-length decoder",                                      &DXVA2_ModeMPEG2and1_VLD,               AV_CODEC_ID_MPEG1VIDEO, NULL },
    { "MPEG-2 motion compensation",                                                   &DXVA2_ModeMPEG2_MoComp,                0, NULL },
    { "MPEG-2 inverse discrete cosine transform",                                     &DXVA2_ModeMPEG2_IDCT,                  0, NULL },

    /* MPEG-1 http://download.microsoft.com/download/B/1/7/B172A3C8-56F2-4210-80F1-A97BEA9182ED/DXVA_MPEG1_VLD.pdf */
    { "MPEG-1 variable-length decoder, no D pictures",                                &DXVA2_ModeMPEG1_VLD,                   0, NULL },

    /* H.264 http://www.microsoft.com/downloads/details.aspx?displaylang=en&FamilyID=3d1c290b-310b-4ea2-bf76-714063a6d7a6 */
    { "H.264 variable-length decoder, film grain technology",                         &DXVA2_ModeH264_F,                      AV_CODEC_ID_H264, PROF_H264_HIGH },
    { "H.264 variable-length decoder, no film grain technology (Intel ClearVideo)",   &DXVA_Intel_H264_NoFGT_ClearVideo,      AV_CODEC_ID_H264, PROF_H264_HIGH },
    { "H.264 variable-length decoder, no film grain technology",                      &DXVA2_ModeH264_E,                      AV_CODEC_ID_H264, PROF_H264_HIGH },
    { "H.264 variable-length decoder, no film grain technology, FMO/ASO",             &DXVA_ModeH264_VLD_WithFMOASO_NoFGT,    AV_CODEC_ID_H264, PROF_H264_HIGH },
    { "H.264 variable-length decoder, no film grain technology, Flash",               &DXVA_ModeH264_VLD_NoFGT_Flash,         AV_CODEC_ID_H264, PROF_H264_HIGH },

    { "H.264 inverse discrete cosine transform, film grain technology",               &DXVA2_ModeH264_D,                      0, NULL },
    { "H.264 inverse discrete cosine transform, no film grain technology",            &DXVA2_ModeH264_C,                      0, NULL },
    { "H.264 inverse discrete cosine transform, no film grain technology (Intel)",    &DXVADDI_Intel_ModeH264_C,              0, NULL },

    { "H.264 motion compensation, film grain technology",                             &DXVA2_ModeH264_B,                      0, NULL },
    { "H.264 motion compensation, no film grain technology",                          &DXVA2_ModeH264_A,                      0, NULL },
    { "H.264 motion compensation, no film grain technology (Intel)",                  &DXVADDI_Intel_ModeH264_A,              0, NULL },

    /* http://download.microsoft.com/download/2/D/0/2D02E72E-7890-430F-BA91-4A363F72F8C8/DXVA_H264_MVC.pdf */
    { "H.264 stereo high profile, mbs flag set",                                      &DXVA_ModeH264_VLD_Stereo_Progressive_NoFGT, 0, NULL },
    { "H.264 stereo high profile",                                                    &DXVA_ModeH264_VLD_Stereo_NoFGT,             0, NULL },
    { "H.264 multiview high profile",                                                 &DXVA_ModeH264_VLD_Multiview_NoFGT,          0, NULL },

    /* SVC http://download.microsoft.com/download/C/8/A/C8AD9F1B-57D1-4C10-85A0-09E3EAC50322/DXVA_SVC_2012_06.pdf */
    { "H.264 scalable video coding, Scalable Baseline Profile",                       &DXVA_ModeH264_VLD_SVC_Scalable_Baseline,            0, NULL },
    { "H.264 scalable video coding, Scalable Constrained Baseline Profile",           &DXVA_ModeH264_VLD_SVC_Restricted_Scalable_Baseline, 0, NULL },
    { "H.264 scalable video coding, Scalable High Profile",                           &DXVA_ModeH264_VLD_SVC_Scalable_High,                0, NULL },
    { "H.264 scalable video coding, Scalable Constrained High Profile",               &DXVA_ModeH264_VLD_SVC_Restricted_Scalable_High_Progressive, 0, NULL },

    /* WMV */
    { "Windows Media Video 8 motion compensation",                                    &DXVA2_ModeWMV8_B,                      0, NULL },
    { "Windows Media Video 8 post processing",                                        &DXVA2_ModeWMV8_A,                      0, NULL },

    { "Windows Media Video 9 IDCT",                                                   &DXVA2_ModeWMV9_C,                      0, NULL },
    { "Windows Media Video 9 motion compensation",                                    &DXVA2_ModeWMV9_B,                      0, NULL },
    { "Windows Media Video 9 post processing",                                        &DXVA2_ModeWMV9_A,                      0, NULL },

    /* VC-1 */
    { "VC-1 variable-length decoder",                                                 &DXVA2_ModeVC1_D,                       AV_CODEC_ID_VC1, NULL },
    { "VC-1 variable-length decoder",                                                 &DXVA2_ModeVC1_D,                       AV_CODEC_ID_WMV3, NULL },
    { "VC-1 variable-length decoder",                                                 &DXVA2_ModeVC1_D2010,                   AV_CODEC_ID_VC1, NULL },
    { "VC-1 variable-length decoder",                                                 &DXVA2_ModeVC1_D2010,                   AV_CODEC_ID_WMV3, NULL },
    { "VC-1 variable-length decoder 2 (Intel)",                                       &DXVA_Intel_VC1_ClearVideo_2,           0, NULL },
    { "VC-1 variable-length decoder (Intel)",                                         &DXVA_Intel_VC1_ClearVideo,             0, NULL },

    { "VC-1 inverse discrete cosine transform",                                       &DXVA2_ModeVC1_C,                       0, NULL },
    { "VC-1 motion compensation",                                                     &DXVA2_ModeVC1_B,                       0, NULL },
    { "VC-1 post processing",                                                         &DXVA2_ModeVC1_A,                       0, NULL },

    /* Xvid/Divx: TODO */
    { "MPEG-4 Part 2 nVidia bitstream decoder",                                       &DXVA_nVidia_MPEG4_ASP,                 0, NULL },
    { "MPEG-4 Part 2 variable-length decoder, Simple Profile",                        &DXVA_ModeMPEG4pt2_VLD_Simple,          0, NULL },
    { "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, no GMC",       &DXVA_ModeMPEG4pt2_VLD_AdvSimple_NoGMC, 0, NULL },
    { "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, GMC",          &DXVA_ModeMPEG4pt2_VLD_AdvSimple_GMC,   0, NULL },
    { "MPEG-4 Part 2 variable-length decoder, Simple&Advanced Profile, Avivo",        &DXVA_ModeMPEG4pt2_VLD_AdvSimple_Avivo, 0, NULL },

    /* HEVC */
    { "HEVC Main profile",                                                            &DXVA_ModeHEVC_VLD_Main,                AV_CODEC_ID_HEVC, PROF_HEVC_MAIN },
    { "HEVC Main 10 profile",                                                         &DXVA_ModeHEVC_VLD_Main10,              AV_CODEC_ID_HEVC, PROF_HEVC_MAIN10 },

    /* H.261 */
    { "H.261 decoder, restricted profile A",                                          &DXVA_ModeH261_A,                       0, NULL },
    { "H.261 decoder, restricted profile B",                                          &DXVA_ModeH261_B,                       0, NULL },

    /* H.263 */
    { "H.263 decoder, restricted profile A",                                          &DXVA_ModeH263_A,                       0, NULL },
    { "H.263 decoder, restricted profile B",                                          &DXVA_ModeH263_B,                       0, NULL },
    { "H.263 decoder, restricted profile C",                                          &DXVA_ModeH263_C,                       0, NULL },
    { "H.263 decoder, restricted profile D",                                          &DXVA_ModeH263_D,                       0, NULL },
    { "H.263 decoder, restricted profile E",                                          &DXVA_ModeH263_E,                       0, NULL },
    { "H.263 decoder, restricted profile F",                                          &DXVA_ModeH263_F,                       0, NULL },

    /* VPx */
    { "VP8",                                                                          &DXVA_ModeVP8_VLD,                      0, NULL },
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT( 57, 17, 100 ) && LIBAVCODEC_VERSION_MICRO >= 100
    { "VP9 profile 0",                                                                &DXVA_ModeVP9_VLD_Profile0,             AV_CODEC_ID_VP9, NULL },
#else
    { "VP9 profile 0",                                                                &DXVA_ModeVP9_VLD_Profile0,             0, NULL },
#endif

    { NULL, NULL, 0, NULL }
};

static int FindVideoServiceConversion(vlc_va_t *, directx_sys_t *, const es_format_t *fmt);
static void DestroyVideoDecoder(vlc_va_t *, directx_sys_t *);
static void DestroyVideoService(vlc_va_t *, directx_sys_t *);
static void DestroyDeviceManager(vlc_va_t *, directx_sys_t *);
static void DestroyDevice(vlc_va_t *, directx_sys_t *);

char *directx_va_GetDecoderName(const GUID *guid)
{
    for (unsigned i = 0; DXVA_MODES[i].name; i++) {
        if (IsEqualGUID(DXVA_MODES[i].guid, guid))
            return strdup(DXVA_MODES[i].name);
    }

    char *psz_name = malloc(36);
    if (likely(psz_name))
        asprintf(&psz_name, "Unknown decoder " GUID_FMT, GUID_PRINT(*guid));
    return psz_name;
}

/* */
int directx_va_Setup(vlc_va_t *va, directx_sys_t *dx_sys, AVCodecContext *avctx)
{
    int surface_alignment = 16;
    int surface_count = 4;

    if (dx_sys->width == avctx->coded_width && dx_sys->height == avctx->coded_height
     && dx_sys->decoder != NULL)
        goto ok;

    /* */
    DestroyVideoDecoder(va, dx_sys);

    avctx->hwaccel_context = NULL;
    if (avctx->coded_width <= 0 || avctx->coded_height <= 0)
        return VLC_EGENERIC;

    /* */
    msg_Dbg(va, "directx_va_Setup id %d %dx%d", dx_sys->codec_id, avctx->coded_width, avctx->coded_height);

    switch ( dx_sys->codec_id )
    {
    case AV_CODEC_ID_MPEG2VIDEO:
        /* decoding MPEG-2 requires additional alignment on some Intel GPUs,
           but it causes issues for H.264 on certain AMD GPUs..... */
        surface_alignment = 32;
        surface_count += 2;
        break;
    case AV_CODEC_ID_HEVC:
        /* the HEVC DXVA2 spec asks for 128 pixel aligned surfaces to ensure
           all coding features have enough room to work with */
        surface_alignment = 128;
        surface_count += 16;
        break;
    case AV_CODEC_ID_H264:
        surface_count += 16;
        break;
    default:
        surface_count += 2;
    }

    if ( avctx->active_thread_type & FF_THREAD_FRAME )
        surface_count += dx_sys->thread_count;

    if (surface_count > MAX_SURFACE_COUNT)
        return VLC_EGENERIC;

    dx_sys->surface_count = surface_count;

#define ALIGN(x, y) (((x) + ((y) - 1)) & ~((y) - 1))
    dx_sys->width  = avctx->coded_width;
    dx_sys->height = avctx->coded_height;
    dx_sys->surface_width  = ALIGN(dx_sys->width, surface_alignment);
    dx_sys->surface_height = ALIGN(dx_sys->height, surface_alignment);

    /* FIXME transmit a video_format_t by VaSetup directly */
    video_format_t fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.i_width = dx_sys->width;
    fmt.i_height = dx_sys->height;
    fmt.i_frame_rate = avctx->framerate.num;
    fmt.i_frame_rate_base = avctx->framerate.den;

    if (dx_sys->pf_create_decoder_surfaces(va, dx_sys->codec_id, &fmt))
        return VLC_EGENERIC;

    if (avctx->coded_width != dx_sys->surface_width ||
        avctx->coded_height != dx_sys->surface_height)
        msg_Warn( va, "surface dimensions (%dx%d) differ from avcodec dimensions (%dx%d)",
                  dx_sys->surface_width, dx_sys->surface_height,
                  avctx->coded_width, avctx->coded_height);

    dx_sys->pf_setup_avcodec_ctx(va);

    for (int i = 0; i < dx_sys->surface_count; i++) {
        vlc_va_surface_t *surface = &dx_sys->surface[i];
        surface->refcount = 0;
        surface->order = 0;
        surface->p_lock = &dx_sys->surface_lock;
        surface->p_pic = dx_sys->pf_alloc_surface_pic(va, &fmt, i);
        if (unlikely(surface->p_pic == NULL))
            return VLC_EGENERIC;
    }

ok:
    return VLC_SUCCESS;
}

void DestroyVideoDecoder(vlc_va_t *va, directx_sys_t *dx_sys)
{
    dx_sys->pf_destroy_surfaces(va);

    for (int i = 0; i < dx_sys->surface_count; i++)
        IUnknown_Release( dx_sys->hw_surface[i] );

    for (int i = 0; i < dx_sys->surface_count; i++)
        if (dx_sys->surface[i].p_pic)
            picture_Release(dx_sys->surface[i].p_pic);

    if (dx_sys->decoder)
        IUnknown_Release( dx_sys->decoder );

    dx_sys->decoder = NULL;
    dx_sys->surface_count = 0;
}

/* FIXME it is nearly common with VAAPI */
int directx_va_Get(vlc_va_t *va, directx_sys_t *dx_sys, picture_t *pic, uint8_t **data)
{
    /* Check the device */
    if (dx_sys->pf_check_device(va)!=VLC_SUCCESS)
        return VLC_EGENERIC;

    vlc_mutex_lock( &dx_sys->surface_lock );

    /* Grab the oldest unused surface, in case none are, use the oldest used one
     * XXX using the used one is a workaround in case a problem happens with libavcodec */
    int i, old = -1, old_used = -1;

    for (i = 0; i < dx_sys->surface_count; i++) {
        vlc_va_surface_t *surface = &dx_sys->surface[i];
        if (((old == -1 || surface->order < dx_sys->surface[old].order)) && !surface->refcount)
            old = i;
        if (old_used == -1 || surface->order < dx_sys->surface[old_used].order)
            old_used = i;
    }
    if (old >= 0)
        i = old;
    else if (old_used >= 0)
    {
        msg_Warn(va, "couldn't find a free decoding buffer, using index %d", old_used);
        i = old_used;
    }

    vlc_va_surface_t *surface = &dx_sys->surface[i];

    surface->refcount = 1;
    surface->order = ++dx_sys->surface_order;
    *data = (void *)dx_sys->hw_surface[i];
    pic->context = surface;

    vlc_mutex_unlock( &dx_sys->surface_lock );

    return VLC_SUCCESS;
}

void directx_va_Release(void *opaque)
{
    picture_t *pic = opaque;
    vlc_va_surface_t *surface = pic->context;
    vlc_mutex_lock( surface->p_lock );

    surface->refcount--;
    pic->context = NULL;
    picture_Release(pic);

    vlc_mutex_unlock( surface->p_lock );
}

void directx_va_Close(vlc_va_t *va, directx_sys_t *dx_sys)
{
    DestroyVideoDecoder(va, dx_sys);
    DestroyVideoService(va, dx_sys);
    DestroyDeviceManager(va, dx_sys);
    DestroyDevice(va, dx_sys);

    if (dx_sys->hdecoder_dll)
        FreeLibrary(dx_sys->hdecoder_dll);

    vlc_mutex_destroy( &dx_sys->surface_lock );
}

int directx_va_Open(vlc_va_t *va, directx_sys_t *dx_sys,
                    AVCodecContext *ctx, const es_format_t *fmt, bool b_dll)
{
    // TODO va->sys = sys;
    dx_sys->codec_id = ctx->codec_id;

    vlc_mutex_init( &dx_sys->surface_lock );

    if (b_dll) {
        /* Load dll*/
        dx_sys->hdecoder_dll = LoadLibrary(dx_sys->psz_decoder_dll);
        if (!dx_sys->hdecoder_dll) {
            msg_Warn(va, "cannot load DirectX decoder DLL");
            goto error;
        }
        msg_Dbg(va, "DLLs loaded");
    }

    /* */
    if (dx_sys->pf_create_device(va)) {
        msg_Err(va, "Failed to create DirectX device");
        goto error;
    }
    msg_Dbg(va, "CreateDevice succeed");

    if (dx_sys->pf_create_device_manager(va)) {
        msg_Err(va, "D3dCreateDeviceManager failed");
        goto error;
    }

    if (dx_sys->pf_create_video_service(va)) {
        msg_Err(va, "DxCreateVideoService failed");
        goto error;
    }

    /* */
    if (FindVideoServiceConversion(va, dx_sys, fmt)) {
        msg_Err(va, "FindVideoServiceConversion failed");
        goto error;
    }

    dx_sys->thread_count = ctx->thread_count;

    return VLC_SUCCESS;

error:
    return VLC_EGENERIC;
}

static bool profile_supported(const directx_va_mode_t *mode, const es_format_t *fmt)
{
    bool is_supported = mode->p_profiles == NULL || !mode->p_profiles[0];
    if (!is_supported)
    {
        int profile = fmt->i_profile;
        if (mode->codec == AV_CODEC_ID_H264)
        {
            uint8_t h264_profile;
            if ( h264_get_profile_level(fmt, &h264_profile, NULL, NULL) )
                profile = h264_profile;
        }
        if (mode->codec == AV_CODEC_ID_HEVC)
        {
            uint8_t hevc_profile;
            if (hevc_get_profile_level(fmt, &hevc_profile, NULL, NULL) )
                profile = hevc_profile;
        }

        if (profile <= 0)
            is_supported = true;
        else for (const int *p_profile = &mode->p_profiles[0]; *p_profile; ++p_profile)
        {
            if (*p_profile == profile)
            {
                is_supported = true;
                break;
            }
        }
    }
    return is_supported;
}

void DestroyVideoService(vlc_va_t *va, directx_sys_t *dx_sys)
{
    dx_sys->pf_destroy_video_service(va);
    if (dx_sys->d3ddec)
        IUnknown_Release(dx_sys->d3ddec);
}

/**
 * Find the best suited decoder mode GUID and render format.
 */
static int FindVideoServiceConversion(vlc_va_t *va, directx_sys_t *dx_sys, const es_format_t *fmt)
{
    input_list_t p_list = { 0 };
    int err = dx_sys->pf_get_input_list(va, &p_list);
    if (err != VLC_SUCCESS)
        return err;
    if (p_list.count == 0) {
        msg_Warn( va, "No input format found for HWAccel" );
        return VLC_EGENERIC;
    }

    err = VLC_EGENERIC;
    /* Retreive supported modes from the decoder service */
    for (unsigned i = 0; i < p_list.count; i++) {
        const GUID *g = &p_list.list[i];
        char *psz_decoder_name = directx_va_GetDecoderName(g);
        msg_Dbg(va, "- '%s' is supported by hardware", psz_decoder_name);
        free(psz_decoder_name);
    }

    /* Try all supported mode by our priority */
    const directx_va_mode_t *mode = DXVA_MODES;
    for (; mode->name; ++mode) {
        if (!mode->codec || mode->codec != dx_sys->codec_id)
            continue;

        /* */
        bool is_supported = false;
        for (const GUID *g = &p_list.list[0]; !is_supported && g < &p_list.list[p_list.count]; g++) {
            is_supported = IsEqualGUID(mode->guid, g);
        }
        if ( is_supported )
        {
            is_supported = profile_supported( mode, fmt );
            if (!is_supported)
                msg_Warn( va, "Unsupported profile %d for %s ",
                          fmt->i_profile, directx_va_GetDecoderName(mode->guid) );
        }
        if (!is_supported)
            continue;

        /* */
        msg_Dbg(va, "Trying to use '%s' as input", mode->name);
        if (dx_sys->pf_setup_output(va, mode->guid, &fmt->video)==VLC_SUCCESS)
        {
            dx_sys->input = *mode->guid;
            err = VLC_SUCCESS;
            break;
        }
    }

    p_list.pf_release(&p_list);
    return err;
}

void DestroyDeviceManager(vlc_va_t *va, directx_sys_t *dx_sys)
{
    dx_sys->pf_destroy_device_manager(va);
}

void DestroyDevice(vlc_va_t *va, directx_sys_t *dx_sys)
{
    dx_sys->pf_destroy_device(va);
    if (dx_sys->d3ddev)
        IUnknown_Release( dx_sys->d3ddev );
}
