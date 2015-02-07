#include "globals.h"
#include "Gravity/gravity.h"
#include "domain.h"
#include "peano.h"

#define DOMAIN_SPLIT_MEM_THRES -0.8
#define DOMAIN_SPLIT_CPU_THRES -1
#define DOMAIN_NBUNCHES_PER_THREAD 4.0

static void reset_bunchlist();
static void find_global_domain_extend();
static void fill_bunches(const int, const int, const int, const int);
static void remove_empty_bunches(int *nTop_Leaves, int *max_level);
static void split_bunch(const int, const int);
static void reallocate_topnodes(); // not thread safe
static bool imbalance_small(const int);
static void communicate_particles();
static void communicate_bunches();
static void print_domain_decomposition (const int);

static int compare_bunches_by_key(const void *a, const void *b);
static int compare_bunches_by_target(const void *a, const void *b); 
static int compare_bunches_by_npart(const void *a, const void *b); 

union Domain_Node_List *D = NULL; 

static double max_mem_imbal = 0, max_cpu_imbal = 0;
static double Top_Node_Alloc_Factor = 0;

static int Max_NBunches = 0;
static int NBunches = 0;

/* 
 * Distribute particles in bunches, which are continuous
 * on the Peano curve. The bunches correspond to nodes of the tree. 
 * Bunches also give the top nodes in the tree, with some info added. 
 * We keep a global list of all the bunches that also contains the 
 * workload and memory footprint of each bunch. An optimal way of 
 * distributing bunches minimises memory and workload imbalance over all
 * Tasks. 
 * To achieve this, we measure mem & cpu cost and refine bunches until the
 * heaviest have roughly equal cost. Then we distribute them across MPI
 * ranks. 
 * Upon reentry we reconstruct the bunchlist to cover the whole domain by 
 * completing the Peano key on every level separately.
 */

void Domain_Decomposition()
{
	Profile("Domain Decomposition");

	find_global_domain_extend();
	
	Sort_Particles_By_Peano_Key();
		
	reset_bunchlist();

	fill_bunches(0, NBunches, 0, Task.Npart_Total); // let's see what we have
	
	#pragma omp barrier

	int max_level = 0, nTop_Leaves = 0;

	for (;;) {
	
		Qsort(Sim.NThreads, D, NBunches, sizeof(*D), &compare_bunches_by_key);

		communicate_bunches();

		#pragma omp single copyprivate(nTop_Leaves,max_level)
		remove_empty_bunches(&nTop_Leaves, &max_level);

		if (imbalance_small(nTop_Leaves))
			break;

		int old_nBunches = NBunches;

		#pragma omp barrier

		for (int i = 0; i < old_nBunches; i++ ) {
			
			if (D[i].Bunch.Modify != 0) { // split into 8

				int first_new_bunch = NBunches;

				#pragma omp barrier

				split_bunch(i, first_new_bunch);
				
				#pragma omp barrier

				fill_bunches(first_new_bunch, 8, D[i].Bunch.First_Part, 
						D[i].Bunch.Npart);
				
				#pragma omp barrier

				#pragma omp single
				memset(&D[i].Bunch, 0, sizeof(*D)); // mark for deletion

			} // if 
		} // for i
	} // for (;;)

	#pragma omp barrier

	rprintf("        Finished %d Top Nodes, %d Top Leaves, max level %d\n\n", 
			NBunches, nTop_Leaves, max_level);

#ifdef DEBUG
	print_domain_decomposition(max_level);
#endif

	communicate_particles();

	NTop_Nodes = NBunches;

	Sig.Force_Domain = false;

	Profile("Domain Decomposition");

	return ;
}

/*
 * Make room for some bunches and build the first node manually.
 */

void Init_Domain_Decomposition()
{
	Top_Node_Alloc_Factor = (double) 4096 / Task.Npart_Total;

	reallocate_topnodes();

	memset(D, 0, Max_NBunches * sizeof(*D));
	
	NBunches = 1;

	D[0].Bunch.Key = 0xFFFFFFFFFFFFFFFF;
	D[0].Bunch.Npart = D[0].Bunch.Level = D[0].Bunch.Target = 0;

	#pragma omp parallel
	{
	
	find_global_domain_extend();

	rprintf("\nInitial Domain size is %g, \n"
			"   Origin at x = %4g, y = %4g, z = %4g, \n"
			"   Center at x = %4g, y = %4g, z = %4g. \n"
			"   CoM    at x = %4g, y = %4g, z = %4g. \n",
			Domain.Size, Domain.Origin[0], Domain.Origin[1], Domain.Origin[2],
			Domain.Center[0], Domain.Center[1], Domain.Center[2],
			Domain.Center_Of_Mass[0], Domain.Center_Of_Mass[1],
			Domain.Center_Of_Mass[2]);

	} // omp parallel
	
	return;
}

