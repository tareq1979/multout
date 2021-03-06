/* MULTOUT.C (double prec) */
/* release version */
/* copyright 1992,1993,1994,1995,1996 by David L. Woodruff and David M. Rocke */
/* AS IS */
/* THERE IS NO WARRANTY OF ANY KIND */
/* This software may be freely distributed for non-commercial use. */

/* Compilation Notes: */
/* 1. This is a single file C program using only standard libraries */
/* 2. You will need the -lm linker switch on most Unix and related systems */
/* 3. You will almost surely need to use a large memory model on a 16 bit PC */

/* Other Notes: */
/* 0. This program assumes that the data are in general position */
/* 1. see the function Info_Exit() for usage instructions */
/* 2. see the paper "Idenfication of Outliers in Multivariate Data" */
/*    by Rocke and Woodruff for detailed explanations */
/* 3. the program gencross.c can generate test data */
/* 4. The optional parms file is for experts only; see Load_Parms() */
/* 5. If the outlier detection phase is too slow, */
/*    you can increase the simulation tolerance (the last parm) */
/* 6. Singularities in the data are not handled very well (see 0.)*/
/* 7. send questions to dlwoodruff@ucdavis.edu; (916)752-0515 */

#define BANNER "multout version 3.03.1\nCopyright 1992,93,94,95,96 by David L. Woodruff and David M. Rocke\n"

/* Release Notes: */
/* 
Differences between 2.00.04 and 3.00
1. The program outputs an M-Estimate of location and shape before beginning
   the simulations for outlier cutoff and the file is closed. Hence, the
   program can be killed during the simulations and useful output will
   be present.
2. The program outputs the final estimate of location and shape used to 
   generate the robust distances.
*/
/*3.01 
1. One error message preceded by *error* 
2. exit(1) if singularity is encountered 
3. iterations is now an optional command line arg
*/
/* 3.02
1. changes in singularity messages
*/
/* 3.03 changes in comments */

#include <stdio.h>       /* magical incantations */
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <assert.h>
#include <memory.h>
#include <search.h>
#include <float.h>

/*------------------------------------------------------------------*/
void Info_Exit()
/* give the user some hints, then exit */
{
  printf("Usage: multout infile outfile [iterations [parmsfile]]\n\n");
  printf(" where infile contains: p n\n"); 
  printf("                        data record one (p data elements)\n");
  printf("                        data record two\n");
  printf("                            . . . \n");
  printf("and iterations is an integer controlling the time spent.\n");
  printf("For light contamination use 1 iteration; for heavy,");
  printf(" try (n)(p) or more.\n");
  printf("If it is not specified, (n)(p) is used.\n");
  printf("\nExample: multout suspect.dat suspect.out 10000\n");
  exit(1);
}
/*------------------------------------------------------------------*/

/* parms */
int Lambda;                             /* target partition size */
float Cut1, Cut2;                       /* for outlier ID */
float SimTol;

/* define the algorithm begin used */
#define BaseSubSampleSize (int)(XCnt/2)       /* whence we "lap" */
/* define the means of evaluating a partition as SCRIT or MVECRIT */

/*#include "std.c"*/

/* to allow compilation of the same code on the DECstations */
/* set host ULTRIX or PC */
#define PC 1
#define ULTRIX 2

#define HOST ULTRIX
/* version 6: Ultrix really means OSF/1 */
#if HOST == ULTRIX
#   define far                          /* no need for memory kludges */
#   define _fmalloc malloc              /* ditto */
#   define _ffree free
#endif

#define True -1 			/* for use in */
#define False 0 			/* setting vars only  */

#define SEEDFILE "SEED.DAT"		/* to allow variations in seed */

#define ALLCHK(x) if (x == NULL) {printf ("allocation error\n"); assert(x);}
#define NTOL (double)0.00001            /* newton tolerance */
#define WTOL (double)0.001              /* weight convergence tolerance */
/*#define LTOL 1./(double)(XCnt*VectLen)*/  /* lambda tolerance */
#define LTOL 1./(double)VectLen            /* lambda tolerance */ /*12/21/92*/
#define NTIMEOUT 100                    /* max newton iters */
#define GMULT 1.2                       /* controls ghost precision */
#define dabs(x) (((x) < 0.) ? -(x) : (x))

/* shared parms */
int Trace;			  /* controls debug trace dump info */
long ItersAllowed;
/* run time parm (for all but simulated annealing) */
float Cut1, Cut2;                       /* for outlier ID */

/* global data */
int VectLen;			  /* number of attributes */
int XCnt;			  /* number of observations */
double *X;			  /* The observations */
#define XRow(i) (X+(i-1)*VectLen)
#define Xof(i,j) *(X+(i-1)*VectLen+j-1) /* X[i,j] */
double InstanceID = 0.;   /* to hash an instance ID using Diag of data */

/* the J set is defined by non-zero JBit indicators */
int *JBits;			  /* indicators for J set (squander bits) */
int *BestJBits;			  /* best XJ seen so far */
int *OnesList, *ZerosList;	  /* indexes of bits set and zero */
double *XBarJ;			  /* x bar values for the J set */
#define XBarJof(i) *(XBarJ+i-1)   /* XBarJ[i] */
double *XJ;			  /* data matrix corresp. to J set */
#define XJof(i,j) *(XJ+(i-1)*VectLen+j-1) /* XJ[i,j] */
double *ZJ;			  /* e concat XJ */
#define ZJof(i,j) *(ZJ+(i-1)*(VectLen+1)+j-1) /* ZJ[i,j] */

/* covariance matrix, rectangular space to facilitate GJ inversion */
/* the inverse is going to be in the right half of the C matrix, so */
/* space is not allocated, just a macro to get at the data */
double *C;			  /* COLUMN MAJOR covariance matrix */
#define Cof(i,j) (*(C+(i-1)+(j-1)*VectLen))  /* C[i,j] */
double *A;			  /* ZZt */
#define Aof(i,j) (*(A+(i-1)+(j-1)*(VectLen+1)))  /* A[i,j] */
double Determinant;    /* the product of the pivots, in this case */
#define C_1of(i,j) *(C+VectLen*VectLen+(i-1)+(j-1)*VectLen)	/* C-1[i,j] */
#define A_1of(i,j) *(A+(VectLen+1)*(VectLen+1)+(i-1)+(j-1)*(VectLen+1))
long SingularCnt=0;		  /* count the number of Det=0 seen */

/* many of these vectors are global only to make dynamic allocation easy */
/* the so-called sqresiduals vector is the squared mahalanobis distances */
double *SqResiduals;   /* squared distances, zero based */
double *kSqSpace;      /* for sorting in compute_k */
double *dTilde;        /* modified distances (used in s estimation) */
double *wVector;       /* weights vector stored to save comp. & test converg.*/
double *OldwVector;    /* last iteration's wieght vector */
double Sumw, Sumv;     /* save some time in M iterations */
double mJ2;	       /* the left hand side of Rouss...(1.24) */
double c1=0., b0=0.;    /* "constants" for S estimation " */
double M=0.;           /* "constant" for t-biweight */
double ActualBP;       /* breakdown point implied by c and b0 <= RequestedBP */
double *uAu;           /* local to the descent routine */
/* also need a record for the times when we want to know who is at the dist */
struct ResidRec{
    double SqMahalDist; 	  /* distance to the point */
    int SampleNum;		  /* index into X */
};
struct ResidRec *ResidRecs;	  /* to be used whenever needed */

/* minimization variables */
double ObjectiveValue;		  /* to be minimized */
double BestObjectiveValue;	  /* to keep score */

/* Random Number generator declarations */
#define Ua (long)1317	  /*a,b, and c are for URan*/
#define Ub (long)27699
#define Uc (long)131072
long seed;			  /* random number seed (running) */
long SeedSave;                    /* so we have the original seed as ID */

/* enhanced algorithm declarations */
#   define HALFWAY (VectLen + 1) / 2 + XCnt / 4  /*between p+1 and 1/2 samp*/

/* forward declarations */
double Compute_k();
void Set_c_and_b0();
double rho(double d);
void Standardize_X();
void Use_Algo_Rej_Code();  // have assumed n==XCnt
void Pre_Check_Data();

int i_i;  /* to be used by copy macro */
#define Copy(x,y,z) for (i_i=0; i_i<(z); i_i++) x[i_i] = y[i_i]

