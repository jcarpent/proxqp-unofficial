//
// Copyright (c) 2022, INRIA
//
/**
 * @file wrapper.hpp 
*/

#ifndef PROXSUITE_QP_DENSE_WRAPPER_HPP
#define PROXSUITE_QP_DENSE_WRAPPER_HPP
#include <proxsuite/qp/dense/solver.hpp>
#include <proxsuite/qp/dense/helpers.hpp>
#include <proxsuite/qp/dense/preconditioner/ruiz.hpp>
#include <chrono>

namespace proxsuite {
namespace qp {
namespace dense {
///
/// @brief This class defines the API of PROXQP solver with dense backend.
///
/*!
 * Wrapper class for using proxsuite API with dense backend
 * for solving linearly constrained convex QP problem using ProxQp algorithm.  
 * 
 * Example usage:
 * ```cpp
#include <Eigen/Core>
#include <Eigen/Cholesky>
#include <proxsuite/qp/dense/dense.hpp>
#include <proxsuite/veg/util/dbg.hpp>
#include <test/include/util.hpp>

using T = double;
auto main() -> int {

	// Generate a random QP problem with primal variable dimension of size dim; n_eq equality constraints and n_in inequality constraints
	ldlt_test::rand::set_seed(1);
	qp::isize dim = 10;
	qp::isize n_eq(dim / 4);
	qp::isize n_in(dim / 4);
	T strong_convexity_factor(1.e-2);
	T sparsity_factor = 0.15; // controls the sparsity of each matrix of the problem generated
	T eps_abs = T(1e-9);
	Qp<T> qp{
			random_with_dim_and_neq_and_n_in,
			dim,
			n_eq,
			n_in,
			sparsity_factor,
			strong_convexity_factor}; 
	
	// Solve the problem
	qp::dense::QP<T> Qp{dim, n_eq, n_in}; // creating QP object
	Qp.settings.eps_abs = eps_abs; // choose accuracy needed
	Qp.init(qp.H, qp.g, qp.A, qp.b, qp.C, qp.u, qp.l); // setup the QP object
	Qp.solve(); // solve the problem

	// Verify solution accuracy
	T pri_res = std::max(
			(qp.A * Qp.results.x - qp.b).lpNorm<Eigen::Infinity>(),
			(qp::dense::positive_part(qp.C * Qp.results.x - qp.u) +
			qp::dense::negative_part(qp.C * Qp.results.x - qp.l))
					.lpNorm<Eigen::Infinity>());
	T dua_res = (qp.H * Qp.results.x + qp.g + qp.A.transpose() * Qp.results.y +
					qp.C.transpose() * Qp.results.z)
					.lpNorm<Eigen::Infinity>();
	VEG_ASSERT(pri_res <= eps_abs);
	VEG_ASSERT(dua_res <= eps_abs);

	// Some solver statistics
	std::cout << "------solving qp with dim: " << dim
						<< " neq: " << n_eq << " nin: " << n_in << std::endl;
	std::cout << "primal residual: " << pri_res << std::endl;
	std::cout << "dual residual: " << dua_res << std::endl;
	std::cout << "total number of iteration: " << Qp.results.info.iter
						<< std::endl;
}
 * ```
 */
///// QP object
template <typename T>
struct QP {
	Results<T> results;
	Settings<T> settings;
	Model<T> model;
	Workspace<T> work;
	preconditioner::RuizEquilibration<T> ruiz;
	/*!
	 * Default constructor using QP model dimensions.
	 * @param _dim primal variable dimension.
	 * @param _n_eq number of equality constraints.
	 * @param _n_in number of inequality constraints.
	 */
	QP(isize _dim, isize _n_eq, isize _n_in)
			: results(_dim, _n_eq, _n_in),
				settings(),
				model(_dim, _n_eq, _n_in),
				work(_dim, _n_eq, _n_in),
				ruiz(preconditioner::RuizEquilibration<T>{_dim, _n_eq + _n_in}) {
				work.timer.stop();
				}
	/*!
	 * Setups the QP model (with dense matrix format) and equilibrates it if specified by the user. 
	 * @param H quadratic cost input defining the QP model.
	 * @param g linear cost input defining the QP model.
	 * @param A equality constraint matrix input defining the QP model.
	 * @param b equality constraint vector input defining the QP model.
	 * @param C inequality constraint matrix input defining the QP model.
	 * @param u lower inequality constraint vector input defining the QP model.
	 * @param l lower inequality constraint vector input defining the QP model.
	 * @param compute_preconditioner boolean parameter for executing or not the preconditioner.
	 * @param rho proximal step size wrt primal variable.
	 * @param mu_eq proximal step size wrt equality constrained multiplier.
	 * @param mu_in proximal step size wrt inequality constrained multiplier.
	 */
	void init(
			MatRef<T> H,
			VecRef<T> g,
			MatRef<T> A,
			VecRef<T> b,
			MatRef<T> C,
			VecRef<T> u,
			VecRef<T> l,
			bool compute_preconditioner = true,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt) {
		// dense case
		if (settings.initial_guess == InitialGuessStatus::WARM_START_WITH_PREVIOUS_RESULT){
			work.refactorize=true; // necessary for the first solve (then refactorize only if there is an update of the matrices)
		}else{
			work.refactorize=false;
		}
		work.proximal_parameter_update=false;
		if (settings.compute_timings){
					work.timer.stop();
					work.timer.start();
		}
		PreconditionerStatus preconditioner_status;
		if (compute_preconditioner){
			preconditioner_status = proxsuite::qp::PreconditionerStatus::EXECUTE;
		}else{
			preconditioner_status = proxsuite::qp::PreconditionerStatus::IDENTITY;
		}
		proxsuite::qp::dense::update_proximal_parameters(results, work, rho, mu_eq, mu_in);
		proxsuite::qp::dense::setup(
				H,
				g,
				A,
				b,
				C,
				u,
				l,
				settings,
				model,
				work,
				results,
				ruiz,
				preconditioner_status);
		if (settings.compute_timings){
			results.info.setup_time = work.timer.elapsed().user; // in microseconds
		}
	};
	/*!
	 * Setups the QP model (with sparse matrix format) and equilibrates it if specified by the user. 
	 * @param H quadratic cost input defining the QP model.
	 * @param g linear cost input defining the QP model.
	 * @param A equality constraint matrix input defining the QP model.
	 * @param b equality constraint vector input defining the QP model.
	 * @param C inequality constraint matrix input defining the QP model.
	 * @param u lower inequality constraint vector input defining the QP model.
	 * @param l lower inequality constraint vector input defining the QP model.
	 * @param compute_preconditioner bool parameter for executing or not the preconditioner.
	 * @param rho proximal step size wrt primal variable.
	 * @param mu_eq proximal step size wrt equality constrained multiplier.
	 * @param mu_in proximal step size wrt inequality constrained multiplier.
	 */
	void init(
			const SparseMat<T>& H,
			VecRef<T> g,
			const SparseMat<T>& A,
			VecRef<T> b,
			const SparseMat<T>& C,
			VecRef<T> u,
			VecRef<T> l,
			bool compute_preconditioner = true,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt) {
		// sparse case
		if (settings.initial_guess == InitialGuessStatus::WARM_START_WITH_PREVIOUS_RESULT){
			work.refactorize=true; // necessary for the first solve (then refactorize only if there is an update of the matrices)
		}else{
			work.refactorize=false;
		}
		work.proximal_parameter_update=false;
		if (settings.compute_timings){
					work.timer.stop();
					work.timer.start();
		}
		PreconditionerStatus preconditioner_status;
		if (compute_preconditioner){
			preconditioner_status = proxsuite::qp::PreconditionerStatus::EXECUTE;
		}else{
			preconditioner_status = proxsuite::qp::PreconditionerStatus::IDENTITY;
		}
		proxsuite::qp::dense::update_proximal_parameters(results, work, rho, mu_eq, mu_in);
		proxsuite::qp::dense::setup(
				H,
				g,
				A,
				b,
				C,
				u,
				l,
				settings,
				model,
				work,
				results,
				ruiz,
				preconditioner_status);
		if (settings.compute_timings){
			results.info.setup_time = work.timer.elapsed().user; // in microseconds
		}
	};
	/*!
	 * Updates the QP model (with dense matrix format) and re-equilibrates it if specified by the user. 
	 * @param H quadratic cost input defining the QP model.
	 * @param g linear cost input defining the QP model.
	 * @param A equality constraint matrix input defining the QP model.
	 * @param b equality constraint vector input defining the QP model.
	 * @param C inequality constraint matrix input defining the QP model.
	 * @param u lower inequality constraint vector input defining the QP model.
	 * @param l lower inequality constraint vector input defining the QP model.
	 * @param update_preconditioner bool parameter for updating or not the preconditioner and the associated scaled model.
	 * @param rho proximal step size wrt primal variable.
	 * @param mu_eq proximal step size wrt equality constrained multiplier.
	 * @param mu_in proximal step size wrt inequality constrained multiplier.
	 */
	void update(
			const std::optional<MatRef<T>> H,
			std::optional<VecRef<T>> g,
			const std::optional<MatRef<T>> A,
			std::optional<VecRef<T>> b,
			const std::optional<MatRef<T>> C,
			std::optional<VecRef<T>> u,
			std::optional<VecRef<T>> l,
			bool update_preconditioner = true,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt) {
		// dense case
		work.refactorize=false;
		work.proximal_parameter_update=false;
		if (settings.compute_timings){
					work.timer.stop();
					work.timer.start();
		}
		PreconditionerStatus preconditioner_status;
		if (update_preconditioner){
			preconditioner_status = proxsuite::qp::PreconditionerStatus::EXECUTE;
		}else{
			preconditioner_status = proxsuite::qp::PreconditionerStatus::KEEP;
		}
		bool real_update = !(H == std::nullopt && g == std::nullopt && A == std::nullopt &&
		    b == std::nullopt && C == std::nullopt && u == std::nullopt &&
		    l == std::nullopt);
		if (real_update) {
			proxsuite::qp::dense::update(H,g,A,b,C,u,l,model,work);
		}
		proxsuite::qp::dense::update_proximal_parameters(results, work, rho, mu_eq, mu_in);
		proxsuite::qp::dense::setup(
					MatRef<T>(model.H),
					VecRef<T>(model.g),
					MatRef<T>(model.A),
					VecRef<T>(model.b),
					MatRef<T>(model.C),
					VecRef<T>(model.u),
					VecRef<T>(model.l),
					settings,
					model,
					work,
					results,
					ruiz,
					preconditioner_status);
		if (settings.compute_timings){
			results.info.setup_time = work.timer.elapsed().user; // in microseconds
		}
	};
	/*!
	 * Updates the QP model (with sparse matrix format) and equilibrates it if specified by the user. 
	 * @param H quadratic cost input defining the QP model.
	 * @param g linear cost input defining the QP model.
	 * @param A equality constraint matrix input defining the QP model.
	 * @param b equality constraint vector input defining the QP model.
	 * @param C inequality constraint matrix input defining the QP model.
	 * @param u lower inequality constraint vector input defining the QP model.
	 * @param l lower inequality constraint vector input defining the QP model.
	 * @param update_preconditioner bool parameter for executing or not the preconditioner.
	 * @param rho proximal step size wrt primal variable.
	 * @param mu_eq proximal step size wrt equality constrained multiplier.
	 * @param mu_in proximal step size wrt inequality constrained multiplier.
	 */
	void update(
			const std::optional<SparseMat<T>> H,
			std::optional<VecRef<T>> g,
			const std::optional<SparseMat<T>> A,
			std::optional<VecRef<T>> b,
			const std::optional<SparseMat<T>> C,
			std::optional<VecRef<T>> u,
			std::optional<VecRef<T>> l,
			bool update_preconditioner = true,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt) {
		// sparse case
		work.refactorize=false;
		work.proximal_parameter_update=false;
		if (settings.compute_timings){
					work.timer.stop();
					work.timer.start();
		}
		PreconditionerStatus preconditioner_status;
		if (update_preconditioner){
			preconditioner_status = proxsuite::qp::PreconditionerStatus::EXECUTE;
		}else{
			preconditioner_status = proxsuite::qp::PreconditionerStatus::KEEP;
		}
		bool real_update = !(H == std::nullopt && g == std::nullopt && A == std::nullopt &&
		    b == std::nullopt && C == std::nullopt && u == std::nullopt &&
		    l == std::nullopt);
		if (real_update) {
			proxsuite::qp::dense::update(H,g,A,b,C,u,l,model,work);
		}
		proxsuite::qp::dense::update_proximal_parameters(results, work, rho, mu_eq, mu_in);
		proxsuite::qp::dense::setup(
				MatRef<T>(model.H),
				VecRef<T>(model.g),
				MatRef<T>(model.A),
				VecRef<T>(model.b),
				MatRef<T>(model.C),
				VecRef<T>(model.u),
				VecRef<T>(model.l),
				settings,
				model,
				work,
				results,
				ruiz,
				preconditioner_status);
		if (settings.compute_timings){
			results.info.setup_time = work.timer.elapsed().user; // in microseconds
		}
	};
	/*!
	 * Updates the QP model vectors only (to avoid ambiguity through overloading) and equilibrates it if specified by the user. 
	 * @param H quadratic cost input defining the QP model.
	 * @param g linear cost input defining the QP model.
	 * @param A equality constraint matrix input defining the QP model.
	 * @param b equality constraint vector input defining the QP model.
	 * @param C inequality constraint matrix input defining the QP model.
	 * @param u lower inequality constraint vector input defining the QP model.
	 * @param l lower inequality constraint vector input defining the QP model.
	 * @param update_preconditioner bool parameter for executing or not the preconditioner.
	 * @param rho proximal step size wrt primal variable.
	 * @param mu_eq proximal step size wrt equality constrained multiplier.
	 * @param mu_in proximal step size wrt inequality constrained multiplier.
	 */
	void update(
			const std::nullopt_t H,
			std::optional<VecRef<T>> g,
			const std::nullopt_t A,
			std::optional<VecRef<T>> b,
			const std::nullopt_t C,
			std::optional<VecRef<T>> u,
			std::optional<VecRef<T>> l,
			bool update_preconditioner = true,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt, 
			std::optional<T> mu_in = std::nullopt) {
		work.refactorize=false;
		work.proximal_parameter_update=false;
		// treat the case when H, A and C are nullopt, in order to avoid ambiguity through overloading
		if (settings.compute_timings){
					work.timer.stop();
					work.timer.start();
		}
		PreconditionerStatus preconditioner_status;
		if (update_preconditioner){
			preconditioner_status = proxsuite::qp::PreconditionerStatus::EXECUTE;
		}else{
			preconditioner_status = proxsuite::qp::PreconditionerStatus::KEEP;
		}
		bool real_update = !(g == std::nullopt &&
		    b == std::nullopt && u == std::nullopt &&
		    l == std::nullopt);
		if (real_update) {
			// update the model
			if (g != std::nullopt) {
				model.g = g.value().eval();
			} 
			if (b != std::nullopt) {
				model.b = b.value().eval();
			}
			if (u != std::nullopt) {
				model.u = u.value().eval();
			}
			if (l != std::nullopt) {
				model.l = l.value().eval();
			} 
		}
		proxsuite::qp::dense::update_proximal_parameters(results, work, rho, mu_eq, mu_in);
		proxsuite::qp::dense::setup(
				MatRef<T>(model.H),
				VecRef<T>(model.g),
				MatRef<T>(model.A),
				VecRef<T>(model.b),
				MatRef<T>(model.C),
				VecRef<T>(model.u),
				VecRef<T>(model.l),
				settings,
				model,
				work,
				results,
				ruiz,
				preconditioner_status);
		if (settings.compute_timings){
			results.info.setup_time = work.timer.elapsed().user; // in microseconds
		}
	};
	/*!
	 * Solves the QP problem using PRXOQP algorithm.
	 */
	void solve() {
		qp_solve( //
				settings,
				model,
				results,
				work,
				ruiz);
	};
	/*!
	 * Solves the QP problem using PROXQP algorithm using a warm start.
	 * @param x primal warm start.
	 * @param y dual equality warm start.
	 * @param z dual inequality warm start.
	 */
	void solve(std::optional<VecRef<T>> x,
			std::optional<VecRef<T>> y,
			std::optional<VecRef<T>> z) {
		proxsuite::qp::dense::warm_start(x, y, z, results, settings);
		qp_solve( //
				settings,
				model,
				results,
				work,
				ruiz);
	};
	/*!
	 * Clean-ups solver's results and workspace.
	 */
	void cleanup() {
		results.cleanup();
		work.cleanup();
	}
};
/*!
* Solves the QP problem using PROXQP algorithm without the need to define a QP object, with matrices defined by Dense Eigen matrices.
* It is possible to set up some of the solver parameters (warm start, initial guess option, proximal step sizes, absolute and relative accuracies, maximum number of iterations, preconditioner execution).
* @param H quadratic cost input defining the QP model.
* @param g linear cost input defining the QP model.
* @param A equality constraint matrix input defining the QP model.
* @param b equality constraint vector input defining the QP model.
* @param C inequality constraint matrix input defining the QP model.
* @param u lower inequality constraint vector input defining the QP model.
* @param l lower inequality constraint vector input defining the QP model.
* @param x primal warm start.
* @param y dual equality constraint warm start.
* @param z dual inequality constraint warm start.
* @param verbose if set to true, the solver prints more information about each iteration.
* @param compute_preconditioner bool parameter for executing or not the preconditioner.
* @param compute_timings boolean parameter for computing the solver timings.
* @param rho proximal step size wrt primal variable.
* @param mu_eq proximal step size wrt equality constrained multiplier.
* @param mu_in proximal step size wrt inequality constrained multiplier.
* @param eps_abs absolute accuracy threshold.
* @param eps_rel relative accuracy threshold.
* @param max_iter maximum number of iteration.
* @param initial_guess initial guess option for warm starting or not the initial iterate values.
*/
template <typename T>
qp::Results<T> solve(MatRef<T> H,
					 VecRef<T> g,
					 MatRef<T> A,
					 VecRef<T> b,
					 MatRef<T> C,
					 VecRef<T> u,
					 VecRef<T> l,
			std::optional<VecRef<T>> x = std::nullopt,
			std::optional<VecRef<T>> y = std::nullopt,
			std::optional<VecRef<T>> z = std::nullopt,
			std::optional<T> eps_abs = std::nullopt,
			std::optional<T> eps_rel = std::nullopt,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt,
			std::optional<bool> verbose = std::nullopt,
			bool compute_preconditioner = true,
			bool compute_timings = true,
			std::optional<isize> max_iter = std::nullopt,
			proxsuite::qp::InitialGuessStatus initial_guess = proxsuite::qp::InitialGuessStatus::EQUALITY_CONSTRAINED_INITIAL_GUESS
			){
	
	isize n(H.rows());
	isize n_eq(A.rows());
	isize n_in(C.rows());

	QP<T> Qp(n, n_eq, n_in);
	Qp.settings.initial_guess = initial_guess;

	if (eps_abs!=std::nullopt){
		Qp.settings.eps_abs = eps_abs.value();
	}
	if (eps_rel!=std::nullopt){
		Qp.settings.eps_rel = eps_rel.value();
	}
	if (verbose!=std::nullopt){
		Qp.settings.verbose = verbose.value();
	}
	if (max_iter!=std::nullopt){
		Qp.settings.max_iter = verbose.value();
	}
	Qp.settings.compute_timings = compute_timings;
	Qp.init(H,g,A,b,C,u,l,compute_preconditioner,rho,mu_eq,mu_in);
	Qp.solve(x,y,z); 

	return Qp.results;
};
/*!
* Solves the QP problem using PROXQP algorithm without the need to define a QP object, with matrices defined by Dense Eigen matrices.
* It is possible to set up some of the solver parameters (warm start, initial guess option, proximal step sizes, absolute and relative accuracies, maximum number of iterations, preconditioner execution).
* @param H quadratic cost input defining the QP model.
* @param g linear cost input defining the QP model.
* @param A equality constraint matrix input defining the QP model.
* @param b equality constraint vector input defining the QP model.
* @param C inequality constraint matrix input defining the QP model.
* @param u lower inequality constraint vector input defining the QP model.
* @param l lower inequality constraint vector input defining the QP model.
* @param x primal warm start.
* @param y dual equality constraint warm start.
* @param z dual inequality constraint warm start.
* @param verbose if set to true, the solver prints more information about each iteration.
* @param compute_preconditioner bool parameter for executing or not the preconditioner.
* @param compute_timings boolean parameter for computing the solver timings.
* @param rho proximal step size wrt primal variable.
* @param mu_eq proximal step size wrt equality constrained multiplier.
* @param mu_in proximal step size wrt inequality constrained multiplier.
* @param eps_abs absolute accuracy threshold.
* @param eps_rel relative accuracy threshold.
* @param max_iter maximum number of iteration.
* @param initial_guess initial guess option for warm starting or not the initial iterate values.
*/
template <typename T>
qp::Results<T> solve(const SparseMat<T>& H,
					 VecRef<T> g,
					 const SparseMat<T>& A,
					 VecRef<T> b,
					 const SparseMat<T>& C,
					 VecRef<T> u,
					 VecRef<T> l,
			std::optional<VecRef<T>> x = std::nullopt,
			std::optional<VecRef<T>> y = std::nullopt,
			std::optional<VecRef<T>> z = std::nullopt,
			std::optional<T> eps_abs = std::nullopt,
			std::optional<T> eps_rel = std::nullopt,
			std::optional<T> rho = std::nullopt,
			std::optional<T> mu_eq = std::nullopt,
			std::optional<T> mu_in = std::nullopt,
			std::optional<bool> verbose = std::nullopt,
			bool compute_preconditioner = true,
			bool compute_timings = true,
			std::optional<isize> max_iter = std::nullopt,
			proxsuite::qp::InitialGuessStatus initial_guess = proxsuite::qp::InitialGuessStatus::EQUALITY_CONSTRAINED_INITIAL_GUESS
			){
	
	isize n(H.rows());
	isize n_eq(A.rows());
	isize n_in(C.rows());

	QP<T> Qp(n, n_eq, n_in);
	Qp.settings.initial_guess = initial_guess;

	if (eps_abs!=std::nullopt){
		Qp.settings.eps_abs = eps_abs.value();
	}
	if (eps_rel!=std::nullopt){
		Qp.settings.eps_rel = eps_rel.value();
	}
	if (verbose!=std::nullopt){
		Qp.settings.verbose = verbose.value();
	}
	if (max_iter!=std::nullopt){
		Qp.settings.max_iter = verbose.value();
	}
	Qp.settings.compute_timings = compute_timings;
	Qp.init(H,g,A,b,C,u,l,compute_preconditioner,rho,mu_eq,mu_in);
	Qp.solve(x,y,z); 

	return Qp.results;
};

} // namespace dense
} // namespace qp
} // namespace proxsuite

#endif /* end of include guard PROXSUITE_QP_DENSE_WRAPPER_HPP */
