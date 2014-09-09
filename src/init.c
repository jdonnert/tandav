/* Initialise global variables */
#include "globals.h"
#include "io/io.h"
#include "domain.h"

struct Global_Simulation_Properties Sim;
struct Parameters_From_File Param; 
struct Simulation_Signals Sig;

#pragma omp threadprivate(Task)
struct Local_Task_Properties Task = { 0 };

struct Particle_Data *P = NULL;

void Read_and_Init() 
{
	Read_Parameter_File(Param.File);
	
 	Init_Profiler();

	Init_Memory_Management();

	Init_Domain_Decomposition();

	P = Malloc(Task.Npart_TotalMax*sizeof(*P));

	switch (Param.Start_Flag) {

		case 0: 

			Read_Snapshot(Param.InputFile);
			
			break;

		case 1: 
			
			Read_Restart_File();
			
			break;

		default: 
			
			Assert(0, "Start Flag not handled");
	}
	
	return ;
}

