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

/** \file     QuantRDOQ.h
    \brief    RDOQ class (header)
*/

#ifndef __QUANTRDOQ__
#define __QUANTRDOQ__

#include "CommonDef.h"
#include "Unit.h"
#include "ChromaFormat.h"
#include "Contexts.h"
#include "ContextModelling.h"

#include "Quant.h"

//! \ingroup CommonLib
//! \{

// ====================================================================================================================
// Class definition
// ====================================================================================================================

/// transform and quantization class
class QuantRDOQ : public Quant
{
public:
  QuantRDOQ( const Quant* other );
  ~QuantRDOQ();

public:
#if HEVC_USE_SCALING_LISTS
  void setFlatScalingList   ( const int maxLog2TrDynamicRange[MAX_NUM_CHANNEL_TYPE], const BitDepths &bitDepths );
  void setScalingList       ( ScalingList *scalingList, const int maxLog2TrDynamicRange[MAX_NUM_CHANNEL_TYPE], const BitDepths &bitDepths);
#endif
  // quantization
  void quant                ( TransformUnit &tu, const ComponentID &compID, const CCoeffBuf &pSrc, TCoeff &uiAbsSum, const QpParam &cQP, const Ctx& ctx );
#if JVET_N0413_RDPCM
  void forwardRDPCM         ( TransformUnit &tu, const ComponentID &compID, const CCoeffBuf &pSrc, TCoeff &uiAbsSum, const QpParam &cQP, const Ctx &ctx );
#endif

private:
#if HEVC_USE_SCALING_LISTS
  double* xGetErrScaleCoeff              ( uint32_t list, uint32_t sizeX, uint32_t sizeY, int qp ) { return m_errScale             [sizeX][sizeY][list][qp]; };  //!< get Error Scale Coefficent
  double& xGetErrScaleCoeffNoScalingList ( uint32_t list, uint32_t sizeX, uint32_t sizeY, int qp ) { return m_errScaleNoScalingList[sizeX][sizeY][list][qp]; };  //!< get Error Scale Coefficent
  void    xInitScalingList               ( const QuantRDOQ* other );
  void    xDestroyScalingList            ();
  void    xSetErrScaleCoeff              ( uint32_t list, uint32_t sizeX, uint32_t sizeY, int qp, const int maxLog2TrDynamicRange[MAX_NUM_CHANNEL_TYPE], const BitDepths &bitDepths );
#else
  double  xGetErrScaleCoeff              ( const bool needsSqrt2, SizeType width, SizeType height, int qp, const int maxLog2TrDynamicRange, const int channelBitDepth);
#endif
#if JVET_N0413_RDPCM
  void    xDequantSample                 ( TCoeff& pRes, TCoeff& coeff, const TrQuantParams& trQuantParams );
#endif
  // RDOQ functions
  void xRateDistOptQuant(TransformUnit &tu, const ComponentID &compID, const CCoeffBuf &pSrc, TCoeff &uiAbsSum, const QpParam &cQP, const Ctx &ctx);

  inline uint32_t xGetCodedLevel( double&            rd64CodedCost,
                              double&            rd64CodedCost0,
                              double&            rd64CodedCostSig,
                              Intermediate_Int   lLevelDouble,
                              uint32_t               uiMaxAbsLevel,
                              const BinFracBits* fracBitsSig,
                              const BinFracBits& fracBitsPar,
                              const BinFracBits& fracBitsGt1,
                              const BinFracBits& fracBitsGt2,
                              const int          remGt2Bins,
                              const int          remRegBins,
                              unsigned           goRiceZero,
                              uint16_t             ui16AbsGoRice,
                              int                iQBits,
                              double             errorScale,
                              bool               bLast,
                              bool               useLimitedPrefixLength,
                              const int          maxLog2TrDynamicRange ) const;
  inline int xGetICRate     ( const uint32_t         uiAbsLevel,
                              const BinFracBits& fracBitsPar,
                              const BinFracBits& fracBitsGt1,
                              const BinFracBits& fracBitsGt2,
                              const int          remGt2Bins,
                              const int          remRegBins,
                              unsigned           goRiceZero,
                              const uint16_t       ui16AbsGoRice,
                              const bool         useLimitedPrefixLength,
                              const int          maxLog2TrDynamicRange  ) const;
  inline double xGetRateLast         ( const int* lastBitsX, const int* lastBitsY,
                                       unsigned        PosX, unsigned   PosY                              ) const;

