#ifndef COSMOLOGY_H
#define COSMOLOGY_H

#include "includes.h"

extern struct Current_Cosmology_In_Code_Units {
	const double Hubble_Constant; // Constants
	const double Omega_Lambda;
	const double Omega_Matter;
	const double Omega_Baryon;
	const double Omega_0;
	const double Omega_Rad;
	const double Rho_Crit0;
	double Hubble_Parameter; // Changing every timestep 
	double Redshift;
	double Expansion_Factor;
	double Sqrt_Expansion_Factor;
	double Critical_Density;
	double Grav_Accel_Factor;
	double Hydro_Accel_Factor;
	double Press_Factor;
} Cosmo;
#pragma omp threadprivate(Cosmo)


double Hubble_Parameter(const double a); // H(a) = H0 * E_Hubble(a)
double E_Hubble(const double a);
double Critical_Density(double);

#ifdef COMOVING
void Set_Current_Cosmology(const double a);
void Init_Cosmology();
#else // ! COMOVING
static inline void Set_Current_Cosmology(const double a) {};
static inline void Init_Cosmology() {};
#endif // ! COMOVING

#endif // COSMOLOGY_H
