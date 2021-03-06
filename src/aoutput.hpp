/** @file aoutput.hpp
 * @brief A collection of subroutines to write mesh data to various kinds of output formats
 */

#ifndef AOUTPUT_H

#include "aspatial.hpp"

#define AOUTPUT_H 1

namespace acfd {

/// Interface for output to files
template <short nvars>
class Output
{
public:
	Output(const UMesh2dh *const mesh, const Spatial<nvars> *const fv);

	/// Exports data for the entire domain
	/** \param[in] u The field variables
	 * \param[in] volfile The name of the file to be written.
	 */
	virtual void exportVolumeData(const MVector& u, const std::string volfile) const = 0;

	/// Exports data on surfaces
	/** \param[in] u The multi-vector containing field variables.
	 * \param[in] wbcm A list of `wall' boundary face markers for which output is needed.
	 * \param[in] obcm A list of `other' boundary face markers at which some other output is needed.
	 * \param[in] basename The base name for the files that will be written.
	 */
	virtual void exportSurfaceData(const MVector& u, const std::vector<int> wbcm,
			const std::vector<int> obcm, const std::string basename) const = 0;

protected:
	const UMesh2dh *const m;
	const Spatial<nvars> *const space;
};

/// Output for flow simulations
class FlowOutput : public Output<NVARS>
{
public:
	/// Sets required data
	/** \param[in] angleOfAttack The angle of attack in radians
	 */
	FlowOutput(const UMesh2dh *const m, const Spatial<NVARS> *const fv,
			const IdealGasPhysics *const physics, const a_real angleOfAttack);
	
	/** For each cell, writes out the coordinates of the cell-centre,
	 * density, velocities, pressure, temperature and Mach number.
	 */
	void exportVolumeData(const MVector& u, const std::string volfile) const;

	/// Computes Cp, Csf, Cl, Cd_p and Cd_sf on surfaces
	/** \param[in] u The multi-vector containing conserved variables
	 * \param[in] grad Gradients of converved variables at cell-centres
	 * \param[in] iwbcm The marker of the boundaries on which the computation is to be done
	 * \param[in,out] output On output, contains for each boundary face having the marker im : 
	 *                   Cp and Csf, in that order
	 */
	std::tuple<a_real,a_real,a_real> computeSurfaceData(const MVector& u,
			const std::vector<FArray<NDIM,NVARS>,aligned_allocator<FArray<NDIM,NVARS>>>& grad,
			const int iwbcm, MVector& output) const;

	/// Export surface data
	/** We compute pressure and skin-friction coefficients for wall boundaries, and
	 * normalized x- and y-velocities along other boundaries.
	 * We also compute lift and drag for wall boundaries.
	 * \sa Output::exportSurfaceData
	 */
	void exportSurfaceData(const MVector& u, const std::vector<int> wbcm,
			const std::vector<int> obcm, const std::string basename) const;

protected:
	using Output<NVARS>::m;
	using Output<NVARS>::space;
	const IdealGasPhysics *const phy;
	const a_real av[NDIM];				///< Unit vector in the direction of freestream flow
};

/** \brief Writes multiple scalar data sets and one vector data set, 
 * all cell-centered data, to a file in VTU format.
 * 
 * If either x or y is a 0x0 matrix, it is ignored.
 * \param fname is the output vtu file name
 */
void writeScalarsVectorToVtu_CellData(std::string fname, const UMesh2dh& m, 
		const amat::Array2d<double>& x, std::string scaname[], 
		const amat::Array2d<double>& y, std::string vecname);

/// Writes nodal data to VTU file
void writeScalarsVectorToVtu_PointData(std::string fname, const UMesh2dh& m, 
		const amat::Array2d<double>& x, std::string scaname[], 
		const amat::Array2d<double>& y, std::string vecname);

/// Writes a hybrid mesh in VTU format.
/** VTK does not have a 9-node quadrilateral, so we ignore the cell-centered note for output.
 */
void writeMeshToVtu(std::string fname, UMesh2dh& m);

}
#endif
