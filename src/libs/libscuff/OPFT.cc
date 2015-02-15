/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * OPFT.cc     -- computation of power, force, and torque using overlap
 *             -- integrals between RWG basis functions
 *
 * homer reid  -- 5/2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

#include <libhrutil.h>

#include "libscuff.h"
#include "libscuffInternals.h"

#include "cmatheval.h"

#define II cdouble(0.0,1.0)

namespace scuff {

/***************************************************************/
/* these constants identify various types of overlap           */
/* integrals; they are only used in this file.                 */
/* Note that these constants are talking about types of        */
/* overlap *integrals*, not to be confused with the various    */
/* types of overlap *matrices,* which are index by different   */
/* constants defined in libscuff.h. (The entries of the overlap*/
/* matrices are linear combinations of various types of overlap*/
/* integrals.)                                                 */
/***************************************************************/
#define OVERLAP_OVERLAP           0
#define OVERLAP_CROSS             1
#define OVERLAP_BULLET_X          2    
#define OVERLAP_NABLANABLA_X      3
#define OVERLAP_TIMESNABLA_X      4
#define OVERLAP_BULLET_Y          5  
#define OVERLAP_NABLANABLA_Y      6
#define OVERLAP_TIMESNABLA_Y      7
#define OVERLAP_BULLET_Z          8  
#define OVERLAP_NABLANABLA_Z      9
#define OVERLAP_TIMESNABLA_Z     10
#define OVERLAP_RXBULLET_X       11    
#define OVERLAP_RXNABLANABLA_X   12
#define OVERLAP_RXTIMESNABLA_X   13
#define OVERLAP_RXBULLET_Y       14  
#define OVERLAP_RXNABLANABLA_Y   15
#define OVERLAP_RXTIMESNABLA_Y   16
#define OVERLAP_RXBULLET_Z       17  
#define OVERLAP_RXNABLANABLA_Z   18
#define OVERLAP_RXTIMESNABLA_Z   19

#define NUMOVERLAPS 20

// Note: the prefactor of (10/3) in the force and torque factors 
// below arises as follows: the force quantity that we would compute
// without it has units of 
// 1 watt / c = (1 joule/s) * (10-8 s/m) / 3
//            = (10/3) nanoNewton
// so we want to multiply the number we would naively 
// compute by 10/3 to get a force in nanonewtons.
// similarly for the torque: multiplying by 10/3 gives the torque
// in nanoNewtons*microns (assuming the incident field was 
// measured in units of volts / microns)
#define TENTHIRDS 3.33333333333333333333333

/***************************************************************/
/* this is a helper function for GetOverlaps that computes the */
/* contributions of a single panel to the overlap integrals.   */
/***************************************************************/
void AddOverlapContributions(RWGSurface *S, RWGPanel *P, int iQa, int iQb, 
                             double Sign, double LL, double *Overlaps)
{
  double *Qa   = S->Vertices + 3*P->VI[ iQa ];
  double *QaP1 = S->Vertices + 3*P->VI[ (iQa+1)%3 ];
  double *QaP2 = S->Vertices + 3*P->VI[ (iQa+2)%3 ];
  double *Qb   = S->Vertices + 3*P->VI[ iQb ];
  double *ZHat = P->ZHat;

  double L1[3], L2[3], DQ[3];
  VecSub(QaP1, Qa, L1);
  VecSub(QaP2, QaP1, L2);
  VecSub(Qa, Qb, DQ);

  double ZxL1[3], ZxL2[3], ZxDQ[3], ZxQa[3], QaxZxL1[3], QaxZxL2[3];
  VecCross(ZHat,   L1,     ZxL1);
  VecCross(ZHat,   L2,     ZxL2);
  VecCross(ZHat,   DQ,     ZxDQ);
  VecCross(ZHat,   Qa,     ZxQa);
  VecCross(Qa,   ZxL1,  QaxZxL1);
  VecCross(Qa,   ZxL2,  QaxZxL2);

  double PreFac = Sign * LL / (2.0*P->Area);

  double L1dL1 = L1[0]*L1[0] + L1[1]*L1[1] + L1[2]*L1[2];
  double L1dL2 = L1[0]*L2[0] + L1[1]*L2[1] + L1[2]*L2[2];
  double L1dDQ = L1[0]*DQ[0] + L1[1]*DQ[1] + L1[2]*DQ[2];
  double L2dL2 = L2[0]*L2[0] + L2[1]*L2[1] + L2[2]*L2[2];
  double L2dDQ = L2[0]*DQ[0] + L2[1]*DQ[1] + L2[2]*DQ[2];

  double TimesFactor = (  (2.0*L1[0]+L2[0])*ZxDQ[0]  
                        + (2.0*L1[1]+L2[1])*ZxDQ[1] 
                        + (2.0*L1[2]+L2[2])*ZxDQ[2]  ) / 6.0;

  double BulletFactor1 =  (L1dL1 + L1dL2)/4.0 + L1dDQ/3.0 + L2dL2/12.0 + L2dDQ/6.0;
  double BulletFactor2 =  (L1dL1 + L1dL2)/5.0 + L1dDQ/4.0 + L2dL2/15.0 + L2dDQ/8.0;
  double BulletFactor3 =  L1dL1/10.0 + 2.0*L1dL2/15.0 + L1dDQ/8.0 + L2dL2/20.0 + L2dDQ/12.0;
  double NablaCrossFactor =  (L1dL1 + L1dL2)/2.0 + L2dL2/6.0;

  Overlaps[0]  += PreFac * BulletFactor1;
  Overlaps[1]  += PreFac * TimesFactor;

  Overlaps[2]  += PreFac * ZHat[0] * BulletFactor1;
  Overlaps[3]  += PreFac * ZHat[0] * 2.0;
  Overlaps[4]  += PreFac * (2.0*ZxL1[0] + ZxL2[0]) / 3.0;

  Overlaps[5]  += PreFac * ZHat[1] * BulletFactor1;
  Overlaps[6]  += PreFac * ZHat[1] * 2.0;
  Overlaps[7]  += PreFac * (2.0*ZxL1[1] + ZxL2[1]) / 3.0;

  Overlaps[8]  += PreFac * ZHat[2] * BulletFactor1;
  Overlaps[9]  += PreFac * ZHat[2] * 2.0;
  Overlaps[10] += PreFac * (2.0*ZxL1[2] + ZxL2[2]) / 3.0;

  Overlaps[11] -= PreFac * (ZxQa[0]*BulletFactor1 + ZxL1[0]*BulletFactor2 + ZxL2[0]*BulletFactor3);
  Overlaps[12] -= PreFac * (2.0*ZxQa[0] + 4.0*ZxL1[0]/3.0 + 2.0*ZxL2[0]/3.0);
  Overlaps[13] += PreFac * (ZHat[0]*NablaCrossFactor + 2.0*QaxZxL1[0]/3.0 + QaxZxL2[0]/3.0);

  Overlaps[14] -= PreFac * (ZxQa[1]*BulletFactor1 + ZxL1[1]*BulletFactor2 + ZxL2[1]*BulletFactor3);
  Overlaps[15] -= PreFac * (2.0*ZxQa[1] + 4.0*ZxL1[1]/3.0 + 2.0*ZxL2[1]/3.0);
  Overlaps[16] += PreFac * (ZHat[1]*NablaCrossFactor + 2.0*QaxZxL1[1]/3.0 + QaxZxL2[1]/3.0);

  Overlaps[17] -= PreFac * (ZxQa[2]*BulletFactor1 + ZxL1[2]*BulletFactor2 + ZxL2[2]*BulletFactor3);
  Overlaps[18] -= PreFac * (2.0*ZxQa[2] + 4.0*ZxL1[2]/3.0 + 2.0*ZxL2[2]/3.0);
  Overlaps[19] += PreFac * (ZHat[2]*NablaCrossFactor + 2.0*QaxZxL1[2]/3.0 + QaxZxL2[2]/3.0);

}

/***************************************************************/
/* Get the overlap integrals between a single pair of RWG      */
/* basis functions on an RWG surface.                          */
/*                                                             */
/* entries of output array:                                    */
/*                                                             */
/*  Overlaps [0] = O^{\bullet}_{\alpha\beta}                   */
/*   = \int f_a \cdot f_b                                      */
/*                                                             */
/*  Overlaps [1] = O^{\times}_{\alpha\beta}                    */
/*   = \int f_a \cdot (nHat \times f_b)                        */
/*                                                             */
/*  Overlaps [2] = O^{x,\bullet}_{\alpha\beta}                 */
/*   = \int nHat_x f_a \cdot f_b                               */
/*                                                             */
/*  Overlaps [3] = O^{x,\nabla\nabla}_{\alpha\beta}            */
/*   = \int nHat_x (\nabla \cdot f_a) (\nabla \cdot f_b)       */
/*                                                             */
/*  Overlaps [4] = O^{x,\times\nabla}_{\alpha\beta}            */
/*   = \int (nHat \times \cdot f_a)_x (\nabla \cdot f_b)       */
/*                                                             */
/*  [5,6,7]  = like [2,3,4] but with x-->y                     */
/*  [8,9,10] = like [2,3,4] but with x-->z                     */
/*                                                             */
/*  [11-19]: like [2...10] but with an extra factor of         */
/*           (rHat x ) thrown in for torque purposes           */
/*                                                             */
/* note: for now, the origin about which torque is computed    */
/* coincides with the origin of the coordinate system in which */
/* the surface mesh was defined (i.e. the point with coords    */
/* (0,0,0) in the mesh file, as transformed by any             */
/* GTransformations that have been applied since the mesh file */
/* was read in.) if you want to compute the torque about a     */
/* different origin, a quick-and-dirty procedure is to         */
/* apply a GTransformation to the surface, compute the         */
/* overlaps, then undo the GTransformation.                    */
/***************************************************************/
void RWGSurface::GetOverlaps(int neAlpha, int neBeta, double *Overlaps)
{
  RWGEdge *EAlpha = Edges[neAlpha];
  RWGEdge *EBeta  = Edges[neBeta];

  RWGPanel *PAlphaP = Panels[EAlpha->iPPanel];
  RWGPanel *PAlphaM = (EAlpha->iMPanel == -1) ? 0 : Panels[EAlpha->iMPanel];
  int iQPAlpha = EAlpha->PIndex;
  int iQMAlpha = EAlpha->MIndex;
  int iQPBeta  = EBeta->PIndex;
  int iQMBeta  = EBeta->MIndex;

  double LL = EAlpha->Length * EBeta->Length;

  memset(Overlaps,0,NUMOVERLAPS*sizeof(double));

  if ( EAlpha->iPPanel == EBeta->iPPanel )
   AddOverlapContributions(this, PAlphaP, iQPAlpha, iQPBeta,  1.0, LL, Overlaps);
  if ( EAlpha->iPPanel == EBeta->iMPanel )
   AddOverlapContributions(this, PAlphaP, iQPAlpha, iQMBeta, -1.0, LL, Overlaps);
  if ( (EAlpha->iMPanel!=-1) && (EAlpha->iMPanel == EBeta->iPPanel ) )
   AddOverlapContributions(this, PAlphaM, iQMAlpha, iQPBeta, -1.0, LL, Overlaps);
  if ( (EAlpha->iMPanel!=-1) && (EAlpha->iMPanel == EBeta->iMPanel ) )
   AddOverlapContributions(this, PAlphaM, iQMAlpha, iQMBeta,  1.0, LL, Overlaps);
  
}

/*****************************************************************/
/* this is a simpler interface to the above routine that returns */
/* the simple overlap integral and sets *pOTimes = crossed       */
/* overlap integral if it is non-NULL                            */
/*****************************************************************/
double RWGSurface::GetOverlap(int neAlpha, int neBeta, double *pOTimes)
{
  double Overlaps[NUMOVERLAPS];
  GetOverlaps(neAlpha, neBeta, Overlaps);
  if (pOTimes) *pOTimes=Overlaps[1];
  return Overlaps[0];
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
int GetOverlappingEdgeIndices(RWGSurface *S, int nea, int nebArray[5])
{
  nebArray[0] = nea;

  RWGEdge *E   = S->Edges[nea];
  RWGPanel *PP = S->Panels[ E->iPPanel ]; 
  int      iQP = E->PIndex;
  nebArray[1] = PP->EI[ (iQP+1)%3 ];
  nebArray[2] = PP->EI[ (iQP+2)%3 ];

  if ( E->iMPanel == -1 )
   return 3;

  RWGPanel *PM = S->Panels[ E->iMPanel ];
  int      iQM = E->MIndex;
  nebArray[3] = PM->EI[ (iQM+1)%3 ];
  nebArray[4] = PM->EI[ (iQM+2)%3 ];
   
  return 5;
}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GetOPFT(RWGGeometry *G, int SurfaceIndex, cdouble Omega,
             HVector *KNVector, HVector *RHS, HMatrix *RytovMatrix,
             double PFT[8], double **ByEdge)
{
  
  if (SurfaceIndex<0 || SurfaceIndex>=G->NumSurfaces)
   { memset(PFT,0,8*sizeof(double));
     Warn("GetOPFT called for unknown surface #i",SurfaceIndex);
     return;
   };

  RWGSurface *S=G->Surfaces[SurfaceIndex];
  int Offset = G->BFIndexOffset[SurfaceIndex];
  int NE=S->NumEdges;

  /*--------------------------------------------------------------*/
  /*- get material parameters of exterior medium -----------------*/
  /*--------------------------------------------------------------*/
  cdouble ZZ=ZVAC, k2=Omega*Omega;
  cdouble Eps, Mu;
  G->RegionMPs[S->RegionIndices[0]]->GetEpsMu(Omega, &Eps, &Mu);
  k2 *= Eps*Mu;
  ZZ *= sqrt(Mu/Eps);

  /*--------------------------------------------------------------*/
  /*- initialize edge-by-edge contributions to zero --------------*/
  /*--------------------------------------------------------------*/
  if (ByEdge)
   { for(int nq=0; nq<NUMPFT; nq++)
      if (ByEdge[nq])
       memset(ByEdge[nq],0,NE*sizeof(double));
   };

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  double PAbs=0.0, Fx=0.0, Fy=0.0, Fz=0.0, Taux=0.0, Tauy=0.0, Tauz=0.0;
  for(int nea=0; nea<NE; nea++)
   { 
     /*--------------------------------------------------------------*/
     /* populate an array whose indices are the 3 or 5 edges         */
     /* that have nonzero overlaps with edge #nea, then loop over    */
     /* those edges                                                  */
     /*--------------------------------------------------------------*/
     int nebArray[5];
     int nebCount= GetOverlappingEdgeIndices(S, nea, nebArray);
     for(int nneb=0; nneb<nebCount; nneb++)
      { 
        int neb=nebArray[nneb];
        double Overlaps[20];
        S->GetOverlaps(nea, neb, Overlaps);

        /*--------------------------------------------------------------*/
        /*--------------------------------------------------------------*/
        /*--------------------------------------------------------------*/
        cdouble KK, KN, NK, NN;
        if (KNVector && S->IsPEC)
         { 
           cdouble kAlpha =       KNVector->GetEntry(Offset + nea);
           cdouble kBeta  =       KNVector->GetEntry(Offset + neb);
           KK = conj(kAlpha) * kBeta;
           KN = NK = NN = 0.0;
         }
        else if (KNVector && !(S->IsPEC) )
         { 
           cdouble kAlpha =       KNVector->GetEntry(Offset + 2*nea + 0);
           cdouble nAlpha = -ZVAC*KNVector->GetEntry(Offset + 2*nea + 1);
           cdouble kBeta  =       KNVector->GetEntry(Offset + 2*neb + 0);
           cdouble nBeta  = -ZVAC*KNVector->GetEntry(Offset + 2*neb + 1);

           KK = conj(kAlpha) * kBeta;
           KN = conj(kAlpha) * nBeta;
           NK = conj(nAlpha) * kBeta;
           NN = conj(nAlpha) * nBeta;
         }
        else
         {
           KK = RytovMatrix->GetEntry(Offset+2*neb+0, Offset+2*nea+0);
           KN = RytovMatrix->GetEntry(Offset+2*neb+1, Offset+2*nea+0);
           NK = RytovMatrix->GetEntry(Offset+2*neb+0, Offset+2*nea+1);
           NN = RytovMatrix->GetEntry(Offset+2*neb+1, Offset+2*nea+1);
         };

       /*--------------------------------------------------------------*/
       /*--------------------------------------------------------------*/
       /*--------------------------------------------------------------*/
       // power
       double dPAbs = 0.25*real( (KN-NK) * Overlaps[OVERLAP_CROSS] );

       // force, torque
       double dF[3], dTau[3];
       dF[0] = 0.25*TENTHIRDS*
               real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_BULLET_X] - Overlaps[OVERLAP_NABLANABLA_X]/k2)
                     +(NK-KN)*2.0*Overlaps[OVERLAP_TIMESNABLA_X] / (II*Omega)
                   );

       dF[1] = 0.25*TENTHIRDS*
               real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_BULLET_Y] - Overlaps[OVERLAP_NABLANABLA_Y]/k2)
                     +(NK-KN)*2.0*Overlaps[OVERLAP_TIMESNABLA_Y] / (II*Omega)
                   );

       dF[2] = 0.25*TENTHIRDS*
               real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_BULLET_Z] - Overlaps[OVERLAP_NABLANABLA_Z]/k2)
                     +(NK-KN)*2.0*Overlaps[OVERLAP_TIMESNABLA_Z] / (II*Omega)
                   );

       dTau[0] = 0.25*TENTHIRDS*
                 real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_RXBULLET_X] - Overlaps[OVERLAP_RXNABLANABLA_X]/k2)
                       +(NK-KN)*2.0*Overlaps[OVERLAP_RXTIMESNABLA_X] / (II*Omega)
                     );

       dTau[1] = 0.25*TENTHIRDS*
                 real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_RXBULLET_Y] - Overlaps[OVERLAP_RXNABLANABLA_Y]/k2)
                       +(NK-KN)*2.0*Overlaps[OVERLAP_RXTIMESNABLA_Y] / (II*Omega)
                     );

       dTau[2] = 0.25*TENTHIRDS*
                 real( -(KK*ZZ + NN/ZZ)*(Overlaps[OVERLAP_RXBULLET_Z] - Overlaps[OVERLAP_RXNABLANABLA_Z]/k2)
                       +(NK-KN)*2.0*Overlaps[OVERLAP_RXTIMESNABLA_Z] / (II*Omega)
                     );

       /*--------------------------------------------------------------*/
       /*- accumulate contributions to full sums ----------------------*/
       /*--------------------------------------------------------------*/
       PAbs += dPAbs;
       Fx   += dF[0];
       Fy   += dF[1];
       Fz   += dF[2];
       Taux += dTau[0];
       Tauy += dTau[1];
       Tauz += dTau[2];

       /*--------------------------------------------------------------*/
       /*- accumulate contributions to by-edge sums -------------------*/
       /*--------------------------------------------------------------*/
       if (ByEdge) 
        {  
          if (ByEdge[0]) ByEdge[0][nea] += dPAbs;
          if (ByEdge[1]) ByEdge[1][nea] += dF[0];
          if (ByEdge[2]) ByEdge[2][nea] += dF[1];
          if (ByEdge[3]) ByEdge[3][nea] += dF[2];
          if (ByEdge[4]) ByEdge[4][nea] += dTau[0];
          if (ByEdge[5]) ByEdge[5][nea] += dTau[1];
          if (ByEdge[6]) ByEdge[6][nea] += dTau[2];
        };

      } // for (int nneb=... 

   }; // for(int nea=0; nea<S->NE; nea++)

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  PFT[0] = PAbs;
  PFT[1] = 0.0;
  PFT[2] = Fx;
  PFT[3] = Fy;
  PFT[4] = Fz;
  PFT[5] = Taux;
  PFT[6] = Tauy;
  PFT[7] = Tauz;

  /*--------------------------------------------------------------*/
  /*- if an RHS vector was specified, compute the extinction      */
  /*- (total power) and use it to compute the scattered power     */
  /*--------------------------------------------------------------*/
  if (KNVector && RHS)
   { double Extinction=0.0;
     for (int ne=0, nbf=0; ne<NE; ne++)
      { 
        cdouble kAlpha =   KNVector->GetEntry(Offset + nbf);
        cdouble vEAlpha = -ZVAC*RHS->GetEntry(Offset + nbf);
        nbf++;
        Extinction += 0.5*real( conj(kAlpha)*vEAlpha );
        if (S->IsPEC) continue;

        cdouble nAlpha  = -ZVAC*KNVector->GetEntry(Offset + nbf);
        cdouble vHAlpha =       -1.0*RHS->GetEntry(Offset + nbf);
        nbf++;
        Extinction += 0.5*real( conj(nAlpha)*vHAlpha );
      };
     PFT[1] = Extinction - PFT[0];
   };

} // GetOPFT

}// namespace scuff
