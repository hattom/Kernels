/*
Copyright (c) 2013, Intel Corporation

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions 
are met:

* Redistributions of source code must retain the above copyright 
      notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above 
      copyright notice, this list of conditions and the following 
      disclaimer in the documentation and/or other materials provided 
      with the distribution.
* Neither the name of Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products 
      derived from this software without specific prior written 
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN 
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
POSSIBILITY OF SUCH DAMAGE.
*/

/*******************************************************************

NAME:    Reduce

PURPOSE: This program tests the efficiency with which a collection of 
         vectors that are distributed among the processes can be added in
         elementwise fashion. The number of vectors per process is two, 
         so that a reduction will take place even if the code runs on
         just a single process.
  
USAGE:   The program takes as input the length of the vectors, plus the
         number of times the reduction is repeated.

               <progname> <# iterations><#vector pairs><vector length>
  
         The output consists of diagnostics to make sure the 
         algorithm worked, and of timing statistics.

FUNCTIONS CALLED:

         Other than OCR or standard C functions, the following external 
         functions are used in this program:

         wtime();
         bail_out();

HISTORY: Written by Rob Van der Wijngaart, May 2014.
  
*******************************************************************/

#include "stdio.h"
#include <par-res-kern_general.h>
#include <par-res-kern_ocr.h>

ocrGuid_t VectorAllocator(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
  // create a vector pair as a data block
  int        vector_length, seqno, i;
  ocrGuid_t dbGuid;
  u8         error;
  void *addr;
  vector_length = (int) paramv[0];
  ocrGuid_t EventGuid = (ocrGuid_t) paramv[1];
  error = ocrDbCreate(&dbGuid, &addr, vector_length*2*sizeof(double), 0/*flags*/,
                      NULL_GUID, NO_ALLOC);
  fprintf(stderr, "Created data block with GUID %p\n", (void*)dbGuid);
  ocrEventSatisfy(EventGuid, dbGuid);
  return((ocrGuid_t)error);
}
 
ocrGuid_t VectorInitializer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
  // create a vector pair as a data block
  int        vector_length, seqno, i;
  ocrGuid_t dbGuid;
  u8         error;
  void *addr;
  vector_length = (int) paramv[0];
  seqno         = (int) paramv[1];
  ocrGuid_t EventGuid = (ocrGuid_t) paramv[2];
  for (i=0; i<vector_length*2; i++)   ((double *)depv[0].ptr)[i] = seqno+1;
  fprintf(stderr, "Initialized data block %d\n", seqno);
  ocrEventSatisfy(EventGuid, depv[0].guid);
  return((ocrGuid_t)error);
}
 
ocrGuid_t LocalReducer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
  // combine "local" pair of vectors into a single vector
  int        vector_length, i;
  ocrGuid_t dbGuid;

  vector_length       = (int)       paramv[0];
  ocrGuid_t EventGuid = (ocrGuid_t) paramv[1];
  for (i=0; i<vector_length; i++) 
    ((double *)(depv[0].ptr))[i] += ((double *)(depv[0].ptr))[i+vector_length];
  fprintf(stderr, "Combined vector pair and satisfied event guid %p\n", (void*)EventGuid);
  ocrEventSatisfy(EventGuid, depv[0].guid);
  return(NULL_GUID);
}

ocrGuid_t GlobalSerialReducer(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
  // combine all locally reduced vectors into a single vector
  int        vector_length, i, seqno, npairs;
  ocrGuid_t dbGuid;

  vector_length       = (int)       paramv[0];
  ocrGuid_t EventGuid = (ocrGuid_t) paramv[1];
  npairs              = (int)       paramv[2];
  
  for (seqno=1; seqno<npairs; seqno++) {
    for (i=0; i<vector_length; i++)
      ((double *)(depv[0].ptr))[i] += ((double *)(depv[seqno].ptr))[i];
    fprintf(stderr, "Combined vectors 0 and %d\n", seqno);
  }
  ocrEventSatisfy(EventGuid, depv[0].guid);
  fprintf(stderr, "Satisfied event guid %p\n", (void*)EventGuid);
  return(NULL_GUID);  
}
 
ocrGuid_t WEDT(u32 paramc, u64* paramv, u32 depc, ocrEdtDep_t depv[]) {
  fprintf(stderr, "executing wrapup EDT\n");
  int i, npairs, vector_length, error=0;
  double *vector;
  double epsilon=1.e-8; /* error tolerance                                   */
  double element_value; /* verification value                                */

  npairs        = (int) paramv[0];
  vector_length = (int) paramv[1];
  vector        = (double *)(depv[0].ptr);
  element_value = npairs*(npairs+1.0);

  for (i=0; i<vector_length; i++) {
     if (ABS(vector[i] - element_value) >= epsilon) {
        error = 1;
#ifdef VERBOSE
        fprintf(stderr, "ERROR at i=%d; value: %lf; reference value: %lf\n",
                i, vector[i], element_value);
#else
        fprintf(stderr, "First error at i=%d; value: %lf; reference value: %lf\n",
               i, vector[i], element_value);
        break;
#endif
     }
  }
  if (!error) {
    printf("Solution validates\n");
#ifdef VERBOSE
    fprintf(stderr, "Element verification value: %lf\n", element_value);
#endif
  }

  ocrShutdown();
  return(NULL_GUID);
}