/*---------------------------------------------------------------------------*/
double URan( long *seed)
/*return a uniform 0,1 rv using and returning the seed*/
{
   do *seed = (Ua * *seed + Ub) % Uc; while (*seed == 0);
   return((double)*seed	/ (double)Uc);
}

/*--------------------------------------------------------------------------*/
double Norm(double mu, double sd, long *z)
/* return a normal deviate with mean mu and std dev sd,
   use seed z */
/* just waste a deviate... */

{
double v1,v2,r;

   do {
     v1 = 2.0 * URan(z) - 1.;
     v2 = 2.0 * URan(z) - 1.;
     r = v1*v1 + v2*v2;
   } while (r >= 1.0);
   return(mu + sd * (v1 * sqrt(-2.0 * log(r)/r)));
}

/*-------------------------------------------------------------------------*/
void Load_Data(char *DATAFILE)
/* make and load the Data matrix */
/* the input file is assumed to be line records with the first two */
/* values the integer vector length and the number of observations */
/* X array is ROW MAJOR!!! */
{
    FILE *f;			  /* input file stream record */
    int spot;			  /* to loop */
    int r;                        /* for error detection */

    if ((f = fopen(DATAFILE,"r")) == NULL) {
	printf("\nCould not open %s for read\n\n",DATAFILE);
	Info_Exit();
    }
    fscanf(f,"%d %d",&VectLen, &XCnt);
    if (XCnt <= VectLen) {
      printf("\n*error* For vectors of length %d, there must be at least %d points\n\n",
		VectLen, VectLen+1);
	Info_Exit();
    }
    X = malloc(VectLen*XCnt*sizeof(double));
    ALLCHK(X)

    for (spot=0; spot<VectLen*XCnt; spot++) fscanf(f,"%lf",X+spot);
    fclose(f);

    if ((f = fopen(SEEDFILE,"r")) == NULL) {
	printf("\nCould not open the seed file %s for read\n",SEEDFILE);
        printf("This file should contain an integer between 1 and 100000.\n");
        exit(1);
    }
    r = fscanf(f,"%ld",&seed);
    if (r != 1) {
	printf("\nCould not read the seed from %s\n",SEEDFILE);
        printf("This file should contain an integer between 1 and 100000.\n");
        exit(1);
    }
    SeedSave = seed;
    fclose(f);

    Set_c_and_b0();
    Pre_Check_Data();
}

/*-------------------------------------------------------------------------*/
void Dump_Data(char *msg)
/* dump the X array */
{
    int row, col;

    printf("Dump of X data array %s\n",msg);
    for (row=1; row<=XCnt; row++) {
	for (col=1; col<=VectLen; col++) printf("%E ",Xof(row,col));
	printf("\n");
    }
}

/*-------------------------------------------------------------------------*/
void Make_Room()
/* allocate space for global data structures */
/* everything is made, no matter what you are doing .... */
{
    JBits = malloc((XCnt)*sizeof(int));
    ALLCHK(JBits)
    OnesList = malloc(XCnt*sizeof(int));
    ALLCHK(OnesList)
    ZerosList = malloc(XCnt*sizeof(int));
    ALLCHK(ZerosList)
    BestJBits = malloc(XCnt*sizeof(int));
    ALLCHK(BestJBits)
    XBarJ = malloc(VectLen*sizeof(double));
    ALLCHK(XBarJ)
    XJ = malloc((XCnt)*VectLen*sizeof(double)); /* lots of room */
    ALLCHK(XJ)
    ZJ = malloc(sizeof(double)*(size_t)XCnt*(size_t)(VectLen+1)); /* lots */
    ALLCHK(ZJ)
    C = malloc(VectLen*VectLen*2*sizeof(double));
    ALLCHK(C)
    A = malloc((VectLen+1)*(VectLen+1)*2*sizeof(double));
    ALLCHK(A)
    SqResiduals = malloc((XCnt)*sizeof(double));
    ALLCHK(SqResiduals)
    kSqSpace = malloc((XCnt)*sizeof(double));
    ALLCHK(kSqSpace)
    dTilde = malloc((XCnt)*sizeof(double));
    ALLCHK(dTilde)
    wVector = malloc((XCnt)*sizeof(double));
    ALLCHK(wVector)
    OldwVector = malloc((XCnt)*sizeof(double));
    ALLCHK(OldwVector)
    ResidRecs = malloc((XCnt)*sizeof(struct ResidRec));
    ALLCHK(ResidRecs)
    uAu = malloc((XCnt+1)*sizeof(double));
    ALLCHK(uAu)    
}

/*-------------------------------------------------------------------------*/
void Check_Bits(int JCnt, char *msg)
/* debug tool */
{
   int OPos, ZPos;  /* to loop through substring lists */
   int i, locj;     /* to loop through JBits */

    locj = 0;
    for (i=0; i<XCnt; i++) locj += *(JBits+i);
    if (locj != BaseSubSampleSize) {
        printf("There are only %d bits rather than %d in J\n",locj,BaseSubSampleSize);
        printf("%s\n",msg);
        for (i=0; i<XCnt; i++) printf("%d ",*(JBits+i)); printf("\n");
        exit(1);
    }
    for (OPos=0; OPos < JCnt; OPos++) if (!(*(JBits+*(OnesList+OPos)))) {
        printf("zero bit Opos=%d, jbit pos is %d\n",OPos, *(OnesList+OPos));
        printf("%s\n",msg);
        exit(1);
    }
    for (ZPos=0; ZPos < JCnt; ZPos++) if ((*(JBits+*(ZerosList+ZPos)))) {
        printf("one bit Zpos=%d, jbit pos is %d\n",ZPos, *(ZerosList+ZPos));
        printf("%s\n",msg);
        exit(1);
    }
    printf("bits OK: %s\n",msg);
    for (i=0; i<XCnt; i++) printf("%d ",*(JBits+i)); printf("\n");
}

/*-------------------------------------------------------------------------*/
void Create_SubString_Lists(const int JCnt)
/* Update sub-string lists for ones and zeros in JBits	*/
/* no need to clear first */
/* JCnt is the size of the sub-sample */
/* (this isn't as useful in genetic.c because we may never use them...)*/
{
    int row,oh=0,zee=0;			/* to loop */

    for (row=0; row<XCnt; row++)
	if (*(JBits+row)) *(OnesList+oh++) = row; else *(ZerosList+zee++)=row;
}

/*-------------------------------------------------------------------------*/
void Form_XJ()
/* put rows in XJ corresponding to the J set indexes */
/* note that the indicator set is zero based; also note row data vectors*/
{
    int Xrow, XJrow, col;		 /* to loop */

    XJrow = 1;
    for (Xrow = 1; Xrow <= XCnt; Xrow++) {
	if (*(JBits+Xrow-1)) {
	    for (col = 1; col <= VectLen; col++)
		XJof(XJrow, col) = Xof(Xrow, col);
	    /* the row index is in the J set */
	    XJrow++;
	}
    }
}

/*-------------------------------------------------------------------------*/
void Form_ZJ(double *X)
/* put rows in ZJ corresponding to the J set indexes */
/* note that the indicator set is zero based; also note row data vectors*/
/* X is provided as an argument to allow for ghost images */
{
    int Zrow, ZJrow, col;		 /* to loop */

    ZJrow = 1;
    for (Zrow = 1; Zrow <= XCnt; Zrow ++) {
	if (*(JBits+Zrow-1)) {
	    ZJof(ZJrow, 1) = 1;
	    for (col = 2; col <= VectLen+1; col++)
		ZJof(ZJrow, col) = Xof(Zrow, col-1);
	    /* the row index is in the J set */
	    ZJrow++;
	}
    }
}

/*-------------------------------------------------------------------------*/
void Dump_XJ(const int JCnt, char *msg)
/* dump the bit map and the rows */
/* JCnt is the size of the sub-sample */
{
    int row,col;	   /* to loop */

    printf("J set and corresp. data matrix %s\n",msg);
    for (row=0; row<XCnt; row++) printf("%2d",*(JBits+row));
    printf("\n\n");
    for (row=1; row<=JCnt; row++) {
	for (col=1; col<=VectLen; col++) printf("%7E ",XJof(row,col));
	printf("\n");
    }
}

