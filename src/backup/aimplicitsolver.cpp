/** @file aimplicitsolver.cpp
 * @brief Implements classes for implicit solution of Euler/Navier-Stokes equations.
 * @author Aditya Kashi
 * @date May 6, 2016
 */

#include "aimplicitsolver.hpp"

namespace acfd {

ImplicitSolverBase::ImplicitSolverBase(const UMesh2dh* const mesh, const int _order, std::string invflux, std::string reconst, std::string limiter, std::string linear_solver, 
		const double cfl_num, const double init_cfl, const int switch_stepi, const int switch_step, const double relaxation_factor)
	: m(mesh), cfl_init(init_cfl), cfl(cfl_num), switchstepi(switch_stepi), switchstep(switch_step), order(_order), w(relaxation_factor)
{
	g = 1.4;

	std::cout << "ImplicitSolverBase: Setting up implicit solver for spatial order " << order << std::endl;

	// for 2D Euler equations, we have 4 variables
	nvars = 4;
	// for upto second-order finite volume, we only need 1 Guass point per face
	ngaussf = 1;

	solid_wall_id = 2;
	inflow_outflow_id = 4;

	// allocation
	m_inverse.setup(m->gnelem(),1);		// just a vector for FVM. For DG, this will be an array of Matrices
	residual.setup(m->gnelem(),nvars);
	u.setup(m->gnelem(), nvars);
	uinf.setup(1, nvars);
	integ.setup(m->gnelem(), 1);
	dtl.setup(m->gnelem(),1);
	dudx.setup(m->gnelem(), nvars);
	dudy.setup(m->gnelem(), nvars);
	uleft.setup(m->gnaface(), nvars);
	uright.setup(m->gnaface(), nvars);
	rc.setup(m->gnelem(),m->gndim());
	rcg.setup(m->gnface(),m->gndim());
	ug.setup(m->gnface(),nvars);
	gr = new amat::Matrix<a_real>[m->gnaface()];
	for(int i = 0; i <  m->gnaface(); i++)
	{
		gr[i].setup(ngaussf, m->gndim());
	}

	for(int i = 0; i < m->gnelem(); i++)
	{
		m_inverse(i) = 2.0/mesh->gjacobians(i);
	}

	// set inviscid flux scheme
	if(invflux == "VANLEER")
		inviflux = new VanLeerFlux(g);
	else if(invflux == "ROE")
	{
		inviflux = new RoeFlux(g);
		std::cout << "ImplicitSolver: Using Roe fluxes." << std::endl;
	}
	else if(invflux == "HLLC")
	{
		inviflux = new HLLCFlux(g);
		std::cout << "ImplicitSolver: Using HLLC fluxes." << std::endl;
	}
	else
		std::cout << "ImplicitSolver: ! Flux scheme not available!" << std::endl;

	// set reconstruction scheme
	std::cout << "ImplicitSolver: Reconstruction scheme is " << reconst << std::endl;
	if(reconst == "GREENGAUSS")
	{
		rec = new GreenGaussReconstruction();
		//rec->setup(m, &u, &ug, &dudx, &dudy, &rc, &rcg);
	}
	else 
	{
		rec = new WeightedLeastSquaresReconstruction();
	}
	if(order == 1) std::cout << "ImplicitSolver: No reconstruction" << std::endl;

	// set limiter
	if(limiter == "NONE")
	{
		lim = new NoLimiter(m, &u, &ug, &dudx, &dudy, &rcg, &rc, gr, &uleft, &uright);
		std::cout << "ImplicitSolver: No limiter will be used." << std::endl;
	}
	else if(limiter == "WENO")
	{
		lim = new WENOLimiter(m, &u, &ug, &dudx, &dudy, &rcg, &rc, gr, &uleft, &uright);
		std::cout << "ImplicitSolver: WENO limiter selected.\n";
	}

}

ImplicitSolverBase::~ImplicitSolverBase()
{
	delete rec;
	delete inviflux;
	delete lim;
	delete [] gr;
}

void ImplicitSolverBase::compute_ghost_cell_coords_about_midpoint()
{
	int iface, ielem, idim, ip1, ip2;
	std::vector<a_real> midpoint(m->gndim());
	for(iface = 0; iface < m->gnbface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		ip1 = m->gintfac(iface,2);
		ip2 = m->gintfac(iface,3);

		for(idim = 0; idim < m->gndim(); idim++)
		{
			midpoint[idim] = 0.5 * (m->gcoords(ip1,idim) + m->gcoords(ip2,idim));
		}

		for(idim = 0; idim < m->gndim(); idim++)
			rcg(iface,idim) = 2*midpoint[idim] - rc(ielem,idim);
	}
}

void ImplicitSolverBase::compute_ghost_cell_coords_about_face()
{
	int ied, ielem;
	a_real x1, y1, x2, y2, xs, ys, xi, yi;

	for(ied = 0; ied < m->gnbface(); ied++)
	{
		ielem = m->gintfac(ied,0); //int lel = ielem;
		//jelem = m->gintfac(ied,1); //int rel = jelem;
		a_real nx = m->ggallfa(ied,0);
		a_real ny = m->ggallfa(ied,1);

		xi = rc.get(ielem,0);
		yi = rc.get(ielem,1);

		// Note: The ghost cell is a direct reflection of the boundary cell about the boundary-face
		//       It is NOT the reflection about the midpoint of the boundary-face
		x1 = m->gcoords(m->gintfac(ied,2),0);
		x2 = m->gcoords(m->gintfac(ied,3),0);
		y1 = m->gcoords(m->gintfac(ied,2),1);
		y2 = m->gcoords(m->gintfac(ied,3),1);

		if(fabs(nx)>A_SMALL_NUMBER && fabs(ny)>A_SMALL_NUMBER)		// check if nx != 0 and ny != 0
		{
			xs = ( yi-y1 - ny/nx*xi + (y2-y1)/(x2-x1)*x1 ) / ((y2-y1)/(x2-x1)-ny/nx);
			ys = ny/nx*xs + yi - ny/nx*xi;
		}
		else if(fabs(nx)<=A_SMALL_NUMBER)
		{
			xs = xi;
			ys = y1;
		}
		else
		{
			xs = x1;
			ys = yi;
		}
		rcg(ied,0) = 2*xs-xi;
		rcg(ied,1) = 2*ys-yi;
	}
}

/// Function to feed needed data, and compute cell-centers
/** \param Minf Free-stream Mach number
 * \param vinf Free stream velocity magnitude
 * \param a Angle of attack (radians)
 * \param rhoinf Free stream density
 */
void ImplicitSolverBase::loaddata(a_real Minf, a_real vinf, a_real a, a_real rhoinf)
{
	// Note that reference density and reference velocity are the values at infinity
	//cout << "EulerFV: loaddata(): Calculating initial data...\n";
	a_real vx = vinf*cos(a);
	a_real vy = vinf*sin(a);
	a_real p = rhoinf*vinf*vinf/(g*Minf*Minf);
	uinf(0,0) = rhoinf;		// should be 1
	uinf(0,1) = rhoinf*vx;
	uinf(0,2) = rhoinf*vy;
	uinf(0,3) = p/(g-1) + 0.5*rhoinf*vinf*vinf;

	//initial values are equal to boundary values
	for(int i = 0; i < m->gnelem(); i++)
		for(int j = 0; j < nvars; j++)
			u(i,j) = uinf(0,j);

	// Next, get cell centers (real and ghost)
	
	int idim, inode;

	for(int ielem = 0; ielem < m->gnelem(); ielem++)
	{
		for(idim = 0; idim < m->gndim(); idim++)
		{
			rc(ielem,idim) = 0;
			for(inode = 0; inode < m->gnnode(ielem); inode++)
				rc(ielem,idim) += m->gcoords(m->ginpoel(ielem, inode), idim);
			rc(ielem,idim) = rc(ielem,idim) / (a_real)(m->gnnode(ielem));
		}
	}

	int ied, ig;
	a_real x1, y1, x2, y2;

	compute_ghost_cell_coords_about_midpoint();
	//compute_ghost_cell_coords_about_face();

	//Calculate and store coordinates of Gauss points (general implementation)
	// Gauss points are uniformly distributed along the face.
	for(ied = 0; ied < m->gnaface(); ied++)
	{
		x1 = m->gcoords(m->gintfac(ied,2),0);
		y1 = m->gcoords(m->gintfac(ied,2),1);
		x2 = m->gcoords(m->gintfac(ied,3),0);
		y2 = m->gcoords(m->gintfac(ied,3),1);
		for(ig = 0; ig < ngaussf; ig++)
		{
			gr[ied](ig,0) = x1 + (a_real)(ig+1.0)/(a_real)(ngaussf+1.0) * (x2-x1);
			gr[ied](ig,1) = y1 + (a_real)(ig+1.0)/(a_real)(ngaussf+1.0) * (y2-y1);
		}
	}

	rec->setup(m, &u, &ug, &dudx, &dudy, &rc, &rcg);
	std::cout << "ImplicitSolver: loaddata(): Initial data calculated.\n";
}

void ImplicitSolverBase::compute_boundary_states(const amat::Matrix<a_real>& ins, amat::Matrix<a_real>& bs)
{
	a_real nx, ny, vni, pi, ci, Mni, pinf;
	for(int ied = 0; ied < m->gnbface(); ied++)
	{
		nx = m->ggallfa(ied,0);
		ny = m->ggallfa(ied,1);

		vni = (ins.get(ied,1)*nx + ins.get(ied,2)*ny)/ins.get(ied,0);
		pi = (g-1.0)*(ins.get(ied,3) - 0.5*(pow(ins.get(ied,1),2)+pow(ins.get(ied,2),2))/ins.get(ied,0));
		pinf = (g-1.0)*(uinf.get(0,3) - 0.5*(pow(uinf.get(0,1),2)+pow(uinf.get(0,2),2))/uinf.get(0,0));
		ci = sqrt(g*pi/ins.get(ied,0));
		Mni = vni/ci;

		if(m->ggallfa(ied,3) == solid_wall_id)
		{
			bs(ied,0) = ins.get(ied,0);
			bs(ied,1) = ins.get(ied,1) - 2*vni*nx*bs(ied,0);
			bs(ied,2) = ins.get(ied,2) - 2*vni*ny*bs(ied,0);
			bs(ied,3) = ins.get(ied,3);
		}

		if(m->ggallfa(ied,3) == inflow_outflow_id)
		{
			//if(Mni <= -1.0)
			{
				for(int i = 0; i < nvars; i++)
					bs(ied,i) = uinf(0,i);
			}
			/*else if(Mni > -1.0 && Mni < 0)
			{
				// subsonic inflow, specify rho and u according to FUN3D BCs paper
				for(i = 0; i < nvars-1; i++)
					bs(ied,i) = uinf.get(0,i);
				bs(ied,3) = pi/(g-1.0) + 0.5*( uinf.get(0,1)*uinf.get(0,1) + uinf.get(0,2)*uinf.get(0,2) )/uinf.get(0,0);
			}
			else if(Mni >= 0 && Mni < 1.0)
			{
				// subsonic ourflow, specify p accoording FUN3D BCs paper
				for(i = 0; i < nvars-1; i++)
					bs(ied,i) = ins.get(ied,i);
				bs(ied,3) = pinf/(g-1.0) + 0.5*( ins.get(ied,1)*ins.get(ied,1) + ins.get(ied,2)*ins.get(ied,2) )/ins.get(ied,0);
			}
			else
				for(i = 0; i < nvars; i++)
					bs(ied,i) = ins.get(ied,i);*/
		}
	}
}

a_real ImplicitSolverBase::l2norm(const amat::Matrix<a_real>* const v)
{
	a_real norm = 0;
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		norm += v->get(iel)*v->get(iel)*m->gjacobians(iel)/2.0;
	}
	norm = sqrt(norm);
	return norm;
}

