
/*! \file compute.hpp
 */
#ifndef __compute_hpp__
#define __compute_hpp__

#include "bbfmm.h"
#include "environment.hpp" 
#include "kernel_Types.hpp"
#include <omp.h>
#include <fftw3.h>    // fft transform of real input
#define FFTW_FLAG       FFTW_ESTIMATE // option for fftw plan type

template <typename T>
class H2_3D_Compute {
public:
    H2_3D_Compute(T* FMMTree, vector3 * field, vector3 *source, int Ns, int Nf, double* charge,int m,double* stress); 
    T* FMMTree;
    vector3 * field;
    vector3 * source;
    int Ns;
    int Nf;
    double* charge;
    int m;
    void FMMDistribute(nodeT **A, vector3 *field, vector3 *source, int Nf,
                       int Ns, int level);
    void FMMCompute(nodeT **A, vector3 *field, vector3 *source, double *charge,
                    double *K, double *U, double *VT, double *Tkz, int *Ktable,
                    double *Kweights, double *Cweights, double homogen,
                    doft *cutoff, int n, doft *dof,double*stress, int use_chebyshev);
    void UpwardPass(nodeT **A, vector3 *source, double *q, double *Cweights, double *Tkz, double *VT, 
        double *Kweights, doft *cutoff, int n, doft *dof,  int homogen, int curTreeLevel, int use_chebyshev);
    void FMMInteraction(nodeT **A, double *E, int *Ktable, double *U, 
            double *VT, double *Kweights, int n, doft dof,
            doft cutoff, double homogen, int curTreeLevel, int
            use_chebyshev);
    void DownwardPass(nodeT **A, vector3 *field, vector3 *source,
                      double *Cweights, double *Tkz, double *q, doft *cutoff,
                      int n, doft *dof, double *stress, int use_chebyshev);
    void EvaluateField(vector3* field, vector3* source, int Nf,int Ns, doft *dof, double* Kcell);
    void EvaluateField_self(vector3* field, int Nf, doft *dof, double* Kcell);

    void Local2Local(int n, double *r, double *F, double *Fchild, doft *dof, double *Cweights, int use_chebyshev);
    void Moment2Local(int n, double *R, double *cell_mpCoeff, double *FFCoeff,
                                    double *E, int *Ktable, doft dof, doft cutoff, int use_chebyshev);
    void Moment2Moment(int n, double *r, double *Schild, double *SS, doft *dof, double *Cweights);
    void InteractionList(nodeT **A, int levels);
    void DistributeFieldPoints(nodeT **A, vector3 *field, int *fieldlist,
                               int levels);
    void FrequencyProduct(int N, double *Afre, double *xfre, double *res);
    void FreeNode(nodeT *A);

    ~H2_3D_Compute();
};


template <typename T>
H2_3D_Compute<T>::H2_3D_Compute(T * FMMTree,vector3 * field, vector3 *source, int Ns, int Nf, double* charge, int m, double* stress) {
    if (FMMTree->computed == true) {
        if (FMMTree->tree != NULL) {
            FMMTree->FreeNode(FMMTree->tree);
            FMMTree->tree = NULL;
        }
        vector3 center;
        center.x = 0;
        center.y = 0;
        center.z = 0;
        FMMTree->NewNode(&FMMTree->tree,center,FMMTree->L,FMMTree->n);
        FMMTree->BuildFMMHierarchy(&FMMTree->tree,FMMTree->level,FMMTree->n,&FMMTree->cutoff,FMMTree->dof);

    }
    this->FMMTree   =   FMMTree;

    // shift field and source pts s.t. they center around orign
    vector3 xmin;
    xmin.x = 1e32; xmin.y = 1e32; xmin.z = 1e32;
    vector3 xmax;
    xmax.x = -1e32; xmax.y = -1e32; xmax.z = -1e32;

   
    for (int i = 0; i < Nf; ++i) {
        xmin.x = min(xmin.x, field[i].x);
        xmin.y = min(xmin.y, field[i].y);
        xmin.z = min(xmin.z, field[i].z);

        xmax.x = max(xmax.x, field[i].x);
        xmax.y = max(xmax.y, field[i].y);
        xmax.z = max(xmax.z, field[i].z);
    }

    vector3 ctr;
    ctr.x = 0.5 * (xmin.x + xmax.x);
    ctr.y = 0.5 * (xmin.y + xmax.y);
    ctr.z = 0.5 * (xmin.z + xmax.z);

    #pragma omp parallel for
    for (int i = 0; i < Nf; ++i) {
        field[i].x -= ctr.x;
        field[i].y -= ctr.y;
        field[i].z -= ctr.z;
    }

    #pragma omp parallel for
    for (int i = 0; i < Ns; ++i) {
        source[i].x -=  ctr.x;
        source[i].y -=  ctr.y;
        source[i].z -=  ctr.z;
    }


    this-> field    =   field;
    this-> source   =   source;
    this-> Ns       =   Ns;
    this-> Nf       =   Nf;
    this->charge    =   charge;
    this->m         =   m;

    FMMDistribute(&FMMTree->tree,field,source,Nf,Ns, FMMTree->level);
    FMMCompute(&FMMTree->tree,field,source,charge,FMMTree->K,FMMTree->U,FMMTree->VT,FMMTree->Tkz,FMMTree->Ktable,FMMTree->Kweights,FMMTree->Cweights,
                   FMMTree->homogen,&FMMTree->cutoff,FMMTree->n,FMMTree->dof,stress, FMMTree->use_chebyshev);
    FMMTree->computed = true;
}