/*-------------------------------------------------------------------------*/
void Dump_ZJ(const int JCnt, char *msg)
/* dump the bit map and the rows */
/* JCnt is the size of the sub-sample */
{
    int row,col;	   /* to loop */

    printf("J set and corresp. ZJ:  %s\n",msg);
    for (row=0; row<XCnt; row++) printf("%2d",*(JBits+row));
    printf("\n\n");
    for (row=1; row<=JCnt; row++) {
	for (col=1; col<=VectLen+1; col++) printf("%7E ",ZJof(row,col));
	printf("\n");
    }
}

/*-------------------------------------------------------------------------*/
void Compute_XBarJ(const int JCnt)
/* compute XBarJ values (observations are "row" vectors) */
/* JCnt is the size of the sub-sample */
/* very simple */
{
    int row, col;		    /* to loop */
#   define XBCOL *(XBarJ+col-1)	  /* typing aid */

    for (col=1; col<=VectLen; col++) {
	XBCOL = XJof(1,col);
	for (row=2; row<=JCnt; row++) XBCOL += XJof(row,col);
	XBCOL = XBCOL / (JCnt);
    }
}

/*-------------------------------------------------------------------------*/
void Dump_XBarJ(char *msg)
/* display the estimate of the center */
{
    int col;		    /* to loop */

    printf("Dump of Mean for current J set: %s\n",msg);
    for (col=1; col <= VectLen; col++) printf("  %E\n", XBarJof(col));
}

/*-------------------------------------------------------------------------*/
void Randomize_JBits(const int JCnt)
/* produce random J bit settings */
/* URan is to (0,1) not [0,1]; JBits is zero based */
/* JCnt is the size of the sub-sample */
{
    int setsofar=0;	       /* keep track of number set */
    int spot;		       /* element to consider setting */

    memset(JBits, 0, (XCnt)*sizeof(int));

    while (setsofar < JCnt) {
	spot = (int)(URan(&seed) * XCnt);
	if (!(*(JBits+spot))) {
	    ++setsofar;
	    ++(*(JBits+spot));
	}
    }
}

/*-------------------------------------------------------------------------*/
void Form_C(const int JCnt)
/* form the covariance matrix for XJ */
/* JCnt is the size of the sub-sample */

{
    int i, j;		       /* current cell in C */
    int k;		       /* to loop through samples */

    for (i=1; i<=VectLen; i++) {
	for (j=1; j<=i; j++) {
	    Cof(i,j) = 0;
	    for (k=1; k<=JCnt; k++) {
		Cof(i,j) += (XJof(k,i) - XBarJof(i))
			  * (XJof(k,j) - XBarJof(j));
	    }
	    Cof(i,j) = Cof(i,j) / (double)(JCnt - 1);
	}
    }
    for (i=1; i<=VectLen;i++) for (j=i+1; j<=VectLen;j++) Cof(i,j) = Cof(j,i);
}

/*-------------------------------------------------------------------------*/
void Form_A(const int JCnt)
/* form the ZJ * Trans(ZJ) */
/* JCnt is size of half sample */
{
    int i, j;		       /* current cell in C */
    int k;		       /* to loop through samples */

    for (i=1; i<=VectLen+1; i++) {
	for (j=1; j<=i; j++) {
	    Aof(i,j) = 0;
	    for (k=1; k<=JCnt; k++) {
		Aof(i,j) += ZJof(k,i) * ZJof(k,j);
	    }
	}
    }
    for (i=1; i<=VectLen+1;i++) for (j=i+1; j<=VectLen+1;j++) Aof(i,j) = Aof(j,i);
}

/*-------------------------------------------------------------------------*/
void Dump_C(char *msg)
/* dump the C matrix (COLUMN MAJOR storage, but output the usual way) */
{
    int i,j;	/* to loop */
    printf("Entire rectangle: %s\n",msg);
    for (i=0; i<VectLen; i++) {
	for (j=0; j<2*VectLen; j++) printf("%.2E ",*(C+i+j*VectLen));
	printf("|\n");
    }
}

/*-------------------------------------------------------------------------*/
void InvertC(double *C, int VectLen, double *Determinant)
/* things are getting a bit hacked up here..... */
/* assume C is VectLen by VectLen */
/* C must have room for another square on the right */
/* do Gauss-Jordon Elimination on C*x(j) = e(j); Strang 29 */
/* see also page 38 (no use exchanging rows since PDS) */
/* keep a running product of the pivots as the determinant of C */
/* (later, try to do something more efficient (e.g. use Symm)) */
/* COLUMN MAJOR arrays, look out... */
/* pivots are left in original C space, result is on ``right'' side */

{
    /* "locals" */
    int pivotrow, row, col;	/* to loop */
    double Pivot;		/* C[pivotrow, pivotrow]	*/
    double m;			/* multiplier */

    /* put I in the right half */
    for (row=1; row <= VectLen; row++)
	for (col = VectLen+1; col <= 2*VectLen; col++)
	    if (col == row+VectLen) Cof(row,col) = 1.; else Cof(row,col) = 0.;

    /* forward elimination */
    *Determinant = 1;   /* running product */
    for (pivotrow = 1; pivotrow < VectLen; pivotrow++) {
	if (Cof(pivotrow,pivotrow) <= 0.0) {
	    /*printf("bad pivot at row %d, you lose\n",pivotrow);*/
	    *Determinant = 0;
	    return;
	}
	else { /* non-zero pivot in C[row,row] */
	    Pivot = Cof(pivotrow, pivotrow);   /* two uses for pivot */
	    for (row = pivotrow+1; row <= VectLen; row++) {
		m = Cof(row, pivotrow) / Pivot;
		for (col = pivotrow; col <= VectLen+pivotrow; col++) { 
                    /* assumes no zero pivots shuffles!!!*/
		    Cof(row,col) = Cof(row,col) - m * Cof(pivotrow,col);
		}
	    }
	}
	*Determinant *= Pivot;

    }
    *Determinant *= Cof(VectLen, VectLen);
    if (!*Determinant) {return;}

    /* back substitution, pivots are non-zero, so use them */
    for (pivotrow = 2; pivotrow <=VectLen; pivotrow++){
	Pivot = Cof(pivotrow, pivotrow);
	for (row = 1; row < pivotrow; row++) {
	    if (Cof(row,pivotrow)) {
		m = Cof(row,pivotrow) / Pivot;
		for (col = pivotrow; col <= VectLen+pivotrow; col++) {
		    Cof(row,col) = Cof(row,col) - m*Cof(pivotrow, col);
		}
	    }
	}
    }

    /* finally, divide the rows in the inverse by the pivots */
    for (row = 1; row <= VectLen; row++)
	for (col = VectLen+1; col <= 2*VectLen; col++)
	    Cof(row, col) = Cof(row,col) / Cof(row,row);
}

/*-------------------------------------------------------------------------*/
void InvertA(double *A, int VL, double *Determinant)
/* things are getting a bit hacked up here..... */
/* (what is needed is a few moments to write a general invert.... */
/* this is hideous (we pass in VL+1 or else.... */
/* see InvertC for details*/
{
    /* "locals" */
    int pivotrow, row, col;	/* to loop */
    double Pivot;		/* A[pivotrow, pivotrow]	*/
    double m;			/* multiplier */

    /* put I in the right half */
    for (row=1; row <= VL; row++)
	for (col = VL+1; col <= 2*VL; col++)
	    if (col == row+VL) Aof(row,col) = 1; else Aof(row,col) = 0;

    /* forward elimination */
    *Determinant = 1;   /* running product */
    for (pivotrow = 1; pivotrow < VL; pivotrow++) {
	if (!Aof(pivotrow,pivotrow)) {
	    /* printf("zero pivot at row %d, you lose\n",pivotrow); */
	    *Determinant = 0;
	    return;
	}
	else {
	    /* non-zero pivot in A[row,row] */
	    Pivot = Aof(pivotrow, pivotrow);   /* two uses for pivot */
	    for (row = pivotrow+1; row <= VL; row++) {
		m = Aof(row, pivotrow) / Pivot;
		for (col = pivotrow; col <= VL+pivotrow; col++) { /* assumes no zero pivots shuffles!!!*/
		    Aof(row,col) = Aof(row,col) - m * Aof(pivotrow,col);
		}
	    }
	}
	*Determinant *= Pivot;

    }
    *Determinant *= Aof(VL, VL);
    if (!*Determinant) return;

    /* back substitution, pivots are non-zero, so use them */
    for (pivotrow = 2; pivotrow <=VL; pivotrow++){
	Pivot = Aof(pivotrow, pivotrow);
	for (row = 1; row < pivotrow; row++) {
	    if (Aof(row,pivotrow)) {
		m = Aof(row,pivotrow) / Pivot;
		for (col = pivotrow; col <= VL+pivotrow; col++) {
		    Aof(row,col) = Aof(row,col) - m*Aof(pivotrow, col);
		}
	    }
	}
    }

    /* finally, divide the rows in the inverse by the pivots */
    for (row = 1; row <= VL; row++)
	for (col = VL+1; col <= 2*VL; col++)
	    Aof(row, col) = Aof(row,col) / Aof(row,row);
}

