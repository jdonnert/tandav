#include "globals.h"
#include "proto.h"

static void preamble(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	preamble(argc, argv);	

	Read_Parameter_File(Param.File);

	Init();

	Read_Snapshot(Param.InputFile);

	Setup();

	Update(BEFORE_MAIN_LOOP);
	
	for (;;) {

		Kick_First_Halfstep();
		
		Update(AFTER_FIRST_KICK);

		Drift();

		//if (Time.Current == Time.NextSnap)
			Write_Snapshot();
		
		if (Time.Running == Time.Limit) {

			Write_Restart_File();
			
			break;
		}

		if (Time.Current == Time.End)
			break;

		Drift();

		Update(AFTER_DRIFT);

		Kick_Second_Halfstep();
		
		Update(AFTER_SECOND_KICK);
	}

	rprintf("Simulation Ends ... \n");

	MPI_Finalize();

	return EXIT_SUCCESS;
}

static void preamble(int argc, char *argv[])
{
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &Task.Rank);
	MPI_Comm_size(MPI_COMM_WORLD, &Sim.NTask);

#pragma omp parallel
    {
    Task.ThreadID = omp_get_thread_num();
    Sim.NThreads = omp_get_num_threads();
    }

	if (!Task.Rank) {
		printf("# Tandav #\n\n");

		printf("Using %d MPI tasks, %d OpenMP threads \n\n", 
				Sim.NTask, Sim.NThreads);
		
		print_compile_time_settings();

		Assert(argc >= 2, "Wrong number of arguments, let me help you: \n\n" 
			"USAGE: ./Tandav ParameterFile <StartFlag>\n\n"
			"    StartFlag   Action \n"
			"       0        Read IC and start simulation (default) \n"
			"       1        Read restart files and resume simulation \n"
			"       2        Read snapshot file and continue simulation \n"
			"       10       Dump a valid paramater file for this Config\n"
			);

	}

	strncpy(Param.File, argv[1], CHARBUFSIZE);
	
	if (argc > 2)
		Param.StartFlag = atoi(argv[2]);

	if (Param.StartFlag == 10) 
		Write_Parameter_File(Param.File); // dead end

	MPI_Barrier(MPI_COMM_WORLD);

	return ;
}	