/*
 * Function: FMMDistribute
 * ------------------------------------------------------------------
 * Distribute the field points and sources to the appropriate location
 * in the FMM hierarchy and sets up the interaction list.
 */
template <typename T>
void H2_3D_Compute<T>::FMMDistribute(nodeT **A, vector3 *field, vector3 *source, int Nf,int Ns, int level) {
	int i;
	int *fieldlist;
	fieldlist = (int *)malloc(Nf * sizeof(int));
	
    // Initialize the point distribution for the root node
	for (i=0;i<Nf;i++)
		fieldlist[i] = i;
	(*A)->Nf = Nf;
	(*A)->Ns = Ns;
	
    // Distribute all of the sources and field points to the appropriate cell
	DistributeFieldPoints(A,field,fieldlist,level); 

	
    // Construct the interaction list for the entire octree
	(*A)->neighbors[0] = *A;
    (*A)->max_neighbor_Ns = max((*A)->max_neighbor_Ns, (*A)->Ns);
	(*A)->ineigh = 1;
	InteractionList(A,level);  // build the interaction that need to be computed
    
	free(fieldlist);
    fieldlist = NULL;
}

template <typename T>
void H2_3D_Compute<T>::FMMCompute(nodeT **A, vector3 *field, vector3 *source, double *charge, double *K, double *U, double *VT, double *Tkz, int *Ktable, double *Kweights, double *Cweights, double homogen, doft *cutoff, int n, doft *dof, double *stress, int use_chebyshev) {
	double t0, t1;
    t0 = omp_get_wtime(); 
    UpwardPass(A,source,charge,Cweights,Tkz,VT,Kweights,cutoff,n,dof, homogen, 0, use_chebyshev);
    t1 = omp_get_wtime(); 
    double t_upward = t1 - t0;
    
    t0 = omp_get_wtime(); 
	FMMInteraction(A,K,Ktable,U,VT,Kweights,n,*dof,*cutoff,homogen,0, use_chebyshev);
    t1 = omp_get_wtime(); 
    double t_interaction = t1 - t0;

    t0 = omp_get_wtime(); 
    DownwardPass(A,field,source,Cweights,Tkz,charge,cutoff,n,dof,stress, use_chebyshev);
    t1 = omp_get_wtime(); 
    double t_downward = t1 - t0;

    cout << "upward time = " << t_upward << " interaction time = "  << t_interaction << " downward time = " << t_downward << endl;
}