/*-------------------------------------------------------------------------*/
int Compare_doubles(const void *arg1, const void *arg2)
/* compare *arg1 and *arg2 as per qsort needs */
/* assume that doubles can never really be equal */
{
    if (*(double*)arg1 < *(double*)arg2) return(-1); else return(1);
}

/*--------------------------------------------------------------------------*/
void Compute_Distance_Vector()
/* compute a squared distance vector (called SqResiduals) for the current C_1
 and sub-sample
*/
{
    double rowsum;	      /* to accumulate rightmost mult first */
    int i;		      /* index into vector being formed */
    int row, col;	      /* indexes for vector matrix mult. */
			      /* (row and col refer to c-1) */

    for (i=1; i<=XCnt; i++) {
        *(SqResiduals+i-1) = 0.;
	for (row = 1; row <= VectLen; row++) {
	   rowsum = 0;
	   for (col = 1; col <= VectLen; col++) {
	     rowsum += (Xof(i,col) - XBarJof(col)) * C_1of(row,col);
	   }
	   *(SqResiduals+i-1) += rowsum * (Xof(i,row) - XBarJof(row));
	}
    }
}

/*-------------------------------------------------------------------------*/
void Compute_mJ2()
/* replace the word "median" with (n+p+1)/2 percentile */
/* compute a vector that represents the argument to the med in 1.24 */
/* then find sqrt of its median and place it in global mJ */
/* note that the vector is declared globally for mindless dynamic allocation*/
/* (Could form vectors to save a subtraction at the expense of an assignment*/
/* and loop control; if you have a vector processor, you may want to do it) */
{
    Compute_Distance_Vector();
    qsort(SqResiduals, XCnt, sizeof(double), Compare_doubles);
    mJ2 = *(SqResiduals+(XCnt+VectLen+1)/2);
}

/*-------------------------------------------------------------------------*/
double Mahalanobis_Dist(int SampNo)
/* find the squared mahalanombis distance to the sample SampNo (not 0 based) */
/* using the current C inverse and the current X bar */
{
    double rowsum;	      /* to accumulate rightmost mult first */
    double RetVal=0.;	      /* to collect the distance (squared) */
    int row, col;	      /* indexes for vector matrix mult. */
			      /* (row and col refer to c inverse) */

    for (row = 1; row <= VectLen; row++) {
	rowsum = 0.;
	for (col = 1; col <= VectLen; col++) {
	    rowsum += (Xof(SampNo,col) - XBarJof(col)) * C_1of(row,col);
	}
	RetVal += rowsum * (Xof(SampNo,row) - XBarJof(row));
    }
    return RetVal;
}

/*-------------------------------------------------------------------------*/
void Record_Best()
/* the global variable ObjectiveValue < BestObjectiveValue so do some
   bookkeeping */
{
    int i, cntj;                   /* to count bits */

    BestObjectiveValue = ObjectiveValue;
    Copy(BestJBits, JBits, XCnt);
    cntj = 0;
    for (i=0; i<XCnt; i++) cntj += *(JBits+i);
    if (cntj != BaseSubSampleSize) {
        printf("wrong number of bits in best %d\n",cntj);
        for (i=0; i<XCnt; i++) printf("%d ",*(JBits+i));
        exit(1);
    }
    if (Trace) printf("Objective Value Reduced to %14.9lf\n", ObjectiveValue);
    if (Trace) {for (i=0; i<XCnt; i++) printf("%d ",*(JBits+i)); printf("\n");}
}

/*---------------------------------------------------------------------------*/
void Process_JBits(const int JCnt)
/* Given JBits, do all the calculations	*/
{
    int debcnt=0;  /* debug count */

    Form_ZJ(X);
    Form_A(JCnt);
    InvertA(A, VectLen+1, &Determinant);
    ObjectiveValue = Determinant;   /* LOOK! unscaled by 1/(n-h)^(p+1) */
    if (Determinant <= (double)0.0) {
	printf("Singular Covariance matrix");
	printf("Determinant = %E\n", Determinant);
	Dump_ZJ(JCnt, "zero determinant");
	printf("End of zero determinant dump\n");
	ObjectiveValue = 10 * ObjectiveValue;  /* try to stay out */
	SingularCnt++;
//	return;
        exit(1);
    }

    if (!ObjectiveValue) {
	printf("Zero objective value\n");
	printf("Determinant = %E, mJ2=%E\n", Determinant, mJ2);
	Dump_XJ(JCnt, "Zero objective Value");
	printf("End of zero objective value dump\n");
	exit(1);
    }
    if (ObjectiveValue < BestObjectiveValue){
	Record_Best();
    }
}

/*---------------------------------------------------------------------------*/
double rho(double d)
/* compute the rho function, use conditional compilition for more forms...*/
/* assume that c is global */
/* change this to a macro for slight speed up */
{
    double d2,c2,c4,d4,M2,M4;	/* intermediate calcs */
    double retval;		/* debugging */

   if (d > (M + c1)) return(M*M/2. + c1 * (5.*c1 + 16.*M)/30.);
   else if (d >= M) {
     d2 = d*d; d4 = d2*d2;
     c2 = c1*c1; c4 = c2*c2;
     M2 = M*M; M4 = M2*M2;
     retval = M2/2. - M2 * (M4 - 5.*M2*c2 + 15.*c4)/(30.*c4)
	      +d2*(0.5 + M4/(2*c4) - M2/c2)
	      +d*d2*(4*M/(3*c2) - 4*M2*M/(3*c4))
	      +d4*(3*M2/(2*c4) - 1/(2*c2))
	      -4*M*d*d4/(5*c4) + d4*d2/(6*c4);
     return(retval);
   }
   else return(d*d/2.);
}

/*---------------------------------------------------------------------------*/
double psi(double d)
/* compute the psi function, use conditional compilition for more forms...*/
/* assume that c is global */
/* change this to a macro for slight speed up */
{
    double inner;   /* intermediate calc */

   if (d > (M + c1)) return(0.);
   else if (d >= M) {
     inner = 1. - ((d-M)/c1)*((d-M)/c1);
     return(d * inner * inner);
   } else return(d);
}

/*---------------------------------------------------------------------------*/
double w(double d)
/* compute the weight function, use conditional compilition for more forms...*/
/* assume that c1 is global */
/* change this to a macro for slight speed up */
{
    double inner;   /* intermediate calc */

   if (d > (M + c1)) return(0.);
   else if (d >= M) {
     inner = 1 - ((d-M)/c1)*((d-M)/c1);
     return(inner * inner);
   } else return(1.);
}

/*---------------------------------------------------------------------------*/
double Compute_k()
/* find the k value to enforce the constraint (see Rocke paper) */
/* assumes the distances vector, SqResiduals, has been computed */
{
    double k;                   /* newton converge on k (return val) */

    Copy(kSqSpace, SqResiduals, XCnt);
    qsort(kSqSpace, XCnt, sizeof(double), Compare_doubles);
    k = sqrt(*(kSqSpace+(XCnt+VectLen+1)/2)) / M;
    return(k);     
}

/*---------------------------------------------------------------------------*/
void Compute_wVector_and_Sums()
/* (for s estimation iteration) find a k value and then adjust the distances*/
/* Assume that b0 is global */
/* the result is placed in the global vector dTilde */
/* note: cute math, fk = mean(rho(d/k)) and dfk = -mean(psi(d/k)*d/k^2)
/* also pre-compute the results of calls to the w function */
{
    double k;                   /* newton converge on k*/
    int i;                      /* to loop for sums */

    k = Compute_k();
    Sumw = Sumv = 0.;
    for (i=0; i<XCnt; i++) { 
        Sumw += (*(wVector+i) = w((*(dTilde+i) = sqrt(*(SqResiduals+i))/k)));
        Sumv += (*(wVector+i)) * (*(SqResiduals+i) / (k*k));
    }
}

