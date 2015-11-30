#include "../globals.h"
#include "../domain.h"
#include "gravity.h"
#include "gravity_tree.h"
#include "gravity_periodic.h"

#ifdef GRAVITY_TREE

static struct Walk_Data_Particle copy_send_from(const int ipart);
static void add_recv_to(const int ipart, const struct Walk_Data_Result);

static bool interact_with_topnode(const int, const struct Walk_Data_Particle, 
										struct Walk_Data_Result * restrict);
static void interact_with_topnode_particles(const int, 
		const struct Walk_Data_Particle, struct Walk_Data_Result * restrict);
static void interact(const Float, const Float *, const Float, 
										struct Walk_Data_Result * restrict);

static void gravity_tree_walk(const int, const struct Walk_Data_Particle, 
										struct Walk_Data_Result * restrict);
static void gravity_tree_walk_BH(const int, const struct Walk_Data_Particle, 
										struct Walk_Data_Result * restrict);

static void check_total_momentum();

const Float Epsilon = 105.0/32.0 * GRAV_SOFTENING;

/*
 * We do not walk referencing particles, but copy the required particle data
 * into a send buffer "send". The results are written into a sink buffer "recv".
 * This also makes communication easier.
 * These buffers interact with all local topnodes, either with the node 
 * directly, or with the particles it contains (small top node). Or, walk 
 * the tree and estimate gravitational acceleration using two different
 * opening criteria. Also open all nodes containing ipart to avoid large 
 * maximum errors. Barnes & Hut 1984, Springel 2006, Dehnen & Read 2012.
 */

void Gravity_Tree_Acceleration()
{
	Profile("Grav Tree Walk");

	rprintf("Tree acceleration ");

	#pragma omp for schedule(dynamic)
	for (int i = 0; i < NActive_Particles; i++) {

		int ipart = Active_Particle_List[i];

		const struct Walk_Data_Particle send = copy_send_from(ipart);
		struct Walk_Data_Result recv = { 0 };

		P[ipart].Acc[0] = P[ipart].Acc[1] = P[ipart].Acc[2] = 0;

		for (int j = 0; j < NTop_Nodes; j++) {
			
			if (interact_with_topnode(j, send, &recv))
				continue;

			//if (D[j].TNode.Target < 0) { // not local ?
			//
			//	export_particle_to_rank(ipart, -target-1);	
			//
			//	continue;
			//}

			if (D[j].TNode.Npart <= 8) { // open top leave

				interact_with_topnode_particles(j, send, &recv);

				continue;
			}

			int tree_start = D[j].TNode.Target;

			if (Sig.Use_BH_Criterion) 
				gravity_tree_walk_BH(tree_start, send, &recv);
			else
				gravity_tree_walk(tree_start, send, &recv);

		} // for j
	
		Gravity_Tree_Periodic(send, &recv); // PERIODIC

		add_recv_to(ipart, recv);

	} // for i

	rprintf(" done \n");

	check_total_momentum();

	Profile("Grav Tree Walk");

	#pragma omp barrier

	return ;
}

static struct Walk_Data_Particle copy_send_from(const int ipart)
{
	struct Walk_Data_Particle send = { 0 };

	send.ID = P[ipart].ID;

	send.Pos[0] = P[ipart].Pos[0];
	send.Pos[1] = P[ipart].Pos[1];
	send.Pos[2] = P[ipart].Pos[2];
	
	send.Acc = ALENGTH3(P[ipart].Acc);

	send.Mass = P[ipart].Mass;

	return send;
}

static void add_recv_to(const int ipart, const struct Walk_Data_Result recv)
{
	P[ipart].Acc[0] = recv.Grav_Acc[0];
	P[ipart].Acc[1] = recv.Grav_Acc[1];
	P[ipart].Acc[2] = recv.Grav_Acc[2];

#ifdef OUTPUT_PARTIAL_ACCELERATIONS
	P[ipart].Grav_Acc[0] = recv.Grav_Acc[0];
	P[ipart].Grav_Acc[1] = recv.Grav_Acc[1];
	P[ipart].Grav_Acc[2] = recv.Grav_Acc[2];
#endif

#ifdef GRAVITY_POTENTIAL
	P[ipart].Grav_Pot = recv.Grav_Potential;
#endif

	P[ipart].Cost = recv.Cost;

	return ;
}

/*
 * For top nodes far away, we don't have to do a tree walk or send the particle
 * around. Similar to the normal tree walk we first check if the top node 
 * contains the particle and then check the two criteria.
 */

static bool interact_with_topnode(const int j, 
		const struct Walk_Data_Particle send,
		struct Walk_Data_Result * restrict recv)
{
	const Float nSize = Domain.Size / ((Float)(1UL << D[j].TNode.Level));