/*
 * This increases the room for Bunches/Topnodes by 20 %, 
 * so we can stay minimal in memory. Not thread safe ! 
 */

static void reallocate_topnodes()
{
	Top_Node_Alloc_Factor *= 1.2;
	
	Max_NBunches = Sim.Npart_Total * Top_Node_Alloc_Factor;

	size_t nBytes = Max_NBunches * sizeof(*D); 

	printf("Increasing Top Node Memory to %g KB, Max %d Nodes, Factor %4g \n"
			, nBytes/1024.0, Max_NBunches, Top_Node_Alloc_Factor);

	D = Realloc(D, nBytes, "D");

	return ;
}

/*
 * Transform the top nodes back into a bunch list. 
 * First reset properties overwritten by the union in D. Then add nodes so 
 * the complete domain is covered. This is equivalent to completing every 
 * triplet from bunch i from its level up to 7 until the common part of the
 * original keys is reached. Then every triplet of i+1 is incremented until 
 * the key triplet is reached, top to bottom.
 */

void reset_bunchlist()
{		
	if (NBunches < 2)
		return ;

	rprintf("Domain: Reconstruction %d -> ", NBunches);

	const int nOld_Bunches = NBunches;
	
	#pragma omp barrier 

	#pragma omp single
	{

	if (Tree != NULL)
		Free(Tree);

	Tree = NULL;
	
	if (D[NBunches-1].Bunch.Key != 0xFFFFFFFFFFFFFFFF) { // make end
	
		int i = NBunches++;

		D[i].Bunch.Level = 1;
		D[i].Bunch.Key = 0xFFFFFFFFFFFFFFFF;
		D[i].Bunch.Target = -INT_MAX;
	}

	} // omp single

	#pragma omp flush (D)
	
	struct Bunch_Node *b = Get_Thread_Safe_Buffer(Task.Buffer_Size);

	int nNew = 0;

	#pragma omp for nowait 			
	for (int i = 0; i < nOld_Bunches-1; i++) { // fill to cover whole domain

		shortKey akey = D[i].Bunch.Key;
		shortKey bkey = D[i+1].Bunch.Key;

		int top = 1; // highest level where akey != bkey
	
		uint64_t mask = 0x7ULL << (N_SHORT_BITS-3);

		while ((akey & mask) == (bkey & mask)) {

			top++;
			mask >>= 3;
		}

		for (int j = D[i].Bunch.Level; j > top; j--) { // fill upwards

			int shift = N_SHORT_BITS - 3 * j;

			uint64_t atriplet = (akey >> shift) & 0x7;

			for (int k = atriplet + 1; k < 8; k++) {
				
				uint64_t template = 
					(akey | (0xFFFFFFFFFFFFFFFF >> 3*j)) & ~(0x7ULL << shift);
					
				b[nNew].Key = template | ( (uint64_t)k << shift);
				b[nNew].Level = j;

				nNew++;
				
			}
		}

		int shift = N_SHORT_BITS - 3 * top; // fill at level 'top'

		uint64_t atriplet = (akey >> shift) & 0x7;
		uint64_t btriplet = (bkey >> shift) & 0x7; 

		for (uint64_t k = atriplet + 1; k < btriplet; k++) {
		
			uint64_t template = 
				(akey | (0xFFFFFFFFFFFFFFFF >> 3*top)) & ~(0x7ULL << shift);
			b[nNew].Level = top;
			b[nNew].Key = template | (k << shift);

			nNew++;

		}

		for (int j = top+1; j <= D[i+1].Bunch.Level; j++) { // fill downwards

			int shift = N_SHORT_BITS - 3 * j;

			uint64_t btriplet = (bkey >> shift) & 0x7;
		
			uint64_t template = (bkey | (0xFFFFFFFFFFFFFFFF >> 3*j)) 
														& ~(0x7ULL << shift);
			for (uint64_t k = 0; k < btriplet; k++) {

				b[nNew].Level = j;
				b[nNew].Key = template | (k << shift);

				nNew++;

			} // for k
		} // for j
	} // for i

	int start = 0, end = 0;

	#pragma omp critical
	{

	start = NBunches;
	end = start + nNew;

	while (end >= Max_NBunches)
		reallocate_topnodes();

	NBunches += nNew;

	} // omp critical
	
	int j = 0;
	
	for (int i = start; i < end; i++) {
	
		D[i].Bunch.Key = b[j].Key;
		D[i].Bunch.Target = -INT_MAX;
		D[i].Bunch.Level = b[j++].Level;
	}

	#pragma omp barrier

	#pragma omp for
	for (int i = 0; i < NBunches; i++) { // reset values in all bunches

		D[i].Bunch.Npart = 0;
		D[i].Bunch.Modify = 0;
	 	D[i].Bunch.Cost = 0;
		D[i].Bunch.First_Part = INT_MAX;

		if (D[i].Bunch.Target >= 0)
			D[i].Bunch.Is_Local = true;

	} // for i < NBunches

	rprintf("%d bunches\n", NBunches-nOld_Bunches);

	Qsort(Sim.NThreads, D, NBunches, sizeof(*D), &compare_bunches_by_key);

	return ;
}

