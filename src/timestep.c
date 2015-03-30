#include "globals.h"
#include "timestep.h"

/* 
 * The number of bins is given by the number of bits in an integer time 
 */

#define N_INT_BINS (sizeof(intime_t) * CHAR_BIT)
#define COUNT_TRAILING_ZEROS(x) __builtin_ctzll(x)

static int max_active_time_bin();
static void set_particle_timebins();
static void set_system_timestep();
static int timestep2timebin(const double dt);
static void print_timebins();

static float get_global_timestep_constraint();
static float get_physical_timestep(const int);
static float cosmological_timestep(const int ipart, const Float accel);

struct TimeData Time = { 0 };
struct IntegerTimeLine Int_Time = { 0 };

static int Time_Bin_Min = N_INT_BINS-1, Time_Bin_Max = 0;

/* 
 * All active particles get a new step that is smaller than or equal to 
 * the largest active bin. We also set the fullstep signal. 
 */

void Set_New_Timesteps()
{
	Profile("Timesteps");

	set_particle_timebins();

	#pragma omp single
	{

	MPI_Allreduce(MPI_IN_PLACE, &Time_Bin_Min, 1, MPI_INT, MPI_MIN,
			MPI_COMM_WORLD);

	MPI_Allreduce(MPI_IN_PLACE, &Time_Bin_Max, 1, MPI_INT, MPI_MAX,
			MPI_COMM_WORLD);

	set_system_timestep(Time_Bin_Max, Time_Bin_Min);

	Time.Max_Active_Bin = max_active_time_bin();

	} // omp single

	Sig.Fullstep = false;

	if (Int_Time.Current == Int_Time.Next_Full_Step) {

		Sig.Fullstep = true;

		#pragma omp barrier

		#pragma omp single
		Int_Time.Next_Full_Step = Umin(Int_Time.End,
						Int_Time.Current + (1ULL << Time_Bin_Max) );
	}

	Make_Active_Particle_List();

	#pragma omp master
	print_timebins();

	#pragma omp barrier

	Profile("Timesteps");

	return ;
}

/* 
 * The timeline is represented by an integer, where an increment of one 
 * corresponds to the whole integration time divided by 2^(N_INT_BINS-1).
 * In comoving coordinates the timesteps are divided in log space, which
 * means, we are effectively stepping in redshift. Time integration is in da.
 */

void Setup_Time_Integration()
{
	Time.Next_Snap = Time.First_Snap;

	Time.NSnap = (Time.End - Time.Begin)/Time.Bet_Snap + 1;

	rprintf("\nSimulation timeline: \n"
			"   start = %g, end = %g, delta = %g, NSnap = %d \n",
			Time.Begin, Time.End, Time.Bet_Snap, Time.NSnap);

#ifdef COMOVING
	rprintf("   initial redshift = %g, final redshift = %g \n\n",
			1.0/Time.Begin - 1, 1.0/Time.End - 1);
#endif // COMOVING

	Assert(Time.NSnap > 0, "Timeline does not seem to produce any outputs");

	Int_Time.Beg = (intime_t) 0;
	Int_Time.End = (intime_t) 1 << (N_INT_BINS - 1);
	Int_Time.Current = Int_Time.Beg;

#ifdef COMOVING
	Time.Step_Max = log(Time.End) - log(Time.Begin); // step in log(a)
#else
	Time.Step_Max = Time.End - Time.Begin;
#endif // ! COMOVING

	Time.Step_Min = Time.Step_Max / Int_Time.End;

	Time.Current = Integer_Time2Integration_Time(Int_Time.Beg);

	Time.Max_Active_Bin = N_INT_BINS - 1;

	size_t nBytes = Task.Npart_Total_Max * sizeof(*Active_Particle_List);

	Active_Particle_List = Malloc(nBytes, "Active Part List");

	NActive_Particles = Task.Npart_Total;

	for (int i = 0; i < NActive_Particles; i++)
		Active_Particle_List[i] = i;
 
	return ;
}

/* 
 * Find smallest allowed timestep for local particles given the time step 
 * criteria. Find local max & min to these bins. 
 */

