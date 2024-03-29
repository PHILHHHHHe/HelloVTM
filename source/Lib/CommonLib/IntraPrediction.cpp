/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2019, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/** \file     Prediction.cpp
    \brief    prediction class
*/

#include "IntraPrediction.h"

#include "Unit.h"
#include "UnitTools.h"
#include "Buffer.h"

#include "dtrace_next.h"
#include "Rom.h"

#include <memory.h>

#include "CommonLib/InterpolationFilter.h"

//! \ingroup CommonLib
//! \{

// ====================================================================================================================
// Tables
// ====================================================================================================================

const uint8_t IntraPrediction::m_aucIntraFilter[MAX_NUM_CHANNEL_TYPE][MAX_INTRA_FILTER_DEPTHS] =
{
  { // Luma
    20, //   1xn
    20, //   2xn
    20, //   4xn
    14, //   8xn
    2,  //  16xn
    0,  //  32xn
    0,  //  64xn
    0,  // 128xn
  },
  { // Chroma
    40, //   1xn
    40, //   2xn
    40, //   4xn
    28, //   8xn
    4,  //  16xn
    0,  //  32xn
    0,  //  64xn
    0,  // 128xn
  }
};

const TFilterCoeff g_intraGaussFilter[32][4] = {
  { 16, 32, 16, 0 },
  { 15, 29, 17, 3 },
  { 15, 29, 17, 3 },
  { 14, 29, 18, 3 },
  { 13, 29, 18, 4 },
  { 13, 28, 19, 4 },
  { 13, 28, 19, 4 },
  { 12, 28, 20, 4 },
  { 11, 28, 20, 5 },
  { 11, 27, 21, 5 },
  { 10, 27, 22, 5 },
  { 9, 27, 22, 6 },
  { 9, 26, 23, 6 },
  { 9, 26, 23, 6 },
  { 8, 25, 24, 7 },
  { 8, 25, 24, 7 },
  { 8, 24, 24, 8 },
  { 7, 24, 25, 8 },
  { 7, 24, 25, 8 },
  { 6, 23, 26, 9 },
  { 6, 23, 26, 9 },
  { 6, 22, 27, 9 },
  { 5, 22, 27, 10 },
  { 5, 21, 27, 11 },
  { 5, 20, 28, 11 },
  { 4, 20, 28, 12 },
  { 4, 19, 28, 13 },
  { 4, 19, 28, 13 },
  { 4, 18, 29, 13 },
  { 3, 18, 29, 14 },
  { 3, 17, 29, 15 },
  { 3, 17, 29, 15 }
};

// ====================================================================================================================
// Constructor / destructor / initialize
// ====================================================================================================================

IntraPrediction::IntraPrediction()
:
  m_currChromaFormat( NUM_CHROMA_FORMAT )
{
  for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
  {
    for (uint32_t buf = 0; buf < NUM_PRED_BUF; buf++)
    {
      m_piYuvExt[ch][buf] = nullptr;
    }
  }
  for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
  {
    for (uint32_t buf = 0; buf < 4; buf++)
    {
      m_yuvExt2[ch][buf] = nullptr;
    }
  }

  m_piTemp = nullptr;
  m_pMdlmTemp = nullptr;
}

IntraPrediction::~IntraPrediction()
{
  destroy();
}

void IntraPrediction::destroy()
{
  for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
  {
    for (uint32_t buf = 0; buf < NUM_PRED_BUF; buf++)
    {
      delete[] m_piYuvExt[ch][buf];
      m_piYuvExt[ch][buf] = nullptr;
    }
  }
  for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
  {
    for (uint32_t buf = 0; buf < 4; buf++)
    {
      delete[] m_yuvExt2[ch][buf];
      m_yuvExt2[ch][buf] = nullptr;
    }
  }

  delete[] m_piTemp;
  m_piTemp = nullptr;
  delete[] m_pMdlmTemp;
  m_pMdlmTemp = nullptr;
}