void ImplicitSolverBase::compute_RHS()
{
	residual.zeros();
	
	int ied, ielem, jelem, ivar;

	// first, set cell-centered values of boundary cells as left-side values of boundary faces
	for(ied = 0; ied < m->gnbface(); ied++)
	{
		ielem = m->gintfac(ied,0);
		for(ivar = 0; ivar < nvars; ivar++)
			uleft(ied,ivar) = u.get(ielem,ivar);
	}

	// get cell average values at ghost cells using BCs
	compute_boundary_states(uleft, ug);

	if(order == 2)
	{
		rec->compute_gradients();
		// compute states at faces' quadrature points. For boundary faces, only uleft is computed
		lim->compute_face_values();
	}
	else
	{
		// if order is 1, set the face data same as cell-centred data for all faces
		
		// set both left and right states for all interior faces
		for(ied = m->gnbface(); ied < m->gnaface(); ied++)
		{
			ielem = m->gintfac(ied,0);
			jelem = m->gintfac(ied,1);
			for(ivar = 0; ivar < nvars; ivar++)
			{
				uleft(ied,ivar) = u.get(ielem,ivar);
				uright(ied,ivar) = u.get(jelem,ivar);
			}
		}
	}

	// set right (ghost) state for boundary faces
	compute_boundary_states(uleft,uright);

	/** Compute fluxes.
	 * The integral of the maximum magnitude of eigenvalue over each face is also computed:
	 * \f[
	 * \int_{f_i} (|v_n| + c) \mathrm{d}l
	 * \f]
	 * so that time steps can be calculated for explicit time stepping.
	 */
	a_real n[NDIM], len, pi, ci, vni, Mni, pj, cj, vnj, Mnj, vmags;
	int lel, rel;
	const a_real *ulp, *urp; a_real fluxp[NVARS];

	for(ied = 0; ied < m->gnaface(); ied++)
	{
		lel = m->gintfac(ied,0);	// left element
		rel = m->gintfac(ied,1);	// right element

		n[0] = m->ggallfa(ied,0);
		n[1] = m->ggallfa(ied,1);
		len = m->ggallfa(ied,2);

		ulp = uleft.row_pointer(ied);
		urp = uright.row_pointer(ied);

		// compute flux
		inviflux->get_flux(ulp, urp, n, fluxp);

		// integrate over the face
		for(ivar = 0; ivar < nvars; ivar++)
				fluxp[ivar] *= len;

		// scatter the flux to elements' residuals
		for(ivar = 0; ivar < nvars; ivar++)
		{
			residual(lel,ivar) -= fluxp[ivar];
			if(rel >= 0 && rel < m->gnelem())
				residual(rel,ivar) += fluxp[ivar];
		}

		//calculate presures from u
		pi = (g-1)*(uleft.get(ied,3) - 0.5*(pow(uleft.get(ied,1),2)+pow(uleft.get(ied,2),2))/uleft.get(ied,0));
		pj = (g-1)*(uright.get(ied,3) - 0.5*(pow(uright.get(ied,1),2)+pow(uright.get(ied,2),2))/uright.get(ied,0));
		//calculate speeds of sound
		ci = sqrt(g*pi/uleft.get(ied,0));
		cj = sqrt(g*pj/uright.get(ied,0));
		//calculate normal velocities
		vni = (uleft.get(ied,1)*n[0] +uleft.get(ied,2)*n[1])/uleft.get(ied,0);
		vnj = (uright.get(ied,1)*n[0] + uright.get(ied,2)*n[1])/uright.get(ied,0);

		// calculate integ for CFL purposes
		integ(lel,0) += (fabs(vni) + ci)*len;
		if(rel >= 0 && rel < m->gnelem())
			integ(rel,0) += (fabs(vnj) + cj)*len;
	}
}