/*---------------------------------------------------------------------------*/
void M_Iterate()
/* given a C matrix, iterate to an M estimate */
/* note that this routines abuses many data structures, in particular,
   XBarJ is used as the iterated mean and C is adjusted as well
*/
/* assumes that c and b0 have been set */
{
    int i,j,k;                        /* to loop */
    double MaxWDelta;                 /* max delta of a wieight element */
    long siters=0;                    /* to time out on iterations */

    Sumw = 0;
    for (i=0; i<XCnt; i++) *(OldwVector+i) = 1.;
    do {
        InvertC(C, VectLen, &Determinant);
        if (Determinant <= 0.0) {
            printf("Singular Covariance matrix\n");
	        printf("Determinant = %E\n", Determinant);
            printf("w vector\n");
            for (j = 0; j <XCnt; j++) printf("%E ",*(wVector+j));
            printf("\n");
	        printf("End of zero determinant dump from M_Iterate\n");
            exit(1);
        }
        Compute_Distance_Vector();
        Compute_wVector_and_Sums();
        for (j = 1; j <= VectLen; j++) {
            XBarJof(j) = 0.;
            for (i=0; i < XCnt; i++) XBarJof(j) += (*(wVector+i)) * Xof(i+1,j);
            XBarJof(j) = XBarJof(j) / Sumw;
	    }
        for (i=1; i<=VectLen; i++) { /* sorry about the use of k for i */
	    for (j=1; j<=i; j++) {
	        Cof(i,j) = 0;
   	        for (k=1; k<=XCnt; k++) {
		    Cof(i,j) += (*(wVector+k-1))
                              * (Xof(k,i) - XBarJof(i))
			      * (Xof(k,j) - XBarJof(j));
	        }
	        Cof(i,j) = VectLen * Cof(i,j) / Sumv;
	    }
        }
        for (i=1; i<=VectLen;i++) 
            for (j=i+1; j<=VectLen;j++) 
                Cof(i,j) = Cof(j,i);
        MaxWDelta = 0.;
        for (i=0; i<XCnt; i++) {
            if (dabs((*(wVector+i)) - (*(OldwVector+i))) > MaxWDelta)
                MaxWDelta = dabs((*(wVector+i)) - (*(OldwVector+i)));
            *(OldwVector+i) = *(wVector+i);
	}
        if (siters++ > (long)(1./(float)WTOL)) {
            printf("Time out in M convergence, MaxWDelta = %lf, WTOL = %lf\n",
                   MaxWDelta, WTOL);
            break;
	}
    } while (MaxWDelta > WTOL); /* wgts converge */
}

/* --------------------------------------------------------------------------*/
double xp(float p)
/* return inverse of normal (approx. Abromowitz and Stegun 941 */
{
  double t;
  if (p > (float)0.5) p = (float)1.-p;
  t = sqrt(log(1/(p*p)));
#define a0 2.30753
#define a1 0.27061
#define b1 0.99229
#define b2 0.04481
  return t - (a0 + a1*t) / (1+b1*t+b2*t*t);
#undef a0
#undef a1
#undef b1
#undef b2
}

/* --------------------------------------------------------------------------*/
double ChiSq_1(int v, float x)
/* return the x point of a v degree chi square (e.g., its inverse)*/
/* (approx. Abromowitz and Stegun 932 */
/* only above the median */
{
  double inner;
  inner =  1. - 2./(9.*v) + xp(x) * sqrt((double)2./(double)(9.*v));
  return v * inner * inner * inner;
}

/* --------------------------------------------------------------------------*/
float ChiSq(int v, double D, double tol)
/* return the v degree chi square by searching the inverse */
{
  int i=0;  /* debug... get rid of this */
  float bot, top, mid;  /* for binary search */
  double d;             /* mid dist */
  bot = (float)0.5;
  top = (float)1. - (float)tol;
  if ((D > ChiSq_1(v, top)) || (D < ChiSq_1(v, bot))) {
    printf("Cannot find chiSq(%d) for %lf (tol=%lf)\n", v, D, tol);
    exit(1);
  }
  mid = (top+bot)/(float)2.;
  while ((dabs((d = (double)ChiSq_1(v, mid))-D) > tol) && (++i<20)) {
    if (d > D) top = mid; else bot = mid;
    mid = (top + bot) / (float)2.;
  }
  return(mid);
}


/*---------------------------------------------------------------------------*/
void Set_c_and_b0()
/* set the values of c and M using rough approximations */
/* ignore the requested breakdown point */
/* no longer b0 because we are no longer using S */
{    
    double MPlusc;  /* F^-1(1-arp) */

    MPlusc = ChiSq_1(VectLen, (float)0.999);
    M = sqrt(ChiSq_1(VectLen, (float)(XCnt+VectLen+1)/(float)(2*XCnt)));
    c1 = sqrt(MPlusc) - M;
    ActualBP = 0.;  /* not used */
}

/*-------------------------------------------------------------------------*/
int Compare_Resids(const void *arg1, const void *arg2)
/* compare *arg1 and *arg2 as per qsort needs */
/* assume that doubles can never really be equal */
{
  if (((struct ResidRec *)arg1)->SqMahalDist 
      < ((struct ResidRec *)arg2)->SqMahalDist) 
        return(-1); else return(1);
}

/*---------------------------------------------------------------------------*/
double Sq_Rej_Dist(int n, float a, float tol, int UseAlgo)
/* useit controls use of the iterative estimator */
/* find the distance to reject fraction a; WRECKS Xbar, C,etc; */
/* ASSUMES VectLen */
{
  double sofar, oldsofar;  // these are cutoffs (sq distances)
  int cnt, row, col;
  int XCntSave;
  double far *XSave;
  int Blocks, Blk, Sector, CutCnt;
  double *BigSqSpace;

  assert(n > VectLen); assert(n <= XCnt);
  if (a<=0.) return HUGE_VAL;
  if (a>=1.) return 0.;

  XSave = _fmalloc(VectLen*XCnt*sizeof(double)); ALLCHK(XSave)
  Copy(XSave, X, XCnt * VectLen);
  XCntSave = XCnt;
  XCnt = n;

  Blocks = (int) (10./((float)XCnt * a));  /* for small a */
  if (Blocks < 40) Blocks = 40;
  Sector = Blocks * XCnt;
  CutCnt = (int)(a * (float)Sector);
  BigSqSpace = malloc(Sector * sizeof(double)); ALLCHK(BigSqSpace)

  cnt = 0;
  sofar = oldsofar = 0.;
  do {
    for (Blk = 0; Blk < Blocks; Blk++) {
      for (row = 1; row <= XCnt; row++)
       for (col = 1; col <= VectLen; col++)
        Xof(row, col) = XJof(row,col) =  Norm((double)0.,(double)1.,&seed);
      Compute_XBarJ(n);
      Form_C(n);
      /* overall, the next line is brutal hack... */
      if (UseAlgo) Use_Algo_Rej_Code();  // have assumed n==XCnt...
      InvertC(C, VectLen, &Determinant);
      Compute_Distance_Vector();  // of len XCnt
      for (row=0; row<XCnt; row++) BigSqSpace[Blk*XCnt+row] = SqResiduals[row];
    }
    qsort(BigSqSpace, Sector, sizeof(double), Compare_doubles);
    oldsofar = sofar;
    sofar += (BigSqSpace[Sector-CutCnt]+BigSqSpace[Sector-CutCnt-1])/2.;
    if (++cnt > 100) break;
    if (Trace) printf ("ID sector cnt=%d, sofar=%lf\n",cnt, sofar);
  } while ((cnt < 2) 
            || (dabs(oldsofar/(cnt-1) - sofar/cnt) / (sofar/cnt) > tol));
  XCnt = XCntSave;
  Copy(X, XSave, XCnt * VectLen);
  _ffree(XSave); free(BigSqSpace);
  return sofar/cnt;
}