/*
 * We split a bunch into 8 sub-bunches/nodes, adding the largest peano key 
 * contained in the bunch. The position of the bunch is set  to a random 
 * particle position during filling. From it we can later construct the 
 * top node center during Tree construction.
 */

static void split_bunch(const int parent, const int first)
{
	#pragma omp single
	{
	
	if (NBunches + 8 >= Max_NBunches) // make more space !
		reallocate_topnodes();

	NBunches += 8;
	
	} // omp single

	#pragma omp for
	for (int i = 0; i < 8; i++) {

		int dest = first + i;

		D[dest].Bunch.Level = D[parent].Bunch.Level + 1;

		int shift = N_SHORT_BITS - 3 * D[dest].Bunch.Level;

		shortKey bitmask = 0x7ULL << shift;
		shortKey keyfragment = ((shortKey) i) << shift;

		D[dest].Bunch.Key = (D[parent].Bunch.Key & ~bitmask) | keyfragment;
		D[dest].Bunch.Npart = 0;
		D[dest].Bunch.First_Part = INT_MAX;
		D[dest].Bunch.Target = -1;
		D[dest].Bunch.Modify = false;
	}

	return ;
}

static void remove_empty_bunches(int *nTop_Leaves, int *max_level)
{
	int i = 0; 
	
	int n = NBunches, nLeaves = 0, max_lvl = -1;
	
	while (i < n) {
	
		if (D[i].Bunch.Npart == 0) {  // remove
	
			n--;
			
			memmove(&D[i], &D[i+1], (n-i) * sizeof(*D)); // fine for n == i 

			continue;
		} 

		if (D[i].Bunch.Npart <= 8)
			nLeaves++;

		max_lvl = imax(max_lvl, D[i].Bunch.Level);

		i++;
	}

	NBunches = n;
	*max_level = max_lvl;
	*nTop_Leaves = nLeaves;

	return ;
}

/*
 * update particle distribution over nBunches, starting for first_bunch.
 * Every thread works inside the omp buffer, which are later reduced
 */

static void fill_bunches(const int first_bunch, const int nBunches, 
		 const int first_part, const int nPart)
{
	struct Bunch_Node *b = Get_Thread_Safe_Buffer(nBunches*sizeof(*b));
	
	int run = first_bunch;

	for (int i = 0; i < nBunches; i++) {
	
		b[i].First_Part = INT_MAX;
		b[i].Key = D[run].Bunch.Key;

		run++;
	}
	
	run = 0;

	const int last_part = first_part + nPart;
	
	#pragma omp for nowait
	for (int ipart = first_part; ipart < last_part; ipart++) {
		
		double px = (P[ipart].Pos[0] - Domain.Origin[0]) / Domain.Size;
		double py = (P[ipart].Pos[1] - Domain.Origin[1]) / Domain.Size;
		double pz = (P[ipart].Pos[2] - Domain.Origin[2]) / Domain.Size;
		
		shortKey pkey = Short_Peano_Key(px, py, pz);

		while (b[run].Key < pkey)  // particles are ordered by key
			run++;
		
		b[run].Npart++;
		b[run].Cost += P[ipart].Cost;
		b[run].First_Part = imin(b[run].First_Part, ipart);
	}

	#pragma omp critical
	{
	
	const int last_bunch = first_bunch + nBunches;

	run = 0;
	
	for (int i = first_bunch; i < last_bunch; i++) {

		D[i].Bunch.Npart += b[run].Npart;
		D[i].Bunch.Cost += b[run].Cost;
		D[i].Bunch.First_Part = imin(D[i].Bunch.First_Part, b[run].First_Part);

		run++;
	}

	} // omp critical 

	#pragma omp barrier
	
	return ;
}

