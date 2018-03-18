/** \file threads_async_tests.hpp
 * \brief Declarations of tests related to multi-threaded asynchronous preconditioning
 * \author Aditya Kashi
 * \date 2018-03
 */

#ifndef THREADS_ASYNC_TESTS_H
#define THREADS_ASYNC_TESTS_H

#include <vector>
#include <string>
#include <fstream>

#include "../src/autilities.hpp"
#include "../src/alinalg.hpp"

namespace benchmark {

using namespace acfd;

/// Find the dependence of the speed-up from a certain thread-count on the number of async sweeps
/** \param opts Numerics and physics options for the test-case
 * \param numrepeat The number of times to repeat the benchmark and average the results
 * \param numthreads The number of threads to run the case with
 * \param sweep_seq The set of async sweeps to run the case with (1 run for each entry)
 * \param outfile Opened stream context to write the averaged results to
 */
StatusCode test_speedup_sweeps(const FlowParserOptions& opts, const int numrepeat, const int numthreads,
		const std::vector<int>& sweep_seq, std::ofstream& outfile);

/// Run a timing test with a specific number of sweeps with a specific number of threads
/** 
 * \param bctx BLASTed data context which is re-constructed and thus initialized to zero before use,
 *   and contains timing data of the preconditioner on exit.
 * \return Performance data for the run
 */
TimingData run_sweeps(const Spatial<NVARS> *const startprob, const Spatial<NVARS> *const prob,
		const SteadySolverConfig& maintconf, const int nswps,
		KSP ksp, Vec u, Mat A, Mat M, MatrixFreeSpatialJacobian<NVARS>& mfjac, const PetscBool mf_flg,
		Blasted_data& bctx);

}

#endif