void IntraPrediction::init(ChromaFormat chromaFormatIDC, const unsigned bitDepthY)
{
  // if it has been initialised before, but the chroma format has changed, release the memory and start again.
  if (m_piYuvExt[COMPONENT_Y][PRED_BUF_UNFILTERED] != nullptr && m_currChromaFormat != chromaFormatIDC)
  {
    destroy();
  }

  if (m_yuvExt2[COMPONENT_Y][0] != nullptr && m_currChromaFormat != chromaFormatIDC)
  {
    destroy();
  }

  m_currChromaFormat = chromaFormatIDC;

  if (m_piYuvExt[COMPONENT_Y][PRED_BUF_UNFILTERED] == nullptr) // check if first is null (in which case, nothing initialised yet)
  {
    m_iYuvExtSize = (MAX_CU_SIZE * 2 + 1 + MAX_REF_LINE_IDX * 33) * (MAX_CU_SIZE * 2 + 1 + MAX_REF_LINE_IDX * 33);

    for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
    {
      for (uint32_t buf = 0; buf < NUM_PRED_BUF; buf++)
      {
        m_piYuvExt[ch][buf] = new Pel[m_iYuvExtSize];
      }
    }
  }

  if (m_yuvExt2[COMPONENT_Y][0] == nullptr) // check if first is null (in which case, nothing initialised yet)
  {
    m_yuvExtSize2 = (MAX_CU_SIZE) * (MAX_CU_SIZE);

    for (uint32_t ch = 0; ch < MAX_NUM_COMPONENT; ch++)
    {
      for (uint32_t buf = 0; buf < 4; buf++)
      {
        m_yuvExt2[ch][buf] = new Pel[m_yuvExtSize2];
      }
    }
  }

  if (m_piTemp == nullptr)
  {
    m_piTemp = new Pel[(MAX_CU_SIZE + 1) * (MAX_CU_SIZE + 1)];
  }
  if (m_pMdlmTemp == nullptr)
  {
    m_pMdlmTemp = new Pel[(2 * MAX_CU_SIZE + 1)*(2 * MAX_CU_SIZE + 1)];//MDLM will use top-above and left-below samples.
  }
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

// Function for calculating DC value of the reference samples used in Intra prediction
//NOTE: Bit-Limit - 25-bit source
Pel IntraPrediction::xGetPredValDc( const CPelBuf &pSrc, const Size &dstSize )
{
  CHECK( dstSize.width == 0 || dstSize.height == 0, "Empty area provided" );

  int idx, sum = 0;
  Pel dcVal;
  const int width  = dstSize.width;
  const int height = dstSize.height;
  const auto denom     = (width == height) ? (width << 1) : std::max(width,height);
  const auto divShift  = g_aucLog2[denom];
  const auto divOffset = (denom >> 1);

  if ( width >= height )
  {
    for( idx = 0; idx < width; idx++ )
    {
      sum += pSrc.at( 1 + idx, 0 );
    }
  }
  if ( width <= height )
  {
    for( idx = 0; idx < height; idx++ )
    {
      sum += pSrc.at( 0, 1 + idx );
    }
  }

  dcVal = (sum + divOffset) >> divShift;
  return dcVal;
}

  int IntraPrediction::getWideAngle( int width, int height, int predMode )
  {
    if ( predMode > DC_IDX && predMode <= VDIA_IDX )
    {
      int modeShift[] = { 0, 6, 10, 12, 14, 15 };
      int deltaSize = abs(g_aucLog2[width] - g_aucLog2[height]);
      if (width > height && predMode < 2 + modeShift[deltaSize])
      {
        predMode += (VDIA_IDX - 1);
      }
      else if (height > width && predMode > VDIA_IDX - modeShift[deltaSize])
      {
        predMode -= (VDIA_IDX - 1);
      }
    }
    return predMode;
  }

  void IntraPrediction::setReferenceArrayLengths( const CompArea &area )
  {
    // set Top and Left reference samples length
    const int  width    = area.width;
    const int  height   = area.height;

    m_leftRefLength     = (height << 1);
    m_topRefLength      = (width << 1);

  }

void IntraPrediction::predIntraAng( const ComponentID compId, PelBuf &piPred, const PredictionUnit &pu)
{
  const ComponentID    compID       = MAP_CHROMA( compId );
  const ChannelType    channelType  = toChannelType( compID );
  const int            iWidth       = piPred.width;
  const int            iHeight      = piPred.height;
#if JVET_N0413_RDPCM
  const uint32_t       uiDirMode    = isLuma( compId ) && pu.cu->bdpcmMode ? BDPCM_IDX : PU::getFinalIntraMode( pu, channelType );
#else
  const uint32_t       uiDirMode    = PU::getFinalIntraMode( pu, channelType );
#endif

  CHECK( g_aucLog2[iWidth] < 2 && pu.cs->pcv->noChroma2x2, "Size not allowed" );
  CHECK( g_aucLog2[iWidth] > 7, "Size not allowed" );

  const int multiRefIdx = m_ipaParam.multiRefIndex;
  const int whRatio     = m_ipaParam.whRatio;
  const int hwRatio     = m_ipaParam.hwRatio;

  const int  srcStride  = m_topRefLength  + 1 + (whRatio + 1) * multiRefIdx;
  const int  srcHStride = m_leftRefLength + 1 + (hwRatio + 1) * multiRefIdx;

  const CPelBuf & srcBuf = CPelBuf(getPredictorPtr(compID), srcStride, srcHStride);
  const ClpRng& clpRng(pu.cu->cs->slice->clpRng(compID));

  switch (uiDirMode)
  {
    case(PLANAR_IDX): xPredIntraPlanar(srcBuf, piPred); break;
    case(DC_IDX):     xPredIntraDc(srcBuf, piPred, channelType, false); break;
#if JVET_N0413_RDPCM
    case(BDPCM_IDX):  xPredIntraBDPCM(srcBuf, piPred, pu.cu->bdpcmMode, clpRng); break;
#endif
    default:          xPredIntraAng(srcBuf, piPred, channelType, clpRng); break;
  }

  if (m_ipaParam.applyPDPC)
  {
    PelBuf dstBuf = piPred;
    const int scale = ((g_aucLog2[iWidth] - 2 + g_aucLog2[iHeight] - 2 + 2) >> 2);
    CHECK(scale < 0 || scale > 31, "PDPC: scale < 0 || scale > 31");

    if (uiDirMode == PLANAR_IDX)
    {
      for (int y = 0; y < iHeight; y++)
      {
        int wT = 32 >> std::min(31, ((y << 1) >> scale));
        const Pel left = srcBuf.at(0, y + 1);
        for (int x = 0; x < iWidth; x++)
        {
          const Pel top = srcBuf.at(x + 1, 0);
          int wL = 32 >> std::min(31, ((x << 1) >> scale));
          dstBuf.at(x, y) = ClipPel((wL * left + wT * top + (64 - wL - wT) * dstBuf.at(x, y) + 32) >> 6, clpRng);
        }
      }
    }
    else if (uiDirMode == DC_IDX)
    {
      const Pel topLeft = srcBuf.at(0, 0);
      for (int y = 0; y < iHeight; y++)
      {
        int wT = 32 >> std::min(31, ((y << 1) >> scale));
        const Pel left = srcBuf.at(0, y + 1);
        for (int x = 0; x < iWidth; x++)
        {
          const Pel top = srcBuf.at(x + 1, 0);
          int wL = 32 >> std::min(31, ((x << 1) >> scale));
          int wTL = (wL >> 4) + (wT >> 4);
          dstBuf.at(x, y) = ClipPel((wL * left + wT * top - wTL * topLeft + (64 - wL - wT + wTL) * dstBuf.at(x, y) + 32) >> 6, clpRng);
        }
      }
    }
    else if (uiDirMode == HOR_IDX)
    {
      const Pel topLeft = srcBuf.at(0, 0);
      for (int y = 0; y < iHeight; y++)
      {
        int wT = 32 >> std::min(31, ((y << 1) >> scale));
        for (int x = 0; x < iWidth; x++)
        {
          const Pel top = srcBuf.at(x + 1, 0);
          int wTL = wT;
          dstBuf.at(x, y) = ClipPel((wT * top - wTL * topLeft + (64 - wT + wTL) * dstBuf.at(x, y) + 32) >> 6, clpRng);
        }
      }
    }
    else if (uiDirMode == VER_IDX)
    {
      const Pel topLeft = srcBuf.at(0, 0);
      for (int y = 0; y < iHeight; y++)
      {
        const Pel left = srcBuf.at(0, y + 1);
        for (int x = 0; x < iWidth; x++)
        {
          int wL = 32 >> std::min(31, ((x << 1) >> scale));
          int wTL = wL;
          dstBuf.at(x, y) = ClipPel((wL * left - wTL * topLeft + (64 - wL + wTL) * dstBuf.at(x, y) + 32) >> 6, clpRng);
        }
      }
    }
  }
}
void IntraPrediction::predIntraChromaLM(const ComponentID compID, PelBuf &piPred, const PredictionUnit &pu, const CompArea& chromaArea, int intraDir)
{
  int  iLumaStride = 0;
  PelBuf Temp;
  if ((intraDir == MDLM_L_IDX) || (intraDir == MDLM_T_IDX))
  {
    iLumaStride = 2 * MAX_CU_SIZE + 1;
    Temp = PelBuf(m_pMdlmTemp + iLumaStride + 1, iLumaStride, Size(chromaArea));
  }
  else
  {
  iLumaStride = MAX_CU_SIZE + 1;
  Temp = PelBuf(m_piTemp + iLumaStride + 1, iLumaStride, Size(chromaArea));
  }
  int a, b, iShift;
  xGetLMParameters(pu, compID, chromaArea, a, b, iShift);

  ////// final prediction
  piPred.copyFrom(Temp);
  piPred.linearTransform(a, iShift, b, true, pu.cs->slice->clpRng(compID));
}

void IntraPrediction::xFilterGroup(Pel* pMulDst[], int i, Pel const * const piSrc, int iRecStride, bool bAboveAvaillable, bool bLeftAvaillable)
{
  pMulDst[0][i] = (piSrc[1] + piSrc[iRecStride + 1] + 1) >> 1;

  pMulDst[1][i] = (piSrc[iRecStride] + piSrc[iRecStride + 1] + 1) >> 1;

  pMulDst[3][i] = (piSrc[0] + piSrc[1] + 1) >> 1;

  pMulDst[2][i] = (piSrc[0] + piSrc[1] + piSrc[iRecStride] + piSrc[iRecStride + 1] + 2) >> 2;

}



/** Function for deriving planar intra prediction. This function derives the prediction samples for planar mode (intra coding).
 */

//NOTE: Bit-Limit - 24-bit source
void IntraPrediction::xPredIntraPlanar( const CPelBuf &pSrc, PelBuf &pDst )
{
  const uint32_t width  = pDst.width;
  const uint32_t height = pDst.height;
  const uint32_t log2W  = g_aucLog2[width  < 2 ? 2 : width];
  const uint32_t log2H  = g_aucLog2[height < 2 ? 2 : height];

  int leftColumn[MAX_CU_SIZE + 1], topRow[MAX_CU_SIZE + 1], bottomRow[MAX_CU_SIZE], rightColumn[MAX_CU_SIZE];
  const uint32_t offset = 1 << (log2W + log2H);

  // Get left and above reference column and row
  for( int k = 0; k < width + 1; k++ )
  {
    topRow[k] = pSrc.at( k + 1, 0 );
  }

  for( int k = 0; k < height + 1; k++ )
  {
    leftColumn[k] = pSrc.at( 0, k + 1 );
  }

  // Prepare intermediate variables used in interpolation
  int bottomLeft = leftColumn[height];
  int topRight = topRow[width];

  for( int k = 0; k < width; k++ )
  {
    bottomRow[k] = bottomLeft - topRow[k];
    topRow[k]    = topRow[k] << log2H;
  }

  for( int k = 0; k < height; k++ )
  {
    rightColumn[k] = topRight - leftColumn[k];
    leftColumn[k]  = leftColumn[k] << log2W;
  }

  const uint32_t finalShift = 1 + log2W + log2H;
  const uint32_t stride     = pDst.stride;
  Pel*       pred       = pDst.buf;
  for( int y = 0; y < height; y++, pred += stride )
  {
    int horPred = leftColumn[y];

    for( int x = 0; x < width; x++ )
    {
      horPred += rightColumn[y];
      topRow[x] += bottomRow[x];

      int vertPred = topRow[x];
      pred[x]      = ( ( horPred << log2H ) + ( vertPred << log2W ) + offset ) >> finalShift;
    }
  }
}




void IntraPrediction::xPredIntraDc( const CPelBuf &pSrc, PelBuf &pDst, const ChannelType channelType, const bool enableBoundaryFilter )
{
  const Pel dcval = xGetPredValDc( pSrc, pDst );
  pDst.fill( dcval );

#if HEVC_USE_DC_PREDFILTERING
  if( enableBoundaryFilter )
  {
    xDCPredFiltering( pSrc, pDst, channelType );
  }
#endif
}

#if HEVC_USE_DC_PREDFILTERING
/** Function for filtering intra DC predictor. This function performs filtering left and top edges of the prediction samples for DC mode (intra coding).
 */
void IntraPrediction::xDCPredFiltering(const CPelBuf &pSrc, PelBuf &pDst, const ChannelType &channelType)
{
  uint32_t iWidth = pDst.width;
  uint32_t iHeight = pDst.height;
  int x, y;

  if (isLuma(channelType) && (iWidth <= MAXIMUM_INTRA_FILTERED_WIDTH) && (iHeight <= MAXIMUM_INTRA_FILTERED_HEIGHT))
  {
    //top-left
    pDst.at(0, 0) = (Pel)((pSrc.at(1, 0) + pSrc.at(0, 1) + 2 * pDst.at(0, 0) + 2) >> 2);

    //top row (vertical filter)
    for ( x = 1; x < iWidth; x++ )
    {
      pDst.at(x, 0) = (Pel)((pSrc.at(x + 1, 0)  +  3 * pDst.at(x, 0) + 2) >> 2);
    }

    //left column (horizontal filter)
    for ( y = 1; y < iHeight; y++ )
    {
      pDst.at(0, y) = (Pel)((pSrc.at(0, y + 1) + 3 * pDst.at(0, y) + 2) >> 2);
    }
  }

  return;
}
#endif

// Function for initialization of intra prediction parameters
void IntraPrediction::initPredIntraParams(const PredictionUnit & pu, const CompArea area, const SPS& sps)
{
  const ComponentID compId = area.compID;
  const ChannelType chType = toChannelType(compId);

  const bool        useISP = NOT_INTRA_SUBPARTITIONS != pu.cu->ispMode && isLuma( chType );

  const Size   cuSize    = Size( pu.cu->blocks[compId].width, pu.cu->blocks[compId].height );
  const Size   puSize    = Size( area.width, area.height );
  const Size&  blockSize = useISP ? cuSize : puSize;
  const int      dirMode = PU::getFinalIntraMode(pu, chType);
  const int     predMode = getWideAngle( blockSize.width, blockSize.height, dirMode );

  m_ipaParam.whRatio              = std::max( unsigned( 1 ), blockSize.width  / blockSize.height ) ;
  m_ipaParam.hwRatio              = std::max( unsigned( 1 ), blockSize.height / blockSize.width  ) ;
  m_ipaParam.isModeVer            = predMode >= DIA_IDX;
  m_ipaParam.multiRefIndex        = isLuma (chType) ? pu.multiRefIdx : 0 ;
  m_ipaParam.refFilterFlag        = false;
  m_ipaParam.interpolationFlag    = false;
  m_ipaParam.applyPDPC            = !useISP && m_ipaParam.multiRefIndex == 0;

  const int    intraPredAngleMode = (m_ipaParam.isModeVer) ? predMode - VER_IDX : -(predMode - HOR_IDX);


  int absAng = 0;
  if (dirMode > DC_IDX && dirMode < NUM_LUMA_MODE) // intraPredAngle for directional modes
  {
    static const int angTable[32]    = { 0,    1,    2,    3,    4,    6,     8,   10,   12,   14,   16,   18,   20,   23,   26,   29,   32,   35,   39,  45,  51,  57,  64,  73,  86, 102, 128, 171, 256, 341, 512, 1024 };
    static const int invAngTable[32] = { 0, 8192, 4096, 2731, 2048, 1365,  1024,  819,  683,  585,  512,  455,  410,  356,  315,  282,  256,  234,  210, 182, 161, 144, 128, 112,  95,  80,  64,  48,  32,  24,  16,    8 }; // (256 * 32) / Angle

    const int     absAngMode         = abs(intraPredAngleMode);
    const int     signAng            = intraPredAngleMode < 0 ? -1 : 1;
                  absAng             = angTable  [absAngMode];

    m_ipaParam.invAngle              = invAngTable[absAngMode];
    m_ipaParam.intraPredAngle        = signAng * absAng;
    m_ipaParam.applyPDPC            &= m_ipaParam.intraPredAngle == 0 || m_ipaParam.intraPredAngle >= 12; // intra prediction modes: HOR, VER, x, where x>=VDIA-8 or x<=2+8
  }

  // high level conditions and DC intra prediction 
  if(   sps.getSpsRangeExtension().getIntraSmoothingDisabledFlag() 
#if JVET_N0671_INTRA_TPM_ALIGNWITH420
    || !isLuma( chType ) 
#else
    || ( !isLuma( chType ) && pu.chromaFormat != CHROMA_444 )
#endif
    || useISP
    || m_ipaParam.multiRefIndex
    || DC_IDX == dirMode
    )
  {   
    if (useISP)
    {
      m_ipaParam.interpolationFlag = (m_ipaParam.isModeVer ? puSize.width : puSize.height) > 8 ? true : false ;
    }
  }
#if JVET_N0413_RDPCM
  else if (isLuma( chType ) && pu.cu->bdpcmMode) // BDPCM
  {
    m_ipaParam.refFilterFlag = false;
  }
#endif
  else if (dirMode == PLANAR_IDX) // Planar intra prediction 
  {
    m_ipaParam.refFilterFlag = puSize.width * puSize.height > 32 ? true : false;
  }
  else if (!useISP)// HOR, VER and angular modes (MDIS)
  {
    bool filterFlag = false; 

    if (predMode != dirMode ) // wide-anlge mode
    {
      filterFlag = true;
    }
    else
    {
      const int diff = std::min<int>( abs( dirMode - HOR_IDX ), abs( dirMode - VER_IDX ) );
      const int log2Size = ((g_aucLog2[puSize.width] + g_aucLog2[puSize.height]) >> 1);
      CHECK( log2Size >= MAX_INTRA_FILTER_DEPTHS, "Size not supported" );
      filterFlag = (diff > m_aucIntraFilter[chType][log2Size]);
    }

    // Selelection of either ([1 2 1] / 4 ) refrence filter OR Gaussian 4-tap interpolation filter
    if (filterFlag)
    {
      const bool isRefFilter       =  isIntegerSlope(absAng); 
#if JVET_N0435_WAIP_HARMONIZATION
      m_ipaParam.refFilterFlag = isRefFilter && puSize.width * puSize.height > 32;
#else
      m_ipaParam.refFilterFlag     =  isRefFilter;
#endif
      m_ipaParam.interpolationFlag = !isRefFilter;
    }
  }
}


/** Function for deriving the simplified angular intra predictions.
*
* This function derives the prediction samples for the angular mode based on the prediction direction indicated by
* the prediction mode index. The prediction direction is given by the displacement of the bottom row of the block and
* the reference row above the block in the case of vertical prediction or displacement of the rightmost column
* of the block and reference column left from the block in the case of the horizontal prediction. The displacement
* is signalled at 1/32 pixel accuracy. When projection of the predicted pixel falls inbetween reference samples,
* the predicted value for the pixel is linearly interpolated from the reference samples. All reference samples are taken
* from the extended main reference.
*/
//NOTE: Bit-Limit - 25-bit source

void IntraPrediction::xPredIntraAng( const CPelBuf &pSrc, PelBuf &pDst, const ChannelType channelType, const ClpRng& clpRng)
{
  int width =int(pDst.width);
  int height=int(pDst.height);

  const bool bIsModeVer     = m_ipaParam.isModeVer;
  const int  whRatio        = m_ipaParam.whRatio;
  const int  hwRatio        = m_ipaParam.hwRatio;
  const int  multiRefIdx    = m_ipaParam.multiRefIndex;            
  const int  intraPredAngle = m_ipaParam.intraPredAngle;
  const int  invAngle       = m_ipaParam.invAngle;

  Pel* refMain;
  Pel* refSide;

  Pel  refAbove[2 * MAX_CU_SIZE + 3 + 33 * MAX_REF_LINE_IDX];
  Pel  refLeft [2 * MAX_CU_SIZE + 3 + 33 * MAX_REF_LINE_IDX];

  // Initialize the Main and Left reference array.
  if (intraPredAngle < 0)
  {
    const int width    = pDst.width + 1;
    const int height   = pDst.height + 1;
    const int lastIdx  = (bIsModeVer ? width : height) + multiRefIdx;
    const int firstIdx = (((bIsModeVer ? height : width) - 1) * intraPredAngle) >> 5;

    for (int x = 0; x < width + 1 + multiRefIdx; x++)
    {
      refAbove[x + height - 1] = pSrc.at( x, 0 );
    }
    for (int y = 0; y < height + 1 + multiRefIdx; y++)
    {
      refLeft[y + width - 1] = pSrc.at( 0, y );
    }
    refMain = (bIsModeVer ? refAbove + height : refLeft  + width ) - 1;
    refSide = (bIsModeVer ? refLeft  + width  : refAbove + height) - 1;

    // Extend the Main reference to the left.
    int invAngleSum    = 128;       // rounding for (shift by 8)
    for( int k = -1; k > firstIdx; k-- )
    {
      invAngleSum += invAngle;
      refMain[k] = refSide[invAngleSum>>8];
    }
    refMain[lastIdx] = refMain[lastIdx-1];
    refMain[firstIdx] = refMain[firstIdx+1];
  }
  else
  {
    for (int x = 0; x < m_topRefLength + 1 + (whRatio + 1) * multiRefIdx; x++)
    {
      refAbove[x+1] = pSrc.at(x, 0);
    }
    for (int y = 0; y < m_leftRefLength + 1 + (hwRatio + 1) * multiRefIdx; y++)
    {
      refLeft[y+1]  = pSrc.at(0, y);
    }
    refMain = bIsModeVer ? refAbove : refLeft ;
    refSide = bIsModeVer ? refLeft  : refAbove;

    refMain++;
    refSide++;
    refMain[-1] = refMain[0];
    auto lastIdx = 1 + ((bIsModeVer) ? m_topRefLength + (whRatio + 1) * multiRefIdx : m_leftRefLength +  (hwRatio + 1) * multiRefIdx);
    refMain[lastIdx] = refMain[lastIdx-1];
  }

  // swap width/height if we are doing a horizontal mode:
  Pel tempArray[MAX_CU_SIZE*MAX_CU_SIZE];
  const int dstStride = bIsModeVer ? pDst.stride : MAX_CU_SIZE;
  Pel *pDstBuf = bIsModeVer ? pDst.buf : tempArray;
  if (!bIsModeVer)
  {
    std::swap(width, height);
  }

  // compensate for line offset in reference line buffers
  refMain += multiRefIdx;
  refSide += multiRefIdx;

  if( intraPredAngle == 0 )  // pure vertical or pure horizontal
  {
    for( int y = 0; y < height; y++ )
    {
      for( int x = 0; x < width; x++ )
      {
        pDstBuf[y*dstStride + x] = refMain[x + 1];
      }
    }
#if HEVC_USE_HOR_VER_PREDFILTERING
    if (edgeFilter && multiRefIdx == 0)
    {
      for( int y = 0; y < height; y++ )
      {
        pDstBuf[y*dstStride] = ClipPel( pDstBuf[y*dstStride] + ( ( refSide[y + 1] - refSide[0] ) >> 1 ), clpRng );
      }
    }
#endif
  }
  else
  {
    Pel *pDsty=pDstBuf;
    for (int y = 0, deltaPos = intraPredAngle * (1 + multiRefIdx); y<height; y++, deltaPos += intraPredAngle, pDsty += dstStride)
    {
      const int deltaInt   = deltaPos >> 5;
      const int deltaFract = deltaPos & (32 - 1);

      if ( !isIntegerSlope( abs(intraPredAngle) ) )
      {
        if( isLuma(channelType) )
        {
          Pel                        p[4];
          const bool                 useCubicFilter = !m_ipaParam.interpolationFlag;
          TFilterCoeff const * const f              = (useCubicFilter) ? InterpolationFilter::getChromaFilterTable(deltaFract) : g_intraGaussFilter[deltaFract];

          int         refMainIndex   = deltaInt + 1;

          for( int x = 0; x < width; x++, refMainIndex++ )
          {
            p[0] = refMain[refMainIndex - 1];
            p[1] = refMain[refMainIndex];
            p[2] = refMain[refMainIndex + 1];
            p[3] = f[3] != 0 ? refMain[refMainIndex + 2] : 0;

            pDstBuf[y*dstStride + x] = static_cast<Pel>((static_cast<int>(f[0] * p[0]) + static_cast<int>(f[1] * p[1]) + static_cast<int>(f[2] * p[2]) + static_cast<int>(f[3] * p[3]) + 32) >> 6);

            if( useCubicFilter ) // only cubic filter has negative coefficients and requires clipping
            {
              pDstBuf[y*dstStride + x] = ClipPel( pDstBuf[y*dstStride + x], clpRng );
            }
          }
        }
        else
        {
          // Do linear filtering
          const Pel *pRM = refMain + deltaInt + 1;
          int lastRefMainPel = *pRM++;
          for( int x = 0; x < width; pRM++, x++ )
          {
            int thisRefMainPel = *pRM;
            pDsty[x + 0] = ( Pel ) ( ( ( 32 - deltaFract )*lastRefMainPel + deltaFract*thisRefMainPel + 16 ) >> 5 );
            lastRefMainPel = thisRefMainPel;
          }
        }
      }
      else
      {
        // Just copy the integer samples
        for( int x = 0; x < width; x++ )
        {
          pDsty[x] = refMain[x + deltaInt + 1];
        }
      }
      const int scale = ((g_aucLog2[width] - 2 + g_aucLog2[height] - 2 + 2) >> 2);
      CHECK(scale < 0 || scale > 31, "PDPC: scale < 0 || scale > 31");
      if (m_ipaParam.applyPDPC)
      {
        if (m_ipaParam.intraPredAngle == 32) // intra prediction modes: 2 and VDIA 
        {
          int wT = 16 >> std::min(31, ((y << 1) >> scale));

          for (int x = 0; x < width; x++)
          {
            int wL = 16 >> std::min(31, ((x << 1) >> scale));
            if (wT + wL == 0) break;

            int c = x + y + 1;
            if (c >= 2 * height) { wL = 0; }
            if (c >= 2 * width)  { wT = 0; }
            const Pel left = (wL != 0) ? refSide[c + 1] : 0;
            const Pel top  = (wT != 0) ? refMain[c + 1] : 0;

            pDsty[x] = ClipPel((wL * left + wT * top + (64 - wL - wT) * pDsty[x] + 32) >> 6, clpRng);
          }
        }
        else
        {
          int invAngleSum0 = 2;
          for (int x = 0; x < width; x++)
          {
            invAngleSum0 += invAngle;
            int deltaPos0 = invAngleSum0 >> 2;
            int deltaFrac0 = deltaPos0 & 63;
            int deltaInt0 = deltaPos0 >> 6;

            int deltay = y + deltaInt0 + 1;
            if (deltay >(bIsModeVer ? m_leftRefLength : m_topRefLength) - 1) break;

            int wL = 32 >> std::min(31, ((x << 1) >> scale));
            if (wL == 0) break;
            Pel *p = refSide + deltay;

            Pel left = p[deltaFrac0 >> 5];
            pDsty[x] = ClipPel((wL * left + (64 - wL) * pDsty[x] + 32) >> 6, clpRng);
          }
        }
      }
    }
#if HEVC_USE_HOR_VER_PREDFILTERING
    if( edgeFilter && absAng <= 1 )
    {
      for( int y = 0; y < height; y++ )
      {
        pDstBuf[y*dstStride] = ClipPel( pDstBuf[y*dstStride] + ((refSide[y + 1] - refSide[0]) >> 2), clpRng );
      }
    }
#endif
  }

  // Flip the block if this is the horizontal mode
  if( !bIsModeVer )
  {
    for( int y = 0; y < height; y++ )
    {
      for( int x = 0; x < width; x++ )
      {
        pDst.at( y, x ) = pDstBuf[x];
      }
      pDstBuf += dstStride;
    }
  }
}

#if JVET_N0413_RDPCM
void IntraPrediction::xPredIntraBDPCM(const CPelBuf &pSrc, PelBuf &pDst, const uint32_t dirMode, const ClpRng& clpRng )
{
  const int wdt = pDst.width;
  const int hgt = pDst.height;

  const int strideP = pDst.stride;
  const int strideS = pSrc.stride;

  CHECK( !( dirMode == 1 || dirMode == 2 ), "Incorrect BDPCM mode parameter." );

  Pel* pred = &pDst.buf[0];
  if( dirMode == 1 )
  {
    Pel  val;
    for( int y = 0; y < hgt; y++ )
    {
      val = pSrc.buf[(y + 1) * strideS];
      for( int x = 0; x < wdt; x++ )
      {
        pred[x] = val;
      }
      pred += strideP;
    }
  }
  else
  {
    for( int y = 0; y < hgt; y++ )
    {
      for( int x = 0; x < wdt; x++ )
      {
        pred[x] = pSrc.buf[x + 1];
      }
      pred += strideP;
    }
  }
}
#endif

bool IntraPrediction::useDPCMForFirstPassIntraEstimation(const PredictionUnit &pu, const uint32_t &uiDirMode)
{
  return CU::isRDPCMEnabled(*pu.cu) && pu.cu->transQuantBypass && (uiDirMode == HOR_IDX || uiDirMode == VER_IDX);
}

#if JVET_N0302_SIMPLFIED_CIIP
void IntraPrediction::geneWeightedPred(const ComponentID compId, PelBuf &pred, const PredictionUnit &pu, Pel *srcBuf)
{
  const int            width = pred.width;
  const int            height = pred.height;
  const int            srcStride = width;
  const int            dstStride = pred.stride;

  Pel*                 dstBuf = pred.buf;
  int wIntra, wMerge;

  const Position posBL = pu.Y().bottomLeft();
  const Position posTR = pu.Y().topRight();
  const PredictionUnit *neigh0 = pu.cs->getPURestricted(posBL.offset(-1, 0), pu, CHANNEL_TYPE_LUMA);
  const PredictionUnit *neigh1 = pu.cs->getPURestricted(posTR.offset(0, -1), pu, CHANNEL_TYPE_LUMA);
  bool isNeigh0Intra = neigh0 && (CU::isIntra(*neigh0->cu));
  bool isNeigh1Intra = neigh1 && (CU::isIntra(*neigh1->cu));

  if (isNeigh0Intra && isNeigh1Intra)
  {
    wIntra = 3; wMerge = 1;
  }
  else
  {
    if (!isNeigh0Intra && !isNeigh1Intra)
    {
      wIntra = 1; wMerge = 3;
    }
    else
    {
      wIntra = 2; wMerge = 2;
    }
  }
  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      dstBuf[y*dstStride + x] = (wMerge * dstBuf[y*dstStride + x] + wIntra * srcBuf[y*srcStride + x] + 2) >> 2;
    }
  }
}
#else
void IntraPrediction::geneWeightedPred(const ComponentID compId, PelBuf &pred, const PredictionUnit &pu, Pel *srcBuf)
{
  const int            width = pred.width;
  const int            height = pred.height;
  const int            srcStride = width;
  const int            dstStride = pred.stride;

  const uint32_t       dirMode = PU::getFinalIntraMode(pu, toChannelType(compId));
  const ClpRng&        clpRng(pu.cu->cs->slice->clpRng(compId));
  Pel*                 dstBuf = pred.buf;
  int                  k, l;

  bool                 modeDC = (dirMode <= DC_IDX);
  Pel                  wIntra1 = 6, wInter1 = 2, wIntra2 = 5, wInter2 = 3, wIntra3 = 3, wInter3 = 5, wIntra4 = 2, wInter4 = 6;

  if (modeDC || width < 4 || height < 4)
  {
    for (k = 0; k<height; k++)
    {
      for (l = 0; l<width; l++)
      {
        dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * 4) + (srcBuf[k*srcStride + l] * 4)) >> 3), clpRng);
      }
    }
  }
  else
  {
    if (dirMode <= DIA_IDX)
    {
      int interval = (width >> 2);

      for (k = 0; k<height; k++)
      {
        for (l = 0; l<width; l++)
        {
          if (l<interval)
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter1) + (srcBuf[k*srcStride + l] * wIntra1)) >> 3), clpRng);
          }
          else if (l >= interval && l < (2 * interval))
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter2) + (srcBuf[k*srcStride + l] * wIntra2)) >> 3), clpRng);
          }
          else if (l >= (interval * 2) && l < (3 * interval))
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter3) + (srcBuf[k*srcStride + l] * wIntra3)) >> 3), clpRng);
          }
          else
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter4) + (srcBuf[k*srcStride + l] * wIntra4)) >> 3), clpRng);
          }
        }
      }
    }
    else
    {
      int interval = (height >> 2);
      for (k = 0; k<height; k++)
      {
        for (l = 0; l<width; l++)
        {
          if (k<interval)
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter1) + (srcBuf[k*srcStride + l] * wIntra1)) >> 3), clpRng);
          }
          else if (k >= interval && k < (2 * interval))
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter2) + (srcBuf[k*srcStride + l] * wIntra2)) >> 3), clpRng);
          }
          else if (k >= (interval * 2) && k < (3 * interval))
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter3) + (srcBuf[k*srcStride + l] * wIntra3)) >> 3), clpRng);
          }
          else
          {
            dstBuf[k*dstStride + l] = ClipPel((((dstBuf[k*dstStride + l] * wInter4) + (srcBuf[k*srcStride + l] * wIntra4)) >> 3), clpRng);
          }
        }
      }
    }
  }
}
#endif
void IntraPrediction::switchBuffer(const PredictionUnit &pu, ComponentID compID, PelBuf srcBuff, Pel *dst)
{
  Pel  *src = srcBuff.bufAt(0, 0);
  int compWidth = compID == COMPONENT_Y ? pu.Y().width : pu.Cb().width;
  int compHeight = compID == COMPONENT_Y ? pu.Y().height : pu.Cb().height;
  for (int i = 0; i < compHeight; i++)
  {
    memcpy(dst, src, compWidth * sizeof(Pel));
    src += srcBuff.stride;
    dst += compWidth;
  }
}

