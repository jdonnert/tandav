#include "globals.h"
#include "domain.h"
#include "timestep.h"

/*
 * Setup extra modules. In particular, allocate static memory blocks here !
 */

void Setup()
{
	Profile("Setup");

	Setup_Time_Integration();

	Setup_Domain_Decomposition();

	Print_Memory_Usage();

	Setup_Cosmology(); // COMOVING

	Setup_Comoving(); // COMOVING

	Profile("Setup");

	return ;
}