static float dt_max = 0;

static void set_particle_timebins()
{
	#pragma omp single
	{

	Time_Bin_Min = N_INT_BINS-1;
	Time_Bin_Max = 0;

	} // omp single

	if (Sig.Fullstep || Sig.First_Step)
		dt_max = get_global_timestep_constraint(); 

	#pragma omp for reduction(min:Time_Bin_Min) reduction(max:Time_Bin_Max)
	for (int i = 0; i < NActive_Particles; i++) {

		int ipart = Active_Particle_List[i];

		float dt = get_physical_timestep(ipart);

		dt = fmin(dt, dt_max);

#ifdef COMOVING
		dt *= Cosmo.Hubble_Parameter; // convert dt to dln(a)
#endif
	
		Assert(dt >= Time.Step_Min, "Timestep too small or not finite ! \n"
				"        ipart=%d, ID=%d, dt=%g, acc=(%g,%g,%g)",
				ipart, P[ipart].ID, dt,
				P[ipart].Acc[0], P[ipart].Acc[1], P[ipart].Acc[2]);

		int want = timestep2timebin(dt);

		int allowed = MAX(Time.Max_Active_Bin, P[ipart].Time_Bin);

		P[ipart].Time_Bin = MIN(want, allowed);

		Time_Bin_Min = MIN(Time_Bin_Min, P[ipart].Time_Bin);
		Time_Bin_Max = MAX(Time_Bin_Max, P[ipart].Time_Bin);
	}

	//#pragma omp single nowait
	//MPI_Allreduce(MPI_IN_PLACE, Time_Bin_Max, MPI_FLOAT, MPI_MAX, 
	//	MPI_COMM_WORLD);
	
	//#pragma omp single 
	//MPI_Allreduce(MPI_IN_PLACE, Time_Bin_Min, MPI_FLOAT, MPI_MIN, 
	//	MPI_COMM_WORLD);

	#pragma omp flush

	return ;
}

/*
 * Set global system timestep. We also have to consider
 * the first and last step separately and stay in sync with the timeline,
 * i.e. we can choose a longer timestep only if it the next time is a 
 * multiple of it.
 */

static void set_system_timestep()
{
	intime_t step_bin = (intime_t) 1 << Time_Bin_Min; // step down ?

	intime_t step_sync = 1ULL << COUNT_TRAILING_ZEROS(Int_Time.Current);

	if (Int_Time.Current == Int_Time.Beg) // treat beginning t0
		step_sync = step_bin;

	intime_t step_end = Int_Time.End - Int_Time.Current; // don't overstep end

	Int_Time.Step = umin(step_end, umin(step_bin, step_sync));

	Int_Time.Next += Int_Time.Step;

	Time.Next = Integer_Time2Integration_Time(Int_Time.Next);

	Time.Step = Time.Next - Time.Current; // in a

	if (Sig.First_Step)
		Time.Max_Active_Bin = Time_Bin_Min; // correct first step

	return ;
}

/*
 * The highest active time bin is the last set bit in the current
 * integer time.  
 */

static int max_active_time_bin()
{
	return COUNT_TRAILING_ZEROS(Int_Time.Next);
}

void Make_Active_Particle_List()
{
	int i = 0;

	#pragma omp single
	{

	for (int ipart = 0; ipart < Task.Npart_Total; ipart++) {

		if (P[ipart].Time_Bin <= Time.Max_Active_Bin)
			Active_Particle_List[i++] = ipart;
	}

	NActive_Particles = i;

	Assert(NActive_Particles > 0,
			"No Active Particles, instead %d, bin max %d"
			, i, Time.Max_Active_Bin);

	} // omp single

	return ;
}

/* 
 * Give the integration timestep from timebin and convert from integer to 
 * integration time. In comoving coordinates/cosmological simulations we 
 * multi-step in "dlog(a) = 1+z. We return here dln(a) from the timebin. 
 * Note that dt = dlog(a) / H(a).
 */

#ifdef COMOVING