void IntraPrediction::geneIntrainterPred(const CodingUnit &cu)
{
  if (!cu.firstPU->mhIntraFlag)
  {
    return;
  }

  const PredictionUnit* pu = cu.firstPU;

  initIntraPatternChType(cu, pu->Y());
  predIntraAng(COMPONENT_Y, cu.cs->getPredBuf(*pu).Y(), *pu);

  initIntraPatternChType(cu, pu->Cb());
  predIntraAng(COMPONENT_Cb, cu.cs->getPredBuf(*pu).Cb(), *pu);

  initIntraPatternChType(cu, pu->Cr());
  predIntraAng(COMPONENT_Cr, cu.cs->getPredBuf(*pu).Cr(), *pu);

  for (int currCompID = 0; currCompID < 3; currCompID++)
  {
    ComponentID currCompID2 = (ComponentID)currCompID;
    PelBuf tmpBuf = currCompID == 0 ? cu.cs->getPredBuf(*pu).Y() : (currCompID == 1 ? cu.cs->getPredBuf(*pu).Cb() : cu.cs->getPredBuf(*pu).Cr());
    switchBuffer(*pu, currCompID2, tmpBuf, getPredictorPtr2(currCompID2, 0));
  }
}

inline bool isAboveLeftAvailable  ( const CodingUnit &cu, const ChannelType &chType, const Position &posLT );
inline int  isAboveAvailable      ( const CodingUnit &cu, const ChannelType &chType, const Position &posLT, const uint32_t uiNumUnitsInPU, const uint32_t unitWidth, bool *validFlags );
inline int  isLeftAvailable       ( const CodingUnit &cu, const ChannelType &chType, const Position &posLT, const uint32_t uiNumUnitsInPU, const uint32_t unitWidth, bool *validFlags );
inline int  isAboveRightAvailable ( const CodingUnit &cu, const ChannelType &chType, const Position &posRT, const uint32_t uiNumUnitsInPU, const uint32_t unitHeight, bool *validFlags );
inline int  isBelowLeftAvailable  ( const CodingUnit &cu, const ChannelType &chType, const Position &posLB, const uint32_t uiNumUnitsInPU, const uint32_t unitHeight, bool *validFlags );

