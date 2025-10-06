#define _XOPEN_SOURCE 600

// TRABALHO1: ci1316 2o semestre 2025
// Aluno1: João Marcelo Caboblo       GRR: 20221227
// Aluno2: Luíza Weigert Diapp        GRR: 20221252

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

//Garantir que o código dentro desse bloco só seja compilado uma vez
#ifndef _PS_HELPERS_DEFINED_
#define _PS_HELPERS_DEFINED_

// Thread Pool + 3 barreiras 
static pthread_barrier_t ps_poolBarrier;   // B1: pool (alinhamento de início)
static pthread_barrier_t ps_algoBarrier;   // B2: entre Fase 1 (partialSum) e Fase 2
static pthread_barrier_t ps_doneBarrier;   // B3: término (garante que todas concluíram)

// Estado global do pool
static int  ps_nThreads = 0;
static long ps_nTotal   = 0;
static int  ps_tid[MAX_THREADS];
static pthread_t ps_threads[MAX_THREADS];

// Divisão de trabalho balanceada [0,N) em P faixas
static inline void ps_compute_chunk(long N, int P, int tid, long *L, long *R){
    long base = N / P; //calcula o tamanho base de cada pedaço
    long rem  = N % P; //calcula o resto da divisão
    long start = tid * base + (tid < rem ? tid : rem); //calcula onde começa o trecho da thread no vetor (adiciona 1 elemento extra para as primeiras rem threads)
    long len   = base + (tid < rem ? 1 : 0); //tamanho do pedaço da thread, as primeiras rm threads pegam base +1 elementos
    *L = start; *R = start + len;
}

// Worker do pool: usa o Vector global e partialSum
static void *ps_worker(void *arg){
    //Pega o número da thread (seu ID lógico) que foi passado como ponteiro no pthread_create.
    int my = *((int*)arg);

    // B1 — começar juntos
    pthread_barrier_wait(&ps_poolBarrier);

    long L,R; ps_compute_chunk(ps_nTotal, ps_nThreads, my, &L, &R);

    // Fase 1: apenas LER a faixa e escrever partialSum[my]
    TYPE sum = 0;
    for(long i=L;i<R;++i) sum += Vector[i];
    partialSum[my] = sum;

    // B2 — garantir que todas preencheram partialSum
    pthread_barrier_wait(&ps_algoBarrier);

    // Fase 2: calcular meu prefixo lendo partialSum[0..my-1] e escrever in-place
    TYPE myPrefix = 0;
    for(int k=0;k<my;++k) myPrefix += partialSum[k];

    TYPE acc = myPrefix;
    for(long i=L;i<R;++i){
        acc += Vector[i];
        ((volatile TYPE*)Vector)[i] = acc;
    }

    // B3 — todas concluem a Fase 2 antes de retornar ao chamador
    pthread_barrier_wait(&ps_doneBarrier);

    return NULL;
}
#endif

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
   

void ParallelPrefixSumPth( volatile TYPE *Vec, 
                           long nTotalElmts,
                           int nThreads ){
   // pthread_t Thread[MAX_THREADS];
   // int my_thread_id[MAX_THREADS];

   if (nThreads < 1) nThreads = 1;
   if (nThreads > MAX_THREADS) nThreads = MAX_THREADS;

   ps_nThreads = nThreads;
   ps_nTotal   = nTotalElmts;

   //Cria barreitas que serão utilizadas no código para sincronia de threads
   pthread_barrier_init(&ps_poolBarrier, NULL, (unsigned)ps_nThreads); //Cria barreita de início já que as threads são detached
   pthread_barrier_init(&ps_algoBarrier, NULL, (unsigned)ps_nThreads); //Barreira depois de todas as threads fazer a soma de todos os elementos de cada uma
   pthread_barrier_init(&ps_doneBarrier, NULL, (unsigned)ps_nThreads); //Verifica que todas as barreiras terminaram 

   //Barreira de atributos
   pthread_attr_t attr; // Declara a variável de atributos
   pthread_attr_init(&attr); //Inicializa com valores padrão.
   pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); //Ajusta configurações específicas para não utilizar join.

   //Inicia todas as threads detached 
   for(int t=1; t<ps_nThreads; ++t){
       ps_tid[t] = t;
       if(pthread_create(&ps_threads[t], &attr, ps_worker, &ps_tid[t]) != 0){
           fprintf(stderr, "Erro: pthread_create\n");
           exit(1);
       }
   }
   //Destrói variável de atributos 
   pthread_attr_destroy(&attr);

   //as threads 1, 2, 3... são criadas com pthread_create; a thread principal (main) é usada como thread 0, sem precisar criar mais uma.
   //todas as threads: executam a mesma função (ps_worker); trabalham ao mesmo tempo; 
   //são sincronizadas com barreiras (começam e terminam juntas) -> isso define uma pool thread
   ps_tid[0] = 0; 
   (void)ps_worker(&ps_tid[0]); 

   //Destrói barreiras criadas anteriormente
   pthread_barrier_destroy(&ps_doneBarrier);
   pthread_barrier_destroy(&ps_algoBarrier);
   pthread_barrier_destroy(&ps_poolBarrier);
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