double Timebin2Timestep(const int TimeBin)
{
	return Time.Step_Min*((intime_t) 1 << TimeBin); // in dlog(a)
}

double Integer_Time2Integration_Time(const intime_t Integer_Time)
{
	return Time.Begin * exp(Integer_Time * Time.Step_Min); // a
}

double Integer2Physical_Time(const intime_t Integer_Time)
{
	return Integer_Time2Integration_Time(Integer_Time) / Cosmo.Hubble_Parameter;
}

#else // ! COMOVING

double Timebin2Timestep(const int TimeBin)
{
	return Time.Step_Min * ((intime_t) 1 << TimeBin);
}

double Integer2Physical_Time(const intime_t Integer_Time)
{
	return Time.Begin + Integer_Time * Time.Step_Min;
}

double Integer_Time2Integration_Time(const intime_t Integer_Time)
{
	return Integer2Physical_Time(const intime_t Integer_Time);
}

#endif // ! COMOVING


/* 
 * Convert a timestep to a power 2 based timebin via ceil(log2(StepMax/dt)) 
 */

static int timestep2timebin(const double dt)
{
	int exponent;

	frexp(Time.Step_Max/dt, &exponent);

	return N_INT_BINS - 1 - exponent - 1;
}

static void print_timebins()
{
	int npart[N_INT_BINS] = { 0 };

	for (int ipart = 0; ipart < Task.Npart_Total; ipart++)
		npart[P[ipart].Time_Bin]++;

	int npart_global[N_INT_BINS] = { 0 };

	MPI_Reduce(npart, npart_global, N_INT_BINS, MPI_INT, MPI_SUM, Sim.Master,
			MPI_COMM_WORLD);

	if (!Task.Is_MPI_Master)
		goto skip;

	int imin = -1, imax = -1;

	for (int i = 0; i < N_INT_BINS; i++)
		if (npart_global[i] != 0 && imin < 0)
			imin = i;

	for (int i = N_INT_BINS-1; i > -1; i--)
		if (npart_global[i] != 0 && imax < 0)
			imax = i;

	char step[CHARBUFSIZE] = {"Step"};

	if (Sig.Fullstep)
		sprintf(step,"Fullstep ");

#ifdef COMOVING
	rprintf("\n%s <%d> a = %g -> %g\n\n"
			"NActive %d, z = %g, da = %g \n"
	    	"   Bin       nGas        nDM A    dlog(a)\n",
			step, Time.Step_Counter++, Time.Current,
			Integer_Time2Integration_Time(Int_Time.Next),
			NActive_Particles, 1/Time.Current-1,Time.Step);
#else
	rprintf("\n%s <%d> t = %g -> %g\n\n"
			"Systemstep %g, NActive %d \n"
	    	"   Bin       nGas        nDM A    dt\n",
			step, Time.Step_Counter++, Time.Current,
			Integer_Time2Integration_Time(Int_Time.Next),
			Time.Step, NActive_Particles );
#endif // ! COMOVING

	for (int i = imax; i > Time.Max_Active_Bin; i--)
		printf("   %2d    %7d     %7d %s  %16.12f \n",
			i, 0, npart_global[i], " ", Timebin2Timestep(i));

	for (int i = Time.Max_Active_Bin; i >= imin; i--)
		printf("   %2d    %7d     %7d %s  %16.12f \n",
			i, 0, npart_global[i], "X", Timebin2Timestep(i));

	printf("\n");

	if (Sig.Fullstep)
		rprintf("Next full step at t = %g \n\n",
				Integer_Time2Integration_Time(Int_Time.Next_Full_Step));

	skip:;

	return ;
}

#undef N_INT_BINS
#undef COUNT_TRAILING_ZEROS

/*
 * Collect all timesteps 
 */

static float get_physical_timestep(const int ipart)
{
	const Float acc_phys = Acceleration_Physical(ipart);

	float dt = FLT_MAX;

#ifdef GRAVITY
	float dt_cosmo = cosmological_timestep(ipart, acc_phys);

	dt = fmin(dt, dt_cosmo);
#endif
	
	// add yours here

	return dt;
}