	Float dr[3] = {D[j].TNode.Pos[0] - send.Pos[0],
				   D[j].TNode.Pos[1] - send.Pos[1],
				   D[j].TNode.Pos[2] - send.Pos[2]};
	
	if (fabs(dr[0]) < 0.6 * nSize) // inside subtree ? -> always walk
		if (fabs(dr[1]) < 0.6 * nSize)
			if (fabs(dr[2]) < 0.6 * nSize)
				return false; 

	dr[0] = D[j].TNode.CoM[0] - send.Pos[0];
	dr[1] = D[j].TNode.CoM[1] - send.Pos[1];
	dr[2] = D[j].TNode.CoM[2] - send.Pos[2];

	Periodic_Nearest(dr); // PERIODIC

	Float r2 = dr[0]*dr[0] + dr[1]*dr[1] + dr[2]*dr[2];

	Float node_mass = D[j].TNode.Mass;

	if (Sig.Use_BH_Criterion) {

		if (nSize*nSize > r2 * TREE_OPEN_PARAM_BH)
			return false;

	} else {

		Float fac = send.Acc/Const.Gravity * TREE_OPEN_PARAM_REL;

		if (node_mass*nSize*nSize > r2*r2 * fac)
			return false;
	}
	
	interact(node_mass, dr, r2, recv);

	return true;
}

/*
 * Top nodes with less than 8 particles point not to the tree but to P as 
 * targets
 */

static void interact_with_topnode_particles(const int j, 
		const struct Walk_Data_Particle send, 
		struct Walk_Data_Result * restrict recv)
{
	const int first = D[j].TNode.Target;
	const int last = first + D[j].TNode.Npart;

	for (int jpart = first; jpart < last; jpart++) {

		Float dr[3] = {P[jpart].Pos[0] - send.Pos[0],
					   P[jpart].Pos[1] - send.Pos[1] ,
			           P[jpart].Pos[2] - send.Pos[2] };

		Periodic_Nearest(dr); // PERIODIC
		
		Float r2 = p2(dr[0]) + p2(dr[1]) + p2(dr[2]);

		if (r2 != 0)
			interact(P[jpart].Mass, dr, r2, recv);
	}

	return ;
}

/*
 * This function walks the local tree and computes the gravitational 
 * acceleration using the relative opening criterion (Springel 2005).
 * If we encounter a particle bundle we interact with all of them.
 */

static void gravity_tree_walk(const int tree_start, 
		const struct Walk_Data_Particle send, 
		struct Walk_Data_Result * restrict recv)
{
	const Float fac = send.Acc / Const.Gravity * TREE_OPEN_PARAM_REL;

	const int tree_end = tree_start + Tree[tree_start].DNext;

	int node = tree_start;

	while (node != tree_end) {

		if (Tree[node].DNext < 0) { // encountered particle bundle

			int first = -Tree[node].DNext - 1; // part index is offset by 1
			int last = first + Tree[node].Npart;

			for (int jpart = first; jpart < last; jpart++ ) {

				Float dr[3] = {P[jpart].Pos[0] - send.Pos[0],
								P[jpart].Pos[1] - send.Pos[1],
								P[jpart].Pos[2] - send.Pos[2]};
				
				Periodic_Nearest(dr); // PERIODIC

				Float r2 = dr[0]*dr[0] + dr[1]*dr[1] + dr[2]*dr[2];
				
				if (r2 != 0) 
					interact(P[jpart].Mass, dr, r2, recv);
			}

			node++;

			continue;
		}

		Float dr[3] = {Tree[node].CoM[0] - send.Pos[0],
					   Tree[node].CoM[1] - send.Pos[1],
					   Tree[node].CoM[2] - send.Pos[2]};
		
		Periodic_Nearest(dr); // PERIODIC

		Float r2 = dr[0]*dr[0] + dr[1]*dr[1] + dr[2]*dr[2];

		Float nMass = Tree[node].Mass;

		Float nSize = Node_Size(node); // now check opening criteria

		if (nMass*nSize*nSize > r2*r2 * fac) { // relative criterion
			
			node++;

			continue;
		}

		Float ds[3] = {Tree[node].Pos[0] - send.Pos[0],
					   Tree[node].Pos[1] - send.Pos[1],
					   Tree[node].Pos[2] - send.Pos[2]};

		if (fabs(ds[0]) < 0.6 * nSize) {  

			if (fabs(ds[1]) < 0.6 * nSize) {

				if (fabs(ds[2]) < 0.6 * nSize) {

					node++;

					continue;
				}
			}
		}

		interact(nMass, dr, r2, recv); // use node

		node += Tree[node].DNext; // skip branch

	} // while

	return ;
}

/*
 * Walk tree and use the B&H opening criterion, which does not require a prior
 * particle acceleration.
 */