void IntraPrediction::initIntraPatternChType(const CodingUnit &cu, const CompArea &area, const bool forceRefFilterFlag)
{
  const CodingStructure& cs   = *cu.cs;

  if (!forceRefFilterFlag)
  {
    initPredIntraParams(*cu.firstPU, area, *cs.sps);
  }

  Pel *refBufUnfiltered   = m_piYuvExt[area.compID][PRED_BUF_UNFILTERED];
  Pel *refBufFiltered     = m_piYuvExt[area.compID][PRED_BUF_FILTERED];

  setReferenceArrayLengths( cu.ispMode && isLuma( area.compID ) ? cu.blocks[area.compID] : area );

  // ----- Step 1: unfiltered reference samples -----
  xFillReferenceSamples( cs.picture->getRecoBuf( area ), refBufUnfiltered, area, cu );
  // ----- Step 2: filtered reference samples -----
  if( m_ipaParam.refFilterFlag || forceRefFilterFlag )
  {
    xFilterReferenceSamples( refBufUnfiltered, refBufFiltered, area, *cs.sps, cu.firstPU->multiRefIdx );
  }
}

void IntraPrediction::xFillReferenceSamples( const CPelBuf &recoBuf, Pel* refBufUnfiltered, const CompArea &area, const CodingUnit &cu )
{
  const ChannelType      chType = toChannelType( area.compID );
  const CodingStructure &cs     = *cu.cs;
  const SPS             &sps    = *cs.sps;
  const PreCalcValues   &pcv    = *cs.pcv;

  const int multiRefIdx         = (area.compID == COMPONENT_Y) ? cu.firstPU->multiRefIdx : 0;

  const int  tuWidth            = area.width;
  const int  tuHeight           = area.height;
  const int  predSize           = m_topRefLength;
  const int  predHSize          = m_leftRefLength;
  const int  cuWidth            = cu.blocks[area.compID].width;
  const int  cuHeight           = cu.blocks[area.compID].height;
  const int  whRatio            = cu.ispMode && isLuma(area.compID) ? std::max(1, cuWidth / cuHeight) : std::max(1, tuWidth / tuHeight);
  const int  hwRatio            = cu.ispMode && isLuma(area.compID) ? std::max(1, cuHeight / cuWidth) : std::max(1, tuHeight / tuWidth);
  const int  predStride         = predSize + 1 + (whRatio + 1) * multiRefIdx;

  const bool noShift            = pcv.noChroma2x2 && area.width == 4; // don't shift on the lowest level (chroma not-split)
  const int  unitWidth          = tuWidth  <= 2 && cu.ispMode && isLuma(area.compID) ? tuWidth  : pcv.minCUWidth  >> (noShift ? 0 : getComponentScaleX(area.compID, sps.getChromaFormatIdc()));
  const int  unitHeight         = tuHeight <= 2 && cu.ispMode && isLuma(area.compID) ? tuHeight : pcv.minCUHeight >> (noShift ? 0 : getComponentScaleY(area.compID, sps.getChromaFormatIdc()));

  const int  totalAboveUnits    = (predSize + (unitWidth - 1)) / unitWidth;
  const int  totalLeftUnits     = (predHSize + (unitHeight - 1)) / unitHeight;
  const int  totalUnits         = totalAboveUnits + totalLeftUnits + 1; //+1 for top-left
  const int  numAboveUnits      = std::max<int>( tuWidth / unitWidth, 1 );
  const int  numLeftUnits       = std::max<int>( tuHeight / unitHeight, 1 );
  const int  numAboveRightUnits = totalAboveUnits - numAboveUnits;
  const int  numLeftBelowUnits  = totalLeftUnits - numLeftUnits;

  CHECK( numAboveUnits <= 0 || numLeftUnits <= 0 || numAboveRightUnits <= 0 || numLeftBelowUnits <= 0, "Size not supported" );

  // ----- Step 1: analyze neighborhood -----
  const Position posLT          = area;
  const Position posRT          = area.topRight();
  const Position posLB          = area.bottomLeft();

  bool  neighborFlags[4 * MAX_NUM_PART_IDXS_IN_CTU_WIDTH + 1];
  int   numIntraNeighbor = 0;

  memset( neighborFlags, 0, totalUnits );

  neighborFlags[totalLeftUnits] = isAboveLeftAvailable( cu, chType, posLT );
  numIntraNeighbor += neighborFlags[totalLeftUnits] ? 1 : 0;
  numIntraNeighbor += isAboveAvailable     ( cu, chType, posLT, numAboveUnits,      unitWidth,  (neighborFlags + totalLeftUnits + 1) );
  numIntraNeighbor += isAboveRightAvailable( cu, chType, posRT, numAboveRightUnits, unitWidth,  (neighborFlags + totalLeftUnits + 1 + numAboveUnits) );
  numIntraNeighbor += isLeftAvailable      ( cu, chType, posLT, numLeftUnits,       unitHeight, (neighborFlags + totalLeftUnits - 1) );
  numIntraNeighbor += isBelowLeftAvailable ( cu, chType, posLB, numLeftBelowUnits,  unitHeight, (neighborFlags + totalLeftUnits - 1 - numLeftUnits) );

  // ----- Step 2: fill reference samples (depending on neighborhood) -----
  CHECK((predHSize + 1) * predStride > m_iYuvExtSize, "Reference sample area not supported");

  const Pel*  srcBuf    = recoBuf.buf;
  const int   srcStride = recoBuf.stride;
        Pel*  ptrDst    = refBufUnfiltered;
  const Pel*  ptrSrc;
  const Pel   valueDC   = 1 << (sps.getBitDepth( chType ) - 1);


  if( numIntraNeighbor == 0 )
  {
    // Fill border with DC value
    for (int j = 0; j <= predSize + multiRefIdx; j++) { ptrDst[j] = valueDC; }
    for (int i = 1; i <= predHSize + multiRefIdx; i++) { ptrDst[i*predStride] = valueDC; }
  }
  else if( numIntraNeighbor == totalUnits )
  {
    // Fill top-left border and top and top right with rec. samples
    ptrSrc = srcBuf - (1 + multiRefIdx) * srcStride - (1 + multiRefIdx);
    for (int j = 0; j <= predSize + multiRefIdx; j++) { ptrDst[j] = ptrSrc[j]; }
    ptrSrc = srcBuf - multiRefIdx * srcStride - (1 + multiRefIdx);
    for (int i = 1; i <= predHSize + multiRefIdx; i++) { ptrDst[i*predStride] = *(ptrSrc); ptrSrc += srcStride; }
  }
  else // reference samples are partially available
  {
    // Fill top-left sample(s) if available
    ptrSrc = srcBuf - (1 + multiRefIdx) * srcStride - (1 + multiRefIdx);
    ptrDst = refBufUnfiltered;
    if (neighborFlags[totalLeftUnits])
    {
      ptrDst[0] = ptrSrc[0];
      for (int i = 1; i <= multiRefIdx; i++)
      {
        ptrDst[i] = ptrSrc[i];
        ptrDst[i*predStride] = ptrSrc[i*srcStride];
      }
    }

    // Fill left & below-left samples if available (downwards)
    ptrSrc += (1 + multiRefIdx) * srcStride;
    ptrDst += (1 + multiRefIdx) * predStride;
    for (int unitIdx = totalLeftUnits - 1; unitIdx > 0; unitIdx--)
    {
      if (neighborFlags[unitIdx])
      {
        for (int i = 0; i < unitHeight; i++)
        {
          ptrDst[i*predStride] = ptrSrc[i*srcStride];
        }
      }
      ptrSrc += unitHeight * srcStride;
      ptrDst += unitHeight * predStride;
    }
    // Fill last below-left sample(s)
    if (neighborFlags[0])
    {
      int lastSample = (predHSize % unitHeight == 0) ? unitHeight : predHSize % unitHeight;
      for (int i = 0; i < lastSample; i++)
      {
        ptrDst[i*predStride] = ptrSrc[i*srcStride];
      }
    }

    // Fill above & above-right samples if available (left-to-right)
    ptrSrc = srcBuf - srcStride * (1 + multiRefIdx);
    ptrDst = refBufUnfiltered + 1 + multiRefIdx;
    for (int unitIdx = totalLeftUnits + 1; unitIdx < totalUnits - 1; unitIdx++)
    {
      if (neighborFlags[unitIdx])
      {
        for (int j = 0; j < unitWidth; j++)
        {
          ptrDst[j] = ptrSrc[j];
        }
      }
      ptrSrc += unitWidth;
      ptrDst += unitWidth;
    }
    // Fill last above-right sample(s)
    if (neighborFlags[totalUnits - 1])
    {
      int lastSample = (predSize % unitWidth == 0) ? unitWidth : predSize % unitWidth;
      for (int j = 0; j < lastSample; j++)
      {
        ptrDst[j] = ptrSrc[j];
      }
    }

    // pad from first available down to the last below-left
    ptrDst = refBufUnfiltered;
    int lastAvailUnit = 0;
    if (!neighborFlags[0])
    {
      int firstAvailUnit = 1;
      while (firstAvailUnit < totalUnits && !neighborFlags[firstAvailUnit])
      {
        firstAvailUnit++;
      }

      // first available sample
      int firstAvailRow = 0;
      int firstAvailCol = 0;
      if (firstAvailUnit < totalLeftUnits)
      {
        firstAvailRow = (totalLeftUnits - firstAvailUnit) * unitHeight + multiRefIdx;
      }
      else if (firstAvailUnit == totalLeftUnits)
      {
        firstAvailRow = multiRefIdx;
      }
      else
      {
        firstAvailCol = (firstAvailUnit - totalLeftUnits - 1) * unitWidth + 1 + multiRefIdx;
      }
      const Pel firstAvailSample = ptrDst[firstAvailCol + firstAvailRow * predStride];

      // last sample below-left (n.a.)
      int lastRow = predHSize + multiRefIdx;

      // fill left column
      for (int i = lastRow; i > firstAvailRow; i--)
      {
        ptrDst[i*predStride] = firstAvailSample;
      }
      // fill top row
      if (firstAvailCol > 0)
      {
        for (int j = 0; j < firstAvailCol; j++)
        {
          ptrDst[j] = firstAvailSample;
        }
      }
      lastAvailUnit = firstAvailUnit;
    }

    // pad all other reference samples.
    int currUnit = lastAvailUnit + 1;
    while (currUnit < totalUnits)
    {
      if (!neighborFlags[currUnit]) // samples not available
      {
        // last available sample
        int lastAvailRow = 0;
        int lastAvailCol = 0;
        if (lastAvailUnit < totalLeftUnits)
        {
          lastAvailRow = (totalLeftUnits - lastAvailUnit - 1) * unitHeight + multiRefIdx + 1;
        }
        else if (lastAvailUnit == totalLeftUnits)
        {
          lastAvailCol = multiRefIdx;
        }
        else
        {
          lastAvailCol = (lastAvailUnit - totalLeftUnits) * unitWidth + multiRefIdx;
        }
        const Pel lastAvailSample = ptrDst[lastAvailCol + lastAvailRow * predStride];

        // fill current unit with last available sample
        if (currUnit < totalLeftUnits)
        {
          for (int i = lastAvailRow - 1; i >= lastAvailRow - unitHeight; i--)
          {
            ptrDst[i*predStride] = lastAvailSample;
          }
        }
        else if (currUnit == totalLeftUnits)
        {
          for (int i = 1; i < multiRefIdx + 1; i++)
          {
            ptrDst[i*predStride] = lastAvailSample;
          }
          for (int j = 0; j < multiRefIdx + 1; j++)
          {
            ptrDst[j] = lastAvailSample;
          }
        }
        else
        {
          int numSamplesInUnit = (currUnit == totalUnits - 1) ? ((predSize % unitWidth == 0) ? unitWidth : predSize % unitWidth) : unitWidth;
          for (int j = lastAvailCol + 1; j <= lastAvailCol + numSamplesInUnit; j++)
          {
            ptrDst[j] = lastAvailSample;
          }
        }
      }
      lastAvailUnit = currUnit;
      currUnit++;
    }
}
  // padding of extended samples above right with the last sample
  int lastSample = multiRefIdx + predSize;
  for (int j = 1; j <= whRatio * multiRefIdx; j++) { ptrDst[lastSample + j] = ptrDst[lastSample]; }
  // padding of extended samples below left with the last sample
  lastSample = multiRefIdx + predHSize;
  for (int i = 1; i <= hwRatio * multiRefIdx; i++) { ptrDst[(lastSample + i)*predStride] = ptrDst[lastSample*predStride]; }
}

