#define _POSIX_C_SOURCE 200809L
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define START_TIMER(S) struct timeval start_ ## S , end_ ## S ; gettimeofday(&start_ ## S , NULL);
#define STOP_TIMER(S,T) gettimeofday(&end_ ## S, NULL); T->S += (double)(end_ ## S .tv_sec-start_ ## S.tv_sec)+(double)(end_ ## S .tv_usec-start_ ## S .tv_usec)/1000000;

#include "stdlib.h"
#include "math.h"
#include "sys/time.h"
#include "xmmintrin.h"
#include "pmmintrin.h"
#include "mpi.h"
#include "omp.h"
#include "stdio.h"
#include "unistd.h"

struct dataobj
{
  void *restrict data;
  int * size;
  int * npsize;
  int * dsize;
  int * hsize;
  int * hofs;
  int * oofs;
} ;

struct neighborhood
{
  int lll, llc, llr, lcl, lcc, lcr, lrl, lrc, lrr;
  int cll, clc, clr, ccl, ccc, ccr, crl, crc, crr;
  int rll, rlc, rlr, rcl, rcc, rcr, rrl, rrc, rrr;
} ;

struct profiler
{
  double section0;
  double section1;
  double section2;
} ;

static void sendrecvtxyz(struct dataobj *restrict a0_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, int ogtime, int ogx, int ogy, int ogz, int ostime, int osx, int osy, int osz, int fromrank, int torank, MPI_Comm comm, const int nthreads);
static void gathertxyz(float *restrict buf0_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, struct dataobj *restrict a0_vec, int otime, int ox, int oy, int oz, const int nthreads);
static void scattertxyz(float *restrict buf1_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, struct dataobj *restrict a0_vec, int otime, int ox, int oy, int oz, const int nthreads);
static void haloupdate0(struct dataobj *restrict a0_vec, MPI_Comm comm, struct neighborhood * nb, int otime, const int nthreads);