static void gravity_tree_walk_BH(const int tree_start, 
		const struct Walk_Data_Particle send, 
		struct Walk_Data_Result * restrict recv)
{
	int node = tree_start;

	while (Tree[node].DNext != 0 || node == tree_start) {

		if (Tree[node].DNext < 0) { // encountered particle bundle

			int first = -(Tree[node].DNext + 1); // part index is offset by 1
			int last = first + Tree[node].Npart;

			for (int jpart = first; jpart < last; jpart++ ) {

				Float dr[3] = {P[jpart].Pos[0] - send.Pos[0],
							    P[jpart].Pos[1] - send.Pos[1],
					            P[jpart].Pos[2] - send.Pos[2]};

				Periodic_Nearest(dr); // PERIODIC

				Float r2 = p2(dr[0]) + p2(dr[1]) + p2(dr[2]);

				Float mpart = P[jpart].Mass;

				if (r2 != 0)
					interact(mpart, dr, r2, recv);
			}

			node++;

			continue;
		}

		Float dr[3] = {Tree[node].CoM[0] - send.Pos[0],
					   Tree[node].CoM[1] - send.Pos[1],
					   Tree[node].CoM[2] - send.Pos[2]};

		Periodic_Nearest(dr); // PERIODIC

		Float r2 = p2(dr[0]) + p2(dr[1]) + p2(dr[2]);

		Float nMass = Tree[node].Mass;

		Float nSize = Node_Size(node); // now check opening criteria

		if (nSize*nSize > r2 * TREE_OPEN_PARAM_BH) { // BH criterion

			node++; // open

			continue;
		}


		interact(nMass, dr, r2, recv); // use node

		node += fmax(1, Tree[node].DNext);

	} // while

	return ;
}


/*
 * Gravitational force law using Dehnens K1 softening kernel with central 
 * value corresponding to Plummer softening.
 */

static void interact(const Float mass, const Float dr[3], const Float r2, 
		struct Walk_Data_Result * restrict recv)
{

	double r_inv = 1/sqrt(r2);
	double r = 1/r_inv;

#ifdef GRAVITY_POTENTIAL
	double r_inv_pot = r_inv;
#endif

	if (r < Epsilon) { 

		double u = r/Epsilon;
		double u2 = u*u;

		r_inv = sqrt(u * (135*u2*u2 - 294*u2 + 175))/(4*Epsilon) ;

#ifdef GRAVITY_POTENTIAL
		r_inv_pot = (7*u2 - 21*u2*u2 + 28*u3*u2 - 15*u3*u3 + u3*u3*u*8 - 3) 
			/ Epsilon;
#endif
	}

	double acc_mag = Const.Gravity * mass * p2(r_inv);

	recv->Grav_Acc[0] += acc_mag * dr[0] * r_inv;
	recv->Grav_Acc[1] += acc_mag * dr[1] * r_inv;
	recv->Grav_Acc[2] += acc_mag * dr[2] * r_inv;

#ifdef GRAVITY_POTENTIAL
	recv->Grav_Potential += Const.Gravity * mass * r_inv_pot;
#endif

	recv->Cost++;

	return ;
}

/*
 * Bitfield functions on global Tree
 */

Float Node_Size(const int node)
{
	int lvl = Tree[node].Bitfield & 0x3F; // level

	return Domain.Size / ((Float) (1ULL << lvl)); // Domain.Size/2^level
}

int Level(const int node)
{
	return Tree[node].Bitfield & 0x3FUL; // return bit 0-5
}

bool Node_Is(const enum Tree_Bitfield bit, const int node)
{
	return Tree[node].Bitfield & (1UL << bit);
}

void Node_Set(const enum Tree_Bitfield bit, const int node)
{
	Tree[node].Bitfield |= 1UL << bit;

	return ;
}

void Node_Clear(const enum Tree_Bitfield bit, const int node)
{
	Tree[node].Bitfield &= ~(1UL << bit);

	return ;
}

/*
 * Here start MPI communication variables and routines. 
 */


/*
 * Compute total momentum to check the gravity interaction. 
 */

static double Px = 0, Py = 0, Pz = 0, Last = 0;

static void check_total_momentum()
{
	#pragma omp for reduction(+: Px, Py, Pz)
	for (int ipart = 0; ipart < Task.Npart_Total; ipart++) {
	
		Px += P[ipart].Mass * P[ipart].Vel[0];
		Py += P[ipart].Mass * P[ipart].Vel[1];
		Pz += P[ipart].Mass * P[ipart].Vel[2];
	}
	
	double ptotal = sqrt( Px*Px + Py*Py + Pz*Pz );

	double rel_err = (ptotal - Last) / Last;

	rprintf("Total err. due to gravity : %g \n", rel_err);

	#pragma omp single
	Last = ptotal;

	return ;
}

#endif // GRAVITY_TREE