void ImplicitSolverBase::postprocess_point()
{
	std::cout << "ImplicitSolverBase: postprocess_point(): Creating output arrays...\n";
	scalars.setup(m->gnpoin(),3);
	velocities.setup(m->gnpoin(),2);
	amat::Matrix<a_real> c(m->gnpoin(),1);
	
	amat::Matrix<a_real> areasum(m->gnpoin(),1);
	amat::Matrix<a_real> up(m->gnpoin(), nvars);
	up.zeros();
	areasum.zeros();

	int inode, ivar;
	a_int ielem, iface, ip1, ip2, ipoin;

	for(ielem = 0; ielem < m->gnelem(); ielem++)
	{
		for(inode = 0; inode < m->gnnode(ielem); inode++)
			for(ivar = 0; ivar < nvars; ivar++)
			{
				up(m->ginpoel(ielem,inode),ivar) += u.get(ielem,ivar)*m->garea(ielem);
				areasum(m->ginpoel(ielem,inode)) += m->garea(ielem);
			}
	}
	for(iface = 0; iface < m->gnbface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		ip1 = m->gintfac(iface,2);
		ip2 = m->gintfac(iface,3);
		for(ivar = 0; ivar < nvars; ivar++)
		{
			up(ip1,ivar) += ug.get(iface,ivar)*m->garea(ielem);
			up(ip2,ivar) += ug.get(iface,ivar)*m->garea(ielem);
			areasum(ip1) += m->garea(ielem);
			areasum(ip2) += m->garea(ielem);
		}
	}

	for(ipoin = 0; ipoin < m->gnpoin(); ipoin++)
		for(ivar = 0; ivar < nvars; ivar++)
			up(ipoin,ivar) /= areasum(ipoin);
	
	for(ipoin = 0; ipoin < m->gnpoin(); ipoin++)
	{
		scalars(ipoin,0) = up.get(ipoin,0);
		velocities(ipoin,0) = up.get(ipoin,1)/up.get(ipoin,0);
		velocities(ipoin,1) = up.get(ipoin,2)/up.get(ipoin,0);
		//velocities(ipoin,0) = dudx(ipoin,1);
		//velocities(ipoin,1) = dudy(ipoin,1);
		a_real vmag2 = pow(velocities(ipoin,0), 2) + pow(velocities(ipoin,1), 2);
		scalars(ipoin,2) = up.get(ipoin,0)*(g-1) * (up.get(ipoin,3)/up.get(ipoin,0) - 0.5*vmag2);		// pressure
		c(ipoin) = sqrt(g*scalars(ipoin,2)/up.get(ipoin,0));
		scalars(ipoin,1) = sqrt(vmag2)/c(ipoin);
	}
	std::cout << "EulerFV: postprocess_point(): Done.\n";
}

void ImplicitSolverBase::postprocess_cell()
{
	std::cout << "ImplicitSolverBase: postprocess_cell(): Creating output arrays...\n";
	scalars.setup(m->gnelem(), 3);
	velocities.setup(m->gnelem(), 2);
	amat::Matrix<a_real> c(m->gnelem(), 1);

	amat::Matrix<a_real> d = u.col(0);
	scalars.replacecol(0, d);		// populate density data
	//std::cout << "EulerFV: postprocess(): Written density\n";

	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		velocities(iel,0) = u.get(iel,1)/u.get(iel,0);
		velocities(iel,1) = u.get(iel,2)/u.get(iel,0);
		//velocities(iel,0) = dudx(iel,1);
		//velocities(iel,1) = dudy(iel,1);
		a_real vmag2 = pow(velocities(iel,0), 2) + pow(velocities(iel,1), 2);
		scalars(iel,2) = d(iel)*(g-1) * (u.get(iel,3)/d(iel) - 0.5*vmag2);		// pressure
		c(iel) = sqrt(g*scalars(iel,2)/d(iel));
		scalars(iel,1) = sqrt(vmag2)/c(iel);
	}
	std::cout << "ImplicitSolverBase: postprocess_cell(): Done.\n";
}

a_real ImplicitSolverBase::compute_entropy_cell()
{
	postprocess_cell();
	a_real vmaginf2 = uinf(0,1)/uinf(0,0)*uinf(0,1)/uinf(0,0) + uinf(0,2)/uinf(0,0)*uinf(0,2)/uinf(0,0);
	a_real sinf = ( uinf(0,0)*(g-1) * (uinf(0,3)/uinf(0,0) - 0.5*vmaginf2) ) / pow(uinf(0,0),g);

	amat::Matrix<a_real> s_err(m->gnelem(),1);
	a_real error = 0;
	for(int iel = 0; iel < m->gnelem(); iel++)
	{
		s_err(iel) = (scalars(iel,2)/pow(scalars(iel,0),g) - sinf)/sinf;
		error += s_err(iel)*s_err(iel)*m->gjacobians(iel)/2.0;
	}
	error = sqrt(error);

	//a_real h = sqrt((m->jacobians).max());
	a_real h = 1.0/sqrt(m->gnelem());
 
	std::cout << "EulerFV:   " << log10(h) << "  " << std::setprecision(10) << log10(error) << std::endl;

	return error;
}

amat::Matrix<a_real> ImplicitSolverBase::getscalars() const
{
	return scalars;
}

amat::Matrix<a_real> ImplicitSolverBase::getvelocities() const
{
	return velocities;
}

//------------------- end of implicit solver base class ------------------------------------------------//
//////////////////////////////////////////////////////////////////////////////////////////////////////////

ImplicitSolver::ImplicitSolver(const UMesh2dh* const mesh, const int _order, std::string invflux, std::string reconst, std::string limiter, std::string linear_solver, 
		const double cfl_num, const double init_cfl, const int switch_stepi, const int switch_step, const double relaxation_factor)
	: ImplicitSolverBase(mesh, _order, invflux, reconst, limiter, linear_solver, cfl_num, init_cfl, switch_stepi, switch_step, relaxation_factor)
{
	diag = new amat::Matrix<a_real>[m->gnelem()];
	ludiag = new amat::Matrix<a_real>[m->gnelem()];
	diagp = new amat::Matrix<int>[m->gnelem()];
	lower = new amat::Matrix<a_real>[m->gnaface()];
	upper = new amat::Matrix<a_real>[m->gnaface()];
	/** \note Allocation for lower and upper is currently for all faces, whereas what is needed is only interior faces. */

	for(int i = 0; i < m->gnelem(); i++)
	{
		diag[i].setup(nvars,nvars);
		ludiag[i].setup(nvars,nvars);
		diagp[i].setup(nvars,1);
	}
	for(int i = 0; i < m->gnaface(); i++)
	{
		lower[i].setup(nvars,nvars);
		upper[i].setup(nvars,nvars);
	}
	afresidual.setup(m->gnelem(),nvars);	// to store the full residual of linear system to be solved

	// set solver
	if(linear_solver == "SSOR")
	{
		solver = new SSOR_Solver(nvars, m, &afresidual, ludiag, diagp, lower, upper, w);
		std::cout << "ImplicitSolver: Full-matrix SSOR solver will be used." << std::endl;
	}
	else if(linear_solver == "BJ")
	{
		solver = new BJ_Solver(nvars, m, &afresidual, ludiag, diagp, lower, upper, w);
		std::cout << "ImplicitSolver: Block Jacobi solver will be used." << std::endl;
	}
}

ImplicitSolver::~ImplicitSolver()
{
	delete [] diag;
	delete [] ludiag;
	delete [] diagp;
	delete [] lower;
	delete [] upper;
	delete solver;
}