void save(int nthreads, struct profiler * timers, long int read_size)
{

  int myrank;
  MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

  if (myrank != 0)
  {
    return;
  }

  printf(">>>>>>>>>>>>>> MPI REVERSE <<<<<<<<<<<<<<<<<\n");

  printf("Threads %d\n", nthreads);
  printf("Disks %d\n", 0);

  printf("[REV] Section0 %.2lf s\n", timers->section0);
  printf("[REV] Section1 %.2lf s\n", timers->section1);
  printf("[REV] Section2 %.2lf s\n", timers->section2);

  char name[100];
  sprintf(name, "rev_disks_%d_threads_%d.csv", 0, nthreads);

  FILE *fpt;
  fpt = fopen(name, "w");

  fprintf(fpt,"Disks, Threads, Bytes, [REV] Section0, [REV] Section1, [REV] Section2, [IO] Open, [IO] Read, [IO] Close\n");

  fprintf(fpt,"%d, %d, %ld, %.2lf, %.2lf, %.2lf, %d, %d, %d\n", 0, nthreads, read_size,
        timers->section0, timers->section1, timers->section2, 0, 0, 0);

  fclose(fpt);
}
int Gradient(struct dataobj *restrict damp_vec, const float dt, struct dataobj *restrict grad_vec, const float o_x, const float o_y, const float o_z, struct dataobj *restrict rec_vec, struct dataobj *restrict rec_coords_vec, struct dataobj *restrict u_vec, struct dataobj *restrict v_vec, struct dataobj *restrict vp_vec, const int x_M, const int x_m, const int y_M, const int y_m, const int z_M, const int z_m, const int p_rec_M, const int p_rec_m, const int time_M, const int time_m, const int x0_blk0_size, const int x1_blk0_size, const int y0_blk0_size, const int y1_blk0_size, MPI_Comm comm, struct neighborhood * nb, const int nthreads, const int nthreads_nonaffine, struct profiler * timers)
{
  float (*restrict damp)[damp_vec->size[1]][damp_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[damp_vec->size[1]][damp_vec->size[2]]) damp_vec->data;
  float (*restrict grad)[grad_vec->size[1]][grad_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[grad_vec->size[1]][grad_vec->size[2]]) grad_vec->data;
  float (*restrict rec)[rec_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_vec->size[1]]) rec_vec->data;
  float (*restrict rec_coords)[rec_coords_vec->size[1]] __attribute__ ((aligned (64))) = (float (*)[rec_coords_vec->size[1]]) rec_coords_vec->data;
  float (*restrict u)[u_vec->size[1]][u_vec->size[2]][u_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[u_vec->size[1]][u_vec->size[2]][u_vec->size[3]]) u_vec->data;
  float (*restrict v)[v_vec->size[1]][v_vec->size[2]][v_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[v_vec->size[1]][v_vec->size[2]][v_vec->size[3]]) v_vec->data;
  float (*restrict vp)[vp_vec->size[1]][vp_vec->size[2]] __attribute__ ((aligned (64))) = (float (*)[vp_vec->size[1]][vp_vec->size[2]]) vp_vec->data;

  /* Flush denormal numbers to zero in hardware */
  _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
  _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);

  float r0 = 1.0F/(dt*dt);
  float r1 = 1.0F/dt;

  for (int time = time_M, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3); time >= time_m; time -= 1, t0 = (time)%(3), t1 = (time + 2)%(3), t2 = (time + 1)%(3))
  {
    /* Begin section0 */
    START_TIMER(section0)
    haloupdate0(v_vec,comm,nb,t0,nthreads);
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(2) schedule(dynamic,1)
      for (int x0_blk0 = x_m; x0_blk0 <= x_M; x0_blk0 += x0_blk0_size)
      {
        for (int y0_blk0 = y_m; y0_blk0 <= y_M; y0_blk0 += y0_blk0_size)
        {
          for (int x = x0_blk0; x <= MIN(x0_blk0 + x0_blk0_size - 1, x_M); x += 1)
          {
            for (int y = y0_blk0; y <= MIN(y0_blk0 + y0_blk0_size - 1, y_M); y += 1)
            {
              #pragma omp simd aligned(damp,v,vp:64)
              for (int z = z_m; z <= z_M; z += 1)
              {
                float r10 = 1.0F/(vp[x + 6][y + 6][z + 6]*vp[x + 6][y + 6][z + 6]);
                v[t1][x + 6][y + 6][z + 6] = (r1*damp[x + 1][y + 1][z + 1]*v[t0][x + 6][y + 6][z + 6] + r10*(-r0*(-2.0F*v[t0][x + 6][y + 6][z + 6]) - r0*v[t2][x + 6][y + 6][z + 6]) + 1.77777773e-5F*(v[t0][x + 3][y + 6][z + 6] + v[t0][x + 6][y + 3][z + 6] + v[t0][x + 6][y + 6][z + 3] + v[t0][x + 6][y + 6][z + 9] + v[t0][x + 6][y + 9][z + 6] + v[t0][x + 9][y + 6][z + 6]) + 2.39999994e-4F*(-v[t0][x + 4][y + 6][z + 6] - v[t0][x + 6][y + 4][z + 6] - v[t0][x + 6][y + 6][z + 4] - v[t0][x + 6][y + 6][z + 8] - v[t0][x + 6][y + 8][z + 6] - v[t0][x + 8][y + 6][z + 6]) + 2.39999994e-3F*(v[t0][x + 5][y + 6][z + 6] + v[t0][x + 6][y + 5][z + 6] + v[t0][x + 6][y + 6][z + 5] + v[t0][x + 6][y + 6][z + 7] + v[t0][x + 6][y + 7][z + 6] + v[t0][x + 7][y + 6][z + 6]) - 1.30666663e-2F*v[t0][x + 6][y + 6][z + 6])/(r0*r10 + r1*damp[x + 1][y + 1][z + 1]);
              }
            }
          }
        }
      }
    }
    STOP_TIMER(section0,timers)
    /* End section0 */

    /* Begin section1 */
    START_TIMER(section1)
    #pragma omp parallel num_threads(nthreads_nonaffine)
    {
      int chunk_size = (int)(fmax(1, (1.0F/3.0F)*(p_rec_M - p_rec_m + 1)/nthreads_nonaffine));
      #pragma omp for collapse(1) schedule(dynamic,chunk_size)
      for (int p_rec = p_rec_m; p_rec <= p_rec_M; p_rec += 1)
      {
        float posx = -o_x + rec_coords[p_rec][0];
        float posy = -o_y + rec_coords[p_rec][1];
        float posz = -o_z + rec_coords[p_rec][2];
        int ii_rec_0 = (int)(floor(4.0e-2F*posx));
        int ii_rec_1 = (int)(floor(4.0e-2F*posy));
        int ii_rec_2 = (int)(floor(4.0e-2F*posz));
        int ii_rec_3 = 1 + (int)(floor(4.0e-2F*posz));
        int ii_rec_4 = 1 + (int)(floor(4.0e-2F*posy));
        int ii_rec_5 = 1 + (int)(floor(4.0e-2F*posx));
        float px = (float)(posx - 2.5e+1F*(int)(floor(4.0e-2F*posx)));
        float py = (float)(posy - 2.5e+1F*(int)(floor(4.0e-2F*posy)));
        float pz = (float)(posz - 2.5e+1F*(int)(floor(4.0e-2F*posz)));
        if (ii_rec_0 >= x_m - 1 && ii_rec_1 >= y_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_1 <= y_M + 1 && ii_rec_2 <= z_M + 1)
        {
          float r2 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6]*vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*py + 1.6e-3F*px*pz - 4.0e-2F*px + 1.6e-3F*py*pz - 4.0e-2F*py - 4.0e-2F*pz + 1)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_2 + 6] += r2;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_1 >= y_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_1 <= y_M + 1 && ii_rec_3 <= z_M + 1)
        {
          float r3 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6]*vp[ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*pz - 1.6e-3F*py*pz + 4.0e-2F*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_1 + 6][ii_rec_3 + 6] += r3;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_2 <= z_M + 1 && ii_rec_4 <= y_M + 1)
        {
          float r4 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6]*vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*py - 1.6e-3F*py*pz + 4.0e-2F*py)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_2 + 6] += r4;
        }
        if (ii_rec_0 >= x_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_0 <= x_M + 1 && ii_rec_3 <= z_M + 1 && ii_rec_4 <= y_M + 1)
        {
          float r5 = (dt*dt)*(vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6]*vp[ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*py*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_0 + 6][ii_rec_4 + 6][ii_rec_3 + 6] += r5;
        }
        if (ii_rec_1 >= y_m - 1 && ii_rec_2 >= z_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_1 <= y_M + 1 && ii_rec_2 <= z_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r6 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6]*vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6])*(6.4e-5F*px*py*pz - 1.6e-3F*px*py - 1.6e-3F*px*pz + 4.0e-2F*px)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_2 + 6] += r6;
        }
        if (ii_rec_1 >= y_m - 1 && ii_rec_3 >= z_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_1 <= y_M + 1 && ii_rec_3 <= z_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r7 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6]*vp[ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*pz)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_1 + 6][ii_rec_3 + 6] += r7;
        }
        if (ii_rec_2 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_2 <= z_M + 1 && ii_rec_4 <= y_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r8 = (dt*dt)*(vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6]*vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6])*(-6.4e-5F*px*py*pz + 1.6e-3F*px*py)*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_2 + 6] += r8;
        }
        if (ii_rec_3 >= z_m - 1 && ii_rec_4 >= y_m - 1 && ii_rec_5 >= x_m - 1 && ii_rec_3 <= z_M + 1 && ii_rec_4 <= y_M + 1 && ii_rec_5 <= x_M + 1)
        {
          float r9 = 6.4e-5F*px*py*pz*(dt*dt)*(vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6]*vp[ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6])*rec[time][p_rec];
          #pragma omp atomic update
          v[t1][ii_rec_5 + 6][ii_rec_4 + 6][ii_rec_3 + 6] += r9;
        }
      }
    }
    STOP_TIMER(section1,timers)
    /* End section1 */

    /* Begin section2 */
    START_TIMER(section2)
    #pragma omp parallel num_threads(nthreads)
    {
      #pragma omp for collapse(2) schedule(static,1)
      for (int x1_blk0 = x_m; x1_blk0 <= x_M; x1_blk0 += x1_blk0_size)
      {
        for (int y1_blk0 = y_m; y1_blk0 <= y_M; y1_blk0 += y1_blk0_size)
        {
          for (int x = x1_blk0; x <= MIN(x1_blk0 + x1_blk0_size - 1, x_M); x += 1)
          {
            for (int y = y1_blk0; y <= MIN(y1_blk0 + y1_blk0_size - 1, y_M); y += 1)
            {
              #pragma omp simd aligned(grad,u,v:64)
              for (int z = z_m; z <= z_M; z += 1)
              {
                grad[x + 1][y + 1][z + 1] += -(r0*(-2.0F*v[t0][x + 6][y + 6][z + 6]) + r0*v[t1][x + 6][y + 6][z + 6] + r0*v[t2][x + 6][y + 6][z + 6])*u[time][x + 6][y + 6][z + 6];
              }
            }
          }
        }
      }
    }
    STOP_TIMER(section2,timers)
    /* End section2 */
  }

  size_t u_size = u_vec->size[2]*u_vec->size[3]*sizeof(float);
  long int read_size = (time_M - time_m+1) * u_vec->size[1] * u_size;
  save(nthreads, timers, read_size);
  return 0;
}