template <typename T>
void H2_3D_Compute<T>::UpwardPass(nodeT **A, vector3 *source, double *q, double *Cweights, double *Tkz, double *VT, double *Kweights, doft *cutoff, int n, doft *dof, int homogen, int curTreeLevel, int use_chebyshev) {
    
    int Ns = (*A)->Ns;                  // Number of source points
    int i;
	int n3 = n*n*n;
	
	/* First gather the sources for all children cells and
	 * then gather for the parent cell - otherwise map all
	 * sources to Chebyshev nodes */
	if ((*A)->leaves[0] != NULL) {  // Going up the tree M2M
        
        // Allocate memory for the source values and initialize
		(*A)->sourceval = (double *)malloc(n3*sizeof(double));
		for (int l=0;l<n3;l++) {
            (*A)->sourceval[l] = 0;
        }
		
        // Determine which children cells contain sources
        #pragma omp parallel for private(i)
		for (i=0;i<8;i++) {
		    if ((*A)->leaves[i]->Ns != 0) {
                UpwardPass(&((*A)->leaves[i]),source,q,Cweights,Tkz,VT,
						   Kweights,cutoff,n,dof,homogen,curTreeLevel+1, use_chebyshev);
                double *Schild;
                Schild = (*A)->leaves[i]->sourceval;
                
                // Manually set the vector from child to parent
                int  idx[3];
                double r[3];
                idx[2] = i%2, idx[1] = i/2%2, idx[0] = i/4%2;
                for (int l=0; l<3; l++)
                    if (idx[l] == 0)
                        r[l] = -1;
                    else
                        r[l] =  1;
                
		        double SS[n3];
                Moment2Moment(n, r, Schild, SS, dof, Cweights);
                
                if (!use_chebyshev) {
                    if (Schild != NULL) {
                        free(Schild), Schild = NULL;
                    }
                    (*A)->leaves[i]->sourceval = NULL;

                }
                
                for (int l=0; l<n3; l++)
                    (*A)->sourceval[l] += SS[l];
            }
        }
        
	}
    
    
    
    else {
        
        int j, k, l1, l2, l3, *sourcelist = (*A)->sourcelist;
        double sum, ihalfL = 2./(*A)->length;
        vector3 scenter = (*A)->center;
        vector3* Ss = new vector3[n*Ns];
        vector3* sourcet = new vector3[Ns];
        // Map all of the source points to the box ([-1 1])^3
        for (j=0;j<Ns;j++) {
            k = sourcelist[j];
            sourcet[j].x = ihalfL*(source[k].x - scenter.x);
            sourcet[j].y = ihalfL*(source[k].y - scenter.y);
            sourcet[j].z = ihalfL*(source[k].z - scenter.z);
        }
        // Compute Ss, the mapping function for the sources
        FMMTree->ComputeSn(sourcet,Tkz,n,Ns,Ss, use_chebyshev);
        
        // Compute the source values
        (*A)->sourceval = (double *)malloc(n3*sizeof(double));
        double *S = (*A)->sourceval;
        int l = 0;
        for (l1=0;l1<n;l1++) {
            for (l2=0;l2<n;l2++) {
                for (l3=0;l3<n;l3++) {
                    sum = 0;
                    for (j=0;j<Ns;j++) {
                        k = sourcelist[j];
                        sum += q[k]*Ss[l1*Ns+j].x*Ss[l2*Ns+j].y*Ss[l3*Ns+j].z;
                    }
                    S[l] = sum;
                    l++;
                }
            }
        }

        if (Ss != NULL){
            delete [] Ss;
            Ss = NULL;
        }
        if (sourcet != NULL){
            delete [] sourcet;
            sourcet = NULL;
        }
	}
    
    
    
    if (!use_chebyshev) {
        
        // pad the source value
        int padSize = (int)round(pow(2*n-1, 3));
        int halfSize = padSize/2;
        int l3Size = n, l1Size = (int)round(pow(l3Size, 2));
        int l2Pad = 2*n-1, l1Pad = (int)round(pow(l2Pad, 2));
        int shift;
        int l1, l2, l3, n3 = n*n*n;
        
        double *x = (*A)->sourceval;
        double *padx    = fftw_alloc_real( padSize  );
        (*A)->sourcefre = fftw_alloc_real( padSize  );
        for (i = 0; i < padSize; i++)
            padx[i] = 0;

        for (i=0; i<n3; i++) {
            l3 = i % l3Size;
            l1 = i / l1Size;
            l2 = i / l3Size % l3Size;
            shift = halfSize+l1*l1Pad+l2*l2Pad+l3;
            padx[shift] = x[i];
        }
        
        fftw_plan p;
        #pragma omp critical (make_plan)
        {
            p = fftw_plan_r2r_1d(padSize, padx, (*A)->sourcefre, FFTW_R2HC, FFTW_FLAG);
        }

        fftw_execute(p);
        x=NULL;
        fftw_free(padx), padx=NULL;
        fftw_destroy_plan(p);

    } else {
        int n3    = n*n*n;
        int dofn3 = n3;
        double *x = (*A)->sourceval;
        double Sw[dofn3];
        
        for (int l=0;l<n3;l++) {
            Sw[l] = x[l] * Kweights[l];
        }

        int incr = 1;
        double a = 1, beta = 0;
        char trans = 'n';

        int Vsize  = dofn3 * cutoff->s;
        int lvl_shift;
        lvl_shift = (curTreeLevel>=2) ? curTreeLevel-2: 0;
        lvl_shift *= !homogen;
        
        (*A)->proxysval = (double*) malloc(cutoff->s * sizeof(double));
        dgemv_(&trans, &cutoff->s, &dofn3, &a, VT + Vsize*lvl_shift, &cutoff->s, Sw, &incr,
           &beta, (*A)->proxysval, &incr);
      }
}