// make sure this function is called after the previous one (compute_RHS) as ug is needed
void ImplicitSolver::compute_LHS()
{
	// compute `eigenvalues' across faces and Euler fluxes
	
	a_int iface, ielem, jelem, face;
	int i,j;
	a_real uij, vij, rhoij, vnij, pi, pj, pij, hi, hj, hij, Rij, cij, lambdaij, n[2], len, u02, vn;
	amat::Matrix<a_real> Ji(nvars,nvars), Jj(nvars,nvars);

	for(ielem = 0; ielem < m->gnelem(); ielem++)
		diag[ielem].zeros();

	// boundary faces
	for(iface = 0; iface < m->gnbface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		n[0] = m->ggallfa(iface,0);
		n[1] = m->ggallfa(iface,1);
		len = m->ggallfa(iface,2);

		pi = (g-1.0)*(u.get(ielem,3) - 0.5*(u.get(ielem,1)*u.get(ielem,1) + u.get(ielem,2)*u.get(ielem,2))/u.get(ielem,0));
		pj = (g-1.0)*(ug.get(3) - 0.5*(ug.get(1)*ug.get(1) + ug.get(2)*ug.get(2))/ug.get(0));
		hi = (u.get(ielem,3) + pi)/u.get(ielem,0);
		hj = (ug.get(3) + pj)/ug.get(0);

		// get Roe-averages
		Rij = sqrt(ug.get(0)/u.get(ielem,0));
		rhoij = Rij*u.get(ielem,0);
		uij = ( Rij * ug.get(1)/ug.get(0) + u.get(ielem,1)/u.get(ielem,0) ) / (Rij + 1.0);
		vij = ( Rij * ug.get(2)/ug.get(0) + u.get(ielem,2)/u.get(ielem,0) ) / (Rij + 1.0);
		vnij = uij*n[0] + vij*n[1];
		hij = (Rij*hj + hi)/(Rij+1.0);
		cij = sqrt( (g-1.0)*(hij - 0.5*(uij*uij+vij*vij)) );

		lambdaij = fabs(vnij) + cij;

		// get J(u_i, n_ij), ie, Jacobian of Euler flux in the normal direction, for ielem
		u02 = u.get(ielem,0)*u.get(ielem,0);
		Ji(0,0) = 0.0;
		Ji(0,1) = n[0]; 
		Ji(0,2) = n[1];
		Ji(0,3) = 0.0;

		Ji(1,0) = n[0] * ( (g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,1)*u.get(ielem,1)/u02) - u.get(ielem,1)*u.get(ielem,2)*n[1]/u02;
		Ji(1,1) = (n[0]*u.get(ielem,1)/u.get(ielem,0)*(3.0-g) + u.get(ielem,2)/u.get(0)*n[1]);
		Ji(1,2) = (u.get(ielem,1)/u.get(ielem,0)*n[1] - (g-1.0)*u.get(ielem,2)/u.get(ielem,0)*n[0]);
		Ji(1,3) = (g-1.0)*n[0];

		Ji(2,0) = -u.get(ielem,1)*u.get(ielem,2)/u02*n[0] + n[1] * ((g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,2)*u.get(ielem,2)/u02);
		Ji(2,1) = (u.get(ielem,2)/u.get(ielem,0)*n[0] - (g-1.0)*u.get(ielem,1)/u.get(ielem,0)*n[1]);
		Ji(2,2) = (u.get(ielem,1)/u.get(ielem,0)*n[0] + n[1]*(3.0-g)*u.get(ielem,2)/u.get(ielem,0));
		Ji(2,3) = (g-1.0)*n[1];

		vn = (u.get(ielem,1)*n[0] + u.get(ielem,2)*n[1])/u.get(ielem,0);
		Ji(3,0) = (vn*((g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))/u02 - g*u.get(ielem,3)/u.get(ielem,0)));
		Ji(3,1) = g*u.get(ielem,3)/u.get(ielem,0)*n[0] - (g-1.0)/u02*(1.5*u.get(ielem,1)*u.get(ielem,1)*n[0]+0.5*u.get(ielem,2)*u.get(ielem,2)*n[0]+u.get(ielem,1)*u.get(ielem,2)*n[1]);
		Ji(3,2) = g*u.get(ielem,3)/u.get(ielem,0)*n[1] -(g-1.0)/u02*(u.get(ielem,1)*u.get(ielem,2)*n[0]+1.5*u.get(ielem,2)*u.get(ielem,2)*n[1]+0.5*u.get(ielem,1)*u.get(ielem,1)*n[1]);
		Ji(3,3) = g*vn;

		// add contribution to diagonal part of LHS
		for(i = 0; i < nvars; i++)
			diag[ielem](i,i) += 0.5*(Ji(i,i) + lambdaij)*len;
		for(i = 0; i < nvars; i++)
			for(j = 0; j < nvars; j++)
			{
				if(i==j) continue;
				diag[ielem](i,j) += 0.5*Ji(i,j)*len;
			}
	}

	// interior faces
	for(iface = m->gnbface(); iface < m->gnaface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		jelem = m->gintfac(iface,1);
		n[0] = m->ggallfa(iface,0);
		n[1] = m->ggallfa(iface,1);
		len = m->ggallfa(iface,2);

		pi = (g-1.0)*(u.get(ielem,3) - 0.5*(u.get(ielem,1)*u.get(ielem,1) + u.get(ielem,2)*u.get(ielem,2))/u.get(ielem,0));
		pj = (g-1.0)*(u.get(jelem,3) - 0.5*(u.get(jelem,1)*u.get(jelem,1) + u.get(jelem,2)*u.get(jelem,2))/u.get(jelem,0));
		hi = (u.get(ielem,3) + pi)/u.get(ielem,0);
		hj = (u.get(jelem,3) + pj)/u.get(jelem,0);

		// get Roe-averages
		Rij = sqrt(u.get(jelem,0)/u.get(ielem,0));
		rhoij = Rij*u.get(ielem,0);
		uij = ( Rij * u.get(jelem,1)/u.get(jelem,0) + u.get(ielem,1)/u.get(ielem,0) ) / (Rij + 1.0);
		vij = ( Rij * u.get(jelem,2)/u.get(jelem,0) + u.get(ielem,2)/u.get(ielem,0) ) / (Rij + 1.0);
		vnij = uij*n[0] + vij*n[1];
		hij = (Rij*hj + hi)/(Rij+1.0);
		cij = sqrt( (g-1.0)*(hij - 0.5*(uij*uij+vij*vij)) );

		lambdaij = fabs(vnij) + cij;
		
		// get J(u_i, n_ij), ie, Jacobian of Euler flux in the normal direction, for ielem
		u02 = u.get(ielem,0)*u.get(ielem,0);
		Ji(0,0) = 0.0;
		Ji(0,1) = n[0]; 
		Ji(0,2) = n[1];
		Ji(0,3) = 0.0;

		Ji(1,0) = n[0] * ( (g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,1)*u.get(ielem,1)/u02) - u.get(ielem,1)*u.get(ielem,2)*n[1]/u02;
		Ji(1,1) = (n[0]*u.get(ielem,1)/u.get(ielem,0)*(3.0-g) + u.get(ielem,2)/u.get(0)*n[1]);
		Ji(1,2) = (u.get(ielem,1)/u.get(ielem,0)*n[1] - (g-1.0)*u.get(ielem,2)/u.get(ielem,0)*n[0]);
		Ji(1,3) = (g-1.0)*n[0];

		Ji(2,0) = -u.get(ielem,1)*u.get(ielem,2)/u02*n[0] + n[1] * ((g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,2)*u.get(ielem,2)/u02);
		Ji(2,1) = (u.get(ielem,2)/u.get(ielem,0)*n[0] - (g-1.0)*u.get(ielem,1)/u.get(ielem,0)*n[1]);
		Ji(2,2) = (u.get(ielem,1)/u.get(ielem,0)*n[0] + n[1]*(3.0-g)*u.get(ielem,2)/u.get(ielem,0));
		Ji(2,3) = (g-1.0)*n[1];

		vn = (u.get(ielem,1)*n[0] + u.get(ielem,2)*n[1])/u.get(ielem,0);
		Ji(3,0) = (vn*((g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))/u02 - g*u.get(ielem,3)/u.get(ielem,0)));
		Ji(3,1) = g*u.get(ielem,3)/u.get(ielem,0)*n[0] - (g-1.0)/u02*(1.5*u.get(ielem,1)*u.get(ielem,1)*n[0]+0.5*u.get(ielem,2)*u.get(ielem,2)*n[0]+u.get(ielem,1)*u.get(ielem,2)*n[1]);
		Ji(3,2) = g*u.get(ielem,3)/u.get(ielem,0)*n[1] -(g-1.0)/u02*(u.get(ielem,1)*u.get(ielem,2)*n[0]+1.5*u.get(ielem,2)*u.get(ielem,2)*n[1]+0.5*u.get(ielem,1)*u.get(ielem,1)*n[1]);
		Ji(3,3) = g*vn;
		
		// get J(u_j, n_ij), ie, Jacobian of Euler flux in the normal direction, for jelem
		u02 = u.get(jelem,0)*u.get(jelem,0);
		Jj(0,0) = 0.0;
		Jj(0,1) = n[0]; 
		Jj(0,2) = n[1];
		Jj(0,3) = 0.0;

		Jj(1,0) = n[0] * ( (g-1.0)*(u.get(jelem,1)*u.get(jelem,1)+u.get(jelem,2)*u.get(jelem,2))*0.5/u02 - u.get(jelem,1)*u.get(jelem,1)/u02) - u.get(jelem,1)*u.get(jelem,2)*n[1]/u02;
		Jj(1,1) = (n[0]*u.get(jelem,1)/u.get(jelem,0)*(3.0-g) + u.get(jelem,2)/u.get(0)*n[1]);
		Jj(1,2) = (u.get(jelem,1)/u.get(jelem,0)*n[1] - (g-1.0)*u.get(jelem,2)/u.get(jelem,0)*n[0]);
		Jj(1,3) = (g-1.0)*n[0];

		Jj(2,0) = -u.get(jelem,1)*u.get(jelem,2)/u02*n[0] + n[1] * ((g-1.0)*(u.get(jelem,1)*u.get(jelem,1)+u.get(jelem,2)*u.get(jelem,2))*0.5/u02 - u.get(jelem,2)*u.get(jelem,2)/u02);
		Jj(2,1) = (u.get(jelem,2)/u.get(jelem,0)*n[0] - (g-1.0)*u.get(jelem,1)/u.get(jelem,0)*n[1]);
		Jj(2,2) = (u.get(jelem,1)/u.get(jelem,0)*n[0] + n[1]*(3.0-g)*u.get(jelem,2)/u.get(jelem,0));
		Jj(2,3) = (g-1.0)*n[1];

		vn = (u.get(jelem,1)*n[0] + u.get(jelem,2)*n[1])/u.get(jelem,0);
		Jj(3,0) = (vn*((g-1.0)*(u.get(jelem,1)*u.get(jelem,1)+u.get(jelem,2)*u.get(jelem,2))/u02 - g*u.get(jelem,3)/u.get(jelem,0)));
		Jj(3,1) = g*u.get(jelem,3)/u.get(jelem,0)*n[0] - (g-1.0)/u02*(1.5*u.get(jelem,1)*u.get(jelem,1)*n[0]+0.5*u.get(jelem,2)*u.get(jelem,2)*n[0]+u.get(jelem,1)*u.get(jelem,2)*n[1]);
		Jj(3,2) = g*u.get(jelem,3)/u.get(jelem,0)*n[1] -(g-1.0)/u02*(u.get(jelem,1)*u.get(jelem,2)*n[0]+1.5*u.get(jelem,2)*u.get(jelem,2)*n[1]+0.5*u.get(jelem,1)*u.get(jelem,1)*n[1]);
		Jj(3,3) = g*vn;

		// scatter to diag and construct lower and upper
		
		for(i = 0; i < nvars; i++)
		{
			diag[ielem](i,i) += 0.5*(Ji(i,i) + lambdaij)*len;
			diag[jelem](i,i) += 0.5*(-Jj(i,i) + lambdaij)*len;
			lower[iface](i,i) = 0.5*(-Ji(i,i) - lambdaij)*len;
			upper[iface](i,i) = 0.5*(Jj(i,i) - lambdaij)*len;
		}

		for(i = 0; i < nvars; i++)
			for(j = 0; j < nvars; j++)
			{
				if(i == j) continue;
				diag[ielem](i,j) += 0.5*Ji(i,j)*len;
				diag[jelem](i,j) += -0.5*Jj(i,j)*len;
				lower[iface](i,j) = 0.5*(-Ji(i,j))*len;
				upper[iface](i,j) = 0.5*Jj(i,j)*len;
			}
	}
}