/* 
 * Cosmological N-body step, Dehnen & Read 2011, eq 21
 */


static float cosmological_timestep(const int ipart, const Float acc_phys)
{
#ifdef COMOVING
	return TIME_INT_ACCURACY * sqrt(2 * Cosmo.Expansion_Factor
			* GRAV_SOFTENING / acc_phys);
#else
	return TIME_INT_ACCURACY * sqrt(2 * GRAV_SOFTENING / accel);
#endif //  COMOVING
}

/* 
 * Compute timestep constraints based on global properties.
 * In cosmological simulations the timestep has to be bound by the maximum 
 * displacement given the rms velocity of all particles. The maximum 
 * displacement is set relative to the mean particle separation.
 */

static double v[NPARTYPE] = { 0 }, min_mpart[NPARTYPE] = { 0 };
static long long npart[NPARTYPE] = { 0 };

static float get_global_timestep_constraint()
{	
	float dt_max = Time.Step_Max;

#ifdef COMOVING

	#pragma omp single
	{
		memset(v, 0, sizeof(*v)*NPARTYPE);
		memset(min_mpart, 0, sizeof(*min_mpart)*NPARTYPE);
		memset(npart, 0, sizeof(*npart)*NPARTYPE);

	} // omp single

	double v_thread[NPARTYPE] = { 0 }, min_mpart_thread[NPARTYPE] = { 0 };
	long long npart_thread[NPARTYPE] = { 0 };

	#pragma omp for nowait
	for (int ipart = 0; ipart < Task.Npart_Total; ipart++) {
	
		int type = P[ipart].Type;

		v_thread[type] += ALENGTH3(P[ipart].Vel);
		min_mpart_thread[type] = fmin(min_mpart[type], P[ipart].Mass);
		npart_thread[type]++;
	}

	#pragma omp critical // array-reduce *rolleyes*
	{
	
	for (int type = 0; type < NPARTYPE; type++) {
		
		v[type] += v_thread[type];
		min_mpart[type] = fmin(min_mpart_thread[type], min_mpart[type]);
		npart[type] += npart_thread[type];
	}

	} // omp critical
	
	#pragma omp barrier

	MPI_Allreduce(MPI_IN_PLACE, v, NPARTYPE, MPI_DOUBLE, MPI_SUM, 
			MPI_COMM_WORLD);
	MPI_Allreduce(MPI_IN_PLACE, min_mpart, NPARTYPE, MPI_DOUBLE, MPI_MIN, 
			MPI_COMM_WORLD);
	MPI_Allreduce(MPI_IN_PLACE, npart, NPARTYPE, MPI_LONG_LONG, MPI_SUM, 
			MPI_COMM_WORLD);

	double rho_baryon = Cosmo.Omega_Baryon*Cosmo.Critical_Density;
	double rho_rest = (Cosmo.Omega_0 - Cosmo.Omega_Baryon) 
													* Cosmo.Critical_Density;

	rprintf("Time Displacement Constraint at a = %g \n", Time.Current);

	#pragma omp master
	for (int type = 0; type < NPARTYPE; type++) {
	
		double dmean = 0;

		switch (type) {
		
		case 0: // gas

			dmean = pow( min_mpart[type]/rho_baryon, 1.0/3.0);

			break;

		default: 
			
			dmean = pow( min_mpart[type]/rho_rest, 1.0/3.0);
			
			break;
		}
	
		double vrms = sqrt( v[type]/npart[type] );

		double dt = TIME_DISPL_CONSTRAINT * dmean / vrms 
			* p2(Cosmo.Expansion_Factor) * Cosmo.Hubble_Parameter ;
		
		if (npart[type] != 0)
			rprintf("   Type %d: dmean %g, min(mpart) %g, sqrt(v^2) %g,"
					" dlogmax %g \n", type, dmean, min_mpart[type], vrms, dt);		

		dt_max = fmax(dt, dt_max);
	}

	rprintf("   choosing %g \n", dt_max);

#endif // COMOVING

	return dt_max;
}