template <typename T>
void H2_3D_Compute<T>::FMMInteraction(nodeT **A, double *E, int *Ktable, double *U, 
            double *VT, double *Kweights, int n, doft dof,
            doft cutoff, double homogen, int curTreeLevel, int
            use_chebyshev) {
  
  int Ksize;
  int lvl_shift = (curTreeLevel>=2) ? curTreeLevel-2: 0;
  lvl_shift *= !homogen;

  if (use_chebyshev)
    Ksize = 316 * cutoff.f * cutoff.s;
  else
    Ksize = 316*(2*n-1)*(2*n-1)*(2*n-1);

  int i, j;
  int n3       = n*n*n; 
  int dofn3_f  = n3;
  int cutoff_f = cutoff.f;
  int Usize    = dofn3_f * cutoff.f;

  double L     = (*A)->length;    // Length of cell
  double iL    = 1.0/L;           // Inverse-length  
  double scale = pow(iL,homogen); // Scaling factor for M2L
  
  assert((*A)->Nf > 0); /* Cell cannot be empty. */
     
  double *productfre = NULL; // for uniform grids
  int matSizeDof;
  if (use_chebyshev)
    matSizeDof = cutoff_f;
  else {
    matSizeDof = (int)round(pow(2*n-1,3));
    productfre = fftw_alloc_real( matSizeDof );
    for (i=0;i<matSizeDof;i++)
        productfre[i] = 0;
  } 
    

  double *FFCoeff = (double*) malloc(matSizeDof *sizeof(double));
    

  (*A)->fieldval = (double *)calloc(dofn3_f, sizeof(double)); // initialize to zero
  
     
  vector3 fcenter = (*A)->center;   // Obtain the center of the field cell
  int ninter   = (*A)->iinter;   // Obtain the number of interaction cells
  double *F    = (*A)->fieldval; // Initialize pointer to the field values of A
  double *Pf   = (double*) calloc(cutoff_f, sizeof(double)); // Initialize to zero
    
  // Compute the field values due to all members of the interaction list
  for (i=0;i<ninter;i++) {  
        
    nodeT *B = (*A)->interaction[i]; 

    // Obtain the center of the source cell
    vector3 scenter = B->center;
        
    // Note this is the normalized vector
    double R[3] = { iL*(scenter.x-fcenter.x),
            iL*(scenter.y-fcenter.y),
            iL*(scenter.z-fcenter.z) };

    // initialize to zeros
    for (j=0; j<matSizeDof; j++) {
      FFCoeff[j] = 0;
    }

    if (use_chebyshev) {
       
      Moment2Local(n, R, B->proxysval, FFCoeff, E + Ksize*lvl_shift, Ktable, dof, cutoff, use_chebyshev);
      for (j=0; j<cutoff_f; j++)
        Pf[j] += FFCoeff[j];
      }
        
    else{
      Moment2Local(n, R, B->sourcefre, FFCoeff, E + Ksize*lvl_shift, Ktable, dof, cutoff, use_chebyshev);
      for (j=0; j<matSizeDof; j++)
        productfre[j] += FFCoeff[j];
      }
  } // end for ninter

  free(FFCoeff), FFCoeff=NULL;
     
  if (use_chebyshev) {

    int incr     =  1;
    char trans   = 'n';
    double alpha =  0;
    double F_m2l[n*n*n];
    dgemv_(&trans, &dofn3_f, &cutoff_f, &scale, U + Usize*lvl_shift,
       &dofn3_f, Pf, &incr, &alpha, F_m2l, &incr);

    for (int i = 0; i < n3; i++)
        F[i] +=  F_m2l[i] * Kweights[i];

  } else{ // uniform grids
        
    int padSize = round(pow(2*n-1, 3));
    int l3Size = n, l1Size = (int)round(pow(l3Size, 2));
    int l2Pad = 2*n-1, l1Pad = (int)round(pow(l2Pad, 2));
    int shift;
    int l1, l2, l3;
     
    double *res = fftw_alloc_real( padSize);
    fftw_plan p;
    #pragma omp critical (make_plan)
         p = fftw_plan_r2r_1d(padSize, productfre, res, FFTW_HC2R, FFTW_FLAG);
    fftw_execute(p);

    fftw_destroy_plan(p);

     
    fftw_free(productfre), productfre=NULL;
        
    for (i=0; i<n3; i++) {
      l3 = i % l3Size;
      l1 = i / l1Size;
      l2 = i / l3Size % l3Size;
      shift = l1*l1Pad+l2*l2Pad+l3;
      Pf[i] = res[shift]/padSize;
    }

    fftw_free(res), res = NULL;
     
    // F += FFT result
    for (j=0; j<cutoff_f; j++)
      F[j] += scale *Pf[j]; 
  }
     
  free(Pf);

  
  // Recursively compute the kernel interactions for all children cells
  if ((*A)->leaves[0] != NULL) {
  #pragma omp parallel for private(i)
    for (i=0;i<8;i++) {
      if ((*A)->leaves[i]->Nf != 0) 
        FMMInteraction(&((*A)->leaves[i]), E, Ktable, U, VT,Kweights, n, dof, cutoff, homogen,
                        curTreeLevel+1, use_chebyshev);
    }
  }
}

