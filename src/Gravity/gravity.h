
#if defined(GRAVITY) && defined(GRAVITY_SIMPLE)
void Gravity_Simple_Accel();
#else
inline void Gravity_Simple_Accel() {};
#endif // GRAVITY && GRAVITY_SIMPLE

#if defined(GRAVITY) && defined(GRAVITY_TREE)
void Setup_Gravity_Tree();
void Gravity_Tree_Build();
void Gravity_Tree_Acceleration();
void Gravity_Tree_Update_Kicks(const int ipart, const double dt);
void Gravity_Tree_Update_Topnode_Kicks();
void Gravity_Tree_Update_Drift(const double dt);

extern struct Tree_Node {
	int DNext;			// Distance to the next node; or particle -DNext-1
	uint32_t Bitfield;	// bit 0-5:level, 6-8:key, 9:local, 10:top, 11-31:free
	int DUp;			// Number of nodes to the parent
	int Npart;			// Number of particles in node
	Float Pos[3];		// Node Center
	Float Mass;			// Total Mass of particles inside node
	Float CoM[3];		// Center of Mass
	Float Dp[3];		// Velocity of Center of Mass
} * restrict Tree;

uint32_t NNodes;

struct Walk_Data_Particle { // stores exported particle data
	ID_t ID;
	Float Pos[3];
	Float Acc; 				// only magnitude of the last acceleration
	Float Mass;
};

struct Walk_Data_Result { 	// stores exported summation results
	Float Cost;
	double Grav_Acc[3];
#ifdef GRAVITY_POTENTIAL
	double Grav_Potential;
#endif
};

#else

inline void Setup_Gravity_Tree() {}; 
inline void Gravity_Tree_Build() {};
inline void Gravity_Tree_Acceleration() {};
inline void Gravity_Tree_Update_Kicks(const int ipart, const double dt) {};
inline void Gravity_Tree_Update_Topnode_Kicks() {};
inline void Gravity_Tree_Update_Drift(const double dt) {};

#endif // (GRAVITY && GRAVITY_TREE)

#if defined(GRAVITY) && defined(GRAVITY_TREE) && defined(PERIODIC)
void Gravity_Tree_Periodic(const struct Walk_Data_Particle,
								struct Walk_Data_Result *);
void Tree_Periodic_Nearest(Float dr[3]);
#else
inline void Gravity_Tree_Periodic(const struct Walk_Data_Particle Send,
								struct Walk_Data_Result *Recv) {};
inline void Tree_Periodic_Nearest(Float dr[3]) {};
#endif

#if defined(GRAVITY) && defined(GRAVITY_MULTI_GRID)
void Gravity_Multi_Grid();
#else
inline void Gravity_Multi_Grid() {};
#endif // GRAVITY_MULTI_GRID

