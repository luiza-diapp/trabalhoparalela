#define _XOPEN_SOURCE 600
// TRABALHO1: ci1316 2o semestre 2025
// Aluno1:                            GRR:
// Aluno2:                            GRR:

	///////////////////////////////////////
	///// ATENCAO: NAO MUDAR O MAIN   /////
        ///////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>


#include "chrono.c"

//#define DEBUG 2      // defina 2 para ver debugs
#define DEBUG 0

//#define SEQUENTIAL_VERSION 1      // ATENÇAO: COMENTAR esse #define para rodar o seu codigo paralelo
                
#define MAX_THREADS 64

#define LONG_LONG_T  1
#define DOUBLE_T     2
#define UINT_T        3

// ESCOLHA o tipo dos elementos usando o #define MY_TYPE adequado abaixo
//    a fazer a SOMA DE PREFIXOS:
//#define MY_TYPE LONG_LONG_T        // OBS: o enunciado pedia ESSE (long long)
//#define MY_TYPE DOUBLE_T 
#define MY_TYPE LONG_LONG_T

#if MY_TYPE == LONG_LONG_T
   #define TYPE long long
   #define TYPE_NAME  "long long"
   #define TYPE_FORMAT "%lld"
#elif MY_TYPE == DOUBLE_T
   #define TYPE double
   #define TYPE_NAME  "double"
   #define TYPE_FORMAT "%F"   
#elif MY_TYPE == UINT_T
   #define TYPE unsigned int   
   #define TYPE_NAME  "unsigned int"
   #define TYPE_FORMAT "%u"
   // OBS: para o algoritmo da soma de prefixos
   //  o tipo unsigned int poderá usados para medir tempo APENAS como referência 
   //  pois nao conseguem precisao adequada ou estouram capacidade 
   //  para quantidades razoaveis de elementos
#endif

#define MAX_TOTAL_ELEMENTS (500 * 1000 * 1000) // obs: esse será um tamanho máximo alocado no programa
                                               // para o caso do tipo long long que tem
                                               // 8 bytes, isso daria um vetor de
                                               //    8 * 500 * 1000 * 1000 bytes = 4 Bilhoes de bytes
                                               // então cabe em máquina de 8 GB de RAM
                                                   
int nThreads;		// numero efetivo de threads
			// obtido da linha de comando
int nTotalElements;  // numero total de elementos
		    // obtido da linha de comando

// a pointer to the GLOBAL Vector that will by processed by the threads
//   this will be allocated by malloc
volatile TYPE *Vector;	// will use malloc e free to allow large (>2GB) vectors


chronometer_t parallelPrefixSumTime;
chronometer_t memcpyTime;

volatile TYPE partialSum[MAX_THREADS];
   
int min(int a, int b)
{
	if (a < b)
		return a;
	else
		return b;
}

void verifyPrefixSum( const TYPE *InputVec,       // original (initial) vector
                      volatile TYPE *prefixSumVec,   // prefixSum vector to be verified for correctness
                      long nTotalElmts )
{
    volatile TYPE last = InputVec[0];
    int ok = 1;
    for( long i=1; i<nTotalElmts ; i++ ) {
           if( prefixSumVec[i] != (InputVec[i] + last) ) {
               fprintf( stderr, "In[%ld]= " TYPE_FORMAT "\n"
                                "Out[%ld]= " TYPE_FORMAT " (wrong result!)\n"
                                "Out[%ld]= " TYPE_FORMAT " (ok)\n"
                                "last=" TYPE_FORMAT "\n" , 
                                     i, InputVec[i],
                                     i, prefixSumVec[i],
                                     i-1, prefixSumVec[i-1],
                                     last );
               ok = 0;
               break;
           }
           last = prefixSumVec[i];    
    }  
    if( ok )
       printf( "\nPrefix Sum verified correctly.\n" );
    else
       printf( "\nPrefix Sum DID NOT compute correctly!!!\n" );   
}


void sequentialPrefixSum( volatile TYPE *Vec, 
                          long nTotalElmts,
                          int nThreads )
{
    TYPE last = Vec[0];
    int ok = 1;
    for( long i=1; i<nTotalElmts ; i++ )
           Vec[i] += Vec[i-1];
}  

// === Pool helpers for ParallelPrefixSumPth (Lab v1.2) ============================
// Usar APENAS 1 barreira e NENHUM mutex/cond/atomics.
// Cada thread calcula sua soma parcial, espera na barreira única e depois
// escreve a soma de prefixos IN-PLACE na sua faixa.

