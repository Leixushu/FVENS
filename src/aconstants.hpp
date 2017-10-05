#ifndef ACONSTANTS_H

#define ACONSTANTS_H 1

#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <string>
#include <iomanip>
#include <functional>
#include <time.h>
// Unix only:
#include <sys/time.h>

// for floating point exceptions
#ifdef DEBUG
#include <fenv.h>
#endif

#define PI 3.14159265358979323846
#define SQRT3 1.73205080756887729353

/// tolerance to check if something is zero, ie, macine epsilon
#define ZERO_TOL 2.2e-16

/// A small number likely smaller than most convergence tolerances
#define A_SMALL_NUMBER 1e-12

#define NDIM 2

/// Number of coupled variables for compressible flow computations
#define NVARS 4

#define NGAUSS 1

#ifndef MESHDATA_DOUBLE_PRECISION
#define MESHDATA_DOUBLE_PRECISION 20
#endif

// We don't want any built-in threading in Eigen interfering with ours
#define EIGEN_DONT_PARALLELIZE

#ifndef EIGEN_CORE_H
#include <Eigen/Core>
#endif

namespace acfd
{
#define DOUBLE_PRECISION 1
	
	/// The floating-point type to use for all float computations
	typedef double a_real;

	/// Integer type to use for indexing etc
	/** Using signed types for this might be better than using unsigned types,
	 * eg., to iterate backwards over an entire array (down to index 0).
	 */
	typedef int a_int;
	
	using Eigen::Dynamic;
	using Eigen::RowMajor;
	using Eigen::ColMajor;
	using Eigen::Matrix;

	/// Multi-vector type, used for storing mesh functions like the residual
	typedef Matrix<a_real, Dynamic,Dynamic,RowMajor> MVector;
}

// sparse matrix library
#include <linearoperator.hpp>
namespace acfd {
	using blasted::LinearOperator;
}

#endif
