#include "globals.h"

void Finish()
{
	Finish_Profiler();
	
	Finish_Memory_Management();

	MPI_Finalize();

	return;
}
