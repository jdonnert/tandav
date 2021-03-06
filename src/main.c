#include "includes.h"
#include "init.h"
#include "setup.h"
#include "timestep.h"
#include "kick.h"
#include "drift.h"
#include "update.h"
#include "accel.h"
#include "domain.h"
#include "IO/io.h"
#include "IO/parameter_file.h"

static void preamble(int argc, char *argv[]);

/* 
 * This exposes the time integration of the code. 
 * We use the HOLD integrator from Pelupessy+ 2012. 
 */

extern double arr[10];
int comp(const void *a, const void *b )
{
	double *x = (double *)a;
	double *y = (double *)b;
	
	return (int)(*x > *y) - (*x < *y) ;
}

int main(int argc, char *argv[])
{
	preamble(argc, argv);
	
	Read_and_Init(argc, argv);

	Setup();

	#pragma omp parallel default(shared)
	{

	if (Sig.Restart_Continue) {

		Update(RESTART_CONTINUE);

		goto Restart_Continue; // hop !
	}

	Update(BEFORE_MAIN_LOOP);
	
	while (! Time_Is_Up()) { // run Forest run !

		Update(BEFORE_STEP);

		Set_New_Timesteps();
		
		Kick_First_Halfstep();

		if (Time_For_Snapshot()) {

			Drift_To_Snaptime();

			Write_Snapshot();
		}

		Drift_To_Sync_Point();
		
		Update(AFTER_DRIFT);

		if (Runtime_Limit_Reached()) 
			break;

		Restart_Continue:

		if (Time_For_Domain_Update()) {

			Update(BEFORE_DOMAIN_UPDATE);

			Domain_Decomposition();
		}

		Compute_Acceleration();

		Kick_Second_Halfstep();

		Update(AFTER_STEP);
	}

	if (Time_For_Snapshot())
		Write_Snapshot();

	if (Sig.Restart_Write_File)
		Write_Restart_File();

	} // omp parallel 

	Finish();

	return EXIT_SUCCESS;
}

/* 
 * Here we do OpenMP and MPI init and handle the command line args. 
 * We are using full thread parallelism, i.e. every thread is an MPI rank and
 * takes part in the MPI communication. Hence, every thread needs to have an 
 * unique ID: Task.ID, an MPI rank: Task.Rank, and a thread ID: Task.Thread_ID
 * There is a global MPI master with Task.Is_MPI_Master == true, used only for
 * printing messages. On every MPI rank there is a main thread on which 
 * Task.Is_Thread_Main == true. 
 * Always use Task.Rank inside an omp single region. In a parallel region
 * use Task.ID to uniquely identify a thread across the whole machine.
 */

static void preamble(int argc, char *argv[])
{
	int provided = 0;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

	Assert(provided == MPI_THREAD_MULTIPLE,
		   "MPI thread multiple not supported, have %d :-(", provided);

	MPI_Comm_rank(MPI_COMM_WORLD, &Task.Rank);
	MPI_Comm_size(MPI_COMM_WORLD, &NRank);

	MPI_Is_thread_main(&Task.Is_Thread_Main);

	#pragma omp parallel
	{
	
	#pragma omp single
	{
	
	NThreads = omp_get_num_threads();
	NTask = NRank * NThreads;

	} // omp single

	Task.Thread_ID = omp_get_thread_num();

	if (Task.Rank == MASTER && Task.Thread_ID == MASTER)
		Task.Is_Master = true;

	if (Task.Rank == MASTER)
		Task.Is_MPI_Master = true;

	Task.Seed[2] = 14041981L * Task.Thread_ID; // init thread safe rng

	erand48(Task.Seed); // remove first 0 in some implementations

	} // omp parallel

	if (Task.Is_Master) {

		printf("#### Tandav ####\n\n");

		Print_Compile_Time_Settings();

		printf("\nsizeof(*D) = %zu byte\n", sizeof(*D)*CHAR_BIT/8);

#ifdef GRAVITY_TREE
		printf("sizeof(*Tree) = %zu byte\n", sizeof(*Tree)*CHAR_BIT/8);
#endif

		printf("\nUsing %d MPI tasks, %d OpenMP threads \n\n",
				NRank, NThreads);

		Assert( (argc >= 2) && (argc < 5),
			"Wrong number of arguments, let me help you: \n\n"
			"	USAGE: ./Tandav ParameterFile <StartFlag> <SnapNum>\n\n"
			"	  0  : Read IC file and start simulation (default) \n"
			"	  1  : Read restart files and resume  \n"
			"	  2  : Read snapshot file <SnapNum> and continue \n"
			"	 10  : Dump a valid parameter file for this Config\n");
	}

	strncpy(Param.File, argv[1], CHARBUFSIZE);

	if (argc > 2) // Start Flag given, == 0 otherwise
		Param.Start_Flag = atoi(argv[2]);

	if (Param.Start_Flag == DUMP_PARFILE) {

		Write_Parameter_File(Param.File); // dead end

		Finish();
	}

	MPI_Barrier(MPI_COMM_WORLD);

	return ;

}


