#include "../globals.h"
#include "../proto.h"
#include "io.h"

void sanity_check_input_parameters();

void Read_Parameter_File(const char *filename)
{
	char buf[CHARBUFSIZE] = { "" }, 
		 buf1[CHARBUFSIZE]= { "" }, 
		 buf2[CHARBUFSIZE]= { "" },
		 buf3[CHARBUFSIZE]= { "" };

	bool tagDone[9999] = { false };
	
	if (Task.Is_Master) {

		FILE *fd = fopen(filename, "r");

		Assert(fd != NULL, "Parameter file not found %s \n", filename);
			
		printf("\nReading Parameter file '%s' \n", filename);
			
		while (fgets(buf, CHARBUFSIZE, fd)) {

			sscanf(buf, "%s %s %s", buf1, buf2,buf3) ;

			if (buf1[0] == '%') // commented out
				continue;
			
			int j = -1;

			for (int i = 0; i < NTags; i++) {
				
				int tagNotFound = strcmp(buf1, ParDef[i].tag);
				
				if (!tagNotFound && !tagDone[i]) {

					j = i;

					tagDone[i] = true;
					
					break;
				}
			}

			if (j < 0) // don't know this one, skip
				continue;
				
			printf(" %20s  %s\n", buf1, buf2);

			switch (ParDef[j].type) {

			case COMMENT:
				break;

			case DOUBLE:
				
				*((double *)ParDef[j].addr) = atof(buf2);

				break;

			case STRING:
			
				strncpy((char *)ParDef[j].addr, buf2, CHARBUFSIZE); 

				break;

			case INT:

				*((int *)ParDef[j].addr) = atoi(buf2);
				
				break;

			default:
				Assert(false,"Code Error in ParDef struct: %s", ParDef[j].tag);
			}
		}
		
		fclose(fd);
		
		printf("\n");

		for (int i = 0; i < NTags; i++) // are we missing one ?
           	Assert(tagDone[i] || ParDef[i].type == COMMENT, 
				"Value for tag '%s' missing in parameter file '%s'.\n",
				ParDef[i].tag, filename );

		sanity_check_input_parameters();
	}
	
	MPI_Bcast(&Param, sizeof(Param), MPI_BYTE, MASTER, MPI_COMM_WORLD);

	return ;
}

void Write_Parameter_File(const char *filename)
{
	if (Task.Is_Master) {

		printf("\nWriting Parameter file: %s \n", filename);

		FILE *fd = fopen(filename, "w");

		Assert(fd != NULL, "Can't open file %s for writing \n", filename);

		fprintf(fd, "%% Tandav, autogenerated parameter file %% \n\n");
		
		for (int i = 0; i < NTags; i++) {
		
			fprintf(fd, "%s", ParDef[i].tag);
				
			if (ParDef[i].type != COMMENT)
				fprintf(fd, "		%s \n", ParDef[i].val);
		}

		fclose(fd);

		printf("\ndone, Good Bye.\n\n");
	}

	Finish(); // done here

	return ; // never reached
}

void sanity_check_input_parameters()
{
	Assert(Param.Num_Output_Files > 0, "NumOutputFiles has to be > 0");
	
	Assert(Param.Num_IO_Tasks > 0, "NumIOTasks has to be > 0");
	
	Assert(Param.Buffer_Size < Param.Max_Mem_Size / 8,
			"BufferSize should be much smaller than MaxMemSize");

	Warn((double)Param.Buffer_Size*p2(1024)/sizeof(*P) < 1000.0, 
		"Thread Safe buffer holds less than 1e3 particles "
		"Task.Buffer_Size > %d MB recommended, have %g"
		, 10000*sizeof(*P)/1024/1024 , Param.Buffer_Size/1024.0/1024.0);

	Warn(Param.Num_IO_Tasks > Sim.NRank, 
		"NRank (=%d) can't be smaller than No_IOTasks (=%d)", 
		Sim.NRank,  Param.Num_Output_Files);
	
	Param.Num_IO_Tasks = MIN(Param.Num_IO_Tasks, Sim.NRank);
	
	Warn(Param.Num_IO_Tasks > Param.Num_Output_Files, 
		"Num_IO_Tasks (=%d) can't be larger than Num_Output_Files (=%d)", 
		Param.Num_IO_Tasks,  Param.Num_Output_Files);
	
	Param.Num_Output_Files = MIN(Param.Num_Output_Files, Sim.NRank);

	/* Add your own ! */

	return ;
}