#ifndef CI1316_PREFIX_SUM_HELPERS_ADDED
#define CI1316_PREFIX_SUM_HELPERS_ADDED

// MAX_THREADS, TYPE e partialSum[] já existem no arquivo do professor.
static pthread_barrier_t gBarrier;  // barreira única

typedef struct {
    int  tid;        // [0..nThreads-1]
    long begin;      // índice inicial (inclusive)
    long end;        // índice final (exclusivo)
    volatile TYPE *Vec; // vetor in-place
    int  nThreads;   // apenas informativo
} ThreadCtxPS;

// Divide [0..nTotal) em P faixas contíguas quase iguais.
static inline void ps_compute_chunk(long nTotal, int P, int tid, long *L, long *R) {
    long base = nTotal / P;
    long rem  = nTotal % P;
    long start = tid * base + (tid < rem ? tid : rem);
    long len   = base + (tid < rem ? 1 : 0);
    *L = start;
    *R = start + len;
}

// worker da thread (duas fases separadas por UMA barreira)
static void* ps_worker(void *arg) {
    ThreadCtxPS *ctx = (ThreadCtxPS*)arg;
    const int  tid = ctx->tid;
    const long L   = ctx->begin;
    const long R   = ctx->end;
    volatile TYPE *Vec = ctx->Vec;

    // FASE 1: apenas lê sua faixa e acumula soma parcial
    TYPE s = 0;
    for (long i = L; i < R; ++i) {
        s += Vec[i];
    }
    partialSum[tid] = s;  // uma célula por thread

    // BARREIRA ÚNICA: garante que partialSum está completo
    pthread_barrier_wait(&gBarrier);

    // FASE 2: soma prefixo das somas parciais anteriores e escreve IN-PLACE
    TYPE myPrefix = 0;
    for (int k = 0; k < tid; ++k) myPrefix += partialSum[k];

    TYPE run = 0;
    for (long i = L; i < R; ++i) {
        run += Vec[i];              // ainda lê os valores originais
        ((TYPE*)Vec)[i] = myPrefix + run; // escreve resultado final
    }
    return NULL;
}

#endif // CI1316_PREFIX_SUM_HELPERS_ADDED
// ================================================================================

void ParallelPrefixSumPth( volatile TYPE *Vec, 
                           long nTotalElmts,
                           int nThreads )
{
   pthread_t Thread[MAX_THREADS];
   int my_thread_id[MAX_THREADS];
   

   // criar o POOL de threads aqui!
// (sem mutex/cond/atomics; apenas uma barreira e joins)

if (nThreads < 1) nThreads = 1;
if (nThreads > MAX_THREADS) nThreads = MAX_THREADS;

// Inicializa a barreira com nThreads participantes
pthread_barrier_init(&gBarrier, NULL, (unsigned)nThreads);

// Contextos e criação das threads do pool
ThreadCtxPS ctx[MAX_THREADS];
for (int tid = 0; tid < nThreads; ++tid) {
    long L, R;
    ps_compute_chunk(nTotalElmts, nThreads, tid, &L, &R);
    ctx[tid].tid    = tid;
    ctx[tid].begin  = L;
    ctx[tid].end    = R;
    ctx[tid].Vec    = Vec;
    ctx[tid].nThreads = nThreads;
    pthread_create(&Thread[tid], NULL, ps_worker, &ctx[tid]);
}

// Espera o pool terminar (as duas fases estão dentro do worker)
for (int tid = 0; tid < nThreads; ++tid) {
    pthread_join(Thread[tid], NULL);
}

// Destrói barreira
pthread_barrier_destroy(&gBarrier);

   
}

