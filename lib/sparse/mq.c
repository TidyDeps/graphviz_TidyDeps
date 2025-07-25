/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property 
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * https://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

/* Modularity Quality definition:

   We assume undirected graph. Directed graph should be converted by summing edge weights.

   Given a partition P of V into k clusters.

   Let E(i,j) be the set of edges between cluster i and j.
   Let |E(i,j)| be the sum of edge weights of edges in E(i,j).

   Let E(i,i) be the set of edges within cluster i, but excluding self-edges.
   Let |E(i,i)| be the sum of edge weights of edges in E(i,i).

   Let V(i) be the sets of vertices in i

   The intra-cluster edges concentration for a cluster i is 
   (the denominator could be |V(i)|*(|V(i)-1)/2 strictly speaking as we exclude self-edges):

     |E(i,i)|
   -----------
   (|V(i)|^2/2)

   The inter-cluster edges concentration between cluster i and j is

     |E(i,j)|
   ------------
   |V(i)|*|V(j)|

   So the cluster index is defined as the average intra cluster edge concentration, minus
   the inter-cluster edge concentration:

   .                               |E(i,i)|                                        |E(i,j)|
   MQ(P) = (1/k) * \sum_{i=1...k} ------------ - (1/(k*(k-1)/2)) * \sum_{i<j} ------------------- = mq_in/k - mq_out/(k*(k-1)/2)
   .                              (|V(i)|^2/2)                                   |V(i)|*|V(j)|

   or

   .                                 |E(i,i)|                                     |E(i,j)|
   MQ(P)/2 = (1/k) * \sum_{i=1...k} ------------ - (1/(k*(k-1))) * \sum_{i<j} ------------------ = mq_in/k - mq_out/(k*(k-1))
   .                                |V(i)|^2                                   |V(i)|*|V(j)|

   Notice that if we assume the graph is unweights (edge weights = 1), then 0<= MQ <= 1.
   For weighted graph, MQ may not be within 0 to 1. We could normalized it, but 
   for comparing clustering quality of the same graph but different partitioning, this 
   unnormalized quantity is not a problem.

*/

#define STANDALONE
#include <limits.h>
#include <sparse/general.h>
#include <sparse/SparseMatrix.h>
#include <sparse/mq.h>
#include <stdbool.h>
#include <string.h>
#include <util/alloc.h>
#include <util/list.h>

static double get_mq(SparseMatrix A, int *assignment, int *ncluster0, double *mq_in0, double *mq_out0, double **dout0){
  /* given a symmetric matrix representation of a graph and an assignment of nodes into clusters, calculate the modularity quality.
   assignment: assignment[i] gives the cluster assignment of node i. 0 <= assignment[i] < ncluster.
   ncluster: number of clusters
   mq_in: the part of MQ to do with intra-cluster edges, before divide by 1/k
   mq_out: the part of MQ to do with inter-cluster edges, before divide by 1/(k*(k-1))
   mq = 2*(mq_in/k - mq_out/(k*(k-1)));
  */
  int ncluster = 0;
  int n = A->m;
  bool test_pattern_symmetry_only = false;
  int *counts, *ia = A->ia, *ja = A->ja, k, i, j, jj;
  double mq_in = 0, mq_out = 0, *a = NULL, Vi, Vj;
  int c;
  double *dout;


  assert(SparseMatrix_is_symmetric(A, test_pattern_symmetry_only));
  (void)test_pattern_symmetry_only;
  assert(A->n == n);
  if (A->type == MATRIX_TYPE_REAL) a = A->a;

  counts = gv_calloc(n, sizeof(int));

  for (i = 0; i < n; i++){
    assert(assignment[i] >= 0 && assignment[i] < n);
    if (counts[assignment[i]] == 0) ncluster++;
    counts[assignment[i]]++;
  }
  k = ncluster;
  assert(ncluster <= n);

  for (i = 0; i < n; i++){
    assert(assignment[i] < ncluster);
    c = assignment[i];
    Vi = counts[c];
    for (j = ia[i] ; j < ia[i+1]; j++){
      /* ASSUME UNDIRECTED */
      jj = ja[j];
      if (jj >= i) continue;
      assert(assignment[jj] < ncluster);
      Vj = counts[assignment[jj]];
      if (assignment[jj] == c){
	if (a) {
	  mq_in += a[j]/(Vi*Vi);
	} else {
	  mq_in += 1./(Vi*Vi);
	}
      } else {
	if (a) {
	  mq_out += a[j]/(Vi*Vj);
	} else {
	  mq_out += 1./(Vi*Vj);
	}
      }
      
    }
  }

  /* calculate scaled out degree */
  dout = gv_calloc(n, sizeof(double));
  for (i = 0; i < n; i++){
    for (j = ia[i]; j < ia[i+1]; j++){
      jj = ja[j];
      if (jj == i) continue;
      if (a){
	dout[i] += a[j]/(double) counts[assignment[jj]];
      } else {
	dout[i] += 1./(double) counts[assignment[jj]];
      }
    }
  }

  *ncluster0 = k;
  *mq_in0 = mq_in;
  *mq_out0 = mq_out;
  *dout0 = dout;
  free(counts);

  if (k > 1){
    return 2*(mq_in/k - mq_out/(k*(k-1)));
  } else {
    return 2*mq_in;
  }
}