/*---------------------------------------------------------------------------*/
float ID_Good(float a1, float a2)
/* on input, the distance vector must be ready to use */
/* on output, set JBits set for nominally good points */
/* return the cutoff distance */
/* do a two step using a1 and a2 as fraction to reject */
/* this routine is very fragile (as noted) */
/* all the globals make it a hack */
{
  double far *SqSave; // to save "real" data
  int XCntSave;
  double CutPt1, CutPt2;  // cutoff for sq dist
  int i,n;       // index and temp data size

  SqSave = _fmalloc(XCnt*sizeof(double)); ALLCHK(SqSave)
  Copy(SqSave, SqResiduals, XCnt);

  CutPt1 = Sq_Rej_Dist(XCnt, a1, SimTol, True); // wreck the globals...
  n = 0;
  for (i=0; i<XCnt; i++) 
    if (SqSave[i] < CutPt1) {JBits[i] = 1; ++n;}
    else JBits[i]=0;
  if (Trace) printf("first cut uses %E and leaves %d\n",CutPt1, n);
  if (n <= VectLen) {
    printf("Warning: too few points kept with cutoff fraction %f\n", a1);
    n = XCntSave;
  } else {
    Form_XJ();
    Compute_XBarJ(n);
    Form_C(n);
    InvertC(C, VectLen, &Determinant);
    Compute_Distance_Vector();
  } 
  CutPt2 = ChiSq_1(VectLen, ((float)1.-a2));
  CutPt2 
    /= ChiSq(VectLen+2, 
       ChiSq_1(VectLen, (float)(1.-a1)), (float)0.001)/(float)(1.-a1);
  n = 0;
  for (i=0; i<XCnt; i++) {
    if (Trace) printf("%d %f\n",i,(float)SqSave[i]);
    if (SqResiduals[i] < CutPt2) {JBits[i] = 1; ++n;}
    else JBits[i]=0;
  }
  if (Trace) printf("second cut uses %E and leaves %d\n",CutPt2, n);
  if (n <= VectLen) {
    printf("WARNING: Too few points kept with cutoff fraction %f\n", a2);
  }
  _ffree(SqSave);
  return (float)CutPt2;
}

/*---------------------------------------------------------------------------*/
double Comp_uAu(int uInd)
/* to get give the determint multiplier
   assume that the inverse of A is available
   see Hawkins FSA paper for notation
   As of Jan 27, 1994:
*/
{
    double uau=0;                 /* intermediate vector x inverse prods */
    double rowsumau;              /* to accumulate rightmost mult first */
			          /* (row and col refer to c inverse) */
    int p1, locuInd;
    double *locA1Start;
    int i,j;
#define uauZof(i,j) (j==1 ? 1 : Xof(i,j-1))  /* inefficient */
/*
    for (row = 1; row <= VectLen+1; row++) {
	rowsumau = A_1of(row,1);
	for (col = 2; col <= VectLen+1; col++) {
	    rowsumau += A_1of(row,col) * Xof(uInd,col-1);
	}
	uau += rowsumau * uauZof(uInd, row);       
    }
*/
/* dangerous speed ups */
locA1Start = A+(VectLen+1)*(VectLen+1);
locuInd = uInd - 1;
p1 = VectLen+1;
#define locA1(i,j) *(locA1Start+i+j*p1)
    for (i=0; i <= VectLen; i++) {
        rowsumau = locA1(i,0);
        for (j=1; j<=VectLen; j++) 
            rowsumau += locA1(i,j) * *(X+locuInd*VectLen + j-1);
	uau += rowsumau * uauZof(uInd, i+1);       
    }
    return(uau);
}
#undef uauZof

/*---------------------------------------------------------------------------*/
double Swap_Fact(int GoodOut, int BadIn)
/* give the determint multiplier that results from
   taking goodout out of J and BadIn into J (less than one is good)
   assume that the inverse of A is available
   see Hawkins page 8 for notation
*/
#define Zof(i,j) (j==1 ? 1 : Xof(i,j-1)) /* inefficient */
{
    double retval;                /* for debugging */
    double a1;                    /* avoid address calc */
    double uau=0, vav=0, uav=0;   /* itermediate vector x inverse prods */
    double rowsumav, rowsumau;    /* to accumulate rightmost mult first */
    int row, col;	          /* indexes for vector matrix mult. */
			          /* (row and col refer to c inverse) */

    /* use Z (not X or ZJ) */
    for (row = 1; row <= VectLen+1; row++) {
	rowsumav = rowsumau = 0.;
	for (col = 1; col <= VectLen+1; col++) {
	    rowsumau += (a1=A_1of(row,col)) * Zof(GoodOut,col);
	    rowsumav += a1 * Zof(BadIn,col);
	}
	uau += rowsumau * Zof(GoodOut, row);       
	vav += rowsumav * Zof(BadIn, row);
        uav += rowsumav * Zof(GoodOut, row);
    }
    retval = (1-uau)*(1+vav) + (uav)*(uav);
    return(retval);
}
#undef Zof
/*---------------------------------------------------------------------------*/
int Find_Best_Descent(int JCnt, int *GoodOut, int *BadIn)
/* return false if there is not descent possible */
/* uses sub string lists */
/* return pointers to the best guys to move */
/* A had better correspond to X  */
/* This is way too messy.... */
{
    int OPos, ZPos;  /* to loop through substring lists */
    double BestMoveFact=1.;  /* to find best (1. not HUGE_VAL....)*/
    double MF;       /* avoid extra calls */
    int i;

    for (i=1; i<=XCnt; i++) uAu[i] = Comp_uAu(i);

    for (OPos=0; OPos < JCnt; OPos++) {
        for (ZPos=0; ZPos < XCnt - JCnt; ZPos++) {
          if ((1-uAu[*(OnesList+OPos)+1]) * (1+uAu[*(ZerosList+ZPos)+1])
            < BestMoveFact) {
            /* calling the entire Swap_Fact is a slight waste of time... */
            if ((MF = Swap_Fact(*(OnesList+OPos)+1, *(ZerosList+ZPos)+1))
                 < BestMoveFact) {
                BestMoveFact = MF;
                *GoodOut = *(OnesList+OPos)+1;
                *BadIn = *(ZerosList+ZPos)+1;
                if (Trace) printf("%d out, %d in is current best\n",
                           *(OnesList+OPos)+1, *(ZerosList+ZPos)+1);
	    }
          }
        }
    }
    if (Trace) printf("Best Move has MF=%E\n", BestMoveFact);
    if (BestMoveFact < 1.) return(True); else return(False);
}

/*-------------------------------------------------------------------------*/
void Do_One_Descent(int JCnt)
/* simple descent to a local min */
{
    int GoodOut, BadIn;        /* to swap */

    while (Find_Best_Descent(JCnt, &GoodOut, &BadIn)) {
        *(JBits+GoodOut-1) = 0; *(JBits+BadIn-1) = 1;
        if (Trace) printf("swap %d %d %E\n", GoodOut, BadIn, Determinant);
        Create_SubString_Lists(JCnt);  /* could update ...*/
        Process_JBits(JCnt);
    }
}

/*---------------------------------------------------------------------------*/
void Get_XBarJ(int JCnt, double *XBarJ)
/* x bar for the current J set, using only local variables (except J) */
/* perhaps used by ghost image programs */
{
   int row, col;     /* to loop */
   
    for (col=1; col<=VectLen; col++) {
	XBarJof(col) = 0.;
	for (row=1; row<=XCnt; row++) 
            if (*(JBits+row-1)) XBarJof(col) += Xof(row,col);
	XBarJof(col) /=  JCnt;
    }
}

#define T_T FLT_EPSILON
/*-------------------------------------------------------------------------*/
int Lex_Compare_Data_Recs(const void *arg1, const void *arg2)
/* compare *arg1 and *arg2 as per qsort needs */
/* assume that doubles can never really be equal */
{
  int i;
  for (i=0; i<VectLen; i++) {
    if (*((double *)arg1+i) < *((double *)arg2+i)-T_T) return(-1); 
    if (*((double *)arg1+i) > *((double *)arg2+i)+T_T) return(1); 
  }
  return(0);
}

/*-------------------------------------------------------------*/
void Pre_Check_Data()
/* this routine checks X for duplicates */
/* if any are found, it gripes and dies */
{
  int i,j;
  int Duds = False;
  double *XSave;

  XSave = malloc(VectLen*XCnt*sizeof(double)); ALLCHK(XSave)
  Copy(XSave, X, XCnt * VectLen);
  qsort(X, XCnt, VectLen * sizeof(double), Lex_Compare_Data_Recs);

  for (i=1; i<XCnt; i++) {
    if (!Lex_Compare_Data_Recs(XRow(i), XRow(i+1))) {
       if (!Duds) printf("\nERROR: Duplicate points\n");
       Duds = True;
       for (j=1; j<=VectLen; j++) printf("%lf ",Xof(i,j));
       printf("\n");
    }
  }
  if (Duds) {
    printf("The program will terminate due to duplicate points.\n");
    printf("If a point was included r times, it was displayed r-1 times.\n");
    printf("The tolerance used in duplicate detections was %E\n",
           (double)T_T);
    exit(1);
  }
  Copy(X, XSave, XCnt * VectLen);
  free(XSave);
}
#undef T_T

