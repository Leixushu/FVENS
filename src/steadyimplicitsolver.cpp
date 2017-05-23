#include "aoutput.hpp"
#include "aodesolver.hpp"

using namespace amat;
using namespace std;
using namespace acfd;

int main(int argc, char* argv[])
{
	if(argc < 2)
	{
		cout << "Please give a control file name.\n";
		return -1;
	}

	// Read control file
	ifstream control(argv[1]);

	string dum, meshfile, outf, invflux, invfluxjac, reconst, limiter, linsolver;
	double initcfl, endcfl, M_inf, vinf, alpha, rho_inf, tolerance, lintol, lin_relaxfactor;
	int maxiter, linmaxiterstart, linmaxiterend, rampstart, rampend;
	short inittype;

	control >> dum; control >> meshfile;
	control >> dum; control >> outf;
	control >> dum; control >> M_inf;
	control >> dum; control >> vinf;
	control >> dum; control >> alpha;
	control >> dum; control >> rho_inf;
	control >> dum; control >> inittype;
	control >> dum; control >> invflux;
	control >> dum; control >> invfluxjac;
	control >> dum; control >> reconst;
	control >> dum; control >> limiter;
	control >> dum; control >> initcfl;
	control >> dum; control >> endcfl;
	control >> dum; control >> rampstart;
	control >> dum; control >> rampend;
	control >> dum; control >> tolerance;
	control >> dum; control >> maxiter;
	control >> dum; control >> linsolver;
	control >> dum; control >> lintol;
	control >> dum; control >> linmaxiterstart;
	control >> dum; control >> linmaxiterend;
	control >> dum; control >> lin_relaxfactor;
	control.close(); 

	// Set up mesh

	UMesh2dh m;
	m.readGmsh2(meshfile,2);
	m.compute_topological();
	m.compute_areas();
	m.compute_jacobians();
	m.compute_face_data();

	// set up problem
	
	EulerFV prob(&m, invflux, invfluxjac, reconst, limiter);
	SteadyBackwardEulerSolver time(&m, &prob, initcfl, endcfl, rampstart, rampend, tolerance, maxiter, lintol, linmaxiterstart, linmaxiterend, linsolver);
	prob.loaddata(inittype, M_inf, vinf, alpha*PI/180, rho_inf, time.unknowns());

	// computation
	time.solve();

	Array2d<a_real> scalars;
	Array2d<a_real> velocities;
	prob.postprocess_point(time.unknowns(), scalars, velocities);

	string scalarnames[] = {"density", "mach-number", "pressure"};
	writeScalarsVectorToVtu_PointData(outf, m, scalars, scalarnames, velocities, "velocity");

	cout << "\n--------------- End --------------------- \n\n";
	return 0;
}