void ImplicitSolver::jacobianVectorProduct(const amat::Matrix<a_real>* const du, amat::Matrix<a_real>& ans)
{
	int ielem, jelem, iface, i,j;
	ans.zeros();

	for(ielem = 0; ielem < m->gnelem(); ielem++)
	{
		for(i = 0; i < nvars; i++)
			for(j = 0; j < nvars; j++)
				ans(ielem,i) += diag[ielem].get(i,j)*du[ielem].get(j);
	}

	for(iface = m->gnbface(); iface < m->gnaface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		jelem = m->gintfac(iface,1);
		for(i = 0; i < nvars; i++)
			for(j = 0; j < nvars; j++)
			{
				ans(jelem,i) += lower[iface].get(i,j)*du[ielem].get(j);
				ans(ielem,i) += upper[iface].get(i,j)*du[jelem].get(j);
			}
	}
}

SteadyStateImplicitSolver::SteadyStateImplicitSolver(const UMesh2dh* const mesh, const int _order, std::string invflux,std::string reconst,std::string limiter, std::string lsolver, const double cfl, 
		const double initcfl, const int switchstepi, const int switchstep, const double omega, const a_real steady_tol, const int steady_maxiter, const a_real lin_tol, const int lin_maxiter)
	: ImplicitSolver(mesh, _order, invflux, reconst, limiter, lsolver, cfl, initcfl, switchstepi, switchstep, omega), steadytol(steady_tol), steadymaxiter(steady_maxiter), lintol(lin_tol), linmaxiter(lin_maxiter)
{ }