template <typename T>
void H2_3D_Compute<T>::DownwardPass(nodeT **A, vector3 *field, vector3 *source,
                  double *Cweights, double *Tkz, double *q, doft *cutoff,
                  int n, doft *dof, double *phi, int use_chebyshev) {
    /* Add the contributions from the parent cell to each child cell -
	 * otherwise compute all direct interactions and then interpolate
	 * to the field points */
	if ((*A)->leaves[0] != NULL) {
        
	    double *F = (*A)->fieldval;              // Field values for cell
        
	    // Determine which children cells contain field points
        int i, l;
        #pragma omp parallel for private(i)
	    for (i=0;i<8;i++) {
            if ((*A)->leaves[i]->Nf != 0) {
                
                double *Fchild = (*A)->leaves[i]->fieldval;
                
                // Manually set the vector from child to parent
                int  idx[3];
                double r[3];
                idx[2] = i%2, idx[1] = i/2%2, idx[0] = i/4%2;
                for (l=0; l<3; l++)
                    if (idx[l] == 0)
                        r[l] = -1;
                    else
                        r[l] =  1;
                
                Local2Local(n, r, F, Fchild, dof, Cweights, use_chebyshev);
                DownwardPass(&((*A)->leaves[i]),field,source,Cweights,Tkz,q,
							 cutoff,n,dof,phi, use_chebyshev);
            }
		}
		
	} else {
        int Nf = (*A)->Nf, i, j, m, *fieldlist = (*A)->fieldlist;
        int l, l1, l2, l3;
        double sum;        
        double *F = (*A)->fieldval;
        
        double ihalfL = 2./(*A)->length, tmp1, tmp2;
        vector3 fieldt[Nf], Sf[n*Nf], fcenter = (*A)->center;

        // Obtain the positions of the field points
        FMMTree->get_Location(*A, field); 
        FMMTree->get_Charge(*A, q, this->Ns, this->m);

        // Map all field points to the box ([-1 1])^3
        for (i=0;i<Nf;i++) {
            fieldt[i].x = ihalfL*((*A)->location[i].x - fcenter.x);
            fieldt[i].y = ihalfL*((*A)->location[i].y - fcenter.y);
            fieldt[i].z = ihalfL*((*A)->location[i].z - fcenter.z);
        }
        
        // Compute Sf, the mapping function for the field points
        FMMTree->ComputeSn(fieldt,Tkz,n,Nf,Sf, use_chebyshev);
        
        // Compute the values at the field points
        for (i=0;i<Nf;i++) {
            // Due to far field interactions
            sum = 0;
            l = 0;
            for (l1=0;l1<n;l1++) {
                tmp1 = Sf[l1*Nf+i].x;
                for (l2=0;l2<n;l2++) {
                    tmp2 = tmp1*Sf[l2*Nf+i].y;
                    for (l3=0;l3<n;l3++) {
                        sum += F[l]*tmp2*Sf[l3*Nf+i].z;
                        l += 1;
                    }
                }
            }
            for (int l = 0; l < this->m; l++)
                (*A)->nodePhi[l*Nf+i] += sum;
        }

        
        // Due to near field interactions
        double *Kcell;
        Kcell = (double*) malloc(Nf * (*A)->max_neighbor_Ns * sizeof(double));
        double alpha = 1, beta = 1;

        for (m=0;m<27;m++) {
            nodeT *B = (*A)->neighbors[m];
            if (m != 13 && B && !(*A)->neighborComputed[m]) {
                        
                int Ns = B->Ns;
                FMMTree->get_Location(B, source);
                FMMTree->get_Charge(B, q, this->Ns, this->m);
                
                // compute submatrix
                EvaluateField((*A)->location,B->location, Nf,Ns,dof, Kcell);
                char transa = 'n';
                char transb = 'n';

                // apply matrix to vector
                dgemm_(&transa, &transb, &Nf, &this->m, &Ns, &alpha, Kcell, &Nf, B->charge, &Ns, &beta, (*A)->nodePhi, &Nf);

                // Do the symmtric part
                transa = 't';
                dgemm_(&transa, &transb, &Ns, &this->m, &Nf, &alpha, Kcell, &Nf, (*A)->charge, &Nf, &beta, B->nodePhi, &Ns);
                B->neighborComputed[26-m] = true; 

            }
        }


        // self interaction
        EvaluateField_self((*A)->location,  Nf, dof, Kcell);
        char transa = 'n';
        char transb = 'n';
        dgemm_(&transa, &transb, &Nf, &this->m, &Nf, &alpha, Kcell, &Nf, (*A)->charge, &Nf, &beta, (*A)->nodePhi, &Nf);


        // transfer potential to the tree
        for (int l=0;l<this->m;l++)
            for (i=0;i<Nf;i++) { 
                j = fieldlist[i];
                phi[l*this->Nf+j] += (*A)->nodePhi[l*Nf+i];
            }

        free(Kcell);
        Kcell = NULL;
	}
}

/*
 * Function: EvaluateField
 * -------------------------------------------------------------------
 * Evaluates the field due to interactions between a pair of cells.
 * P2P kernel.
 */