/*
 * This function defines the metric that decides if a bunch has to be refined
 * into eight sub-bunches. It also sets the target processor, by setting
 * Target to a negative MPI rank value+1.
 */

static bool Stop_Splitting = false;

static bool imbalance_small(const int nTop_Leaves)
{
	const int nHeavy_Leaves = NBunches - nTop_Leaves;

	const double mean_npart = Sim.Npart_Total 
								/ (Sim.NTask * DOMAIN_NBUNCHES_PER_THREAD);
	#pragma omp single
	{
	
	max_mem_imbal = 0, max_cpu_imbal = 0;
	
	Stop_Splitting = true;

	} // omp single

	#pragma omp for reduction(max : max_mem_imbal,max_cpu_imbal) \
					reduction(min : Stop_Splitting)
	for (int i = 0; i < NBunches; i++ ) {

		double rel_mem_load = (D[i].Bunch.Npart - mean_npart) / mean_npart;
		double rel_cpu_load = 0;

		max_mem_imbal = fmax(max_mem_imbal, rel_mem_load);
		max_cpu_imbal = fmax(max_cpu_imbal, rel_cpu_load);
		
		if (NBunches > Sim.NTask * 16) // too deep
			continue;
		else if (D[i].Bunch.Level == N_SHORT_TRIPLETS-1)
			continue;

		if (rel_mem_load > DOMAIN_SPLIT_MEM_THRES) 
			D[i].Bunch.Modify = 1;
		//else if (rel_cpu_load > DOMAIN_SPLIT_CPU_THRES) 
		//	D[i].Bunch.Modify = 1;
		else if (nHeavy_Leaves < Sim.NTask * DOMAIN_NBUNCHES_PER_THREAD) 
			D[i].Bunch.Modify = 1;  
		else 
			continue;

		Stop_Splitting = false; // here come the (de-)refinement recipies
	}

	return Stop_Splitting;
}

static int compare_bunches_by_key(const void *a, const void *b) 
{
	const struct Bunch_Node *x = (const struct Bunch_Node *) a;
	const struct Bunch_Node *y = (const struct Bunch_Node *) b;

	return (int) (x->Key > y->Key) - (x->Key < y->Key);
}

static int compare_bunches_by_target(const void *a, const void *b) 
{
	const struct Bunch_Node *x = (const struct Bunch_Node *) a;
	const struct Bunch_Node *y = (const struct Bunch_Node *) b;

	return (int) (x->Target > y->Target) - (x->Target < y->Target);
}

static int compare_bunches_by_npart(const void *a, const void *b) 
{
	const struct Bunch_Node *x = (const struct Bunch_Node *) a;
	const struct Bunch_Node *y = (const struct Bunch_Node *) b;

	return (int) (x->Npart > y->Npart) - (x->Npart < y->Npart);
}


static void communicate_particles()
{
	//Qsort(Sim.NThreads, D, NBunches, sizeof(*D), &compare_bunches_by_target);

	return ;
}

/*
 * Reduce the Bunch list over all MPI ranks 
 */

static void communicate_bunches()
{
	#pragma omp for
	for (int i = 0; i < NBunches; i++) {

		D[i].Bunch.Target = 0;

		D[i].Bunch.Is_Local = true;
	}
	
	// MPI_Ibcast();
	
	return ;
}
	