/*-------------------------------------------------------------------------*/
void Use_Algo_Rej_Code()
{
  M_Iterate();
}

/*----------------------------------------------------------------------------*/
void Parms_Help(char *S)
{
  printf("\nThere was an error reading parameters from the file %s\n",S);
  printf("The file should contain the partition multiplier (e.g. 5)\n");
  printf("  the trace level (e.g. 0), the cut fraction (e.g. 0.01)\n");
  printf("  and the simulation tolerance (e.g. 0.5)\n");
  printf("Sample Contents:\n 5 0 0.01 0.5\n");  
  exit(1);
}

/*----------------------------------------------------------------------------*/
void Set_Default_Parms()
/* establish values for algorithm parameters */
{
  Lambda = 5 * VectLen;
  Trace = 0;
  Cut1 = Cut2 = (float)0.01;
  SimTol = (float)0.5;
} 

/*----------------------------------------------------------------------------*/
void Dump_Parms(FILE *f)
{
    fprintf(f,"Lambda Multiplier: %d; Trace: %d; \n", Lambda/VectLen, Trace);
    fprintf(f,"Cut Fraction: %f; Simulation Tolerance: %f\n", Cut2, SimTol);
}
 
/*----------------------------------------------------------------------------*/
void Load_Parms(char *PARMSFILE)
/* establish values for algorithm parameters */
{
    FILE *f;	   /* input file stream record pointer */
    int LambdaMult; /* allow partition size to vary w/dimension */
    int r;
    float Cut;

    if ((f = fopen(PARMSFILE,"r")) == NULL) {
       printf("\nCould not open the parms file specified: %s\n\n",PARMSFILE);
       Info_Exit();
    }
    r = fscanf(f,"%d %d %f %f", &LambdaMult, &Trace, &Cut, &SimTol);
    if (r != 4) Parms_Help(PARMSFILE);

    fclose(f);
    if (LambdaMult < 3) {
        printf("Lambda mult is %d, but should be > 2\n",LambdaMult);
        Parms_Help(PARMSFILE);
    }
    if ((Cut < 0) || (Cut > 1)) {
      printf("The cut parm was %f but it must be between 0 and 1\n",Cut);
      Parms_Help(PARMSFILE);
    }
    if (SimTol <= (float)0.0) {
      printf("The simulation tolerance must be greater than 0\n");
      Parms_Help(PARMSFILE);
    }
    
    Cut1 = Cut2 = Cut;
    Lambda = LambdaMult * VectLen;
}

/*-------------------------------------------------------------------------*/
void Forward(int *JCnt)
/* similar to atkinson algorithm */
/* NOTE: we stop at 2p */
{
    int i;		      /* index into vector being formed */

    for (*JCnt=VectLen+1; *JCnt <= 2*VectLen; (*JCnt)++) {
        InvertC(C, VectLen, &Determinant);
        Compute_Distance_Vector();
        for (i=0; i<XCnt; i++) {
          (ResidRecs+i)->SqMahalDist = *(SqResiduals+i);
	  (ResidRecs+i)->SampleNum = i;
        }
        qsort(ResidRecs, XCnt, sizeof(struct ResidRec), Compare_Resids);
        memset(JBits, 0, XCnt*sizeof(int));
        for (i=0; i<*JCnt; i++) *(JBits+(ResidRecs+i)->SampleNum) = 1;
        Form_XJ();
        Compute_XBarJ(*JCnt);
        Form_C(*JCnt);
    }
    /* fill to half sample (not very efficient) */
    *JCnt = BaseSubSampleSize;
    memset(JBits, 0, XCnt*sizeof(int));
    for (i=1; i<= *JCnt; i++) *(JBits+(ResidRecs+i)->SampleNum) = 1;
    Form_XJ();
    Compute_XBarJ(*JCnt);
    Form_C(*JCnt);
    Create_SubString_Lists(*JCnt);
}

/**************************************************************************/
int Partition_Main(long LocalItersAllowed)
{
    long IterCntr=0;			       /* to count iterations */
    int i,BitsInBest;			       /* find out how big winner is*/
    int JCnt;				       /* variable sub-sample size*/

    JCnt = BaseSubSampleSize;
    Randomize_JBits(JCnt);	/* to make a best */
    Process_JBits(JCnt);
    Create_SubString_Lists(JCnt);
    Record_Best();	/* a best */
    for (i=0; i<LocalItersAllowed; i++) {
	IterCntr++;
	Do_One_Descent(JCnt);
	if (Trace) printf("Descent %ld Results in %.3E\n", IterCntr, ObjectiveValue);
        Randomize_JBits(JCnt);
        Process_JBits(JCnt);
        Create_SubString_Lists(JCnt);
    }
    Copy(JBits, BestJBits, XCnt);
    BitsInBest = 0; for (i=0; i<XCnt; i++) if (*(BestJBits+i)) ++BitsInBest;
    Form_XJ();
    Compute_XBarJ(BitsInBest);
    if (Trace) Dump_XBarJ("For Best SubSample in Partition");
}

/*------------------------------------------------------------------*/
void Generate_Permutation(int N, int *p)
/* generate permutation of length N in p */
/* p is zero based, but the permutation is not */
{
    int i,j;				 /* to loop */
    int lspot;				 /* offset in l */
    int *FullList;			 /* unpermuted */
    int *l; 				 /* left to be used */

    FullList = malloc(N * sizeof(int));
    ALLCHK(FullList)
    for (i=0; i<N; i++) *(FullList+i) = i+1;
    l = malloc(N * sizeof(int));
    ALLCHK(l)

    Copy(l, FullList, N);
    for (i=0; i < N; i++) {
	lspot = (int)(URan(&seed) * (float)(N - i));
	*(p+i) = *(l+lspot);
	for (j=lspot; j<N-i; j++) *(l+j) = *(l+j+1);
    }
    free(l); free(FullList);
}

/*------------------------------------------------------------------*/
void Write_First_Results(char *OutFile, char *InFileName)
/* final report */
{
   FILE *f;  /* output file */
   int row, col;
   char flag;

   if (!(f=fopen(OutFile,"w"))) {
     printf("Could not open %s for write\n",OutFile);
     exit(1);
   }
   fprintf(f,BANNER);
   fprintf(f,"\nData read from: %s\n", InFileName);
   fprintf(f,"\nRobust M-Estimate Mean:\n");
   for (col=1; col <= VectLen; col++) fprintf(f," %11.3E\n", XBarJof(col));
   fprintf(f,"\nM-Estimate of Covariance Matrix:\n");
   for (row=1; row<=VectLen; row++) {
     for (col=1; col <= VectLen; col++) fprintf(f," %11.3E", Cof(row,col));
     fprintf(f,"\n");
   }
   fclose(f);
}