template <typename T>
void H2_3D_Compute<T>::EvaluateField(vector3* field, vector3* source, int Nf,int Ns, doft *dof, double* Kcell) {
	int i, j;
	for (j=0;j<Ns;j++) {
        vector3 cur_source = source[j];
		for (i=0;i<Nf;i++) {
            FMMTree->EvaluateKernel(field[i],cur_source,&Kcell[i + Nf * j],dof);
		}
	}
}

template <typename T>
void H2_3D_Compute<T>::EvaluateField_self(vector3* field, int Nf, doft *dof, double* Kcell) {
    int i, j;
    for (j=0;j<Nf;j++) {
        vector3 cur_field = field[j];
        for (i=0;i<=j;i++) {
            FMMTree->EvaluateKernel(field[i],cur_field,&Kcell[i + Nf * j],dof);
            Kcell[j + Nf * i] = Kcell[i + Nf * j];
        }
    }
}


/* Function: L2L
 * Input:
 *       n: Number of Chebyshev nodes
 *       r: (r[0], r[1], r[2]) is the vector from the child box to the parent box
 *       F: Field values of the parent
 *
 * Output:
 *  Fchild: Field values of the child
 *
 */
template <typename T>
void H2_3D_Compute<T>::Local2Local(int n, double *r, double *F, double *Fchild, doft *dof, double *Cweights, int use_chebyshev){
    
    int l, l1, l2, l3, count1, count2, count3, wstart;
	
	int n2 = n*n;                       // n2 = n^2
	int n3 = n*n*n;                     // n3 = n^3
	
	double Fx[n3], Fy[n3];
    
    
    // Recover the index of the child box
    int idx[3] = {0};
    if (  r[0] > 0)
        idx[0] = 1;
    if (  r[1] > 0)
        idx[1] = 1;
    if (  r[2] > 0)
        idx[2] = 1;
    
	// Interpolate the parent field along the x-component
    l = 0;
	if (idx[0] == 0)
	    wstart = 0;
	else
	    wstart = n2;
    
    for (l1=0;l1<n;l1++) {
		count2 = wstart + l1;
		for (l2=0;l2<n;l2++) {
            for (l3=0;l3<n;l3++) {
	            count3 = l2*n + l3;
                Fx[l]  = ddot_(&n,&F[count3],&n2,&Cweights[count2],&n);
                l++;
		    }
		}
    }
    
    // Interpolate the parent field along the y-component
    l = 0;
    if (idx[1] == 0)
        wstart = 0;
    else
        wstart = n2;
    
    for (l1=0;l1<n;l1++) {
        for (l2=0;l2<n;l2++) {
            count2 = wstart + l2;
            for (l3=0;l3<n;l3++) {
                Fy[l] = ddot_(&n,&Fx[l1*n2 + l3],&n,&Cweights[count2],&n);
                l++;
            }
        }
    }
    
    /* Interpolate the parent field along the z-component and add
     to child field */
    l = 0;
    if (idx[2] == 0)
        wstart = 0;
    else
        wstart = n2;
    
    for (l1=0;l1<n2;l1++) {
        count1 = l1*n;
        for (l3=0;l3<n;l3++) {
            count3 = count1;
			Fchild[l] += ddot_(&n,&Fy[count3],&(dof->f), &Cweights[wstart + l3],&n);
			l++;
        }
    }
    
}

template <typename T>
void H2_3D_Compute<T>::Moment2Local(int n, double *R, double *cell_mpCoeff, double *FFCoeff, 
          double *E, int *Ktable, doft dof, doft cutoff, 
          int use_chebyshev) {
     

  int cutoff_f = cutoff.f, cutoff_s = cutoff.s;
  int cutoff2  = cutoff_f * cutoff_s;


  int incr = 1;
  double alpha = 1, beta = 0;
  char trans = 'n';
    
   
  // Compute the field proxy values: Matrix Vector multiplication in
  // BLAS: dgemv - 3b
  // cutoff: cutoff on the number of SVD values, 
  // Ecell : M2L operator - E[count] : precalculated, P input, M
  // expansion Output : Pf L expansion

  // Determine the corresponding index in the lookup table
  int k1 = (int)round(R[0]) + 3;
  int k2 = (int)round(R[1]) + 3;
  int k3 = (int)round(R[2]) + 3;


  // note that k1, k2 and k3 are integers from 0 to 6.
  int ninteract = Ktable[49*k1+7*k2+k3];
  assert(ninteract != -1);
  int count; 
     
  if (use_chebyshev) {
    count = ninteract*cutoff2;
    dgemv_(&trans,&cutoff_f,&cutoff_s,&alpha,E+count,&cutoff_f,cell_mpCoeff,&incr,&beta,FFCoeff,&incr); // 3b  Ecell is the kernel
          
  } else {
    // entry-wise product of cell_mpCoeff
    int N = (int)round(pow(2*n-1, 3));
    count = ninteract*(2*n-1)*(2*n-1)*(2*n-1);
    FrequencyProduct(N, E+count, &cell_mpCoeff[0], &FFCoeff[0]);
     
  }
}

