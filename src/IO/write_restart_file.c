#include "io.h"

void Write_Restart_File()
{
	Profile("Restart File");

	char fname[] = { "restartfiles/restart." };

	rprintf("\nWriting restart files %s \n", fname);

	Profile("Restart File");

	return ;
}