static Multilevel_MQ_Clustering Multilevel_MQ_Clustering_init(SparseMatrix A, int level){
  Multilevel_MQ_Clustering grid;
  int n = A->n, i;
  int *matching;

  assert(A->type == MATRIX_TYPE_REAL);
  assert(SparseMatrix_is_symmetric(A, false));

  if (!A) return NULL;
  assert(A->m == n);
  grid = gv_alloc(sizeof(struct Multilevel_MQ_Clustering_struct));
  grid->level = level;
  grid->n = n;
  grid->A = A;
  grid->P = NULL;
  grid->next = NULL;
  grid->prev = NULL;
  grid->delete_top_level_A = false;
  matching = grid->matching = gv_calloc(n, sizeof(double));
  grid->deg_intra = NULL;
  grid->dout = NULL;
  grid->wgt = NULL;

  if (level == 0){
    double mq = 0, mq_in, mq_out;
    int ncluster;
    double *deg_intra, *wgt, *dout;

    grid->deg_intra = gv_calloc(n, sizeof(double));
    deg_intra = grid->deg_intra;

    grid->wgt = gv_calloc(n, sizeof(double));
    wgt = grid->wgt;

    for (i = 0; i < n; i++){
      deg_intra[i] = 0;
      wgt[i] = 1.;
    }
    for (i = 0; i < n; i++) matching[i] = i;
    mq = get_mq(A, matching, &ncluster, &mq_in, &mq_out, &dout);
    fprintf(stderr,"ncluster = %d, mq = %f\n", ncluster, mq);
    grid->mq = mq;
    grid->mq_in = mq_in;
    grid->mq_out = mq_out;
    grid->dout = dout;
    grid->ncluster = ncluster;

  }


  return grid;
} 

static void Multilevel_MQ_Clustering_delete(Multilevel_MQ_Clustering grid){
  if (!grid) return;
  if (grid->A){
    if (grid->level == 0) {
      if (grid->delete_top_level_A) SparseMatrix_delete(grid->A);
    } else {
      SparseMatrix_delete(grid->A);
    }
  }
  SparseMatrix_delete(grid->P);
  free(grid->matching);
  free(grid->deg_intra);
  free(grid->dout);
  free(grid->wgt);
  Multilevel_MQ_Clustering_delete(grid->next);
  free(grid);
}

DEFINE_LIST(ints, int)