/*
 * Compute the entry-wise product of two frequencies from rfftw
 * Note 'res' has been initialized
 */
template <typename T>
void H2_3D_Compute<T>::FrequencyProduct(int N, double *Afre, double *xfre, double *res) {
    
    int i;
    res[0] += Afre[0]*xfre[0];
    for (i=1; i<N; i++) {
        if (i<(N+1)/2)
            res[i] += Afre[i]*xfre[i] + Afre[N-i]*xfre[N-i];
        else
            res[i] += Afre[N-i]*xfre[i] - Afre[i]*xfre[N-i];
    }
    if (N%2 == 0)
        res[N/2] += Afre[N/2]*xfre[N/2];

}


/* Function: M2M
 * Input:
 *       n: Number of Chebyshev nodes
 *       r: (r[0], r[1], r[2]) is the vector from the child box to the parent box
 *  Schild: Source values of the child
 *
 * Output:
 *  SS: Source values of the parent
 *
 */
template <typename T>
void H2_3D_Compute<T>::Moment2Moment(int n, double *r, double *Schild, double *SS, doft *dof, double *Cweights) {
    
	int l, l1, l2, l3;
	int count1, count2, count3, wstart;
	
	int incr = 1;
	int n2 = n*n;                       // n2 = n^2
	int n3 = n*n*n;                     // n3 = n^3
		
	double Sy[n3], Sz[n3];
    
    
    // Recover the index of the child box
    int idx[3] = {0};
    if (  r[0] > 0)
        idx[0] = 1;
    if (  r[1] > 0)
        idx[1] = 1;
    if (  r[2] > 0)
        idx[2] = 1;
    
    // Gather the children source along the z-component
    if (idx[2] == 0)
	    wstart =  0;
    else
	    wstart =  n2;
    
    l = 0;
    for (l1=0;l1<n2;l1++) {
	    count1 = l1*n;
	    for (l3=0;l3<n;l3++) {
            count3 = count1;
            Sz[l]  = ddot_(&n,&Schild[count3],&(dof->s),&Cweights[wstart + l3*n],&incr);
            l++;
	    }
    }
    
    // Gather the children sources along the y-component
    if (idx[1] == 0)
        wstart =  0;
    else
        wstart =  n2;
    
    l = 0;
    for (l1=0;l1<n;l1++) {
        for (l2=0;l2<n;l2++) {
            count2 = wstart + l2*n;
            for (l3=0;l3<n;l3++) {
                Sy[l]  = ddot_(&n,&Sz[l1*n2 + l3],&n,&Cweights[count2],&incr);
                l++;
            }
        }
    }
    
    /* Gather the children sources along the x-component and determine
     the parent sources */
    l = 0;
    if (idx[0] == 0)
        wstart =  0;
    else
        wstart =  n2;
    
    for (l1=0;l1<n;l1++) {
        count2 = wstart + l1*n;
        for (l2=0;l2<n;l2++) {
            for (l3=0;l3<n;l3++) {
                SS[l] = ddot_(&n,&Sy[l2*n + l3],&n2,&Cweights[count2],&incr);
                l++;
            }
        }
    }
}

/*
 * Function: InteractionList
 * -------------------------------------------------------------------
 * For each node at each level of the octree, the interaction list
 * is determined.
 */
template <typename T>
void H2_3D_Compute<T>::InteractionList(nodeT **A, int levels) {
	int i, j, k, iinter;
	nodeT *B, *C;
	vector3 center1, center2, diff;
	double cutoff;
	
	assert((*A)->Nf > 0);
	if (levels > 0) {
		
		/* Sets the cutoff between near and far to be L (this is equivalent
		 * to a one cell buffer) */
		cutoff = (*A)->length / 2;
		/*
		 * Finds all neighbors that are too close for the far field
		 * approximation and stores them in the neighbors array -
		 * Also finds the neighboring cells that are sufficiently
		 * far away and stores them in interaction array
		 */

		for (i=0;i<27;i++) {
            if ((*A)->neighbors[i] == NULL)
                continue;
			B = (*A)->neighbors[i]; // All the neighbors of A
			for (j=0;j<8;j++) {
				if (B->leaves[j]->Ns != 0) { // All the children j of the neighbor cluster A
					/* Skip empty neighbor clusters. */
					
					center1 = B->leaves[j]->center;
					
					for (k=0;k<8;k++) {
						C = (*A)->leaves[k]; // Child k of cluster A
						
						if (C->Nf != 0) {
							/* Skip empty children clusters. */
							
							iinter = C->iinter;
							center2 = C->center;
							
							diff.x = center1.x - center2.x;
							diff.y = center1.y - center2.y;
							diff.z = center1.z - center2.z;
							
                            int x = round(diff.x / cutoff);
                            int y = round(diff.y / cutoff);
                            int z = round(diff.z / cutoff);

                            bool is_well_seperated = abs(x) > 1 || abs(y) > 1 || abs(z) > 1;


							if (!is_well_seperated) {
                                int index = 9*(x+1) + 3*(y+1) + (z+1);
								C->neighbors[index] = B->leaves[j]; // This is a neighbor
                                C->max_neighbor_Ns = max(C->max_neighbor_Ns, B->leaves[j]->Ns);
								C->ineigh++;
							} else {
								C->interaction[iinter] = B->leaves[j]; // This is a well-separated cluster
								C->iinter++;
							}
						}
						
					}
				}
			}
		}
		
        // recursively build the interaction lists
		if ((*A)->leaves[0] != NULL) {
            #pragma omp parallel for private(k)
			for (k=0;k<8;k++) {
				if ((*A)->leaves[k]->Nf != 0) { /* Skip empty clusters */
					InteractionList(&((*A)->leaves[k]),levels-1);
				}
			}
		}
	}
}