/*------------------------------------------------------------------*/
void Write_Final_Results(char *OutFile, float RejDist)
/* final report */
{
   FILE *f;  /* output file */
   int row, col, JCnt;
   char flag;

   if (!(f=fopen(OutFile,"a"))) {
     printf("Could not open %s for write\n",OutFile);
     exit(1);
   }
   fprintf(f,"\n---------------------------------------------\n");
   fprintf(f,"\nRobust, Unscaled Mahalonobis Distances\n");
   fprintf(f,"An asterisk (*) indicates a potential outlier.\n");
   fprintf(f,"The cutoff for a potential outlier (with a requested\n rejection");
   fprintf(f," of %g of the non-outliers) was determined to be %.3E\n",Cut2, RejDist);   
   for (row=0; row<XCnt; row++) {
     if (JBits[row]) flag = ' '; else flag = '*';
     fprintf(f,"%c point%4d: %.3E\n", flag, row+1, *(SqResiduals+row));
   }
   JCnt = 0;
   for (row=0; row<XCnt; row++) JCnt += JBits[row];
   Form_XJ();
   Compute_XBarJ(JCnt);
   Form_C(JCnt);
   fprintf(f,"Rejection of the points with asterisks leaves %d points\n",JCnt);
   fprintf(f,"with mean:\n");
   for (col=1; col <= VectLen; col++) fprintf(f," %11.3E\n", XBarJof(col));
   fprintf(f,"\and covariance matrix:\n");
   for (row=1; row<=VectLen; row++) {
     for (col=1; col <= VectLen; col++) fprintf(f," %11.3E", Cof(row,col));
     fprintf(f,"\n");
   }

   fprintf(f,"\n\nComplete, Original Data:\n");
   for (row=1; row <= XCnt; row++) {
     for (col=1; col <=VectLen; col++) fprintf(f,"%E ",Xof(row,col));
     fprintf(f,"\n");
   }
   fprintf(f,"\n\nParameters:\n");                               
   fprintf(f,"p=%d, n=%d, Iterations:%ld\n",VectLen, XCnt, ItersAllowed); 
   Dump_Parms(f);
   fclose(f);
}
/*------------------------------------------------------------------*/
void Bad_Run(char *OutFile, char *InFileName)
/* final report when things didn't work out */
{
   FILE *f;  /* output file */
   int row, col;
   
   if (!(f=fopen(OutFile,"w"))) {
     printf("Could not open %s for write\n",OutFile);
     exit(1);
   }
   fprintf(f,BANNER);
   fprintf(f,"\nERROR: Could not get results due to singular data\n\n");
   fprintf(f,"Data read from: %s\n", InFileName);
   fprintf(f,"\n\nData:\n");
   for (row=1; row <= XCnt; row++) {
     for (col=1; col <=VectLen; col++) fprintf(f,"%E ",Xof(row,col));
     fprintf(f,"\n");
   }
   fprintf(f,"\n\nParameters:\n");                               
   fprintf(f,"p=%d, n=%d, Iterations:%ld\n",VectLen, XCnt, ItersAllowed); 
   Dump_Parms(f);
   fclose(f);
}

                               
/**************************************************************************/
int main(int argc, char *argv[])
{
    double far *XSave, far *XWorking;       /* copies of X */
#   define XWorkingof(i,j) *(XWorking+(i-1)*VectLen+j-1) /* col major...*/
    int XCntSave;                   /* overall n */
    double *BestC, *BestXBarJ;      /* best of the partition results */
    double *CSave;                  /* avoid an inversion */
    double *PartC, *PartBar;        /* to allow S-iter on part result */
    double MainBestObj = HUGE_VAL;  /* best partition objective value */
    int PartitionCnt;               /* number of sample partitions */
    int *Permutation;               /* random permutation vector */
    int i,j,k;                      /* to loop */
    int JCnt;                       /* to process sub-samples of partitions */
    int Part;                       /* to loop through partitions */
    FILE *f;                        /* for file check */
    float CutDist;                  /* to report */

    printf(BANNER);
    if ((argc < 3) || (argc > 5)) Info_Exit();

#   ifndef BaseSubSampleSize
      printf("Compiled without BaseSubSampleSize defined.\nAborting");
      exit(1);
#   endif
    Load_Data(argv[1]);
    if (argc > 3) {
      if ((ItersAllowed = atoi(argv[3])) < 0) Info_Exit();
    } else ItersAllowed = VectLen * XCnt;

    if (Trace) Dump_Data("After Load");
    if (argc == 5) Load_Parms(argv[4]); else Set_Default_Parms();
    Make_Room();
    if (!(f=fopen(argv[2],"w"))) Info_Exit();
    fclose(f);
    Set_c_and_b0();

    XSave = _fmalloc(VectLen*XCnt*sizeof(double)); ALLCHK(XSave)
    XWorking = _fmalloc(VectLen*XCnt*sizeof(double)); ALLCHK(XWorking)
    BestC = malloc(VectLen*VectLen*2*sizeof(double)); ALLCHK(BestC)
    CSave = malloc(VectLen*VectLen*2*sizeof(double)); ALLCHK(CSave)
    PartC = malloc(VectLen*VectLen*2*sizeof(double)); ALLCHK(PartC)
    PartBar = malloc(VectLen*sizeof(double)); ALLCHK(PartBar)
    BestXBarJ = malloc(VectLen*sizeof(double)); ALLCHK(BestXBarJ)
    Permutation = malloc(XCnt * sizeof(int)); ALLCHK(Permutation)

    PartitionCnt = (int)(XCnt/Lambda); if (PartitionCnt <= 0) PartitionCnt = 1;
    XCntSave = XCnt;
    Copy(XSave, X, XCnt * VectLen);
    /* randomize the rows of X to make X working */
    /* column major is a pain....*/
    Generate_Permutation(XCnt, Permutation);
    for (j=1; j<=XCnt; j++) for (k=1; k<=VectLen; k++) 
        XWorkingof(j,k) = Xof((*(Permutation+j-1)),k);

    for (Part=0; Part<PartitionCnt; Part++) {
        printf("Begin Partition Cell %d\n",Part+1);
        XCnt = XCntSave / PartitionCnt;
        /* this is where that ridiculous column major idea hurts a little ...*/
        for (j=1; j<=XCnt; j++) for (k=1; k<=VectLen; k++) 
            Xof(j,k) = XWorkingof(j+Part*XCntSave/PartitionCnt, k); 
        Partition_Main(ItersAllowed/PartitionCnt);
        /* now iterate from the optimal */
        /* (remember that indexes in the local (random) X are not valid in X)*/
        Copy(JBits, BestJBits, XCnt); 
        JCnt = 0; for (i=0; i < XCnt; i++) JCnt += *(JBits+i);
        Form_XJ();
        Compute_XBarJ(JCnt);
        Form_C(JCnt);
        M_Iterate();
        Copy(PartC, C, VectLen*VectLen*2);
        Copy(PartBar, XBarJ, VectLen);
        /*use entire sample to a get obj value and save the best C and XBARJ*/
        Copy(X, XSave, XCnt * VectLen);
        XCnt = XCntSave;

        Forward(&JCnt);
        M_Iterate();
        Copy(CSave, C, VectLen*VectLen*2);
        if (Trace) Dump_XBarJ("after iteration on all data");
         InvertC(C, VectLen, &Determinant);
         Compute_Distance_Vector();  /* needed to compute k */
         ObjectiveValue = Determinant * pow((double)Compute_k(), (double)(2. * VectLen));
        if (Trace) printf("Partition ObjectiveValue=%lf\n",ObjectiveValue);
        if (ObjectiveValue < MainBestObj) {
            MainBestObj = ObjectiveValue;
            Copy(BestC, CSave, VectLen*VectLen*2);
            Copy(BestXBarJ, XBarJ, VectLen);
	}
        Copy(C, PartC,VectLen*VectLen*2);
        Copy(XBarJ, PartBar, VectLen);
        /***** duplicate to allow with and without forward */
        M_Iterate();
        Copy(CSave, C, VectLen*VectLen*2);
        if (Trace) Dump_XBarJ("after non-forward iteration on all data again");
         InvertC(C, VectLen, &Determinant);
         Compute_Distance_Vector();  /* needed to compute k */
         ObjectiveValue = Determinant * pow((double)Compute_k(), (double)(2. * VectLen));
        if (Trace) printf("Partition ObjectiveValue=%lf\n",ObjectiveValue);
        if (ObjectiveValue < MainBestObj) {
            MainBestObj = ObjectiveValue;
            Copy(BestC, CSave, VectLen*VectLen*2);
            Copy(BestXBarJ, XBarJ, VectLen);
	}
/*  end dupl */
    }
    _ffree(XWorking);
    Copy(C, BestC,VectLen*VectLen*2);
    Copy(XBarJ, BestXBarJ, VectLen);
    Copy(X, XSave, XCnt * VectLen);
    _ffree(XSave);
    XCnt = XCntSave;
    if (Trace) Dump_XBarJ("best partition");
    Write_First_Results(argv[2], argv[1]); /* needs C intact */
    InvertC(C, VectLen, &Determinant);
    Compute_Distance_Vector();   /* can get wrecked */
    printf("Analysis report written to %s.\n",argv[2]);
    printf("Beginning outlier detection.\n");
    CutDist = ID_Good(Cut1, Cut2);
    Write_Final_Results(argv[2], CutDist); /* wrecks C */
    printf("Done.\nFinal report written to %s\n",argv[2]);
}