int main(int argc, char *argv[])
{
	long i;
	
	///////////////////////////////////////
	///// ATENCAO: NAO MUDAR O MAIN   /////
        ///////////////////////////////////////

	if (argc != 3)
	{
		printf("usage: %s <nTotalElements> <nThreads>\n",
			   argv[0]);
		return 0;
	}
	else
	{
		nThreads = atoi(argv[2]);
		if (nThreads == 0)
		{
			printf("usage: %s <nTotalElements> <nThreads>\n",
				   argv[0]);
			printf("<nThreads> can't be 0\n");
			return 0;
		}
		if (nThreads > MAX_THREADS)
		{
			printf("usage: %s <nTotalElements> <nThreads>\n",
				   argv[0]);
			printf("<nThreads> must be less than %d\n", MAX_THREADS);
			return 0;
		}
		nTotalElements = atoi(argv[1]);
		if (nTotalElements > MAX_TOTAL_ELEMENTS)
		{
			printf("usage: %s <nTotalElements> <nThreads>\n",
				   argv[0]);
			printf("<nTotalElements> must be up to %d\n", MAX_TOTAL_ELEMENTS);
			return 0;
		}
	}

        // allocate the GLOBAL Vector that will by processed by the threads  
        Vector = (TYPE *) malloc( nTotalElements*sizeof(TYPE) );
        if( Vector == NULL )
            printf("Error allocating working Vector of %d elements (size=%ld Bytes)\n",
                     nTotalElements, nTotalElements*sizeof(TYPE) );
        // allocate space for the initial vector 
        TYPE *InitVector = (TYPE *) malloc( nTotalElements*sizeof(TYPE) );
        if( InitVector == NULL )
            printf("Error allocating initVector of %d elements (size=%ld Bytes)\n",
                     nTotalElements, nTotalElements*sizeof(TYPE) );


//    #if DEBUG >= 2 
        // Print INFOS about the prefix sum algorithm
        printf( "Using PREFIX SUM of TYPE %s\n", TYPE_NAME );
        
	/*printf("reading inputs...\n");
	for (int i = 0; i < nTotalElements; i++)
	{
		scanf("%ld", &Vector[i]);
	}*/
	
	// initialize InputVector
	//srand(time(NULL));   // Initialization, should only be called once.
        
        int r;
	for (long i = 0; i < nTotalElements; i++){
	        r = rand();  // Returns a pseudo-random integer
	                     //    between 0 and RAND_MAX.
		InitVector[i] = (r % 10);
		// Vector[i] = 1; // USE 1 FOR debug only
	}

	printf("\n\nwill use %d threads to calculate prefix-sum of %d total elements\n", 
	                     nThreads, nTotalElements);

        chrono_reset( &memcpyTime );
        
	chrono_reset( &parallelPrefixSumTime );
	chrono_start( &parallelPrefixSumTime );

            ////////////////////////////
            // call it N times
            #define NTIMES 1000
            for( int i=0; i<NTIMES ; i++ ) {
            
                // make a copy, measure time taken
                chrono_start( &memcpyTime );
                   memcpy( (void *)Vector, (void *)InitVector, nTotalElements * sizeof(TYPE) );
                chrono_stop( &memcpyTime );
                
                #ifdef SEQUENTIAL_VERSION
                   sequentialPrefixSum( Vector, nTotalElements, nThreads );
                #else
                   // run your ParallelPrefixSumPth algorithm (with thread pool)
                   ParallelPrefixSumPth( Vector, nTotalElements, nThreads );
                #endif   
            }     
      
	// Measuring time of the parallel algorithm 
	//   to AVOID the NTIMES overhead of threads creation and joins...
	//   ... USE your ParallelPrefixSumPth algorithm (with thread POOL above)
	//   
	chrono_stop(&parallelPrefixSumTime);
	// DESCONTAR o tempo das memcpys no cronometro ...
	//   ... pois só queremos saber o tempo do algoritmo de prefixSum
        chrono_decrease(&parallelPrefixSumTime, chrono_gettotal(&memcpyTime) );

        // reportar o tempo após o desconto dos memcpys
	chrono_reportTime(&parallelPrefixSumTime, "parallelPrefixSumTime");
        
	// calcular e imprimir a VAZAO (numero de operacoes/s)
	// descontar o tempo das memcpys pois só queremos saber o tempo do algoritmo de prefixSum
	double total_time_in_seconds = 
	       (double)chrono_gettotal(&parallelPrefixSumTime) / ((double)1000 * 1000 * 1000);
								   
	printf("total_time_in_seconds: %lf s for %d prefix-sum ops\n", 
	           total_time_in_seconds, NTIMES );

	double OPS = ((long)nTotalElements * NTIMES) / total_time_in_seconds;
	printf("Throughput: %lf OP/s\n", OPS);

        ////// RUN THE VERIFICATION ALGORITHM /////
	////////////
        verifyPrefixSum( InitVector, Vector, nTotalElements );
        //////////
        
        // imprimir o tempo total gasto em memcpys
        chrono_reportTime(&memcpyTime, "memcpyTime");
    
    //#if NEVER
    #if DEBUG >= 2 
        // Print InputVector
        printf( "In: " );
	for (int i = 0; i < nTotalElements; i++){
		printf( "%lld ", InitVector[i] );

	}
	printf( "\n" );

    	// Print the result of the prefix Sum
    	printf( "Out: " );
	for (int i = 0; i < nTotalElements; i++){
		printf( "%lld ", Vector[i] );
	}
	printf( "\n" );
    #endif
	return 0;
}
