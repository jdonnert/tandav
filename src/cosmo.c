#include "globals.h"
#include "proto.h"


float Hubble_Function (float a) // i.e. Mo, van den Bosch, White 2010
{
	return  1;
}

float Critical_Density (float a)
{
	return 1;
}


#define TABLESIZE 1000

static float drift_factor_table[TABLESIZE] = { 0 };
static float kick_factor_table[TABLESIZE] = { 0 };

static float symplectic_comoving_drift_factor() // Quinn+ 1996
{
	return 1;
}

static float symplectic_comoving_kick_factor() // Quinn+ 1996
{
	return 1;
}

void Init_Cosmology()
{

	return ;
}

#undef TABLESIZE