/*
 * Function: DistributeFieldPoints
 * -------------------------------------------------------------------
 * Distributes all of the field points to the appropriate cell at the
 * bottom of the octree.
 */
template <typename T>
void H2_3D_Compute<T>::DistributeFieldPoints(nodeT **A, vector3 *field, int *fieldlist,
                                             int levels) {
	int i, j, k, m, z;
	int Nf = (*A)->Nf;
	vector3 point, center;
	int *fieldcell[8], *F, *S;
	for (z = 0; z < 8; z++)
		fieldcell[z] = (int *)malloc(Nf * sizeof(int));
	
	if (levels > 0) {
        // Obtain the center of the cell
		center = (*A)->center;
		
        // Distribute the field points
		if (Nf != 0) {
            // Determine which child cell each field and source point belongs to
			for (i=0;i<Nf;i++) {
				k = fieldlist[i];
				point = field[k];
				
                // Determine which cell the point is in
				if (point.x < center.x) {
					if (point.y < center.y) {
						if (point.z < center.z)
							j = 0;
						else
							j = 1;
					} else {
						if (point.z < center.z)
							j = 2;
						else
							j = 3;
					}
				} else {
					if (point.y < center.y) {
						if (point.z < center.z)
							j = 4;
						else
							j = 5;
					} else {
						if (point.z < center.z)
							j = 6;
						else
							j = 7;
					}
				}
				
                // Add the field point to the list for child cell j
				m = (*A)->leaves[j]->Nf;
				fieldcell[j][m] = k;
				(*A)->leaves[j]->Nf++;
                (*A)->leaves[j]->Ns++;
			}
			
            // Recursively distribute the points
            #pragma omp parallel for private(j)
			for (j=0;j<8;j++) {
				DistributeFieldPoints(&((*A)->leaves[j]),field,fieldcell[j],levels-1);
            
			}
		}
	} else if (levels == 0) {
		Nf = (*A)->Nf;
		(*A)->fieldlist = (int *)malloc(Nf*sizeof(int));
        (*A)->sourcelist = (int *)malloc(Nf*sizeof(int));
        (*A)->nodePhi = (double*)calloc(Nf*this->m,sizeof(double)); 
		F = (*A)->fieldlist;
        S = (*A)->sourcelist;
		for (i=0;i<Nf;i++) {
			F[i] = fieldlist[i];
            S[i] = fieldlist[i];
        }
	}
	for (z = 0; z < 8; z++) {
		free(fieldcell[z]);
        fieldcell[z] = NULL;
    }
}



template <typename T>
H2_3D_Compute<T>::~H2_3D_Compute() {
    if (FMMTree->tree != NULL) {
        FMMTree->FreeNode(FMMTree->tree);
        FMMTree->tree = NULL;
    }
    if (FMMTree->Kweights!= NULL) {
        free(FMMTree->Kweights);
        FMMTree->Kweights = NULL;
    }
    if (FMMTree->Cweights!= NULL) {
        free(FMMTree->Cweights);
        FMMTree->Cweights = NULL;
    }
    if (FMMTree->Tkz!= NULL) {
        free(FMMTree->Tkz);
        FMMTree->Tkz = NULL;
    }
    if (FMMTree->K!= NULL) {
        free(FMMTree->K);
        FMMTree->K = NULL;
    }
    if (FMMTree->U!= NULL) {
        free(FMMTree->U);
        FMMTree->U = NULL;
    }
    if (FMMTree->VT!= NULL) {
        free(FMMTree->VT);
        FMMTree->VT = NULL;
    }
}



#endif //(__compute_hpp__)