  inline double xGetRateSigCoeffGroup( const BinFracBits& fracBitsSigCG,   unsigned uiSignificanceCoeffGroup ) const;

  inline double xGetRateSigCoef      ( const BinFracBits& fracBitsSig,     unsigned uiSignificance           ) const;

  inline double xGetICost            ( double dRate                                                      ) const;
  inline double xGetIEPRate          (                                                                   ) const;

#if JVET_N0280_RESIDUAL_CODING_TS
  void xRateDistOptQuantTS( TransformUnit &tu, const ComponentID &compID, const CCoeffBuf &coeffs, TCoeff &absSum, const QpParam &qp, const Ctx &ctx );

  inline uint32_t xGetCodedLevelTS(       double&             codedCost,
                                          double&             codedCost0,
                                          double&             codedCostSig,
                                          Intermediate_Int    levelDouble,
                                          uint32_t            maxAbsLevel,
                                    const BinFracBits*        fracBitsSig,
                                    const BinFracBits&        fracBitsPar,
                                    const CoeffCodingContext& cctx,
                                    const FracBitsAccess&     fracBitsAccess,
                                    const BinFracBits&        fracBitsSign,
                                    const uint8_t             sign,
                                          uint16_t            ricePar,
                                          int                 qBits,
                                          double              errorScale,
                                          bool                isLast,
                                          bool                useLimitedPrefixLength,
                                    const int                 maxLog2TrDynamicRange ) const;

  inline int xGetICRateTS   ( const uint32_t            absLevel,
                              const BinFracBits&        fracBitsPar,
                              const CoeffCodingContext& cctx,
                              const FracBitsAccess&     fracBitsAccess,
                              const BinFracBits&        fracBitsSign,
                              const uint8_t             sign,
                              const uint16_t            ricePar,
                              const bool                useLimitedPrefixLength,
                              const int                 maxLog2TrDynamicRange  ) const;
#endif
private:
#if HEVC_USE_SCALING_LISTS
  bool    m_isErrScaleListOwner;

  double *m_errScale             [SCALING_LIST_SIZE_NUM][SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of quantization matrix coefficient 4x4
  double  m_errScaleNoScalingList[SCALING_LIST_SIZE_NUM][SCALING_LIST_SIZE_NUM][SCALING_LIST_NUM][SCALING_LIST_REM_NUM]; ///< array of quantization matrix coefficient 4x4
#endif
  // temporary buffers for RDOQ
  double m_pdCostCoeff        [MAX_TB_SIZEY * MAX_TB_SIZEY];
  double m_pdCostSig          [MAX_TB_SIZEY * MAX_TB_SIZEY];
  double m_pdCostCoeff0       [MAX_TB_SIZEY * MAX_TB_SIZEY];
  double m_pdCostCoeffGroupSig[(MAX_TB_SIZEY * MAX_TB_SIZEY) >> MLS_CG_SIZE]; // even if CG size is 2 (if one of the sides is 2) instead of 4, there should be enough space
#if HEVC_USE_SIGN_HIDING
  int    m_rateIncUp          [MAX_TB_SIZEY * MAX_TB_SIZEY];
  int    m_rateIncDown        [MAX_TB_SIZEY * MAX_TB_SIZEY];
  int    m_sigRateDelta       [MAX_TB_SIZEY * MAX_TB_SIZEY];
  TCoeff m_deltaU             [MAX_TB_SIZEY * MAX_TB_SIZEY];
#if JVET_N0413_RDPCM
  TCoeff m_fullCoeff          [MAX_TB_SIZEY * MAX_TB_SIZEY];
#endif
#endif
};// END CLASS DEFINITION QuantRDOQ

//! \}

#endif // __QUANTRDOQ__