void IntraPrediction::xFilterReferenceSamples( const Pel* refBufUnfiltered, Pel* refBufFiltered, const CompArea &area, const SPS &sps
  , int multiRefIdx
)
{
  if (area.compID != COMPONENT_Y)
  {
    multiRefIdx = 0;
  }
  int whRatio          = std::max(1, int(area.width  / area.height));
  int hwRatio          = std::max(1, int(area.height / area.width));
  const int  predSize  = m_topRefLength  + (whRatio + 1) * multiRefIdx;
  const int  predHSize = m_leftRefLength + (hwRatio + 1) * multiRefIdx;
  const int  predStride = predSize + 1;


#if HEVC_USE_INTRA_SMOOTHING_T32 || HEVC_USE_INTRA_SMOOTHING_T64
  // Strong intra smoothing
  ChannelType chType = toChannelType( area.compID );
  if( sps.getUseStrongIntraSmoothing() && isLuma( chType ) )
  {
    const Pel bottomLeft = refBufUnfiltered[predStride * predHSize];
    const Pel topLeft    = refBufUnfiltered[0];
    const Pel topRight   = refBufUnfiltered[predSize];

    const int  threshold     = 1 << (sps.getBitDepth( chType ) - 5);
    const bool bilinearLeft  = abs( (bottomLeft + topLeft)  - (2 * refBufUnfiltered[predStride * tuHeight]) ) < threshold; //difference between the
    const bool bilinearAbove = abs( (topLeft    + topRight) - (2 * refBufUnfiltered[             tuWidth ]) ) < threshold; //ends and the middle

    if( tuWidth >= 32 && tuHeight >= 32 && bilinearLeft && bilinearAbove )
#if !HEVC_USE_INTRA_SMOOTHING_T32
    if( tuWidth > 32 && tuHeight > 32 )
#endif
#if !HEVC_USE_INTRA_SMOOTHING_T64
    if( tuWidth < 64 && tuHeight < 64 )
#endif
    {
      Pel *piDestPtr = refBufFiltered + (predStride * predHSize); // bottom left

      // apply strong intra smoothing
      for (int i = 0; i < predHSize; i++, piDestPtr -= predStride) //left column (bottom to top)
      {
        *piDestPtr = (((predHSize - i) * bottomLeft) + (i * topLeft) + predHSize / 2) / predHSize;
      }
      for( uint32_t i = 0; i <= predSize; i++, piDestPtr++ )            //full top row (left-to-right)
      {
        *piDestPtr = (((predSize - i) * topLeft) + (i * topRight) + predSize / 2) / predSize;
      }

      return;
    }
  }
#endif

  // Regular reference sample filter
  const Pel *piSrcPtr  = refBufUnfiltered + (predStride * predHSize); // bottom left
        Pel *piDestPtr = refBufFiltered   + (predStride * predHSize); // bottom left

  // bottom left (not filtered)
  *piDestPtr = *piSrcPtr;
  piDestPtr -= predStride;
  piSrcPtr  -= predStride;
  //left column (bottom to top)
  for( int i = 1; i < predHSize; i++, piDestPtr -= predStride, piSrcPtr -= predStride)
  {
    *piDestPtr = (piSrcPtr[predStride] + 2 * piSrcPtr[0] + piSrcPtr[-predStride] + 2) >> 2;
  }
  //top-left
  *piDestPtr = (piSrcPtr[predStride] + 2 * piSrcPtr[0] + piSrcPtr[1] + 2) >> 2;
  piDestPtr++;
  piSrcPtr++;
  //top row (left-to-right)
  for( uint32_t i=1; i < predSize; i++, piDestPtr++, piSrcPtr++ )
  {
    *piDestPtr = (piSrcPtr[1] + 2 * piSrcPtr[0] + piSrcPtr[-1] + 2) >> 2;
  }
  // top right (not filtered)
  *piDestPtr=*piSrcPtr;
}

bool isAboveLeftAvailable(const CodingUnit &cu, const ChannelType &chType, const Position &posLT)
{
  const CodingStructure& cs = *cu.cs;
  const Position refPos = posLT.offset(-1, -1);
  const CodingUnit* pcCUAboveLeft = cs.isDecomp( refPos, chType ) ? cs.getCURestricted( refPos, cu, chType ) : nullptr;
  const bool isConstrained = cs.pps->getConstrainedIntraPred();
  bool bAboveLeftFlag;

  if (isConstrained)
  {
    bAboveLeftFlag = pcCUAboveLeft && CU::isIntra(*pcCUAboveLeft);
  }
  else
  {
    bAboveLeftFlag = (pcCUAboveLeft ? true : false);
  }

  return bAboveLeftFlag;
}

int isAboveAvailable(const CodingUnit &cu, const ChannelType &chType, const Position &posLT, const uint32_t uiNumUnitsInPU, const uint32_t unitWidth, bool *bValidFlags)
{
  const CodingStructure& cs = *cu.cs;
  const bool isConstrained = cs.pps->getConstrainedIntraPred();
  bool *pbValidFlags = bValidFlags;
  int iNumIntra = 0;
  int maxDx = uiNumUnitsInPU * unitWidth;

  for (uint32_t dx = 0; dx < maxDx; dx += unitWidth)
  {
    const Position refPos = posLT.offset(dx, -1);

    const CodingUnit* pcCUAbove = cs.isDecomp(refPos, chType) ? cs.getCURestricted(refPos, cu, chType) : nullptr;

    if( pcCUAbove && ( ( isConstrained && CU::isIntra( *pcCUAbove ) ) || !isConstrained ) )
    {
      iNumIntra++;
      *pbValidFlags = true;
    }
    else if( !pcCUAbove )
    {
      return iNumIntra;
    }

    pbValidFlags++;
  }
  return iNumIntra;
}

int isLeftAvailable(const CodingUnit &cu, const ChannelType &chType, const Position &posLT, const uint32_t uiNumUnitsInPU, const uint32_t unitHeight, bool *bValidFlags)
{
  const CodingStructure& cs = *cu.cs;
  const bool isConstrained = cs.pps->getConstrainedIntraPred();
  bool *pbValidFlags = bValidFlags;
  int iNumIntra = 0;
  int maxDy = uiNumUnitsInPU * unitHeight;

  for (uint32_t dy = 0; dy < maxDy; dy += unitHeight)
  {
    const Position refPos = posLT.offset(-1, dy);

    const CodingUnit* pcCULeft = cs.isDecomp(refPos, chType) ? cs.getCURestricted(refPos, cu, chType) : nullptr;

    if( pcCULeft && ( ( isConstrained && CU::isIntra( *pcCULeft ) ) || !isConstrained ) )
    {
      iNumIntra++;
      *pbValidFlags = true;
    }
    else if( !pcCULeft )
    {
      return iNumIntra;
    }

    pbValidFlags--; // opposite direction
  }

  return iNumIntra;
}

int isAboveRightAvailable(const CodingUnit &cu, const ChannelType &chType, const Position &posRT, const uint32_t uiNumUnitsInPU, const uint32_t unitWidth, bool *bValidFlags )
{
  const CodingStructure& cs = *cu.cs;
  const bool isConstrained = cs.pps->getConstrainedIntraPred();
  bool *pbValidFlags = bValidFlags;
  int iNumIntra = 0;

  uint32_t maxDx = uiNumUnitsInPU * unitWidth;

  for (uint32_t dx = 0; dx < maxDx; dx += unitWidth)
  {
    const Position refPos = posRT.offset(unitWidth + dx, -1);

    const CodingUnit* pcCUAbove = cs.isDecomp(refPos, chType) ? cs.getCURestricted(refPos, cu, chType) : nullptr;

    if( pcCUAbove && ( ( isConstrained && CU::isIntra( *pcCUAbove ) ) || !isConstrained ) )
    {
      iNumIntra++;
      *pbValidFlags = true;
    }
    else if( !pcCUAbove )
    {
      return iNumIntra;
    }

    pbValidFlags++;
  }

  return iNumIntra;
}

