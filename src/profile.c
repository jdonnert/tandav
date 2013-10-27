#include "globals.h"
#include "proto.h"

#define MAXPROFILEITEMS 999		// Max number of profiling marks

static struct Profiling_Object {
	char Name[CHARBUFSIZE];
	double Tbeg;
	double Tend;
	double ThisLast;	// Last iteration this CPU
	double Total;		// Total time over all iterations / All CPUs
	double Min;			// Min time spend here by a CPU
	double Max;			// Max Time spend here by a CPU
	double Mean;		// Mean Time spend here by all CPUs
	double Imbalance;	// Time wasted waiting for the slowest CPU
} Prof[MAXPROFILEITEMS];

static int NProfObjs = 0;

static inline int find_index_from_name(const char *name);

void Init_Profiler()
{
	Profile("Whole Run");

	return ;
}

void Finish_Profiler() 
{
	Profile("Whole Run");

	Profile_Report();

	return ;
}

void Profile_Info(const char* file, const char* func, const int line, 
		const char *name)
{
	int i = find_index_from_name(name);

	if (i == NProfObjs) { // new item start profiling
		
		strncpy(Prof[i].Name, name, CHARBUFSIZE);

		NProfObjs++;

		Prof[i].Tbeg = MPI_Wtime();

	} else { // existing item, stop & reduce
		
		Prof[i].Tend = MPI_Wtime();
	
		Prof[i].ThisLast = Prof[i].Tend - Prof[i].Tbeg;

		MPI_Reduce(&Prof[i].ThisLast, &Prof[i].Min, 
				1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

		MPI_Reduce(&Prof[i].ThisLast, &Prof[i].Max, 
				1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

		MPI_Reduce(&Prof[i].ThisLast, &Prof[i].Mean, 
				1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
		Prof[i].Mean /= Sim.NTask;

		Prof[i].Total += Prof[i].Max;
		Prof[i].Imbalance += Prof[i].Max - Prof[i].Min;
	}

	return ;
}

void Profile_Report()
{
	if (Task.Rank)
		return; 

	double now = MPI_Wtime();

	printf("\nProfiler: All sections, total runtime of %g min\n"
		"    Name          Total    Tot Imbal       Max       Mean      Min  "
		"      Imbal\n", (now-Prof[0].Tbeg)/60);

	for (int i = 1; i < NProfObjs; i++ )
		printf("%12s    %8.1g   %8.1g      %8.1g  %8.1g  %8.1g   "
				"%8.1g\n",
				Prof[i].Name, Prof[i].Total/60,Prof[i].Imbalance/60,
				Prof[i].Max/60, Prof[i].Min/60, Prof[i].Mean/60, 
				(Prof[i].Max-Prof[i].Min)/60);

	return ;
}

void Profile_Report_Last()
{
	if (Task.Rank)
		return ; 

	const double now = MPI_Wtime();

	const int i = NProfObjs-1;

	printf("Profiler: Last section, total runtime of %g min\n"
			"Name	  	Total	Min	Max	Mean\n"
			"%s		: %g	%g	%g	%g\n", 
			(now-Prof[0].Tbeg)/60, Prof[i].Name, Prof[i].Total,
			Prof[i].Max, Prof[i].Min, Prof[i].Mean);

	return ;
}

void Write_Logs()
{
	return;
}

static inline int find_index_from_name(const char *name)
{
	int i = 0;

	for (i = 0; i < NProfObjs; i++)
		if (!strncmp(name, Prof[i].Name, CHARBUFSIZE))
			break;

	return i; // may return i=NProfObjs, i.e. new item
}