void SteadyStateImplicitSolver::solve()
{
	int step = 0, linstep;
	a_int iel;
	int i;
	a_real resi = 1.0, linresi, curCFL;
	a_real initres = 1.0, lininitres;
	amat::Matrix<a_real> res(nvars,1);
	res.ones();
	amat::Matrix<a_real>* du = new amat::Matrix<a_real>[m->gnelem()];
	amat::Matrix<a_real>* ddu = new amat::Matrix<a_real>[m->gnelem()];
	for(iel = 0; iel < m->gnelem(); iel++)
	{
		du[iel].setup(nvars,1);
		ddu[iel].setup(nvars,1);
	}

	std::vector<a_real> dunorm(nvars), eps(nvars);

	std::cout << "SteadyStateImplicitSolver: solve(): Beginning time loop..." << std::endl;

	while(resi/initres > steadytol && step < steadymaxiter)
	{
		integ.zeros();		// reset CFL data
		
		// reset time update vector
		for(iel = 0; iel < m->gnelem(); iel++)
			du[iel].zeros();

		//calculate fluxes
		compute_RHS();		// this invokes Flux calculating function after zeroing the residuals
		
		for(iel = 0; iel < m->gnelem(); iel++)
			for(i = 0; i < nvars; i++)
				afresidual(iel,i) = residual.get(iel,i);

		if(step < switchstepi)
			curCFL = cfl_init;
		else if(step < switchstep)
			curCFL = cfl_init + (cfl-cfl_init)/(switchstep-switchstepi)*(step-switchstepi);
		else
			curCFL = cfl;

		double dtmin = 1.0; // for same time-step for all cells
		
		//calculate dt based on CFL
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			dtl(iel) = curCFL*(m->garea(iel)/integ(iel));
			if(dtl.get(iel) < dtmin) dtmin = dtl.get(iel);
		}
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			dtl(iel) = dtmin;
		}
		
		compute_LHS();			// this zeros diag before computing the LHS
		
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			diag[iel](0,0) += m->garea(iel)/dtl.get(iel);
			diag[iel](1,1) += m->garea(iel)/dtl.get(iel);
			diag[iel](2,2) += m->garea(iel)/dtl.get(iel);
			diag[iel](3,3) += m->garea(iel)/dtl.get(iel);

			// save the diagonal block
			ludiag[iel] = diag[iel];

			// LU factorize the diagonal block
			LUfactor(ludiag[iel], diagp[iel]);
		}

		// solve linear system

		linresi = 1.0;
		lininitres = 1.0;
		linstep = 0;
		
		while(linresi/lininitres > lintol && linstep < linmaxiter)
		{
			solver->compute_update(ddu);

			// compute norm of change
			linresi = 0;
			for(iel = 0; iel < m->gnelem(); iel++)
			{
				linresi += ddu[iel].get(0)*ddu[iel].get(0)*m->garea(iel);
			}
			linresi = sqrt(linresi);

			if(linstep == 0)
				lininitres = linresi;

			//if((linstep % 10 == 0 || linstep == linmaxiter-1) && step%10 == 0)
				std::cout << "SteadyStateImplicitSolver: solve():   Lin step " << linstep << ", rel lin residual " << linresi/lininitres << std::endl;

			linstep++;

			// update solution
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
				{
					du[iel](i) += ddu[iel].get(i);
					//u(iel,i) += ddu[iel].get(i);
				}
			
			// compute ODE residual

			// compute the matrix-vector product (dR/du) * du
			jacobianVectorProduct(du, afresidual);
			
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
				{
					afresidual(iel,i) = res.get(iel,i) - afresidual.get(iel,i);
				}
		}

		// get new u, ie, u := u + du
		for(iel = 0; iel < m->gnelem(); iel++)
			for(i = 0; i < nvars; i++)
			{
				u(iel,i) += du[iel].get(i);
			}

		// compute ||u_n - u_(n-1)||
		// mass residual
		resi = 0;
		for(iel = 0; iel < m->gnelem(); iel++)
			resi += du[iel].get(0)*du[iel].get(0)*m->garea(iel);
		resi = sqrt(resi);

		if(step == 0)
			initres = resi;

		//if(step % 10 == 0)
			std::cout << "SteadyStateImplicitSolver: solve(): Step " << step << ", rel residual " << resi/initres << std::endl;
			std::cout << "SteadyStateImplicitSolver: solve(): Step " << step << ",  Mass residual " << resi << std::endl;

		step++;
		/*a_real totalenergy = 0;
		for(int i = 0; i < m->gnelem(); i++)
			totalenergy += u(i,3)*m->jacobians(i);
			std::cout << "EulerFV: solve(): Total energy = " << totalenergy << std::endl;*/
		//if(step == 10000) break;
	}

	if(step == steadymaxiter)
		std::cout << "SteadyStateImplicitSolver: solve(): Exceeded max iterations!" << std::endl;

	delete [] du;
	delete [] ddu;
}
// ----------------------------------- end of full matrix storage implicit solver classes ------------------------------- //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ImplicitSolverMF::ImplicitSolverMF(const UMesh2dh* const mesh, const int _order, std::string invflux, std::string reconst, std::string limiter, std::string linear_solver, 
		const double cfl_num, const double init_cfl, const int switchstepi, const int switch_step, const double relaxation_factor)
	: ImplicitSolverBase(mesh, _order, invflux, reconst, limiter, linear_solver, cfl_num, init_cfl, switchstepi, switch_step, relaxation_factor)
{
	diag = new amat::Matrix<a_real>[m->gnelem()];
	diagp = new amat::Matrix<int>[m->gnelem()];
	lambdaij.setup(m->gnaface(),1);
	elemfaceflux = new amat::Matrix<a_real>[m->gnaface()];
	for(int i = 0; i <  m->gnaface(); i++)
	{
		elemfaceflux[i].setup(2,nvars);
	}

	for(int i = 0; i < m->gnelem(); i++)
	{
		diag[i].setup(nvars,nvars);
		diagp[i].setup(nvars,1);
	}

	eulerflux = new EulerFlux(g);
	
	// set solver
	if(linear_solver == "SSOR")
	{
		solver = new SSOR_MFSolver(nvars, m, &residual, eulerflux, diag, diagp, &lambdaij, elemfaceflux, &u, w);
		std::cout << "ImplicitSolver: Matrix-free SSOR solver will be used." << std::endl;
	}
	else if(linear_solver == "BJ")
	{
		solver = new BJ_MFSolver(nvars, m, &residual, eulerflux, diag, diagp, &lambdaij, elemfaceflux, &u, w);
		std::cout << "ImplicitSolver: Block Jacobi solver will be used." << std::endl;
	}
}

ImplicitSolverMF::~ImplicitSolverMF()
{
	delete eulerflux;
	delete [] diag;
	delete [] diagp;
	delete [] elemfaceflux;
}

