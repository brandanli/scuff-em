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
 * EPPFT.cc    -- 'equivalence-principle power, force, and torque'
 *             -- calculation in scuff-EM
 *
 * homer reid  -- 9/2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libhmat.h>
#include <libhrutil.h>
#include <libTriInt.h>

#include "libscuff.h"
#include "libscuffInternals.h"
#include "TaylorDuffy.h"

#include "cmatheval.h"

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif
#ifdef USE_PTHREAD
#  include <pthread.h>
#endif
#ifdef USE_OPENMP

#endif

#define II cdouble(0.0,1.0)
#define TENTHIRDS (10.0/3.0)
#define NUMPFT 7

namespace scuff {

void GetReducedPotentials_Nearby(RWGGeometry *G, const int ns, const int np, const int iQ,
                                 const double X0[3], const cdouble k,
                                 cdouble *p, cdouble a[3],
                                 cdouble dp[3], cdouble da[3][3],
                                 cdouble ddp[3][3], cdouble dcurla[3][3]);

void GetReducedFields_Nearby(RWGGeometry *G, const int ns,
                             const int np, const int iQ,
                             const double X0[3],  const cdouble k,
                             cdouble e[3], cdouble h[3])
{
  cdouble p[1], a[3], dp[3], da[3][3], ddp[3][3], dcurla[3][3];
  GetReducedPotentials_Nearby(G, ns, np, iQ, X0, k, 
                              p, a, dp, da, ddp, dcurla);

  cdouble k2 = k*k;
  e[0] = a[0] + dp[0]/k2;
  e[1] = a[1] + dp[1]/k2;
  e[2] = a[2] + dp[2]/k2;
  h[0] = da[1][2] - da[2][1];
  h[1] = da[2][0] - da[0][2];
  h[2] = da[0][1] - da[1][0];

}

/***************************************************************/
/* Fetch the particular matrix elements between RWG functions  */
/* that we need to compute the surface EPPFT.                  */
/*                                                             */
/* be[0]       = < b_\alpha | e_beta >                         */
/* bh[0]       = < b_\alpha | h_beta >                         */
/*                                                             */
/* divbe[0..2] = < \div b_\alpha | e_beta_i >  (i=0,1,2)       */
/* divbh[0..2] = < \div b_\alpha | h_beta_i >                  */
/*                                                             */
/* bxe[0..2]   = < b_\alpha \times e_beta >_i                  */
/* bxh[0..2]   = < b_\alpha \times h_beta >_i                  */
/*                                                             */
/***************************************************************/
void GetEPPFTMatrixElements_Cubature(RWGGeometry *G,
                            int nsa, int nsb, int nea, int neb,
                            cdouble k,
                            cdouble be[1],      cdouble bh[1],
                            cdouble divbe[3],   cdouble divbh[3],
                            cdouble bxe[3],     cdouble bxh[3],
                            cdouble divbrxe[3], cdouble divbrxh[3],
                            cdouble rxbxe[3],   cdouble rxbxh[3],
                            bool OmitPanelPair[2][2], int Order=4)
{
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  RWGSurface *S=G->Surfaces[nsa];
  RWGEdge *Ea=S->Edges[nea];
  RWGEdge *Eb=S->Edges[neb];
  double *QP = S->Vertices + 3*Ea->iQP;
  double *V1 = S->Vertices + 3*Ea->iV1;
  double *V2 = S->Vertices + 3*Ea->iV2;
  double *QM = S->Vertices + 3*Ea->iQM;

  double X0[3]={0.0, 0.0, 0.0}; // torque center
  if (S->OTGT) S->OTGT->Apply(X0);
  if (S->GT) S->GT->Apply(X0);

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  double APlus[3], AMinus[3], B[3];
  VecSub(V1, QP, APlus);
  VecSub(V1, QM, AMinus);
  VecSub(V2, V1, B);
    
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  double PP = OmitPanelPair[0][0] ?  0.0 : +1.0;
  double PM = OmitPanelPair[0][1] ?  0.0 : -1.0;
  double MP = OmitPanelPair[1][0] ?  0.0 : -1.0;
  double MM = OmitPanelPair[1][1] ?  0.0 : +1.0;
#if 0
  if(OmitPanelPair[0][0]) printf("Omitting panel pair (00)\n");
  if(OmitPanelPair[0][1]) printf("Omitting panel pair (01)\n");
  if(OmitPanelPair[1][0]) printf("Omitting panel pair (10)\n");
  if(OmitPanelPair[1][1]) printf("Omitting panel pair (11)\n");
#endif

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  int NumPts;
  double *TCR=GetTCR(Order, &NumPts);
  be[0]=bh[0]=0.0;
  memset(divbe,   0, 3*sizeof(cdouble));
  memset(divbh,   0, 3*sizeof(cdouble));
  memset(bxe,     0, 3*sizeof(cdouble));
  memset(bxh,     0, 3*sizeof(cdouble));
  memset(divbrxe, 0, 3*sizeof(cdouble));
  memset(divbrxh, 0, 3*sizeof(cdouble));
  memset(rxbxe,   0, 3*sizeof(cdouble));
  memset(rxbxh,   0, 3*sizeof(cdouble));
  for(int np=0, ncp=0; np<NumPts; np++)
   { 
     /***************************************************************/
     /***************************************************************/
     /***************************************************************/
     double u=TCR[ncp++];
     double v=TCR[ncp++];
     double w=TCR[ncp++] * (Ea->Length);
     u+=v;

     double bPlus[3], XPlus[3], XPmX0[3];
     double bMinus[3], XMinus[3], XMmX0[3];
     double XPmX0dotb=0.0, XMmX0dotb=0.0;
     for(int Mu=0; Mu<3; Mu++)
      { 
        bPlus[Mu] = u*APlus[Mu] + v*B[Mu];
        XPlus[Mu] = bPlus[Mu] + QP[Mu];
        XPmX0[Mu] = XPlus[Mu] - X0[Mu];
        XPmX0dotb += XPmX0[Mu]*bPlus[Mu];

        bMinus[Mu] = u*AMinus[Mu] + v*B[Mu];
        XMinus[Mu] = bMinus[Mu] + QM[Mu];
        XMmX0[Mu]  = XMinus[Mu] - X0[Mu];
        XMmX0dotb += XMmX0[Mu]*bMinus[Mu];
      };

     /***************************************************************/
     /***************************************************************/
     /***************************************************************/
     cdouble ePP[3], ePM[3], eMP[3], eMM[3];
     cdouble hPP[3], hPM[3], hMP[3], hMM[3];
     GetReducedFields_Nearby(G, nsb, Eb->iPPanel, Eb->PIndex, XPlus,  k, ePP, hPP);
     GetReducedFields_Nearby(G, nsb, Eb->iMPanel, Eb->MIndex, XPlus,  k, ePM, hPM);
     GetReducedFields_Nearby(G, nsb, Eb->iPPanel, Eb->PIndex, XMinus, k, eMP, hMP);
     GetReducedFields_Nearby(G, nsb, Eb->iMPanel, Eb->MIndex, XMinus, k, eMM, hMM);

     /***************************************************************/
     /***************************************************************/
     /***************************************************************/
     cdouble ePlus[3], eMinus[3], hPlus[3], hMinus[3];
     cdouble XPmX0dote=0.0, XMmX0dote=0.0, XPmX0doth=0.0, XMmX0doth=0.0;
     for(int Mu=0; Mu<3; Mu++)
      { 
        ePlus[Mu]  = ePP[Mu] - ePM[Mu];
        eMinus[Mu] = eMP[Mu] - eMM[Mu];
        hPlus[Mu]  = hPP[Mu] - hPM[Mu];
        hMinus[Mu] = hMP[Mu] - hMM[Mu];

        XPmX0dote  += XPmX0[Mu]*ePlus[Mu];
        XMmX0dote  += XMmX0[Mu]*eMinus[Mu];

        XPmX0doth  += XPmX0[Mu]*hPlus[Mu];
        XMmX0doth  += XMmX0[Mu]*hMinus[Mu];
      };

     /***************************************************************/
     /***************************************************************/
     /***************************************************************/
     for(int Mu=0; Mu<3; Mu++)
       {
         int MP1=(Mu+1)%3, MP2=(Mu+2)%3;

         be[0] += w*(bPlus[Mu]*ePlus[Mu] - bMinus[Mu]*eMinus[Mu]);
         bh[0] += w*(bPlus[Mu]*hPlus[Mu] - bMinus[Mu]*hMinus[Mu]);

         divbe[Mu] += 2.0*w*( PP*ePP[Mu] + PM*ePM[Mu] + MP*eMP[Mu] + MM*eMM[Mu]);
         divbh[Mu] += 2.0*w*( PP*hPP[Mu] + PM*hPM[Mu] + MP*hMP[Mu] + MM*hMM[Mu]);

         bxe[Mu] += w*( PP*( bPlus[MP1]*ePP[MP2] -  bPlus[MP2]*ePP[MP1])
                       +PM*( bPlus[MP1]*ePM[MP2] -  bPlus[MP2]*ePM[MP1])
                       +MP*(bMinus[MP1]*eMP[MP2] - bMinus[MP2]*eMP[MP1])
                       +MM*(bMinus[MP1]*eMM[MP2] - bMinus[MP2]*eMM[MP1])
                      );

         bxh[Mu] += w*( (bPlus[MP1]*hPlus[MP2]   - bPlus[MP2]*hPlus[MP1])
                       -(bMinus[MP1]*hMinus[MP2] - bMinus[MP2]*hMinus[MP1])
                      );

         divbrxe[Mu] += w*( (XPmX0[MP1]*ePlus[MP2]  - XPmX0[MP2]*ePlus[MP1])
                           -(XMmX0[MP1]*eMinus[MP2] - XMmX0[MP2]*eMinus[MP1])
                          );

         divbrxh[Mu] += w*( (XPmX0[MP1]*hPlus[MP2]  - XPmX0[MP2]*hPlus[MP1])
                           -(XMmX0[MP1]*hMinus[MP2] - XMmX0[MP2]*hMinus[MP1])
                          );

         // [Ax(BxC)]_mu = B_\mu (A\cdot C) - C_\mu (A\cdot B)
         rxbxe[Mu] += w*( (bPlus[Mu]*XPmX0dote  - ePlus[Mu]*XPmX0dotb)
                         -(bMinus[Mu]*XMmX0dote - eMinus[Mu]*XMmX0dotb)
                        );

         rxbxh[Mu] += w*( (bPlus[Mu]*XPmX0doth  - hPlus[Mu]*XPmX0dotb)
                         -(bMinus[Mu]*XMmX0doth - hMinus[Mu]*XMmX0dotb)
                        );
       };

   };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GetEPPFTMatrixElements_TD(double *Va[3], double *Qa,
                               double *Vb[3], double *Qb,
                               int ncv, cdouble k,
                               cdouble divbe[3],
                               cdouble divbh[3],
                               cdouble bxe[3])
{
  TaylorDuffyArgStruct TDArgStruct, *TDArgs=&TDArgStruct;
  InitTaylorDuffyArgs(TDArgs);
  
  double nHat[3]={0,0,0};
  cdouble Result[6], Error[6];

  int PIndex[6]={TD_EPPFT1, TD_EPPFT2, TD_EPPFT3, 
                 TD_EPPFT4, TD_EPPFT5, TD_EPPFT6};
  int KIndex[6]={ TD_HELMHOLTZ, TD_GRADHELMHOLTZ, TD_GRADHELMHOLTZ,
                  TD_HELMHOLTZ, TD_GRADHELMHOLTZ, TD_GRADHELMHOLTZ};
  cdouble KParam[6]={k,k,k,k,k,k};

  TDArgs->WhichCase=ncv;
  TDArgs->NumPKs = 6;
  TDArgs->PIndex=PIndex;
  TDArgs->KIndex=KIndex;
  TDArgs->KParam=KParam;
  TDArgs->V1=Va[0];
  TDArgs->V2=Va[1];
  TDArgs->V3=Va[2];
  TDArgs->V2P=Vb[1];
  TDArgs->V3P=Vb[2];
  TDArgs->Q=Qa;
  TDArgs->QP=Qb;
  TDArgs->Result=Result;
  TDArgs->Error=Error;
  TDArgs->nHat = nHat;
  TDArgs->Result = Result;
  TDArgs->Error = Error;
  
  for(int Mu=0; Mu<3; Mu++)
   { 
     memset(nHat, 0, 3*sizeof(double));
     nHat[Mu] = 1.0;

     TaylorDuffy(TDArgs);

     divbe[Mu] = Result[0] + Result[1]/(k*k);
     divbh[Mu] = Result[2];
     bxe[Mu]   = Result[3] + Result[4]/(k*k);
   };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
void GetEPPFTMatrixElements(RWGGeometry *G,
                            int nsa, int nsb, int nea, int neb,
                            cdouble k,
                            cdouble be[1],      cdouble bh[1],
                            cdouble divbe[3],   cdouble divbh[3],
                            cdouble bxe[3],     cdouble bxh[3],
                            cdouble divbrxe[3], cdouble divbrxh[3],
                            cdouble rxbxe[3],   cdouble rxbxh[3],
                            int Order=4)
{
  RWGSurface *Sa=G->Surfaces[nsa];
  RWGSurface *Sb=G->Surfaces[nsb];

  RWGEdge *Ea=Sa->Edges[nea];
  RWGEdge *Eb=Sb->Edges[neb];
  double LL=Ea->Length * Eb->Length;

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
bool ForceCubature=false;
if (getenv("SCUFF_FORCECUBATURE"))
 { //printf("Forcing cubature.\n");
   ForceCubature=true;
 };
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/


  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  cdouble divbeTD[3] = {0.0, 0.0, 0.0};
  cdouble divbhTD[3] = {0.0, 0.0, 0.0};
  cdouble bxeTD[3]   = {0.0, 0.0, 0.0};
  bool OmitPanelPair[2][2]={ {false, false}, {false, false} };
  bool HaveTDContributions=false;
  if (nsa==nsb && ForceCubature==false)
   for(int A=0; A<2; A++)
    for(int B=0; B<2; B++)
     {
       int npa = (A==0) ? Ea->iPPanel : Ea->iMPanel;
       int npb = (B==0) ? Eb->iPPanel : Eb->iMPanel;
       double *Va[3], *Vb[3], rRel;
       int ncv=AssessPanelPair(Sa,npa,Sb,npb,&rRel,Va,Vb);
       if (ncv>0)
        { 
          OmitPanelPair[A][B]=true;
          HaveTDContributions=true;

          double *Qa = Sa->Vertices + 3*( (A==0) ? Ea->iQP : Ea->iQM);
          double *Qb = Sb->Vertices + 3*( (B==0) ? Eb->iQP : Eb->iQM);

          cdouble Delta_divbe[3], Delta_divbh[3], Delta_bxe[3];
//printf("Computing TD contributions for (%i,%i)\n",npa,npb);
          GetEPPFTMatrixElements_TD(Va, Qa, Vb, Qb, ncv, k,
                                    Delta_divbe, Delta_divbh, Delta_bxe);


          double Sign = (A==B) ? 1.0 : -1.0;
          for(int Mu=0; Mu<3; Mu++)
           { divbeTD[Mu] += Sign*LL*Delta_divbe[Mu];
             divbhTD[Mu] += Sign*LL*Delta_divbh[Mu];
             bxeTD[Mu]   += Sign*LL*Delta_bxe[Mu];
           };
        };

     }; //  for(int A=0; A<2; A++) ...  for(int B=0; B<2; B++)

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  GetEPPFTMatrixElements_Cubature(G, nsa, nsb, nea, neb, k,
                                  be, bh, divbe, divbh, bxe, bxh,
                                  divbrxe, divbrxh, rxbxe, rxbxh,
                                  OmitPanelPair, Order);

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  if (HaveTDContributions)
   { for(int Mu=0; Mu<3; Mu++)
      { divbe[Mu] += divbeTD[Mu];
        divbh[Mu] += divbhTD[Mu];
        bxe[Mu]   += bxeTD[Mu];
      };
   };

}

/***************************************************************/
/* Note: Either KNVector or SigmaMatrix should be non-null.    */
/*                                                             */
/* If ByEdge is non-null, it must be a 2D array of size        */
/*  [7][NE], where NE is the number of interior edges in       */
/*  surface #SurfaceIndex. In this case, if ByEdge[nq] is      */
/*  non-null, then on return ByEdge[nq][ne] is the contribution*/
/*  of edge #ne to quantity #nq.                               */
/***************************************************************/
void RWGGeometry::GetEPPFTTrace(int SurfaceIndex, cdouble Omega,
                                HVector *KNVector, HMatrix *SigmaMatrix,
                                double PFT[NUMPFT], double **ByEdge,
                                bool Exterior)
{
  /*--------------------------------------------------------------*/
  /*- get material parameters of interior and exterior regions    */
  /*--------------------------------------------------------------*/
  RWGSurface *S=Surfaces[SurfaceIndex];
  int Offset = BFIndexOffset[SurfaceIndex];
  int NE = S->NumEdges;
  int nrOut  = S->RegionIndices[0];
  int nrIn   = S->RegionIndices[1];
  if ( S->IsPEC || nrIn==-1 ) // EPPFT not defined for PEC bodies
   { 
     Warn("EPPFTTrace() not implemented for PEC bodies");
     memset(PFT, 0, NUMPFT*sizeof(double)); 
     return;
   };

  cdouble EpsOut, MuOut, EpsIn, MuIn;
  RegionMPs[nrOut]->GetEpsMu(Omega, &EpsOut, &MuOut);
  RegionMPs[nrIn]->GetEpsMu(Omega, &EpsIn, &MuIn);

  cdouble k, ZRel, GammaE, GammaM;
  double Sign;
  bool Interior = !Exterior;
  
  if (Exterior)
   { 
     Sign = 1.0;
     k = Omega * sqrt(EpsOut*MuOut);
     ZRel = sqrt(MuOut/EpsOut);
     GammaE=GammaM=0.0;
   }
  else
   { 
     Sign = -1.0;
     k = Omega * sqrt(EpsIn*MuIn);
     ZRel = sqrt(MuIn/EpsIn);
     GammaE=(1.0/EpsIn - 1.0/EpsOut) * ZVAC;
     GammaM=(1.0/MuIn  - 1.0/MuOut) / ZVAC;
   };

  Log("Computing EPPFT for surface %i (Ext)=(%i,%i) (ZRel=%s)",
       SurfaceIndex,Exterior,z2s(ZRel));

  /*--------------------------------------------------------------*/
  /*- define the constant prefactors that enter the power, force -*/
  /*- and torque formulas                                        -*/
  /*--------------------------------------------------------------*/
  cdouble KZ  = k*ZVAC*ZRel;
  cdouble KOZ = k/(ZVAC*ZRel);
  cdouble Omega2=Omega*Omega;

  cdouble PEE  = +0.5*II*KZ;
  cdouble PEM  = -0.5;
  cdouble PME  = +0.5;
  cdouble PMM  = +0.5*II*KOZ;

  cdouble FEE1 = -0.5*TENTHIRDS*KZ/Omega;
  cdouble FEE2 = +0.5*TENTHIRDS*ZVAC;
  cdouble FEM1 = +0.5*TENTHIRDS/(II*Omega);
  cdouble FEM2 = +0.5*TENTHIRDS*II*KOZ*ZVAC;
  cdouble FME1 = -0.5*TENTHIRDS/(II*Omega);
  cdouble FME2 = -0.5*TENTHIRDS*II*KZ/ZVAC;
  cdouble FMM1 = -0.5*TENTHIRDS*KOZ/Omega;
  cdouble FMM2 = +0.5*TENTHIRDS/ZVAC;
  cdouble FEE3 = +0.25*TENTHIRDS*GammaE/Omega2;
  cdouble FMM3 = +0.25*TENTHIRDS*GammaM/Omega2;
  cdouble FEM3 = -0.25*TENTHIRDS*GammaM*ZVAC/(II*Omega);
  cdouble FME3 = +0.25*TENTHIRDS*GammaE/(II*Omega*ZVAC);

  /*--------------------------------------------------------------*/
  /*- initialize edge-by-edge contributions to zero --------------*/
  /*--------------------------------------------------------------*/
  if (ByEdge)
   { for(int nq=0; nq<NUMPFT; nq++)
      if (ByEdge[nq])
       memset(ByEdge[nq],0,NE*sizeof(double));
   };

  /*--------------------------------------------------------------*/
  /*- loop over all pairs of edges -------------------------------*/
  /*--------------------------------------------------------------*/
  double PAbs=0.0, Fx=0.0, Fy=0.0, Fz=0.0, Taux=0.0, Tauy=0.0, Tauz=0.0;
#ifndef USE_OPENMP
  if (LogLevel>=SCUFF_VERBOSE2) Log(" no multithreading...");
#else
  int NumThreads=GetNumThreads();
  if (LogLevel>=SCUFF_VERBOSE2) Log(" using %i OpenMP threads",NumThreads);
#pragma omp parallel for schedule(dynamic,1),      \
                         num_threads(NumThreads),  \
                         reduction(+:PAbs, Fx, Fy, Fz, Taux, Tauy, Tauz)
#endif
  for(int neab=0; neab<NE*NE; neab++)
   { 
     int nea = neab/NE;
     int neb = neab%NE;
//     if (neb<nea) continue;

     /*--------------------------------------------------------------*/
     /*- Get various overlap integrals between basis function b_\alpha*/
     /*- and the fields of basis function b_\beta.                   */
     /*--------------------------------------------------------------*/
     cdouble be, bh;
     cdouble divbe[3], divbh[3], bxe[3], bxh[3];
     cdouble divbrxe[3], divbrxh[3], rxbxe[3], rxbxh[3];
     int Order=9; // increase for greater accuracy in overlap integrals 
     GetEPPFTMatrixElements(this, SurfaceIndex, SurfaceIndex, nea, neb,
                            k, &be, &bh, divbe, divbh, bxe, bxh,
                            divbrxe, divbrxh, rxbxe, rxbxh, Order);

     /*--------------------------------------------------------------*/
     /*- extract the surface-current coefficient either from the KN -*/
     /*- vector or the Sigma matrix                                 -*/
     /*--------------------------------------------------------------*/
     cdouble KK, KN, NK, NN;
     if (KNVector)
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
      { KK = SigmaMatrix->GetEntry(Offset+2*neb+0, Offset+2*nea+0);
        KN = SigmaMatrix->GetEntry(Offset+2*neb+1, Offset+2*nea+0);
        NK = SigmaMatrix->GetEntry(Offset+2*neb+0, Offset+2*nea+1);
        NN = SigmaMatrix->GetEntry(Offset+2*neb+1, Offset+2*nea+1);
      };

     /*--------------------------------------------------------------*/
     /*- get the contributions of this edge pair to all quantities   */
     /*--------------------------------------------------------------*/
     double dPAbs, dF[3], dTau[3];

     dPAbs = Sign*real( KK*PEE*be + KN*PEM*bh + NK*PME*bh + NN*PMM*be );

     for(int i=0; i<3; i++)
      {   dF[i] = Sign*real(   KK*(FEE1*divbe[i] + FEE2*bxh[i])
                             + KN*(FEM1*divbh[i] + FEM2*bxe[i])
                             + NK*(FME1*divbh[i] + FME2*bxe[i])
                             + NN*(FMM1*divbe[i] + FMM2*bxh[i])
                           );

        dTau[i] = Sign*real(   KK*(FEE1*divbrxe[i] + FEE2*rxbxh[i])
                             + KN*(FEM1*divbrxh[i] + FEM2*rxbxe[i])
                             + NK*(FME1*divbrxh[i] + FME2*rxbxe[i])
                             + NN*(FMM1*divbrxe[i] + FMM2*rxbxh[i])
                           );
      };

     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     /*--------------------------------------------------------------*/
     if (Interior)
      { double Overlaps[20];
        S->GetOverlaps(nea, neb, Overlaps);

        double Divba_n_Divbb[3], nxba_Divbb[3]; 
        double Divba_rxn_Divbb[3], rxnxba_Divbb[3];
        Divba_n_Divbb[0]   = Overlaps[3];
        Divba_n_Divbb[1]   = Overlaps[6];
        Divba_n_Divbb[2]   = Overlaps[9];
        nxba_Divbb[0]      = Overlaps[4];
        nxba_Divbb[1]      = Overlaps[7];
        nxba_Divbb[2]      = Overlaps[10];
        Divba_rxn_Divbb[0] = Overlaps[12];
        Divba_rxn_Divbb[1] = Overlaps[15];
        Divba_rxn_Divbb[2] = Overlaps[18];
        rxnxba_Divbb[0]    = Overlaps[13];
        rxnxba_Divbb[1]    = Overlaps[16];
        rxnxba_Divbb[2]    = Overlaps[19];

        for(int i=0; i<3; i++)
         { 
           dF[i]   -= real (   (FEE3*KK + FMM3*NN) * Divba_n_Divbb[i]
                             + (FEM3*KN + FME3*NK) * nxba_Divbb[i]
                           );

           dTau[i] -= real (   (FEE3*KK + FMM3*NN) * Divba_rxn_Divbb[i]
                             + (FEM3*KN + FME3*NK) * rxnxba_Divbb[i]
                           );
         };

      };

     /*--------------------------------------------------------------*/
     /*- accumulate contributions to full sums ----------------------*/
     /*--------------------------------------------------------------*/
     double Weight = 1.0; // (nea==neb) ? 1.0 : 2.0;
     PAbs += Weight*dPAbs;
     Fx   += Weight*dF[0];
     Fy   += Weight*dF[1];
     Fz   += Weight*dF[2];
     Taux += Weight*dTau[0];
     Tauy += Weight*dTau[1];
     Tauz += Weight*dTau[2];

     /*--------------------------------------------------------------*/
     /*- accumulate contributions to by-edge sums                    */
     /*--------------------------------------------------------------*/
     if (ByEdge) 
      {  
        #pragma omp critical (ByEdge)
         { if (ByEdge[0]) ByEdge[0][nea] += Weight*dPAbs;
           if (ByEdge[1]) ByEdge[1][nea] += Weight*dF[0];
           if (ByEdge[2]) ByEdge[2][nea] += Weight*dF[1];
           if (ByEdge[3]) ByEdge[3][nea] += Weight*dF[2];
           if (ByEdge[4]) ByEdge[4][nea] += Weight*dTau[0];
           if (ByEdge[5]) ByEdge[5][nea] += Weight*dTau[1];
           if (ByEdge[6]) ByEdge[6][nea] += Weight*dTau[2];
         };
      };

   }; // end of multithreaded loop

  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  /*--------------------------------------------------------------*/
  PFT[0] = PAbs;
  PFT[1] = Fx;
  PFT[2] = Fy;
  PFT[3] = Fz;
  PFT[4] = Taux;
  PFT[5] = Tauy;
  PFT[6] = Tauz;

}

} // namespace scuff