int isBelowLeftAvailable(const CodingUnit &cu, const ChannelType &chType, const Position &posLB, const uint32_t uiNumUnitsInPU, const uint32_t unitHeight, bool *bValidFlags )
{
  const CodingStructure& cs = *cu.cs;
  const bool isConstrained = cs.pps->getConstrainedIntraPred();
  bool *pbValidFlags = bValidFlags;
  int iNumIntra = 0;
  int maxDy = uiNumUnitsInPU * unitHeight;

  for (uint32_t dy = 0; dy < maxDy; dy += unitHeight)
  {
    const Position refPos = posLB.offset(-1, unitHeight + dy);

    const CodingUnit* pcCULeft = cs.isDecomp(refPos, chType) ? cs.getCURestricted(refPos, cu, chType) : nullptr;

    if( pcCULeft && ( ( isConstrained && CU::isIntra( *pcCULeft ) ) || !isConstrained ) )
    {
      iNumIntra++;
      *pbValidFlags = true;
    }
    else if ( !pcCULeft )
    {
      return iNumIntra;
    }

    pbValidFlags--; // opposite direction
  }

  return iNumIntra;
}
// LumaRecPixels
void IntraPrediction::xGetLumaRecPixels(const PredictionUnit &pu, CompArea chromaArea)
{
  int iDstStride = 0;
  Pel* pDst0 = 0;
  int curChromaMode = pu.intraDir[1];
  if ((curChromaMode == MDLM_L_IDX) || (curChromaMode == MDLM_T_IDX))
  {
    iDstStride = 2 * MAX_CU_SIZE + 1;
    pDst0 = m_pMdlmTemp + iDstStride + 1;
  }
  else
  {
  iDstStride = MAX_CU_SIZE + 1;
  pDst0 = m_piTemp + iDstStride + 1; //MMLM_SAMPLE_NEIGHBOR_LINES;
  }
  //assert 420 chroma subsampling
  CompArea lumaArea = CompArea( COMPONENT_Y, pu.chromaFormat, chromaArea.lumaPos(), recalcSize( pu.chromaFormat, CHANNEL_TYPE_CHROMA, CHANNEL_TYPE_LUMA, chromaArea.size() ) );//needed for correct pos/size (4x4 Tus)
  
#if JVET_N0671_CCLM
  CHECK(lumaArea.width == chromaArea.width && CHROMA_444 != pu.chromaFormat, "");
  CHECK(lumaArea.height == chromaArea.height && CHROMA_444 != pu.chromaFormat && CHROMA_422 != pu.chromaFormat, "");
#else
  CHECK( lumaArea.width  == chromaArea.width, "" );
  CHECK( lumaArea.height == chromaArea.height, "" );
#endif //JVET_N0671_CCLM

  const SizeType uiCWidth = chromaArea.width;
  const SizeType uiCHeight = chromaArea.height;

  const CPelBuf Src = pu.cs->picture->getRecoBuf( lumaArea );
  Pel const* pRecSrc0   = Src.bufAt( 0, 0 );
  int iRecStride        = Src.stride;
#if JVET_N0671_CCLM
  int logSubWidthC  = getChannelTypeScaleX(CHANNEL_TYPE_CHROMA, pu.chromaFormat);
  int logSubHeightC = getChannelTypeScaleY(CHANNEL_TYPE_CHROMA, pu.chromaFormat);

  int iRecStride2       = iRecStride << logSubHeightC;
  const int mult        =          1 << logSubWidthC ;
#else //!JVET_N0671_CCLM
  int iRecStride2       = iRecStride << 1;
#endif //JVET_N0671_CCLM

  const CodingUnit& lumaCU = isChroma( pu.chType ) ? *pu.cs->picture->cs->getCU( lumaArea.pos(), CH_L ) : *pu.cu;
  const CodingUnit&     cu = *pu.cu;

  const CompArea& area = isChroma( pu.chType ) ? chromaArea : lumaArea;

  const uint32_t uiTuWidth  = area.width;
  const uint32_t uiTuHeight = area.height;

  int iBaseUnitSize = ( 1 << MIN_CU_LOG2 );

  const int  iUnitWidth       = iBaseUnitSize >> getComponentScaleX( area.compID, area.chromaFormat );
#if JVET_N0671_CHROMA_FORMAT_422
  const int  iUnitHeight = iBaseUnitSize >> getComponentScaleY(area.compID, area.chromaFormat);
#else
  const int  iUnitHeight = iBaseUnitSize >> getComponentScaleX(area.compID, area.chromaFormat);
#endif // JVET_REXT_CLEANUP_CCLM_422

  const int  iTUWidthInUnits = uiTuWidth / iUnitWidth;
  const int  iTUHeightInUnits = uiTuHeight / iUnitHeight;
  const int  iAboveUnits      = iTUWidthInUnits;
  const int  iLeftUnits       = iTUHeightInUnits;
  const int  chromaUnitWidth = iBaseUnitSize >> getComponentScaleX(COMPONENT_Cb, area.chromaFormat);
#if JVET_N0671_CHROMA_FORMAT_422
  const int  chromaUnitHeight = iBaseUnitSize >> getComponentScaleY(COMPONENT_Cb, area.chromaFormat);
#else
  const int  chromaUnitHeight = iBaseUnitSize >> getComponentScaleX(COMPONENT_Cb, area.chromaFormat);
#endif // JVET_REXT_CLEANUP_CCLM_422
  const int  topTemplateSampNum = 2 * uiCWidth; // for MDLM, the number of template samples is 2W or 2H.
  const int  leftTemplateSampNum = 2 * uiCHeight;
  assert(m_topRefLength >= topTemplateSampNum);
  assert(m_leftRefLength >= leftTemplateSampNum);
  const int  totalAboveUnits = (topTemplateSampNum + (chromaUnitWidth - 1)) / chromaUnitWidth;
  const int  totalLeftUnits = (leftTemplateSampNum + (chromaUnitHeight - 1)) / chromaUnitHeight;
  const int  totalUnits = totalLeftUnits + totalAboveUnits + 1;
  const int  aboveRightUnits = totalAboveUnits - iAboveUnits;
  const int  leftBelowUnits = totalLeftUnits - iLeftUnits;

  int avaiAboveRightUnits = 0;
  int avaiLeftBelowUnits = 0;
  bool  bNeighborFlags[4 * MAX_NUM_PART_IDXS_IN_CTU_WIDTH + 1];
  memset(bNeighborFlags, 0, totalUnits);
  bool bAboveAvaillable, bLeftAvaillable;

  int availlableUnit = isLeftAvailable( isChroma( pu.chType ) ? cu : lumaCU, toChannelType( area.compID ), area.pos(), iLeftUnits, iUnitHeight,
  ( bNeighborFlags + iLeftUnits + leftBelowUnits - 1 ) );

  bLeftAvaillable = availlableUnit == iTUHeightInUnits;

  availlableUnit = isAboveAvailable( isChroma( pu.chType ) ? cu : lumaCU, toChannelType( area.compID ), area.pos(), iAboveUnits, iUnitWidth,
  ( bNeighborFlags + iLeftUnits + leftBelowUnits + 1 ) );

  bAboveAvaillable = availlableUnit == iTUWidthInUnits;

  if (bLeftAvaillable) // if left is not available, then the below left is not available
  {
    avaiLeftBelowUnits = isBelowLeftAvailable(isChroma(pu.chType) ? cu : lumaCU, toChannelType(area.compID), area.bottomLeftComp(area.compID), leftBelowUnits, iUnitHeight, (bNeighborFlags + leftBelowUnits - 1));
  }

  if (bAboveAvaillable) // if above is not available, then  the above right is not available.
  {
    avaiAboveRightUnits = isAboveRightAvailable(isChroma(pu.chType) ? cu : lumaCU, toChannelType(area.compID), area.topRightComp(area.compID), aboveRightUnits, iUnitWidth, (bNeighborFlags + iLeftUnits + leftBelowUnits + iAboveUnits + 1));
  }

  Pel*       pDst  = nullptr;
  Pel const* piSrc = nullptr;

  bool isFirstRowOfCtu = ((pu.block(COMPONENT_Cb).y)&(((pu.cs->sps)->getMaxCUWidth() >> 1) - 1)) == 0;
#if JVET_N0671_CCLM
  const int strOffset = (CHROMA_444 == pu.chromaFormat) ? 0 : iRecStride;

  int c0_2tap = 1, c1_2tap = 1,                                                     offset_2tap = 1, shift_2tap = 1; //sum = 2
  int c0_3tap = 2, c1_3tap = 1, c2_3tap = 1,                                        offset_3tap = 2, shift_3tap = 2; //sum = 4
  int c0_5tap = 1, c1_5tap = 4, c2_5tap = 1, c3_5tap = 1, c4_5tap = 1,              offset_5tap = 4, shift_5tap = 3; //sum = 8
  int c0_6tap = 2, c1_6tap = 1, c2_6tap = 1, c3_6tap = 2, c4_6tap = 1, c5_6tap = 1, offset_6tap = 4, shift_6tap = 3; //sum = 8

  switch (pu.chromaFormat)
  {
#if JVET_N0671_CHROMA_FORMAT_422
    case CHROMA_422: //overwrite filter coefficient values for 422
      c0_2tap = 1, c1_2tap = 0,                                                     offset_2tap = 0, shift_2tap = 0; //sum = 1
      c0_3tap = 2, c1_3tap = 1, c2_3tap = 1,                                        offset_3tap = 2, shift_3tap = 2; //sum = 4
      c0_5tap = 0, c1_5tap = 1, c2_5tap = 0, c3_5tap = 0, c4_5tap = 0,              offset_5tap = 0, shift_5tap = 0; //sum = 1
      c0_6tap = 2, c1_6tap = 1, c2_6tap = 1, c3_6tap = 0, c4_6tap = 0, c5_6tap = 0, offset_6tap = 2, shift_6tap = 2; //sum = 4
      break;
#endif //JVET_N0671_CHROMA_FORMAT_422

    case CHROMA_444:  //overwrite filter coefficient values for 422
      c0_2tap = 1, c1_2tap = 0,                                                     offset_2tap = 0, shift_2tap = 0; //sum = 1
      c0_3tap = 1, c1_3tap = 0, c2_3tap = 0,                                        offset_3tap = 0, shift_3tap = 0; //sum = 1
      c0_5tap = 0, c1_5tap = 1, c2_5tap = 0, c3_5tap = 0, c4_5tap = 0,              offset_5tap = 0, shift_5tap = 0; //sum = 1
      c0_6tap = 1, c1_6tap = 0, c2_6tap = 0, c3_6tap = 0, c4_6tap = 0, c5_6tap = 0, offset_6tap = 0, shift_6tap = 0; //sum = 1
      break;

    default:
      break;
  }
#endif //JVET_N0671_CCLM

  if( bAboveAvaillable )
  {
    pDst  = pDst0    - iDstStride;
    int addedAboveRight = 0;
    if ((curChromaMode == MDLM_L_IDX) || (curChromaMode == MDLM_T_IDX))
    {
      addedAboveRight = avaiAboveRightUnits*chromaUnitWidth;
    }
    for (int i = 0; i < uiCWidth + addedAboveRight; i++)
    {
      if (isFirstRowOfCtu)
      {
        piSrc = pRecSrc0 - iRecStride;

#if JVET_N0671_CCLM 
        if ((i == 0 && !bLeftAvaillable) || (i == uiCWidth + addedAboveRight - 1 + logSubWidthC))
#else //!JVET_N0671_CCLM
        if (i == 0 && !bLeftAvaillable)
#endif //JVET_N0671_CCLM
        {
#if JVET_N0671_CCLM
          pDst[i] = piSrc[mult * i];
#else //!JVET_N0671_CCLM
          pDst[i] = piSrc[2 * i];
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst[i] = (piSrc[mult * i] * c0_3tap + piSrc[mult * i - 1] * c1_3tap + piSrc[mult * i + 1] * c2_3tap + offset_3tap) >> shift_3tap;
#else //!JVET_N0671_CCLM
          pDst[i] = ( piSrc[2 * i] * 2 + piSrc[2 * i - 1] + piSrc[2 * i + 1] + 2 ) >> 2;
#endif //JVET_N0671_CCLM
        }
      }
      else if( pu.cs->sps->getCclmCollocatedChromaFlag() )
      {
        piSrc = pRecSrc0 - iRecStride2;
        
#if JVET_N0671_CCLM
        if ((i == 0 && !bLeftAvaillable) || (i == uiCWidth + addedAboveRight - 1 + logSubWidthC))
#else //!JVET_N0671_CCLM
        if( i == 0 && !bLeftAvaillable )
#endif //JVET_N0671_CCLM
        {
#if JVET_N0671_CCLM
          pDst[i] = (piSrc[mult * i] * c0_3tap + piSrc[mult * i - strOffset] * c1_3tap + piSrc[mult * i + strOffset] * c2_3tap + offset_3tap) >> shift_3tap;
#else //!JVET_N0671_CCLM
          pDst[i] = ( piSrc[2 * i] * 2 + piSrc[2 * i - iRecStride] + piSrc[2 * i + iRecStride] + 2 ) >> 2;
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst[i] = (piSrc[mult * i - strOffset] * c0_5tap
                  +  piSrc[mult * i]             * c1_5tap + piSrc[mult * i - 1] * c2_5tap + piSrc[mult * i + 1] * c3_5tap
                  +  piSrc[mult * i + strOffset] * c4_5tap
                  +  offset_5tap) >> shift_5tap;
#else //!JVET_N0671_CCLM
          pDst[i] = ( piSrc[2 * i - iRecStride]
                    + piSrc[2 * i             ] * 4 + piSrc[2 * i - 1] + piSrc[2 * i + 1]
                    + piSrc[2 * i + iRecStride]
                    + 4 ) >> 3;
#endif //JVET_N0671_CCLM
        }
      }
      else
      {
        piSrc = pRecSrc0 - iRecStride2;
        
#if JVET_N0671_CCLM
        if ((i == 0 && !bLeftAvaillable) || (i == uiCWidth + addedAboveRight - 1 + logSubWidthC))
#else //!JVET_N0671_CCLM
        if (i == 0 && !bLeftAvaillable)
#endif //JVET_N0671_CCLM
        {
#if JVET_N0671_CCLM
          pDst[i] = (piSrc[mult * i] * c0_2tap + piSrc[mult * i + strOffset] * c1_2tap + offset_2tap) >> shift_2tap;
#else //!JVET_N0671_CCLM
          pDst[i] = ( piSrc[2 * i] + piSrc[2 * i + iRecStride] + 1 ) >> 1;
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst[i] = ((piSrc[mult * i]            * c0_6tap + piSrc[mult * i - 1]             * c1_6tap + piSrc[mult * i + 1]             * c2_6tap)
                  + (piSrc[mult * i + strOffset] * c3_6tap + piSrc[mult * i - 1 + strOffset] * c4_6tap + piSrc[mult * i + 1 + strOffset] * c5_6tap)
                  + offset_6tap) >> shift_6tap;
#else //!JVET_N0671_CCLM
          pDst[i] = ( ( ( piSrc[2 * i             ] * 2 ) + piSrc[2 * i - 1             ] + piSrc[2 * i + 1             ] )
                    + ( ( piSrc[2 * i + iRecStride] * 2 ) + piSrc[2 * i - 1 + iRecStride] + piSrc[2 * i + 1 + iRecStride] )
                    + 4 ) >> 3;
#endif //JVET_N0671_CCLM
        }
      }
    }
  }

  if( bLeftAvaillable )
  {
    pDst  = pDst0    - 1;

#if JVET_N0671_CCLM
    piSrc = pRecSrc0 - 2 - logSubWidthC;
#else //!JVET_N0671_CCLM
    piSrc = pRecSrc0 - 3;
#endif //JVET_N0671_CCLM

    int addedLeftBelow = 0;
    if ((curChromaMode == MDLM_L_IDX) || (curChromaMode == MDLM_T_IDX))
    {
      addedLeftBelow = avaiLeftBelowUnits*chromaUnitHeight;
    }
    
    for (int j = 0; j < uiCHeight + addedLeftBelow; j++)
    {
      if( pu.cs->sps->getCclmCollocatedChromaFlag() )
      {
#if JVET_N0671_CCLM
        if ((j == 0 && !bAboveAvaillable) || (j == uiCHeight + addedLeftBelow - 1 + logSubWidthC))
        {
          pDst[0] = ( piSrc[1] * c0_3tap + piSrc[0] * c1_3tap + piSrc[2] * c2_3tap + offset_3tap) >> shift_3tap;
#else //!JVET_N0671_CCLM
        if ( j == 0 && !bAboveAvaillable )
        {
          pDst[0] = ( piSrc[1] * 2 + piSrc[0] + piSrc[2] + 2 ) >> 2;
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst[0] = ( piSrc[1 - strOffset] * c0_5tap
                    + piSrc[1            ] * c1_5tap + piSrc[0] * c2_5tap + piSrc[2] * c3_5tap
                    + piSrc[1 + strOffset] * c4_5tap
                    + offset_5tap ) >> shift_5tap;
#else //!JVET_N0671_CCLM
          pDst[0] = ( piSrc[1 - iRecStride]
                    + piSrc[1             ] * 4 + piSrc[0] + piSrc[2]
                    + piSrc[1 + iRecStride]
                    + 4 ) >> 3;
#endif //JVET_N0671_CCLM
        }
      }
      else
      {
#if JVET_N0671_CCLM
        pDst[0] = ((piSrc[1]             * c0_6tap + piSrc[0]         * c1_6tap + piSrc[2]             * c2_6tap)
                +  (piSrc[1 + strOffset] * c3_6tap + piSrc[strOffset] * c4_6tap + piSrc[2 + strOffset] * c5_6tap)
                +   offset_6tap) >> shift_6tap;
#else //!JVET_N0671_CCLM
        pDst[0] = ( ( piSrc[1             ] * 2 + piSrc[0         ] + piSrc[2             ] )
                  + ( piSrc[1 + iRecStride] * 2 + piSrc[iRecStride] + piSrc[2 + iRecStride] )
                  + 4 ) >> 3;
#endif //JVET_N0671_CCLM
      }

      piSrc += iRecStride2;
      pDst  += iDstStride;
    }
  }

  // inner part from reconstructed picture buffer
  for( int j = 0; j < uiCHeight; j++ )
  {
    for( int i = 0; i < uiCWidth; i++ )
    {
      if( pu.cs->sps->getCclmCollocatedChromaFlag() )
      {
        if( i == 0 && !bLeftAvaillable )
        {
          if ( j == 0 && !bAboveAvaillable )
          {
#if JVET_N0671_CCLM
            pDst0[i] = pRecSrc0[mult * i];
#else //!JVET_N0671_CCLM
            pDst0[i] = pRecSrc0[2 * i];
#endif //JVET_N0671_CCLM
          }
          else
          {
#if JVET_N0671_CCLM
            pDst0[i] = (pRecSrc0[mult * i] * c0_3tap + pRecSrc0[mult * i - strOffset] * c1_3tap + pRecSrc0[mult * i + strOffset] * c2_3tap + offset_3tap) >> shift_3tap;
#else //!JVET_N0671_CCLM
            pDst0[i] = ( pRecSrc0[2 * i] * 2 + pRecSrc0[2 * i - iRecStride] + pRecSrc0[2 * i + iRecStride] + 2 ) >> 2;
#endif //JVET_N0671_CCLM
          }
        }
#if JVET_N0671_CCLM
        else if ( j == 0 && !bAboveAvaillable )
#else //JVET_N0671_CCLM
        else if ( j == 0 && !bAboveAvaillable )
#endif //JVET_N0671_CCLM
        {
#if JVET_N0671_CCLM
          pDst0[i] = (pRecSrc0[mult * i] * c0_3tap + pRecSrc0[mult * i - 1] * c1_3tap + pRecSrc0[mult * i + 1] * c2_3tap + offset_3tap) >> shift_3tap;
#else //!JVET_N0671_CCLM
          pDst0[i] = ( pRecSrc0[2 * i] * 2 + pRecSrc0[2 * i - 1] + pRecSrc0[2 * i + 1] + 2 ) >> 2;
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst0[i] = (pRecSrc0[mult * i - strOffset] * c0_5tap
                   +  pRecSrc0[mult * i]             * c1_5tap + pRecSrc0[mult * i - 1] * c2_5tap + pRecSrc0[mult * i + 1] * c3_5tap
                   +  pRecSrc0[mult * i + strOffset] * c4_5tap
                   +  offset_5tap) >> shift_5tap;
#else //!JVET_N0671_CCLM
          pDst0[i] = ( pRecSrc0[2 * i - iRecStride]
                     + pRecSrc0[2 * i             ] * 4 + pRecSrc0[2 * i - 1] + pRecSrc0[2 * i + 1]
                     + pRecSrc0[2 * i + iRecStride]
                     + 4 ) >> 3;
#endif //JVET_N0671_CCLM
        }
      }
      else
      {

#if JVET_N0671_CCLM 
        if ((i == 0 && !bLeftAvaillable) || (i == uiCWidth - 1 + logSubWidthC))
#else //JVET_N0671_CCLM
        if ( i == 0 && !bLeftAvaillable )
#endif //JVET_N0671_CCLM
        {
#if JVET_N0671_CCLM
          pDst0[i] = (pRecSrc0[mult * i] * c0_2tap + pRecSrc0[mult * i + strOffset] * c1_2tap + offset_2tap) >> shift_2tap;
#else //!JVET_N0671_CCLM
          pDst0[i] = ( pRecSrc0[2 * i] + pRecSrc0[2 * i + iRecStride] + 1 ) >> 1;
#endif //JVET_N0671_CCLM
        }
        else
        {
#if JVET_N0671_CCLM
          pDst0[i] = (pRecSrc0[mult * i]             * c0_6tap + pRecSrc0[mult * i + 1]             * c1_6tap + pRecSrc0[mult * i - 1]             * c2_6tap
                    + pRecSrc0[mult * i + strOffset] * c3_6tap + pRecSrc0[mult * i + 1 + strOffset] * c4_6tap + pRecSrc0[mult * i - 1 + strOffset] * c5_6tap
                    + offset_6tap) >> shift_6tap;
#else //!JVET_N0671_CCLM
          pDst0[i] = ( pRecSrc0[2 * i             ] * 2 + pRecSrc0[2 * i + 1             ] + pRecSrc0[2 * i - 1             ]
                     + pRecSrc0[2 * i + iRecStride] * 2 + pRecSrc0[2 * i + 1 + iRecStride] + pRecSrc0[2 * i - 1 + iRecStride]
                     + 4 ) >> 3;
#endif //JVET_N0671_CCLM
        }
      }
    }

    pDst0    += iDstStride;
    pRecSrc0 += iRecStride2;
  }
}
void IntraPrediction::xGetLMParameters(const PredictionUnit &pu, const ComponentID compID,
                                              const CompArea &chromaArea,
                                              int &a, int &b, int &iShift)
{
  CHECK(compID == COMPONENT_Y, "");

  const SizeType cWidth  = chromaArea.width;
  const SizeType cHeight = chromaArea.height;

  const Position posLT = chromaArea;

  CodingStructure & cs = *(pu.cs);
  const CodingUnit &cu = *(pu.cu);

  const SPS &        sps           = *cs.sps;
  const uint32_t     tuWidth     = chromaArea.width;
  const uint32_t     tuHeight    = chromaArea.height;
  const ChromaFormat nChromaFormat = sps.getChromaFormatIdc();

  const int baseUnitSize = 1 << MIN_CU_LOG2;
  const int unitWidth    = baseUnitSize >> getComponentScaleX(chromaArea.compID, nChromaFormat);
  const int unitHeight   = baseUnitSize >> getComponentScaleX(chromaArea.compID, nChromaFormat);

  const int tuWidthInUnits  = tuWidth / unitWidth;
  const int tuHeightInUnits = tuHeight / unitHeight;
  const int aboveUnits      = tuWidthInUnits;
  const int leftUnits       = tuHeightInUnits;
  int topTemplateSampNum = 2 * cWidth; // for MDLM, the template sample number is 2W or 2H;
  int leftTemplateSampNum = 2 * cHeight;
  assert(m_topRefLength >= topTemplateSampNum);
  assert(m_leftRefLength >= leftTemplateSampNum);
  int totalAboveUnits = (topTemplateSampNum + (unitWidth - 1)) / unitWidth;
  int totalLeftUnits = (leftTemplateSampNum + (unitHeight - 1)) / unitHeight;
  int totalUnits = totalLeftUnits + totalAboveUnits + 1;
  int aboveRightUnits = totalAboveUnits - aboveUnits;
  int leftBelowUnits = totalLeftUnits - leftUnits;
  int avaiAboveRightUnits = 0;
  int avaiLeftBelowUnits = 0;
  int avaiAboveUnits = 0;
  int avaiLeftUnits = 0;

  int curChromaMode = pu.intraDir[1];
  bool neighborFlags[4 * MAX_NUM_PART_IDXS_IN_CTU_WIDTH + 1];
  memset(neighborFlags, 0, totalUnits);

  bool aboveAvailable, leftAvailable;

  int availableUnit =
    isAboveAvailable(cu, CHANNEL_TYPE_CHROMA, posLT, aboveUnits, unitWidth,
    (neighborFlags + leftUnits + leftBelowUnits + 1));
  aboveAvailable = availableUnit == tuWidthInUnits;

  availableUnit =
    isLeftAvailable(cu, CHANNEL_TYPE_CHROMA, posLT, leftUnits, unitHeight,
    (neighborFlags + leftUnits + leftBelowUnits - 1));
  leftAvailable = availableUnit == tuHeightInUnits;
  if (leftAvailable) // if left is not available, then the below left is not available
  {
    avaiLeftUnits = tuHeightInUnits;
    avaiLeftBelowUnits = isBelowLeftAvailable(cu, CHANNEL_TYPE_CHROMA, chromaArea.bottomLeftComp(chromaArea.compID), leftBelowUnits, unitHeight, (neighborFlags + leftBelowUnits - 1));
  }
  if (aboveAvailable) // if above is not available, then  the above right is not available.
  {
    avaiAboveUnits = tuWidthInUnits;
    avaiAboveRightUnits = isAboveRightAvailable(cu, CHANNEL_TYPE_CHROMA, chromaArea.topRightComp(chromaArea.compID), aboveRightUnits, unitWidth, (neighborFlags + leftUnits + leftBelowUnits + aboveUnits + 1));
  }
  Pel *srcColor0, *curChroma0;
  int  srcStride, curStride;

  PelBuf temp;
  if ((curChromaMode == MDLM_L_IDX) || (curChromaMode == MDLM_T_IDX))
  {
    srcStride = 2 * MAX_CU_SIZE + 1;
    temp = PelBuf(m_pMdlmTemp + srcStride + 1, srcStride, Size(chromaArea));
  }
  else
  {
  srcStride = MAX_CU_SIZE + 1;
  temp        = PelBuf(m_piTemp + srcStride + 1, srcStride, Size(chromaArea));
  }
  srcColor0 = temp.bufAt(0, 0);
  curChroma0 = getPredictorPtr(compID);

  curStride = m_topRefLength + 1;

  curChroma0 += curStride + 1;

  unsigned internalBitDepth = sps.getBitDepth(CHANNEL_TYPE_CHROMA);

  int minLuma[2] = {  MAX_INT, 0 };
  int maxLuma[2] = { -MAX_INT, 0 };

  Pel *src = srcColor0 - srcStride;
  Pel *cur = curChroma0 - curStride;
#if !JVET_N0271_SIMPLFIED_CCLM
  int minDim = 1;
#endif
  int actualTopTemplateSampNum = 0;
  int actualLeftTemplateSampNum = 0;
  if (curChromaMode == MDLM_T_IDX)
  {
    leftAvailable = 0;
#if JVET_N0271_SIMPLFIED_CCLM
    avaiAboveRightUnits = avaiAboveRightUnits > (cHeight/unitWidth) ?  cHeight/unitWidth : avaiAboveRightUnits;
#endif
    actualTopTemplateSampNum = unitWidth*(avaiAboveUnits + avaiAboveRightUnits);
#if !JVET_N0271_SIMPLFIED_CCLM
    minDim = actualTopTemplateSampNum;
#endif
  }
  else if (curChromaMode == MDLM_L_IDX)
  {
    aboveAvailable = 0;
#if JVET_N0271_SIMPLFIED_CCLM
     avaiLeftBelowUnits = avaiLeftBelowUnits > (cWidth/unitHeight) ? cWidth/unitHeight : avaiLeftBelowUnits;
#endif
    actualLeftTemplateSampNum = unitHeight*(avaiLeftUnits + avaiLeftBelowUnits);
#if !JVET_N0271_SIMPLFIED_CCLM
    minDim = actualLeftTemplateSampNum;
#endif
  }
  else if (curChromaMode == LM_CHROMA_IDX)
  {
    actualTopTemplateSampNum = cWidth;
    actualLeftTemplateSampNum = cHeight;
#if !JVET_N0271_SIMPLFIED_CCLM
    minDim = leftAvailable && aboveAvailable ? 1 << g_aucPrevLog2[std::min(actualLeftTemplateSampNum, actualTopTemplateSampNum)]
      : 1 << g_aucPrevLog2[leftAvailable ? actualLeftTemplateSampNum : actualTopTemplateSampNum];
#endif
  }
#if JVET_N0271_SIMPLFIED_CCLM
  int startPos[2]; //0:Above, 1: Left
  int pickStep[2];

  int aboveIs4 = leftAvailable  ? 0 : 1;
  int leftIs4 =  aboveAvailable ? 0 : 1;

  startPos[0] = actualTopTemplateSampNum >> (2 + aboveIs4);
  pickStep[0] = std::max(1, actualTopTemplateSampNum >> (1 + aboveIs4));

  startPos[1] = actualLeftTemplateSampNum >> (2 + leftIs4);
  pickStep[1] = std::max(1, actualLeftTemplateSampNum >> (1 + leftIs4));

  Pel selectLumaPix[4] = { 0, 0, 0, 0 };
  Pel selectChromaPix[4] = { 0, 0, 0, 0 };

  int cntT, cntL;
  cntT = cntL = 0;
  int cnt = 0;
  if (aboveAvailable)
  {
    cntT = std::min(actualTopTemplateSampNum, (1 + aboveIs4) << 1);
    src = srcColor0 - srcStride;
    cur = curChroma0 - curStride;
    for (int pos = startPos[0]; cnt < cntT; pos += pickStep[0], cnt++)
    {
      selectLumaPix[cnt] = src[pos];
      selectChromaPix[cnt] = cur[pos];
    }
  }

  if (leftAvailable)
  {
    cntL = std::min(actualLeftTemplateSampNum, ( 1 + leftIs4 ) << 1 );
    src = srcColor0 - 1;
    cur = curChroma0 - 1;
    for (int pos = startPos[1], cnt = 0; cnt < cntL; pos += pickStep[1], cnt++)
    {
      selectLumaPix[cnt + cntT] = src[pos * srcStride];
      selectChromaPix[cnt+ cntT] = cur[pos * curStride];
    }
  }
  cnt = cntL + cntT;
 
  if (cnt == 2)
  {
    selectLumaPix[3] = selectLumaPix[0]; selectChromaPix[3] = selectChromaPix[0];
    selectLumaPix[2] = selectLumaPix[1]; selectChromaPix[2] = selectChromaPix[1];
    selectLumaPix[0] = selectLumaPix[1]; selectChromaPix[0] = selectChromaPix[1];
    selectLumaPix[1] = selectLumaPix[3]; selectChromaPix[1] = selectChromaPix[3];
  }

  int minGrpIdx[2] = { 0, 2 };
  int maxGrpIdx[2] = { 1, 3 };
  int *tmpMinGrp = minGrpIdx;
  int *tmpMaxGrp = maxGrpIdx;
  if (selectLumaPix[tmpMinGrp[0]] > selectLumaPix[tmpMinGrp[1]]) std::swap(tmpMinGrp[0], tmpMinGrp[1]);
  if (selectLumaPix[tmpMaxGrp[0]] > selectLumaPix[tmpMaxGrp[1]]) std::swap(tmpMaxGrp[0], tmpMaxGrp[1]);
  if (selectLumaPix[tmpMinGrp[0]] > selectLumaPix[tmpMaxGrp[1]]) std::swap(tmpMinGrp, tmpMaxGrp);
  if (selectLumaPix[tmpMinGrp[1]] > selectLumaPix[tmpMaxGrp[0]]) std::swap(tmpMinGrp[1], tmpMaxGrp[0]);

  minLuma[0] = (selectLumaPix[tmpMinGrp[0]] + selectLumaPix[tmpMinGrp[1]] + 1 )>>1;
  minLuma[1] = (selectChromaPix[tmpMinGrp[0]] + selectChromaPix[tmpMinGrp[1]] + 1) >> 1;
  maxLuma[0] = (selectLumaPix[tmpMaxGrp[0]] + selectLumaPix[tmpMaxGrp[1]] + 1 )>>1;
  maxLuma[1] = (selectChromaPix[tmpMaxGrp[0]] + selectChromaPix[tmpMaxGrp[1]] + 1) >> 1;
#else
  int numSteps = minDim;

  if (aboveAvailable)
  {
    for (int j = 0; j < numSteps; j++)
    {
      int idx = (j * actualTopTemplateSampNum) / minDim;

      if (minLuma[0] > src[idx])
      {
        minLuma[0] = src[idx];
        minLuma[1] = cur[idx];
      }
      if (maxLuma[0] < src[idx])
      {
        maxLuma[0] = src[idx];
        maxLuma[1] = cur[idx];
      }
    }
  }

  if (leftAvailable)
  {
    src = srcColor0 - 1;
    cur = curChroma0 - 1;

    for (int i = 0; i < numSteps; i++)
    {
      int idx = (i * actualLeftTemplateSampNum) / minDim;

      if (minLuma[0] > src[srcStride * idx])
      {
        minLuma[0] = src[srcStride * idx];
        minLuma[1] = cur[curStride * idx];
      }
      if (maxLuma[0] < src[srcStride * idx])
      {
        maxLuma[0] = src[srcStride * idx];
        maxLuma[1] = cur[curStride * idx];
      }
    }
  }
#endif

  if (leftAvailable || aboveAvailable)
  {
    int diff = maxLuma[0] - minLuma[0];
    if (diff > 0)
    {
      int diffC = maxLuma[1] - minLuma[1];
      int x = floorLog2( diff );
      static const uint8_t DivSigTable[1 << 4] = {
        // 4bit significands - 8 ( MSB is omitted )
        0,  7,  6,  5,  5,  4,  4,  3,  3,  2,  2,  1,  1,  1,  1,  0
      };
      int normDiff = (diff << 4 >> x) & 15;
      int v = DivSigTable[normDiff] | 8;
      x += normDiff != 0;

      int y = floorLog2( abs( diffC ) ) + 1;
      int add = 1 << y >> 1;
      a = (diffC * v + add) >> y;
      iShift = 3 + x - y;
      if ( iShift < 1 ) {
        iShift = 1;
        a = ( (a == 0)? 0: (a < 0)? -15 : 15 );   // a=Sign(a)*15
      }
      b = minLuma[1] - ((a * minLuma[0]) >> iShift);
    }
    else
    {
      a = 0;
      b = minLuma[1];
      iShift = 0;
    }
  }
  else
  {
    a = 0;

    b = 1 << (internalBitDepth - 1);

    iShift = 0;
  }
}

#if JVET_N0217_MATRIX_INTRAPRED
void IntraPrediction::initIntraMip( const PredictionUnit &pu )
{
  CHECK( pu.lwidth() > MIP_MAX_WIDTH || pu.lheight() > MIP_MAX_HEIGHT, "Error: block size not supported for MIP" );

  // derive above and left availability
  AvailableInfo availInfo = PU::getAvailableInfoLuma(pu);

  // prepare input (boundary) data for prediction
  m_matrixIntraPred.prepareInputForPred(pu.cs->picture->getRecoBuf(COMPONENT_Y), pu.Y(), pu.cu->slice->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA), availInfo);
}

void IntraPrediction::predIntraMip( const ComponentID compId, PelBuf &piPred, const PredictionUnit &pu )
{
  CHECK( compId != COMPONENT_Y, "Error: chroma not supported" );
  CHECK( pu.lwidth() > MIP_MAX_WIDTH || pu.lheight() > MIP_MAX_HEIGHT, "Error: block size not supported for MIP" );
  CHECK( pu.lwidth() != (1 << g_aucLog2[pu.lwidth()]) || pu.lheight() != (1 << g_aucLog2[pu.lheight()]), "Error: expecting blocks of size 2^M x 2^N" );

  // generate mode-specific prediction
  const int bitDepth = pu.cu->slice->getSPS()->getBitDepth(CHANNEL_TYPE_LUMA);
  m_matrixIntraPred.predBlock( pu.Y(), pu.intraDir[CHANNEL_TYPE_LUMA], piPred, bitDepth );
}
#endif

//! \}