// make sure this function is called after the previous one (compute_RHS) as ug is needed
void ImplicitSolverMF::compute_LHS()
{
	// compute `eigenvalues' across faces and Euler fluxes
	
	a_int iface, ielem, jelem, face;
	a_real uij, vij, rhoij, vnij, pi, pj, pij, hi, hj, hij, Rij, cij, n[2], len;

	// boundary faces
	for(iface = 0; iface < m->gnbface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		n[0] = m->ggallfa(iface,0);
		n[1] = m->ggallfa(iface,1);
		len = m->ggallfa(iface,2);

		pi = (g-1.0)*(u.get(ielem,3) - 0.5*(u.get(ielem,1)*u.get(ielem,1) + u.get(ielem,2)*u.get(ielem,2))/u.get(ielem,0));
		pj = (g-1.0)*(ug.get(3) - 0.5*(ug.get(1)*ug.get(1) + ug.get(2)*ug.get(2))/ug.get(0));
		hi = (u.get(ielem,3) + pi)/u.get(ielem,0);
		hj = (ug.get(3) + pj)/ug.get(0);

		// get Roe-averages
		Rij = sqrt(ug.get(0)/u.get(ielem,0));
		rhoij = Rij*u.get(ielem,0);
		uij = ( Rij * ug.get(1)/ug.get(0) + u.get(ielem,1)/u.get(ielem,0) ) / (Rij + 1.0);
		vij = ( Rij * ug.get(2)/ug.get(0) + u.get(ielem,2)/u.get(ielem,0) ) / (Rij + 1.0);
		vnij = uij*n[0] + vij*n[1];
		hij = (Rij*hj + hi)/(Rij+1.0);
		cij = sqrt( (g-1.0)*(hij - 0.5*(uij*uij+vij*vij)) );

		lambdaij(iface) = fabs(vnij) + cij;

		// get Euler flux
		eulerflux->evaluate_flux_2(u, ielem, n, elemfaceflux[iface], 0);
	}
	// interior faces
	for(iface = m->gnbface(); iface < m->gnaface(); iface++)
	{
		ielem = m->gintfac(iface,0);
		jelem = m->gintfac(iface,1);
		n[0] = m->ggallfa(iface,0);
		n[1] = m->ggallfa(iface,1);
		len = m->ggallfa(iface,2);

		pi = (g-1.0)*(u.get(ielem,3) - 0.5*(u.get(ielem,1)*u.get(ielem,1) + u.get(ielem,2)*u.get(ielem,2))/u.get(ielem,0));
		pj = (g-1.0)*(u.get(jelem,3) - 0.5*(u.get(jelem,1)*u.get(jelem,1) + u.get(jelem,2)*u.get(jelem,2))/u.get(jelem,0));
		hi = (u.get(ielem,3) + pi)/u.get(ielem,0);
		hj = (u.get(jelem,3) + pj)/u.get(jelem,0);

		// get Roe-averages
		Rij = sqrt(u.get(jelem,0)/u.get(ielem,0));
		rhoij = Rij*u.get(ielem,0);
		uij = ( Rij * u.get(jelem,1)/u.get(jelem,0) + u.get(ielem,1)/u.get(ielem,0) ) / (Rij + 1.0);
		vij = ( Rij * u.get(jelem,2)/u.get(jelem,0) + u.get(ielem,2)/u.get(ielem,0) ) / (Rij + 1.0);
		vnij = uij*n[0] + vij*n[1];
		hij = (Rij*hj + hi)/(Rij+1.0);
		cij = sqrt( (g-1.0)*(hij - 0.5*(uij*uij+vij*vij)) );

		lambdaij(iface) = fabs(vnij) + cij;
		
		// get Euler flux
		eulerflux->evaluate_flux_2(u, ielem, n, elemfaceflux[iface], 0);
		eulerflux->evaluate_flux_2(u, jelem, n, elemfaceflux[iface], 1);
	}

	// compute diagonal blocks
	a_int ifael;
	a_real u02, vn;
	for(ielem = 0; ielem < m->gnelem(); ielem++)
	{
		diag[ielem].zeros();
		u02 = u.get(ielem,0)*u.get(ielem,0);

		for(ifael = 0; ifael < m->gnfael(ielem); ifael++)
		{
			face = m->gelemface(ielem,ifael);
			n[0] = m->ggallfa(face,0);
			n[1] = m->ggallfa(face,1);
			len = m->ggallfa(face,2);

			diag[ielem](0,0) += 0.5*lambdaij.get(face)*len;
			diag[ielem](0,1) += 0.5*n[0]*len; 
			diag[ielem](0,2) += 0.5*n[1]*len;

			diag[ielem](1,0) += 0.5*( n[0] * ( (g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,1)*u.get(ielem,1)/u02) 
					- u.get(ielem,1)*u.get(ielem,2)*n[1]/u02 )*len;
			diag[ielem](1,1) += 0.5* (n[0]*u.get(ielem,1)/u.get(ielem,0)*(3.0-g) + u.get(ielem,2)/u.get(0)*n[1] + lambdaij.get(face))*len;
			diag[ielem](1,2) += 0.5* (u.get(ielem,1)/u.get(ielem,0)*n[1] - (g-1.0)*u.get(ielem,2)/u.get(ielem,0)*n[0])*len;
			diag[ielem](1,3) += 0.5*(g-1.0)*n[0]*len;

			diag[ielem](2,0) += 0.5*(-u.get(ielem,1)*u.get(ielem,2)/u02*n[0] + 
					n[1] * ( (g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))*0.5/u02 - u.get(ielem,2)*u.get(ielem,2)/u02))*len;
			diag[ielem](2,1) += 0.5*(u.get(ielem,2)/u.get(ielem,0)*n[0] - (g-1.0)*u.get(ielem,1)/u.get(ielem,0)*n[1])*len;
			diag[ielem](2,2) += 0.5*(u.get(ielem,1)/u.get(ielem,0)*n[0] + n[1]*(3.0-g)*u.get(ielem,2)/u.get(ielem,0) + lambdaij.get(face))*len;
			diag[ielem](2,3) += 0.5*(g-1.0)*n[1]*len;

			vn = (u.get(ielem,1)*n[0] + u.get(ielem,2)*n[1])/u.get(ielem,0);
			diag[ielem](3,0) += 0.5*(vn*((g-1.0)*(u.get(ielem,1)*u.get(ielem,1)+u.get(ielem,2)*u.get(ielem,2))/u02 - g*u.get(ielem,3)/u.get(ielem,0)))*len;
			diag[ielem](3,1) += 0.5*(g*u.get(ielem,3)/u.get(ielem,0)*n[0] - 
				(g-1.0)/u02*(1.5*u.get(ielem,1)*u.get(ielem,1)*n[0]+0.5*u.get(ielem,2)*u.get(ielem,2)*n[0]+u.get(ielem,1)*u.get(ielem,2)*n[1]))*len;
			diag[ielem](3,2) += 0.5*(g*u.get(ielem,3)/u.get(ielem,0)*n[1] -
				(g-1.0)/u02*(u.get(ielem,1)*u.get(ielem,2)*n[0]+1.5*u.get(ielem,2)*u.get(ielem,2)*n[1]+0.5*u.get(ielem,1)*u.get(ielem,1)*n[1]))*len;
			diag[ielem](3,3) += 0.5*(g*vn + lambdaij.get(face))*len;
		}
	}
}

LUSSORSteadyStateImplicitSolverMF::LUSSORSteadyStateImplicitSolverMF(const UMesh2dh* const mesh, const int _order, std::string invflux, std::string reconst, std::string limiter, const double cfl, 
		const double init_cfl, const int switchstepi, const int switch_step, const double omega, const a_real steady_tol, const int steady_maxiter)
	: ImplicitSolverMF(mesh, _order, invflux, reconst, limiter, "SSOR", cfl, init_cfl, switchstepi, switch_step, omega), steadytol(steady_tol), steadymaxiter(steady_maxiter)
{ }

void LUSSORSteadyStateImplicitSolverMF::solve()
{
	int step = 0;
	a_int iel;
	int i;
	a_real resi = 1.0, curCFL, dt;
	a_real initres = 1.0;
	amat::Matrix<a_real>* err;
	err = new amat::Matrix<a_real>[nvars];
	for(int i = 0; i<nvars; i++)
		err[i].setup(m->gnelem(),1);
	amat::Matrix<a_real> res(nvars,1);
	res.ones();
	amat::Matrix<a_real>* du = new amat::Matrix<a_real>[m->gnelem()];
	for(iel = 0; iel < m->gnelem(); iel++)
		du[iel].setup(nvars,1);
	std::cout << "LUSSORSteadyStateImplicitSolver: solve(): Beginning time loop..." << std::endl;

	while(resi/initres > steadytol && step < steadymaxiter)
	{
		// reset fluxes
		integ.zeros();		// reset CFL data

		//calculate fluxes
		compute_RHS();		// this invokes Flux calculating function after zeroing the residuals

		if(step < switchstepi)
			curCFL = cfl_init;
		else if(step < switchstep)
			curCFL = cfl_init + (cfl-cfl_init)/(switchstep-switchstepi)*(step-switchstepi);
		else
			curCFL = cfl;

		//calculate dt based on CFL, and get global dt
		dt = 0.1;
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			dtl(iel) = curCFL*(m->garea(iel)/integ(iel));
			if(dtl.get(iel) < dt) dt = dtl.get(iel);
		}
		
		compute_LHS();			// this zeros diag before computing the LHS
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			/*diag[iel](0,0) += m->garea(iel)/dtl.get(iel);
			diag[iel](1,1) += m->garea(iel)/dtl.get(iel);
			diag[iel](2,2) += m->garea(iel)/dtl.get(iel);
			diag[iel](3,3) += m->garea(iel)/dtl.get(iel);*/
			diag[iel](0,0) += m->garea(iel)/dt;
			diag[iel](1,1) += m->garea(iel)/dt;
			diag[iel](2,2) += m->garea(iel)/dt;
			diag[iel](3,3) += m->garea(iel)/dt;

			LUfactor(diag[iel], diagp[iel]);
		}

		// carry out LU-SSOR
		solver->compute_update(du);

		for(iel = 0; iel < m->gnelem(); iel++)
			for(i = 0; i < nvars; i++)
				u(iel,i) += du[iel].get(i);

		// compute ||u_n - u_(n-1)||
		/*for(i = 0; i < nvars; i++)
		{
			err[i] = du.col(i);
			//err[i] = residual.col(i);
			res(i) = l2norm(&err[i]);
		}
		resi = res.max();*/

		// mass residual
		resi = 0;
		for(iel = 0; iel < m->gnelem(); iel++)
			resi += du[iel].get(0)*du[iel].get(0)*m->garea(iel);
		resi = sqrt(resi);

		if(step == 0)
			initres = resi;

		if(step % 20 == 0)
			std::cout << "LUSSORSteadyStateImplicitSolver: solve(): Step " << step << ", rel residual " << resi/initres << std::endl;

		step++;
		/*a_real totalenergy = 0;
		for(int i = 0; i < m->gnelem(); i++)
			totalenergy += u(i,3)*m->jacobians(i);
			std::cout << "EulerFV: solve(): Total energy = " << totalenergy << std::endl;*/
		//if(step == 10000) break;
	}

	if(step == steadymaxiter)
		std::cout << "LUSSORSteadyStateImplicitSolver: solve(): Exceeded max iterations!" << std::endl;

	delete [] err;
}