static void sendrecvtxyz(struct dataobj *restrict a0_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, int ogtime, int ogx, int ogy, int ogz, int ostime, int osx, int osy, int osz, int fromrank, int torank, MPI_Comm comm, const int nthreads)
{
  float *bufg0_vec;
  posix_memalign((void**)(&bufg0_vec),64,buf_x_size*buf_y_size*buf_z_size*sizeof(float));
  float *bufs0_vec;
  posix_memalign((void**)(&bufs0_vec),64,buf_x_size*buf_y_size*buf_z_size*sizeof(float));

  MPI_Request rrecv;
  MPI_Request rsend;

  MPI_Irecv(bufs0_vec,buf_x_size*buf_y_size*buf_z_size,MPI_FLOAT,fromrank,13,comm,&(rrecv));
  if (torank != MPI_PROC_NULL)
  {
    gathertxyz(bufg0_vec,buf_x_size,buf_y_size,buf_z_size,a0_vec,ogtime,ogx,ogy,ogz,nthreads);
  }
  MPI_Isend(bufg0_vec,buf_x_size*buf_y_size*buf_z_size,MPI_FLOAT,torank,13,comm,&(rsend));
  MPI_Wait(&(rsend),MPI_STATUS_IGNORE);
  MPI_Wait(&(rrecv),MPI_STATUS_IGNORE);
  if (fromrank != MPI_PROC_NULL)
  {
    scattertxyz(bufs0_vec,buf_x_size,buf_y_size,buf_z_size,a0_vec,ostime,osx,osy,osz,nthreads);
  }

  free(bufg0_vec);
  free(bufs0_vec);
}