static Multilevel_MQ_Clustering Multilevel_MQ_Clustering_establish(Multilevel_MQ_Clustering grid, int maxcluster){
  int *matching = grid->matching;
  SparseMatrix A = grid->A;
  int n = grid->n, level = grid->level, nc = 0, nclusters = n;
  double mq = 0, mq_in = 0, mq_out = 0, mq_new, mq_in_new, mq_out_new, mq_max = 0, mq_in_max = 0, mq_out_max = 0;
  int *ia = A->ia, *ja = A->ja;
  double amax = 0;
  double *deg_intra = grid->deg_intra, *wgt = grid->wgt;
  int i, j, k, jj, jc, jmax;
  double gain = 0, *dout = grid->dout, deg_in_i, deg_in_j, wgt_i, wgt_j, a_ij, dout_i, dout_j, dout_max = 0, wgt_jmax = 0;
  double maxgain = 0;
  double total_gain = 0;

  ints_t *neighbors = gv_calloc(n, sizeof(ints_t));

  mq = grid->mq;
  mq_in = grid->mq_in;
  mq_out = grid->mq_out;

  double *deg_intra_new = gv_calloc(n, sizeof(double));
  double *wgt_new = gv_calloc(n, sizeof(double));
  double *deg_inter = gv_calloc(n, sizeof(double));
  int *mask = gv_calloc(n, sizeof(int));
  double *dout_new = gv_calloc(n, sizeof(double));
  for (i = 0; i < n; i++) mask[i] = -1;

  assert(n == A->n);
  for (i = 0; i < n; i++) matching[i] = UNMATCHED;

  /* gain in merging node A into cluster B is
     mq_in_new = mq_in - |E(A,A)|/(V(A))^2 - |E(B,B)|/(V(B))^2 + (|E(A,A)|+|E(B,B)|+|E(A,B)|)/(|V(A)|+|V(B)|)^2
     .         = mq_in - deg_intra(A)/|A|^2 - deg_intra(B)/|B|^2 + (deg_intra(A)+deg_intra(B)+a(A,B))/(|A|+|B|)^2

     mq_out_new = mq_out - |E(A,B)|/(|V(A)|*V(B)|)-\sum_{C and A connected, C!=B} |E(A,C)|/(|V(A)|*|V(C)|)-\sum_{C and B connected,C!=B} |E(B,C)|/(|V(B)|*|V(C)|)
     .                  + \sum_{C connected to A or B, C!=A, C!=B} (|E(A,C)|+|E(B,C)|)/(|V(C)|*(|V(A)|+|V(B)|)
     .          = mq_out + a(A,B)/(|A|*|B|)-\sum_{C and A connected} a(A,C)/(|A|*|C|)-\sum_{C and B connected} a(B,C)/(|B|*|C|)
     .                  + \sum_{C connected to A or B, C!=A, C!=B} (a(A,C)+a(B,C))/(|C|*(|A|+|B|))
     Denote:
     dout(i) = \sum_{j -- i} a(i,j)/|j|
     then

     mq_out_new = mq_out - |E(A,B)|/(|V(A)|*V(B)|)-\sum_{C and A connected, C!=B} |E(A,C)|/(|V(A)|*|V(C)|)-\sum_{C and B connected,C!=B} |E(B,C)|/(|V(B)|*|V(C)|)
     .                  + \sum_{C connected to A or B, C!=A, C!=B} (|E(A,C)|+|E(B,C)|)/(|V(C)|*(|V(A)|+|V(B)|)
     .          = mq_out + a(A,B)/(|A|*|B|)-dout(A)/|A| - dout(B)/|B|
     .                  + (dout(A)+dout(B))/(|A|+|B|) - (a(A,B)/|A|+a(A,B)/|B|)/(|A|+|B|)
     .          = mq_out -dout(A)/|A| - dout(B)/|B| + (dout(A)+dout(B))/(|A|+|B|)
     after merging A and B into cluster AB,
     dout(AB) = dout(A) + dout(B);
     dout(C) := dout(C) - a(A,C)/|A| - a(B,C)/|B| + a(A,C)/(|A|+|B|) + a(B, C)/(|A|+|B|) 

     mq_new = mq_in_new/(k-1) - mq_out_new/((k-1)*(k-2))
     gain = mq_new - mq
  */
  double *a = A->a;
  for (i = 0; i < n; i++){
    if (matching[i] != UNMATCHED) continue;
    /* accumulate connections between i and clusters */
    for (j = ia[i]; j < ia[i+1]; j++){
      jj = ja[j];
      if (jj == i) continue;
      if ((jc=matching[jj]) != UNMATCHED){
	if (mask[jc] != i) {
	  mask[jc] = i;
	  deg_inter[jc] = a[j];
	} else {
	  deg_inter[jc] += a[j];
	}
      }
    }
    deg_in_i = deg_intra[i];
    wgt_i = wgt[i];
    dout_i = dout[i];

    maxgain = 0; 
    jmax = -1;
    for (j = ia[i]; j < ia[i+1]; j++){
      jj = ja[j];
      if (jj == i) continue;
      jc = matching[jj];
      if (jc == UNMATCHED){
	a_ij = a[j];
	wgt_j = wgt[jj];
	deg_in_j = deg_intra[jj];
	dout_j = dout[jj];
      } else if (deg_inter[jc] < 0){
	continue;
      } else {
	a_ij = deg_inter[jc];
	wgt_j = wgt_new[jc];
	deg_inter[jc] = -1; // so that we do not redo the calculation when we hit another neighbor in cluster jc
	deg_in_j = deg_intra_new[jc];
	dout_j = dout_new[jc];
      }

      mq_in_new = mq_in - deg_in_i/pow(wgt_i, 2) - deg_in_j/pow(wgt_j,2) 
	+ (deg_in_i + deg_in_j + a_ij)/pow(wgt_i + wgt_j,2);

      mq_out_new = mq_out - dout_i/wgt_i - dout_j/wgt_j + (dout_i + dout_j)/(wgt_i + wgt_j);

      if (nclusters > 2){
	mq_new =  2*(mq_in_new/(nclusters - 1) - mq_out_new/((nclusters - 1)*(nclusters - 2)));
      } else {
	mq_new =  2*mq_in_new/(nclusters - 1);
      }

#ifdef DEBUG
      {int ncluster;
	double mq2, mq_in2, mq_out2, *dout2;
	int nc2 = nc;
	int *matching2 = gv_calloc(A->m, sizeof(int));
	memcpy(matching2, matching, sizeof(double)*A->m);
	if (jc != UNMATCHED) {
	  matching2[i] = jc;
	} else {
	  matching2[i] = nc2;
	  matching2[jj] = nc2;
	  nc2++;
	}
	for (k = 0; k < n; k++) if (matching2[k] == UNMATCHED) matching2[k] =nc2++;
	mq2 = get_mq(A, matching2, &ncluster, &mq_in2, &mq_out2, &dout2);
	fprintf(stderr," {dout_i, dout_j}={%f,%f}, {predicted, calculated}: mq = {%f, %f}, mq_in ={%f,%f}, mq_out = {%f,%f}\n",dout_i, dout_j, mq_new, mq2, mq_in_new, mq_in2, mq_out_new, mq_out2);

	mq_new = mq2;
	
      }
#endif

      gain = mq_new - mq;
      if (Verbose) fprintf(stderr,"gain in merging node %d with node %d = %f-%f = %f\n", i, jj, mq, mq_new, gain);
      if (j == ia[i] || gain > maxgain){
	maxgain = gain;
	jmax = jj;
	amax = a_ij;
	dout_max = dout_j;
	wgt_jmax = wgt_j;
	mq_max = mq_new;
	mq_in_max = mq_in_new;
	mq_out_max = mq_out_new;
      }

    }

    /* now merge i and jmax */
    if (maxgain > 0 || (nc >= 1 && nc > maxcluster)){
      total_gain += maxgain;
      jc = matching[jmax];
      if (jc == UNMATCHED){
	fprintf(stderr, "maxgain=%f, merge %d, %d\n",maxgain, i, jmax);
	ints_append(&neighbors[nc], jmax);
	ints_append(&neighbors[nc], i);
	dout_new[nc] = dout_i + dout_max;
	matching[i] = matching[jmax] = nc;
	wgt_new[nc] = wgt[i] + wgt[jmax];
	deg_intra_new[nc] = deg_intra[i] + deg_intra[jmax] + amax;
	nc++;
      } else {	
	fprintf(stderr,"maxgain=%f, merge with existing cluster %d, %d\n",maxgain, i, jc);
	ints_append(&neighbors[jc], i);
	dout_new[jc] = dout_i + dout_max;
	wgt_new[jc] += wgt[i];
	matching[i] = jc;
	deg_intra_new[jc] += deg_intra[i] + amax;
      }
      mq = mq_max;
      mq_in = mq_in_max;
      mq_out = mq_out_max;
      nclusters--;
    } else {
      fprintf(stderr,"gain: %f -- no gain, skip merging node %d\n", maxgain, i);
      assert(maxgain <= 0);
      ints_append(&neighbors[nc], i);
      matching[i] = nc;
      deg_intra_new[nc] = deg_intra[i];
      wgt_new[nc] = wgt[i];
      nc++;
    }


    /* update scaled outdegree of neighbors of i and its merged node/cluster jmax */
    jc = matching[i];
    for (size_t l = ints_size(&neighbors[jc]) - 1; l != SIZE_MAX; --l) {
      mask[ints_get(&neighbors[jc], l)] = n + i;
    }

    for (size_t l = ints_size(&neighbors[jc]) - 1; l != SIZE_MAX; --l) {
      k = ints_get(&neighbors[jc], l);
      for (j = ia[k]; j < ia[k+1]; j++){
	jj = ja[j]; 
	if (mask[jj] == n+i) continue;/* link to within cluster */
	if ((jc = matching[jj]) == UNMATCHED){
	  if (k == i){
	    dout[jj] += -a[j]/wgt_i + a[j]/(wgt_i + wgt_jmax);
	  } else {
	    dout[jj] += -a[j]/wgt_jmax + a[j]/(wgt_i + wgt_jmax);
	  }
	} else {
	  if (k == i){
	    dout_new[jc] += -a[j]/wgt_i + a[j]/(wgt_i + wgt_jmax);
	  } else {
	    dout_new[jc] += -a[j]/wgt_jmax + a[j]/(wgt_i + wgt_jmax);
	  }
	}
      }
    }

  }

  fprintf(stderr,"verbose=%d\n",Verbose);
  if (Verbose) fprintf(stderr,"mq = %f new mq = %f level = %d, n = %d, nc = %d, gain = %g, mq_in = %f, mq_out = %f\n", mq, mq + total_gain, 
		       level, n, nc, total_gain, mq_in, mq_out);
  
#ifdef DEBUG
  {int ncluster;

  mq = get_mq(A, matching, &ncluster, &mq_in, &mq_out, &dout);
  fprintf(stderr," mq = %f\n",mq);

  }
#endif

  if (nc >= 1 && (total_gain > 0 || nc < n)){
    /* now set up restriction and prolongation operator */
    SparseMatrix P, R, R0, B, cA;
    double one = 1.;
    Multilevel_MQ_Clustering cgrid;

    R0 = SparseMatrix_new(nc, n, 1, MATRIX_TYPE_REAL, FORMAT_COORD);
    for (i = 0; i < n; i++){
      jj = matching[i];
      SparseMatrix_coordinate_form_add_entry(R0, jj, i, &one);
    }
    R = SparseMatrix_from_coordinate_format(R0);
    SparseMatrix_delete(R0);
    P = SparseMatrix_transpose(R);
    B = SparseMatrix_multiply(R, A);
    SparseMatrix_delete(R);
    if (!B) {
        free(deg_intra_new);
        free(wgt_new);
        free(dout_new);
        goto RETURN;
    }
    cA = SparseMatrix_multiply(B, P); 
    SparseMatrix_delete(B);
    if (!cA) {
        free(deg_intra_new);
        free(wgt_new);
        free(dout_new);
        goto RETURN;
    }
    grid->P = P;
    level++;
    cgrid = Multilevel_MQ_Clustering_init(cA, level); 
    deg_intra_new = gv_recalloc(deg_intra_new, n, nc, sizeof(double));
    wgt_new = gv_recalloc(wgt_new, n, nc, sizeof(double));
    cgrid->deg_intra = deg_intra_new;
    cgrid->mq = grid->mq + total_gain;
    cgrid->wgt = wgt_new;
    dout_new = gv_recalloc(dout_new, n, nc, sizeof(double));
    cgrid->dout = dout_new;

    cgrid = Multilevel_MQ_Clustering_establish(cgrid, maxcluster);

    grid->next = cgrid;
    cgrid->prev = grid;
  } else {
    /* no more improvement, stop and final clustering found */
    for (i = 0; i < n; i++) matching[i] = i;

    free(deg_intra_new);
    free(wgt_new);
    free(dout_new);
  }

 RETURN:
  for (i = 0; i < n; i++) ints_free(&neighbors[i]);
  free(neighbors);

  free(deg_inter);
  free(mask);
  return grid;
}