static void print_domain_decomposition (const int max_level)
{
	#pragma omp barrier
	#pragma omp flush(D)

	rprintf(" No | Split | npart  |   sum  | first  | trgt  | lvl |"
			"| Max PH key,   Max_level %d \n", max_level);
	
	size_t sum = 0;

	for (int i = 0; i < NBunches; i++) {

		sum += D[i].Bunch.Npart;

		rprintf("%3d |   %d   | %6zu | %6zu | %6d | %5d | %3d || ", 
				i, D[i].Bunch.Modify, D[i].Bunch.Npart, sum, 
				D[i].Bunch.First_Part, D[i].Bunch.Target, D[i].Bunch.Level);

		if (Task.Is_Master)
			Print_Int_Bits64(D[i].Bunch.Key);
	}

	#pragma omp barrier

#ifdef DEBUG
	Assert(sum == Sim.Npart_Total, "More particles in D than in Sim");
#endif

	return ;
}

/*
 * Find the global domain origin and the maximum extent. The 
 * domain is made slightly larger to avoid roundoff problems with the 
 * PH numbers. Not much to do for PERIODIC.
 */

double max_distance = 0;

static void find_global_domain_extend()
{
	#pragma omp barrier

	Find_Global_Center_Of_Mass(&Domain.Center_Of_Mass[0]);

#ifdef PERIODIC
	
	Domain.Origin[0] = Domain.Origin[1] = Domain.Origin[2] = 0;

	Domain.Size = fmax(Sim.Boxsize[0], fmax(Sim.Boxsize[1], Sim.Boxsize[2]));

	for (int i = 0; i < 3; i++)
		Domain.Center[i] = Domain.Origin[i] + 0.5 * Domain.Size;

#else // ! PERIODIC

	#pragma omp single
	max_distance = 0;

	#pragma omp for reduction(max:max_distance)
	for (int ipart = 0; ipart < Task.Npart_Total; ipart++) {
	
		for (int i = 0; i < 3; i++) {
		
			if (P[ipart].Pos[i] > max_distance)
				max_distance = P[ipart].Pos[i];
		
			if (-1*P[ipart].Pos[i] > max_distance)
				max_distance = -1*P[ipart].Pos[i];
		
		} // for i
	} // for ipart

	#pragma omp single
	{
	
	MPI_Allreduce(MPI_IN_PLACE, &max_distance, 1, MPI_DOUBLE, MPI_MAX, 
		MPI_COMM_WORLD);
	
	Domain.Size = 2.05 * max_distance;
	
	} // omp single

	#pragma omp flush

	for (int i = 0; i < 3; i++) {
	
		Domain.Origin[i] = - 0.5 * Domain.Size; 
		Domain.Center[i] = Domain.Origin[i] + 0.5 * Domain.Size ;
	}

#endif // ! PERIODIC

#ifdef DEBUG
	rprintf("\nDomain size is %g, \n"
			"   Origin at x = %4g, y = %4g, z = %4g, \n"
			"   Center at x = %4g, y = %4g, z = %4g. \n"
			"   CoM    at x = %4g, y = %4g, z = %4g. \n",
			Domain.Size, Domain.Origin[0], Domain.Origin[1], Domain.Origin[2],
			Domain.Center[0], Domain.Center[1], Domain.Center[2],
			Domain.Center_Of_Mass[0], Domain.Center_Of_Mass[1],
			Domain.Center_Of_Mass[2]);
#endif

	return ;
}

static double com_x = 0, com_y = 0, com_z = 0, m = 0;

void Find_Global_Center_Of_Mass(double *CoM_out)
{
	#pragma omp single
	com_x = com_y = com_z = m = 0;

	#pragma omp for reduction(+:com_x,com_y,com_z,m) 
	for (int ipart = 0; ipart < Task.Npart_Total; ipart++) {
	
		com_x += P[ipart].Mass * P[ipart].Pos[0];
		com_y += P[ipart].Mass * P[ipart].Pos[1];
		com_z += P[ipart].Mass * P[ipart].Pos[2];
		
		m += P[ipart].Mass;
	}

	double global_com[3] = { com_x, com_y, com_z  };
	double global_m = m;

	#pragma omp single 
	{
	
	MPI_Allreduce(MPI_IN_PLACE, &global_com, 3, MPI_DOUBLE, MPI_MIN,
			MPI_COMM_WORLD);
		
	MPI_Allreduce(MPI_IN_PLACE, &global_m, 1, MPI_DOUBLE, MPI_MIN,
			MPI_COMM_WORLD);

	for (int i = 0; i < 3; i++) 
		CoM_out[i] = global_com[i] / global_m;

	} // omp single

	#pragma omp flush

	return ;

}