int main(int argc, const char ** argv)
{
  int npairs;           /* Number of vector pairs                            */
  int Num_procs;        /* Number of processors                              */
  int root=0;
  int iterations;       /* number of times the reduction is carried out      */
  int i, iter;          /* dummies                                           */
  int vector_length;    /* length of the vectors to be aggregated            */
  double * RESTRICT vector; /* vector to be reduced                          */
  double reduce_time,   /* timing parameters                                 */
         avgtime = 0.0, 
         maxtime = 0.0, 
         mintime = 366.0*8760.0*3600.0; /* set the minimum time to a large 
                             value; one leap year should be enough           */
  u8     error = 0;     /* error flag                                        */
  u64    AllocatorParamv[2];
  u64    InitializerParamv[3];
  u64    LocalReducerParamv[2];
  u64    GlobalSerialReducerParamv[3];
  u64    WEDTParamv[2];
  ocrConfig_t ocrConfig;
  ocrGuid_t *AllocatorOEGuid, *InitializerOEGuid, 
            *LocalReducerOEGuid, GlobalSerialReducerOEGuid;
  ocrGuid_t AllocatorTemplateGuid, InitializerTemplateGuid, 
            LocalReducerTemplateGuid, GlobalSerialReducerTemplateGuid;
  ocrGuid_t WrapupTemplateGuid;
  ocrGuid_t throwawayGuid;

  /***************************************************************************
  ** Initialize the OCR environment
  ****************************************************************************/
  ocrParseArgs(argc, argv, &ocrConfig);

  // Set up up the runtime
  ocrInit(&ocrConfig);

  /***************************************************************************
  ** process, test and broadcast input parameters
  ****************************************************************************/

  if (argc != 4){
    printf("Usage: %s <# iterations> <#vector pairs><vector_length>\n", *argv);
    exit(666);
  }

  iterations    = atoi(*++argv);
  if (iterations < 1) {
    printf("ERROR: Iterations must be positive: %d\n", iterations);
    exit(666);
  }

  npairs    = atoi(*++argv);
  if (npairs < 1) {
    printf("ERROR: Number of vector pairs must be positive: %d\n", npairs);
    exit(666);
  }

  vector_length = atoi(*++argv);
  if (vector_length < 1) {
    printf("ERROR: Vector length should be positive: %d\n", vector_length);
    exit(666);
  }

  printf("OCR Vector Reduction\n");
  printf("Number of vector pairs  = %d\n", npairs);
  printf("Vector length           = %d\n", vector_length);
  printf("Number of iterations    = %d\n", iterations);     

  /* allocate arrays used to hold events signaling completion of steps in 
     the setup and execution of the vector reduction                       */
  AllocatorOEGuid    = (ocrGuid_t *) malloc(npairs*sizeof(ocrGuid_t));
  InitializerOEGuid  = (ocrGuid_t *) malloc(npairs*sizeof(ocrGuid_t));
  LocalReducerOEGuid = (ocrGuid_t *) malloc(npairs*sizeof(ocrGuid_t));

  ocrEdtTemplateCreate(&AllocatorTemplateGuid, VectorAllocator, 2 /*paramc*/, 0 /*depc*/);
  AllocatorParamv[0] = (u64) vector_length;

  ocrEdtTemplateCreate(&LocalReducerTemplateGuid, LocalReducer, 2 /*paramc*/, 1 /*depc*/);
  LocalReducerParamv[0] = (u64) vector_length;
  
  ocrEdtTemplateCreate(&InitializerTemplateGuid, VectorInitializer, 3 /*paramc*/, 1 /*depc*/);
  InitializerParamv[0] = (u64) vector_length;
  
  ocrEdtTemplateCreate(&GlobalSerialReducerTemplateGuid, GlobalSerialReducer, 
                       3 /*paramc*/, npairs /*depc*/);
  GlobalSerialReducerParamv[0] = (u64) vector_length;
  GlobalSerialReducerParamv[2] = (u64) npairs;

  for (i=0; i<npairs; i++) {
    ocrEventCreate(&AllocatorOEGuid[i], OCR_EVENT_ONCE_T, false);
    AllocatorParamv[1] = (u64) AllocatorOEGuid[i];
    ocrEdtCreate(&throwawayGuid, AllocatorTemplateGuid, 
                 EDT_PARAM_DEF, AllocatorParamv, EDT_PARAM_DEF, 
                 NULL, 0, NULL_GUID, NULL);
    fprintf(stderr, "Allocator guid %d = %p\n", i, (void*)throwawayGuid);
  }

  for (iter=0; iter<iterations; iter++) {

     for (i=0; i<npairs; i++) {
       ocrEventCreate(&InitializerOEGuid[i], OCR_EVENT_ONCE_T, false);
       InitializerParamv[1] = (u64) i;
       InitializerParamv[2] = (u64) InitializerOEGuid[i];
       ocrEdtCreate(&throwawayGuid,InitializerTemplateGuid, 
                    EDT_PARAM_DEF, InitializerParamv, EDT_PARAM_DEF, 
                    &AllocatorOEGuid[i], 0, NULL_GUID, NULL);
       fprintf(stderr, "Initializer guid %d = %p\n", i, (void*)throwawayGuid);
     }
   
     printf("Do serial aggregation\n");
     for (i=0; i<npairs; i++) {
       ocrEventCreate(&LocalReducerOEGuid[i], OCR_EVENT_ONCE_T, false);
       LocalReducerParamv[1] = (u64) LocalReducerOEGuid[i];
       ocrEdtCreate(&throwawayGuid, LocalReducerTemplateGuid, 
                    EDT_PARAM_DEF, LocalReducerParamv, EDT_PARAM_DEF, 
                    &InitializerOEGuid[i], 0, NULL_GUID, NULL);
     }
   
     ocrEventCreate(&GlobalSerialReducerOEGuid, OCR_EVENT_ONCE_T, false);
     GlobalSerialReducerParamv[1] = (u64) GlobalSerialReducerOEGuid;
   
     ocrEdtCreate(&throwawayGuid, GlobalSerialReducerTemplateGuid, 
                  EDT_PARAM_DEF, GlobalSerialReducerParamv, EDT_PARAM_DEF,
                  LocalReducerOEGuid, 0, NULL_GUID, NULL);
  }

  WEDTParamv[0] = (u64) npairs;
  WEDTParamv[1] = (u64) vector_length;
  ocrEdtTemplateCreate(&WrapupTemplateGuid, WEDT, 2 /*paramc*/, 1 /*depc*/);
  ocrEdtCreate(&throwawayGuid, WrapupTemplateGuid, 
               EDT_PARAM_DEF, WEDTParamv, EDT_PARAM_DEF, 
               &GlobalSerialReducerOEGuid, 0, NULL_GUID, NULL);

  ocrEdtTemplateDestroy(InitializerTemplateGuid);
  ocrEdtTemplateDestroy(LocalReducerTemplateGuid);
  ocrEdtTemplateDestroy(GlobalSerialReducerTemplateGuid);
  ocrEdtTemplateDestroy(WrapupTemplateGuid);
  ocrFinalize();

  //  for (iter=0; iter<iterations; iter++) { 
  //
  //    /* initialize the arrays                                                    */
  //    for (i=0; i<vector_length; i++) {
  //      vector[i] = (double)(my_ID+1);
  //      vector[vector_length+i] = (double)(my_ID+1);
  //    }
  //
  //    MPI_Barrier(MPI_COMM_WORLD);
  //    reduce_time = wtime();
  //
  //    /* first do the "local" part                                                */
  //    for (i=0; i<vector_length; i++) {
  //      vector[vector_length+i] += vector[i];
  //    }
  //
  //    /* now do the "non-local" part                                              */
  //    MPI_Reduce(vector+vector_length, vector, vector_length, MPI_DOUBLE, MPI_SUM, 
  //               root, MPI_COMM_WORLD);
  //
  //    if (my_ID == root) {
  //      if (iter>0 || iterations==1) { /* skip the first iteration */
  //        reduce_time = wtime() - reduce_time;
  //        avgtime = avgtime + reduce_time;
  //        mintime = MIN(mintime, reduce_time);
  //        maxtime = MAX(maxtime, reduce_time);
  //      }
  //    }
  //  }
  //
  //  /* verify correctness */
  //  if (my_ID == root) {
  //    element_value = Num_procs*(Num_procs+1.0);
  //
  //    for (i=0; i<vector_length; i++) {
  //      if (ABS(vector[i] - element_value) >= epsilon) {
  //        error = 1;
  //#ifdef VERBOSE
  //        printf("ERROR at i=%d; value: %lf; reference value: %lf\n",
  //               i, vector[i], element_value);
  //#else
  //        printf("First error at i=%d; value: %lf; reference value: %lf\n",
  //               i, vector[i], element_value);
  //        break;
  //#endif
  //      }
  //    }
  //  }
  //  bail_out(error);
  //
  //  if (my_ID == root) {
  //    printf("Solution validates\n");
  //#ifdef VERBOSE
  //    printf("Element verification value: %lf\n", element_value);
  //#endif
  //    avgtime = avgtime/(double)(MAX(iterations-1,1));
  //    printf("Rate (MFlops/s): %lf,  Avg time (s): %lf,  Min time (s): %lf",
  //           1.0E-06 * (2.0*Num_procs-1.0)*vector_length/mintime, avgtime, mintime);
  //    printf(", Max time (s): %lf\n", maxtime);
  //  }
  //
  //  MPI_Finalize();
  //  exit(EXIT_SUCCESS);
  //
  /* end of main */
}