static Multilevel_MQ_Clustering Multilevel_MQ_Clustering_new(SparseMatrix A0, int maxcluster){
  /* maxcluster is used to specify the maximum number of cluster desired, e.g., maxcluster=10 means that a maximum of 10 clusters
     is desired. this may not always be realized, and mq may be low when this is specified. Default: maxcluster = 0 */
  Multilevel_MQ_Clustering grid;
  SparseMatrix A = A0;

  if (maxcluster <= 0) maxcluster = A->m;
  if (!SparseMatrix_is_symmetric(A, false) || A->type != MATRIX_TYPE_REAL){
    A = SparseMatrix_get_real_adjacency_matrix_symmetrized(A);
  }
  grid = Multilevel_MQ_Clustering_init(A, 0);

  grid = Multilevel_MQ_Clustering_establish(grid, maxcluster);

  if (A != A0) grid->delete_top_level_A = true; // be sure to clean up later
  return grid;
}


static void hierachical_mq_clustering(SparseMatrix A, int maxcluster,
					      int *nclusters, int **assignment, double *mq){
  /* find a clustering of vertices by maximize mq
     A: symmetric square matrix n x n. If real value, value will be used as edges weights, otherwise edge weights are considered as 1.
     maxcluster: used to specify the maximum number of cluster desired, e.g., maxcluster=10 means that a maximum of 10 clusters
     .   is desired. this may not always be realized, and mq may be low when this is specified. Default: maxcluster = 0 
     nclusters: on output the number of clusters
     assignment: dimension n. Node i is assigned to cluster "assignment[i]". 0 <= assignment < nclusters
   */

  Multilevel_MQ_Clustering grid, cgrid;
  int *matching, i;
  SparseMatrix P;
  assert(A->m == A->n);

  *mq = 0.;

  grid = Multilevel_MQ_Clustering_new(A, maxcluster);

  /* find coarsest */
  cgrid = grid;
  while (cgrid->next){
    cgrid = cgrid->next;
  }

  /* project clustering up */
  double *u = gv_calloc(cgrid->n, sizeof(double));
  for (i = 0; i < cgrid->n; i++) u[i] = (double) (cgrid->matching)[i];
  *nclusters = cgrid->n;
  *mq = cgrid->mq;

  while (cgrid->prev){
    double *v = NULL;
    P = cgrid->prev->P;
    SparseMatrix_multiply_vector(P, u, &v);
    free(u);
    u = v;
    cgrid = cgrid->prev;
  }

  if (*assignment){
    matching = *assignment; 
  } else {
    matching = gv_calloc(grid->n, sizeof(int));
    *assignment = matching;
  }
  for (i = 0; i < grid->n; i++) (matching)[i] = (int) u[i];
  free(u);

  Multilevel_MQ_Clustering_delete(grid);
}



void mq_clustering(SparseMatrix A, int maxcluster,
			   int *nclusters, int **assignment, double *mq){
  /* find a clustering of vertices by maximize mq
     A: symmetric square matrix n x n. If real value, value will be used as edges weights, otherwise edge weights are considered as 1.
     maxcluster: used to specify the maximum number of cluster desired, e.g., maxcluster=10 means that a maximum of 10 clusters
     .   is desired. this may not always be realized, and mq may be low when this is specified. Default: maxcluster = 0 
     nclusters: on output the number of clusters
     assignment: dimension n. Node i is assigned to cluster "assignment[i]". 0 <= assignment < nclusters
   */
  SparseMatrix B;

  assert(A->m == A->n);

  B = SparseMatrix_symmetrize(A, false);

  if (B == A) {
    B = SparseMatrix_copy(A);
  }

  B = SparseMatrix_remove_diagonal(B);

  if (B->type != MATRIX_TYPE_REAL) B = SparseMatrix_set_entries_to_real_one(B);

  hierachical_mq_clustering(B, maxcluster, nclusters, assignment, mq);

  if (B != A) SparseMatrix_delete(B);

}
