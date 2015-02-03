/* Test and timing harness program for developing a dense matrix
   multiplication routine for the CS3014 module */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <omp.h>
#include <pthread.h>
#include <xmmintrin.h>

/* the following two definitions of DEBUGGING control whether or not
   debugging information is written out. To put the program into
   debugging mode, uncomment the following line: */
/*#define DEBUGGING(_x) _x */
/* to stop the printing of debugging information, use the following line: */
#define DEBUGGING(_x)

struct complex {
  float real;
  float imag;
};

struct arguments {
  struct complex ** C;
  struct complex ** A;
  struct complex ** B;
  int max;
  int i;
  int j;
};

struct arguments * new_arguments( struct complex ** A, struct complex ** B, struct complex ** C, int i, int j, int max) {
  struct arguments * newArgs = malloc(sizeof(struct arguments));
  newArgs->C = C;
  newArgs->A = A;
  newArgs->B = B;
  newArgs->max = max;
  newArgs->i = i;
  newArgs->j = j;
  return newArgs;
}

void * calculate_element(void * args) {
  struct arguments * arguments = (struct arguments *) args;
  struct complex sum;
  sum.real = 0.0;
  sum.imag = 0.0;
  float *dest = malloc(sizeof(float)*4);
  int k;
  __m128 aPart;
  __m128 bPart;
  __m128 res;
  struct complex product;
  product.real = 0.0;
  product.imag = 0.0;
  for ( k = 0; k < arguments->max; k++ ) {
    // the following code does: sum += A[i][k] * B[k][j];

    aPart = _mm_loadu_ps(&arguments->A[arguments->i][k].real);
    bPart = _mm_loadu_ps(&arguments->B[k][arguments->j].real);
    aPart = _mm_shuffle_ps(aPart,aPart, _MM_SHUFFLE(3,2,3,2));
    bPart = _mm_shuffle_ps(bPart,bPart, _MM_SHUFFLE(3,2,2,3));
    res = _mm_mul_ps(aPart,bPart);

    _mm_store_ps(dest,res);
    sum.real += dest[0] - dest[1];
    sum.imag += dest[2] + dest[3];
  }
  arguments->C[arguments->i][arguments->j] = sum;
  free(dest);
  pthread_exit(NULL);
}

void fastmul(struct complex ** A, struct complex ** B, struct complex ** C, int a_dim1, int a_dim2, int b_dim2)
{
  int numberOfThreads = a_dim1 * b_dim2;
  pthread_t threads[numberOfThreads];
  struct arguments ** threadArguments = malloc(sizeof(struct arguments) *  numberOfThreads);
  int threadArgIndex = 0;
  int i, j, k;

  for ( i = 0; i < a_dim1; i++ ) {
    for( j = 0; j < b_dim2; j++ ) {
      threadArguments[threadArgIndex] = new_arguments(A,B,C,i,j,a_dim2);
      pthread_create(&threads[threadArgIndex], NULL, calculate_element, (void *)threadArguments[threadArgIndex]);
      threadArgIndex++;
    }
  }

  for(i=0; i < numberOfThreads; i++) {
    pthread_join(threads[i], NULL);
  }
}


/* write matrix to stdout */
void write_out(struct complex ** a, int dim1, int dim2)
{
  int i, j;

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2 - 1; j++ ) {
      printf("%f + %fi ", a[i][j].real, a[i][j].imag);
    }
    printf("%f +%fi\n", a[i][dim2-1].real, a[i][dim2-1].imag);
  }
}


/* create new empty matrix */
struct complex ** new_empty_matrix(int dim1, int dim2)
{
  struct complex ** result = malloc(sizeof(struct complex*) * dim1);
  struct complex * new_matrix = malloc(sizeof(struct complex) * dim1 * dim2);
  int i;

  for ( i = 0; i < dim1; i++ ) {
    result[i] = &(new_matrix[i*dim2]);
  }

  return result;
}

/* take a copy of the matrix and return in a newly allocated matrix */
struct complex ** copy_matrix(struct complex ** source_matrix, int dim1, int dim2)
{
  int i, j;
  struct complex ** result = new_empty_matrix(dim1, dim2);

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      result[i][j] = source_matrix[i][j];
    }
  }

  return result;
}

/* create a matrix and fill it with random numbers */
struct complex ** gen_random_matrix(int dim1, int dim2)
{
  struct complex ** result;
  int i, j;
  struct timeval seedtime;
  int seed;

  result = new_empty_matrix(dim1, dim2);

  /* use the microsecond part of the current time as a pseudorandom seed */
  gettimeofday(&seedtime, NULL);
  seed = seedtime.tv_usec;
  srandom(seed);

  /* fill the matrix with random numbers */
  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      long long upper = random();
      long long lower = random();
      result[i][j].real = (float)((upper << 32) | lower);
      upper = random();
      lower = random();
      result[i][j].imag = (float)((upper << 32) | lower);
    }
  }

  return result;
}