static void gathertxyz(float *restrict buf0_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, struct dataobj *restrict a0_vec, int otime, int ox, int oy, int oz, const int nthreads)
{
  float (*restrict a0)[a0_vec->size[1]][a0_vec->size[2]][a0_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[a0_vec->size[1]][a0_vec->size[2]][a0_vec->size[3]]) a0_vec->data;
  float (*restrict buf0)[buf_y_size][buf_z_size] __attribute__ ((aligned (64))) = (float (*)[buf_y_size][buf_z_size]) buf0_vec;

  #pragma omp parallel num_threads(nthreads)
  {
    #pragma omp for collapse(2) schedule(static,1)
    for (int x = 0; x <= buf_x_size - 1; x += 1)
    {
      for (int y = 0; y <= buf_y_size - 1; y += 1)
      {
        #pragma omp simd aligned(a0:64)
        for (int z = 0; z <= buf_z_size - 1; z += 1)
        {
          buf0[x][y][z] = a0[otime][x + ox][y + oy][z + oz];
        }
      }
    }
  }
}

static void scattertxyz(float *restrict buf1_vec, const int buf_x_size, const int buf_y_size, const int buf_z_size, struct dataobj *restrict a0_vec, int otime, int ox, int oy, int oz, const int nthreads)
{
  float (*restrict a0)[a0_vec->size[1]][a0_vec->size[2]][a0_vec->size[3]] __attribute__ ((aligned (64))) = (float (*)[a0_vec->size[1]][a0_vec->size[2]][a0_vec->size[3]]) a0_vec->data;
  float (*restrict buf1)[buf_y_size][buf_z_size] __attribute__ ((aligned (64))) = (float (*)[buf_y_size][buf_z_size]) buf1_vec;

  #pragma omp parallel num_threads(nthreads)
  {
    #pragma omp for collapse(2) schedule(static,1)
    for (int x = 0; x <= buf_x_size - 1; x += 1)
    {
      for (int y = 0; y <= buf_y_size - 1; y += 1)
      {
        #pragma omp simd aligned(a0:64)
        for (int z = 0; z <= buf_z_size - 1; z += 1)
        {
          a0[otime][x + ox][y + oy][z + oz] = buf1[x][y][z];
        }
      }
    }
  }
}

