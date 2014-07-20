/* Code units, we have them as macro and constant */

#define TIME2CGS (LENGTH2CGS/VELOCITY2CGS)

const struct Units {
	double Length;
	double Mass;
	double Velocity;
	double Time;
	double Energy;
} Unit;

/* Conversion functions to code units */
double Pressure(const int ipart);
double Internal_Energy(const int ipart); // U
double Temperature(const int ipart);

/* Conversion functions to cgs */
double Position_Cgs(const float x);
double Velocity_Cgs(const float v);
double Mass_Cgs(const float mass);
double Density_Cgs(const float rho);
double Number_Density_Cgs(const float rho);
double Pressure_Cgs(const float pres);
double Thermal_Energy_Density_Cgs(const int ipart);