/* check the sum of absolute differences is within reasonable epsilon */
void check_result(struct complex ** result, struct complex ** control, int dim1, int dim2)
{
  int i, j;
  double sum_abs_diff = 0.0;
  const double EPSILON = 0.0625;

  for ( i = 0; i < dim1; i++ ) {
    for ( j = 0; j < dim2; j++ ) {
      double diff;
      diff = abs(control[i][j].real - result[i][j].real);
      sum_abs_diff = sum_abs_diff + diff;
      diff = abs(control[i][j].imag - result[i][j].imag);
      sum_abs_diff = sum_abs_diff + diff;
    }
  }

  if ( sum_abs_diff > EPSILON ) {
    fprintf(stderr, "WARNING: sum of absolute differences (%f) > EPSILON (%f)\n",
	    sum_abs_diff, EPSILON);
  }
}

/* multiply matrix A times matrix B and put result in matrix C */
void matmul(struct complex ** A, struct complex ** B, struct complex ** C, int a_dim1, int a_dim2, int b_dim2)
{
  int i, j, k;

  for ( i = 0; i < a_dim1; i++ ) {
    for( j = 0; j < b_dim2; j++ ) {
      struct complex sum;
      sum.real = 0.0;
      sum.imag = 0.0;
      for ( k = 0; k < a_dim2; k++ ) {
          // the following code does: sum += A[i][k] * B[k][j];
          struct complex product;
          product.real = A[i][k].real * B[k][j].real - A[i][k].imag * B[k][j].imag;
          product.imag = A[i][k].real * B[k][j].imag + A[i][k].imag * B[k][j].real;
          sum.real += product.real;
          sum.imag += product.imag;
      }
      C[i][j] = sum;
    }
  }
}

/* the fast version of matmul written by the team */
void team_matmul(struct complex ** A, struct complex ** B, struct complex ** C, int a_dim1, int a_dim2, int b_dim2)
{
  // this call here is just dummy code
  // insert your own code instead
  fastmul(A, B, C, a_dim1, a_dim2, b_dim2);
  //matmul(A, B, C, a_dim1, a_dim2, b_dim2);
}

void print_matrix(struct complex ** mat, int rows, int columns) {
  int i;
  for(i=0; i < rows; i++) {
    int j;
    for(j=0; j < columns; j++) {
      if(j==0) {
        printf("R: %f    I: %f",mat[i][j].real, mat[i][j].imag);
      } else {
        printf("          R: %f    I: %f",mat[i][j].real, mat[i][j].imag);
      }
    }
    printf("\n");
  }

  printf("\n\n\n");

}

int main(int argc, char ** argv)
{
  struct complex ** A, ** B, ** C;
  struct complex ** control_matrix;
  long long mul_time;
  int a_dim1, a_dim2, b_dim1, b_dim2;
  struct timeval start_time;
  struct timeval stop_time;

  if ( argc != 5 ) {
    fprintf(stderr, "Usage: matmul-harness <A nrows> <A ncols> <B nrows> <B ncols>\n");
    exit(1);
  }
  else {
    a_dim1 = atoi(argv[1]);
    a_dim2 = atoi(argv[2]);
    b_dim1 = atoi(argv[3]);
    b_dim2 = atoi(argv[4]);
  }

  /* check the matrix sizes are compatible */
  if ( a_dim2 != b_dim1 ) {
    fprintf(stderr,
	    "FATAL number of columns of A (%d) does not match number of rows of B (%d)\n",
	    a_dim2, b_dim1);
    exit(1);
  }

  /* allocate the matrices */
  A = gen_random_matrix(a_dim1, a_dim2);
  print_matrix(A,a_dim1, a_dim2);
  B = gen_random_matrix(b_dim1, b_dim2);
  C = new_empty_matrix(a_dim1, b_dim2);
  control_matrix = new_empty_matrix(a_dim1, b_dim2);

  DEBUGGING(write_out(A, a_dim1, a_dim2));

  /* use a simple matmul routine to produce control result */
  matmul(A, B, control_matrix, a_dim1, a_dim2, b_dim2);
  //fastmul(A, B, control_matrix, a_dim1, a_dim2, b_dim2);

  /* record starting time */
  gettimeofday(&start_time, NULL);

  /* perform matrix multiplication */
  team_matmul(A, B, C, a_dim1, a_dim2, b_dim2);

  /* record finishing time */
  gettimeofday(&stop_time, NULL);
  mul_time = (stop_time.tv_sec - start_time.tv_sec) * 1000000L +
    (stop_time.tv_usec - start_time.tv_usec);
  printf("Matmul time: %lld microseconds\n", mul_time);

  DEBUGGING(write_out(C, a_dim1, b_dim2));

  /* now check that the team's matmul routine gives the same answer
     as the known working version */
  check_result(C, control_matrix, a_dim1, b_dim2);



  return 0;
}