SteadyStateImplicitSolverMF::SteadyStateImplicitSolverMF(const UMesh2dh* const mesh, const int _order, std::string invflux, std::string reconst, std::string limiter, std::string lsolver, 
		const double cfl, const double initcfl, const int switchstepi, const int switchstep, const double omega, const a_real steady_tol, const int steady_maxiter, const a_real lin_tol, const int lin_maxiter)
	: ImplicitSolverMF(mesh, _order, invflux, reconst, limiter, lsolver, cfl, initcfl, switchstepi, switchstep, omega), steadytol(steady_tol), steadymaxiter(steady_maxiter), lintol(lin_tol), linmaxiter(lin_maxiter)
{ }

void SteadyStateImplicitSolverMF::solve()
{
	int step = 0, linstep;
	a_int iel;
	int i;
	a_real resi = 1.0, linresi, curCFL;
	a_real initres = 1.0, lininitres;
	amat::Matrix<a_real> res(nvars,1);
	amat::Matrix<a_real> prevresidual(m->gnelem(), nvars);
	res.ones();
	amat::Matrix<a_real>* du = new amat::Matrix<a_real>[m->gnelem()];
	amat::Matrix<a_real>* ddu = new amat::Matrix<a_real>[m->gnelem()];
	for(iel = 0; iel < m->gnelem(); iel++)
	{
		du[iel].setup(nvars,1);
		ddu[iel].setup(nvars,1);
	}

	a_real epsilon = 1.0e-2;
	std::vector<a_real> dunorm(nvars), eps(nvars);

	std::cout << "SteadyStateImplicitSolverMF: solve(): Beginning time loop..." << std::endl;

	while(resi/initres > steadytol && step < steadymaxiter)
	{
		integ.zeros();		// reset CFL data
		// reset time update vector
		for(iel = 0; iel < m->gnelem(); iel++)
			du[iel].zeros();

		//calculate fluxes
		compute_RHS();		// this invokes Flux calculating function after zeroing the residuals
		for(iel = 0; iel < m->gnelem(); iel++)
			for(i = 0; i < nvars; i++)
				prevresidual(iel,i) = residual.get(iel,i);

		if(step < switchstepi)
			curCFL = cfl_init;
		else if(step < switchstep)
			curCFL = cfl_init + (cfl-cfl_init)/(switchstep-switchstepi)*(step-switchstepi);
		else
			curCFL = cfl;

		//calculate dt based on CFL
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			dtl(iel) = curCFL*(m->garea(iel)/integ(iel));
		}
		
		compute_LHS();			// this zeros diag before computing the LHS
		for(iel = 0; iel < m->gnelem(); iel++)
		{
			diag[iel](0,0) += m->garea(iel)/dtl.get(iel);
			diag[iel](1,1) += m->garea(iel)/dtl.get(iel);
			diag[iel](2,2) += m->garea(iel)/dtl.get(iel);
			diag[iel](3,3) += m->garea(iel)/dtl.get(iel);

			LUfactor(diag[iel], diagp[iel]);
		}

		// solve linear system

		linresi = 1.0;
		lininitres = 1.0;
		linstep = 0;
		while(linresi/lininitres > lintol && linstep < linmaxiter)
		{
			solver->compute_update(ddu);

			// compute norm of change
			linresi = 0;
			for(iel = 0; iel < m->gnelem(); iel++)
				linresi += ddu[iel].get(0)*ddu[iel].get(0)*m->garea(iel);
			linresi = sqrt(linresi);

			if(linstep == 0)
				lininitres = linresi;

			if((linstep % 10 == 0 || linstep == linmaxiter-1) && step%10 == 0)
				std::cout << "SteadyStateImplicitSolverMF: solve():   Lin step " << linstep << ", rel lin residual " << linresi/lininitres << std::endl;

			linstep++;

			// update solution
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
				{
					du[iel](i) += ddu[iel].get(i);
					u(iel,i) += ddu[iel].get(i);
				}
			
			// compute ODE residual
			compute_RHS();
			
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
				{
					residual(iel,i) -= m->garea(iel)/dtl.get(iel)*du[iel].get(i);
				}

			// get the finite difference increment eps
			/*for(i = 0; i < nvars; i++)
			{
				dunorm[i] = 0;
				for(iel = 0; iel < m->gnelem(); iel++)
					dunorm[i] += du[iel].get(i)*du[iel].get(i);
				dunorm[i] = sqrt(dunorm[i]);
				eps[i] = epsilon/dunorm[i];
				std::cout << eps[i] << std::endl;
			}
			
			// set u := u + (eps)*du
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
					u(iel,i) += eps[i]*du[iel].get(i);
			
			// compute ODE residual
			compute_RHS();
			
			// get back original u
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
					u(iel,i) -= eps[i]*du[iel].get(i);
			
			// get residual
			for(iel = 0; iel < m->gnelem(); iel++)
				for(i = 0; i < nvars; i++)
				{
					residual(iel,i) -= m->garea(iel)*eps[i]/dtl.get(iel)*du[iel].get(i);
					residual(iel,i) += (eps[i]-1.0)*prevresidual.get(iel,i);
					residual(iel,i) /= eps[i];
				}*/
		}
		
		// get new u, ie, u := u + du
		/*for(iel = 0; iel < m->gnelem(); iel++)
			for(i = 0; i < nvars; i++)
			{
				u(iel,i) += du[iel].get(i);
			}*/

		// compute ||u_n - u_(n-1)||
		// mass residual
		resi = 0;
		for(iel = 0; iel < m->gnelem(); iel++)
			resi += du[iel].get(0)*du[iel].get(0)*m->garea(iel);
		resi = sqrt(resi);

		if(step == 0)
			initres = resi;

		if(step % 10 == 0)
			std::cout << "SteadyStateImplicitSolverMF: solve(): Step " << step << ", rel residual " << resi/initres << std::endl;

		step++;
		/*a_real totalenergy = 0;
		for(int i = 0; i < m->gnelem(); i++)
			totalenergy += u(i,3)*m->jacobians(i);
			std::cout << "EulerFV: solve(): Total energy = " << totalenergy << std::endl;*/
		//if(step == 10000) break;
	}

	if(step == steadymaxiter)
		std::cout << "SteadyStateImplicitSolverMF: solve(): Exceeded max iterations!" << std::endl;

	delete [] du;
	delete [] ddu;
}

}	// end namespace