static void haloupdate0(struct dataobj *restrict a0_vec, MPI_Comm comm, struct neighborhood * nb, int otime, const int nthreads)
{
  sendrecvtxyz(a0_vec,a0_vec->hsize[3],a0_vec->npsize[2],a0_vec->npsize[3],otime,a0_vec->oofs[2],a0_vec->hofs[4],a0_vec->hofs[6],otime,a0_vec->hofs[3],a0_vec->hofs[4],a0_vec->hofs[6],nb->rcc,nb->lcc,comm,nthreads);
  sendrecvtxyz(a0_vec,a0_vec->hsize[2],a0_vec->npsize[2],a0_vec->npsize[3],otime,a0_vec->oofs[3],a0_vec->hofs[4],a0_vec->hofs[6],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->hofs[6],nb->lcc,nb->rcc,comm,nthreads);
  sendrecvtxyz(a0_vec,a0_vec->npsize[1],a0_vec->hsize[5],a0_vec->npsize[3],otime,a0_vec->hofs[2],a0_vec->oofs[4],a0_vec->hofs[6],otime,a0_vec->hofs[2],a0_vec->hofs[5],a0_vec->hofs[6],nb->crc,nb->clc,comm,nthreads);
  sendrecvtxyz(a0_vec,a0_vec->npsize[1],a0_vec->hsize[4],a0_vec->npsize[3],otime,a0_vec->hofs[2],a0_vec->oofs[5],a0_vec->hofs[6],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->hofs[6],nb->clc,nb->crc,comm,nthreads);
  sendrecvtxyz(a0_vec,a0_vec->npsize[1],a0_vec->npsize[2],a0_vec->hsize[7],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->oofs[6],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->hofs[7],nb->ccr,nb->ccl,comm,nthreads);
  sendrecvtxyz(a0_vec,a0_vec->npsize[1],a0_vec->npsize[2],a0_vec->hsize[6],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->oofs[7],otime,a0_vec->hofs[2],a0_vec->hofs[4],a0_vec->hofs[6],nb->ccl,nb->ccr,comm,nthreads);
}
