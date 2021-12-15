#ifndef INRIA_LDLT_OLD_NEW_SOLVER_HPP_HDWGZKCLS
#define INRIA_LDLT_OLD_NEW_SOLVER_HPP_HDWGZKCLS

#include <ldlt/ldlt.hpp>
#include "qp/views.hpp"
#include "ldlt/factorize.hpp"
#include "ldlt/detail/meta.hpp"
#include "ldlt/solve.hpp"
#include "ldlt/update.hpp"
#include "qp/proxqp/old_new_line_search.hpp"
#include <cmath>

#include <iostream>
#include<fstream>

template <typename T>
void saveData(std::string fileName, Eigen::Matrix<T, Eigen::Dynamic, 1>  vector)
{
    //https://eigen.tuxfamily.org/dox/structEigen_1_1IOFormat.html
    const static Eigen::IOFormat CSVFormat(Eigen::FullPrecision, Eigen::DontAlignCols, ", ", "\n");
 
    std::ofstream file(fileName);
    if (file.is_open())
    {
        file << vector.format(CSVFormat);
        file.close();
    }
}

namespace qp {
inline namespace tags {
using namespace ldlt::tags;
}


namespace detail {


template <typename T>
void oldNew_refactorize(
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T rho_new
		) {
		
	qpwork._dw_aug.setZero();
	qpwork._kkt.diagonal().head(qpmodel._dim).array() += rho_new - qpresults._rho; 
	qpwork._kkt.diagonal().segment(qpmodel._dim,qpmodel._n_eq).array() = -qpresults._mu_eq_inv; 
	qpwork._ldl.factorize(qpwork._kkt);

	for (isize j = 0; j < qpresults._n_c; ++j) {
		for (isize i = 0; i < qpmodel._n_in; ++i) {
			if (j == qpwork._current_bijection_map(i)) {
					qpwork._dw_aug.head(qpmodel._dim) = qpwork._c_scaled.row(i);
					qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + j) = - qpresults._mu_in_inv; // mu_in stores the inverse of mu_in
					qpwork._ldl.insert_at(qpmodel._n_eq + qpmodel._dim + j, qpwork._dw_aug.head(qpmodel._dim+qpmodel._n_eq+qpresults._n_c));
					qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + j) = T(0);
			}
		}
	}
	qpwork._dw_aug.setZero();
}

template <typename T>
void oldNew_mu_update(
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T mu_eq_new_inv,
		T mu_in_new_inv) {
	T diff = 0;

	qpwork._dw_aug.head(qpmodel._dim+qpmodel._n_eq+qpresults._n_c).setZero();
	if (qpmodel._n_eq > 0) {
		diff = qpresults._mu_eq_inv -  mu_eq_new_inv; // mu stores the inverse of mu

		for (isize i = 0; i < qpmodel._n_eq; i++) {
			qpwork._dw_aug(qpmodel._dim + i) = T(1);
			qpwork._ldl.rank_one_update(qpwork._dw_aug.head(qpmodel._dim+qpmodel._n_eq+qpresults._n_c), diff);
			qpwork._dw_aug(qpmodel._dim + i) = T(0);
		}
	}
	if (qpresults._n_c > 0) {
		diff = qpresults._mu_in_inv - mu_in_new_inv; // mu stores the inverse of mu
		for (isize i = 0; i < qpresults._n_c; i++) {
			qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + i) = T(1);
			qpwork._ldl.rank_one_update(qpwork._dw_aug.head(qpmodel._dim+qpmodel._n_eq+qpresults._n_c), diff);
			qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + i) = T(0);
		}
	}
}

template <typename T>
void oldNew_iterative_residual(
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		isize inner_pb_dim) {
 

	qpwork._err.head(inner_pb_dim).noalias()  = qpwork._rhs.head(inner_pb_dim);
	/*
	if (chekNoAlias){
		saveData("_err_after_copy"+std::to_string(it_)+str,qpwork._err.eval());
	}
	*/

	qpwork._err.head(qpmodel._dim).noalias()  -= qpwork._h_scaled * qpwork._dw_aug.head(qpmodel._dim);
	/*
	if (chekNoAlias){
		saveData("_err_after_H"+std::to_string(it_)+str,qpwork._err.eval());
	}
	*/
    qpwork._err.head(qpmodel._dim).noalias()  -= qpresults._rho * qpwork._dw_aug.head(qpmodel._dim);
	/*
	if (chekNoAlias){
		saveData("_err_after_rho"+std::to_string(it_)+str,qpwork._err.eval()); 
	}
	*/
    qpwork._err.head(qpmodel._dim).noalias()  -= qpwork._a_scaled.transpose() * qpwork._dw_aug.segment(qpmodel._dim, qpmodel._n_eq);
	/*
	if (chekNoAlias){
		saveData("_err_after_Ahead"+std::to_string(it_)+str,qpwork._err.eval());
	}
	*/

	for (isize i = 0; i < qpmodel._n_in; i++) {
		isize j = qpwork._current_bijection_map(i);
		if (j < qpresults._n_c) {
			qpwork._err.head(qpmodel._dim).noalias()  -= qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + j) * qpwork._c_scaled.row(i);
			qpwork._err(qpmodel._dim + qpmodel._n_eq + j) -=
					(qpwork._c_scaled.row(i).dot(qpwork._dw_aug.head(qpmodel._dim)) -
					 qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + j)  * qpresults._mu_in_inv); // mu stores the inverse of mu
		}
	}
	/*
	if (chekNoAlias){
		saveData("_err_after_act"+std::to_string(it_)+str,qpwork._err.eval());
	}
	*/

	qpwork._err.segment(qpmodel._dim, qpmodel._n_eq).noalias()  -= (qpwork._a_scaled * qpwork._dw_aug.head(qpmodel._dim) - qpwork._dw_aug.segment(qpmodel._dim, qpmodel._n_eq) * qpresults._mu_eq_inv); // mu stores the inverse of mu

}

template <typename T>
void oldNew_iterative_solve_with_permut_fact_new( //
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T eps,
		isize inner_pb_dim,
        const bool VERBOSE
        ){

	qpwork._err.setZero();
	i32 it = 0;

	qpwork._dw_aug.head(inner_pb_dim) = qpwork._rhs.head(inner_pb_dim);
	
	qpwork._ldl.solve_in_place(qpwork._dw_aug.head(inner_pb_dim));
	/*
	if (chekNoAlias){
		std::cout << "-----------before computing residual" << std::endl;
		saveData("rhs_before"+std::to_string(it_)+str,qpwork._rhs);
		saveData("dw_aug_before"+std::to_string(it_)+str,qpwork._rhs);
		saveData("_err_before"+std::to_string(it_)+str,qpwork._err);
		std::cout << "rhs_" << qpwork._rhs << std::endl;
		std::cout << "dw_aug_" << qpwork._dw_aug << std::endl;
		std::cout << "_err" << qpwork._err << std::endl;
	}
	*/


	qp::detail::oldNew_iterative_residual<T>( 
					qpmodel,
					qpresults,
					qpwork,
                    inner_pb_dim);
	/*
	if (chekNoAlias){
		std::cout << "-----------after computing residual" << std::endl;
		std::cout << "rhs_" << qpwork._rhs << std::endl;
		std::cout << "dw_aug_" << qpwork._dw_aug << std::endl;
		std::cout << "_err" << qpwork._err << std::endl;
		saveData("rhs_after"+std::to_string(it_)+str,qpwork._rhs);
		saveData("dw_aug_after"+std::to_string(it_)+str,qpwork._rhs);
		saveData("_err_after"+std::to_string(it_)+str,qpwork._err);
	}
	*/

	++it;
	if (VERBOSE){
		std::cout << "infty_norm(res) " << qp::infty_norm( qpwork._err.head(inner_pb_dim)) << std::endl;
	}
	while (infty_norm( qpwork._err.head(inner_pb_dim)) >= eps) {
		if (it >= qpsettings._nb_iterative_refinement) {
			break;
		} 
		++it;
		qpwork._ldl.solve_in_place( qpwork._err.head(inner_pb_dim));
		qpwork._dw_aug.head(inner_pb_dim).noalias() +=  qpwork._err.head(inner_pb_dim);

		qpwork._err.head(inner_pb_dim).setZero();
		qp::detail::oldNew_iterative_residual<T>(
					qpmodel,
					qpresults,
					qpwork,
                    inner_pb_dim);

		if (VERBOSE){
			std::cout << "infty_norm(res) " << qp::infty_norm(qpwork._err.head(inner_pb_dim)) << std::endl;
		}
	}
	if (qp::infty_norm(qpwork._err.head(inner_pb_dim))>= std::max(eps,qpsettings._eps_refact)){
		{
			
			LDLT_MULTI_WORKSPACE_MEMORY(
				(_htot,Uninit, Mat(qpmodel._dim+qpmodel._n_eq+qpresults._n_c, qpmodel._dim+qpmodel._n_eq+qpresults._n_c),LDLT_CACHELINE_BYTES, T)
				);
			auto Htot = _htot.to_eigen().eval();

			Htot.setZero();
			
			qpwork._kkt.diagonal().segment(qpmodel._dim,qpmodel._n_eq).array() = -qpresults._mu_eq_inv; 
			Htot.topLeftCorner(qpmodel._dim+qpmodel._n_eq, qpmodel._dim+qpmodel._n_eq) = qpwork._kkt;

			Htot.diagonal().segment(qpmodel._dim+qpmodel._n_eq,qpresults._n_c).array() = -qpresults._mu_in_inv; 

			for (isize i = 0; i< qpmodel._n_in ; ++i){
					
					isize j = qpwork._current_bijection_map(i);
					if (j<qpresults._n_c){
						Htot.block(j+qpmodel._dim+qpmodel._n_eq,0,1,qpmodel._dim) = qpwork._c_scaled.row(i) ; 
						Htot.block(0,j+qpmodel._dim+qpmodel._n_eq,qpmodel._dim,1) = qpwork._c_scaled.row(i) ; 
					}
			}
			
			oldNew_refactorize(
						qpmodel,
						qpresults,
						qpwork,
						qpresults._rho
						);
			
			std::cout << " ldl.reconstructed_matrix() - Htot " << infty_norm(qpwork._ldl.reconstructed_matrix() - Htot)<< std::endl;
		}
		it = 0;
		qpwork._dw_aug.head(inner_pb_dim) = qpwork._rhs.head(inner_pb_dim);

		qpwork._ldl.solve_in_place(qpwork._dw_aug.head(inner_pb_dim));

		qp::detail::oldNew_iterative_residual<T>(
					qpmodel,
					qpresults,
					qpwork,
                    inner_pb_dim);
		++it;
		if (VERBOSE){
			std::cout << "infty_norm(res) " << qp::infty_norm( qpwork._err.head(inner_pb_dim)) << std::endl;
		}
		while (infty_norm( qpwork._err.head(inner_pb_dim)) >= eps) {
			if (it >= qpsettings._nb_iterative_refinement) {
				break;
			}
			++it;
			qpwork._ldl.solve_in_place( qpwork._err.head(inner_pb_dim) );
			qpwork._dw_aug.head(inner_pb_dim).noalias()  +=  qpwork._err.head(inner_pb_dim);
  
			qpwork._err.head(inner_pb_dim).setZero();
			qp::detail::oldNew_iterative_residual<T>(
					qpmodel,
					qpresults,
					qpwork,
                    inner_pb_dim);

			if (VERBOSE){
				std::cout << "infty_norm(res) " << qp::infty_norm(qpwork._err.head(inner_pb_dim)) << std::endl;
			}
		}
	}
	qpwork._rhs.head(inner_pb_dim).setZero();
}


template <typename T>
void oldNew_BCL_update(
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T& primal_feasibility_lhs,
		T& bcl_eta_ext,
		T& bcl_eta_in,

		T bcl_eta_ext_init,
		T eps_in_min
		){
		
		if (primal_feasibility_lhs <= bcl_eta_ext) {
			std::cout << "good step"<< std::endl;
			bcl_eta_ext = bcl_eta_ext * pow(qpresults._mu_in_inv, qpsettings._beta_bcl);
			bcl_eta_in = max2(bcl_eta_in * qpresults._mu_in_inv,eps_in_min);
		} else {
			std::cout << "bad step"<< std::endl; 
			qpresults._y = qpwork._ye;
			qpresults._z = qpwork._ze;
			T new_bcl_mu_in(std::min(qpresults._mu_in * qpsettings._mu_update_factor, qpsettings._mu_max_in));
			T new_bcl_mu_eq(std::min(qpresults._mu_eq * qpsettings._mu_update_factor, qpsettings._mu_max_eq));
			T new_bcl_mu_in_inv(max2(qpresults._mu_in_inv * qpsettings._mu_update_inv_factor, qpsettings._mu_max_in_inv)); // mu stores the inverse of mu
			T new_bcl_mu_eq_inv(max2(qpresults._mu_eq_inv * qpsettings._mu_update_inv_factor, qpsettings._mu_max_eq_inv)); // mu stores the inverse of mu


			if (qpresults._mu_in != new_bcl_mu_in || qpresults._mu_eq != new_bcl_mu_eq) {
					{
					++qpresults._n_mu_change;
					}
			}	
			qp::detail::oldNew_mu_update(
				qpmodel,
				qpresults,
				qpwork,
				new_bcl_mu_eq_inv,
				new_bcl_mu_in_inv);
			qpresults._mu_eq = new_bcl_mu_eq;
			qpresults._mu_in = new_bcl_mu_in;
			qpresults._mu_eq_inv = new_bcl_mu_eq_inv;
			qpresults._mu_in_inv = new_bcl_mu_in_inv;
			bcl_eta_ext = bcl_eta_ext_init * pow(qpresults._mu_in_inv, qpsettings._alpha_bcl);
			bcl_eta_in = max2(  qpresults._mu_in_inv  ,eps_in_min);
	}
}

template <typename T>
void oldNew_global_primal_residual(
			qp::Qpdata<T>& qpmodel,
			qp::Qpresults<T>& qpresults,
			qp::OldNew_Qpworkspace<T>& qpwork,
			T& primal_feasibility_lhs,
			T& primal_feasibility_eq_rhs_0,
        	T& primal_feasibility_in_rhs_0,
			T& primal_feasibility_eq_lhs,
			T& primal_feasibility_in_lhs
		){		

				qpwork._primal_residual_eq_scaled.noalias() = qpwork._a_scaled * qpresults._x;
				qpwork._primal_residual_in_scaled_u.noalias() = qpwork._c_scaled * qpresults._x;

				qpwork._ruiz.unscale_primal_residual_in_place_eq(VectorViewMut<T>{from_eigen, qpwork._primal_residual_eq_scaled});
				primal_feasibility_eq_rhs_0 = infty_norm(qpwork._primal_residual_eq_scaled);
				qpwork._ruiz.unscale_primal_residual_in_place_in(VectorViewMut<T>{from_eigen,qpwork._primal_residual_in_scaled_u});
				primal_feasibility_in_rhs_0 = infty_norm(qpwork._primal_residual_in_scaled_u);

				qpwork._primal_residual_in_scaled_l.noalias() = detail::positive_part(qpwork._primal_residual_in_scaled_u -qpmodel._u)+detail::negative_part(qpwork._primal_residual_in_scaled_u-qpmodel._l);
				qpwork._primal_residual_eq_scaled.noalias() -= qpmodel._b ; 

				primal_feasibility_in_lhs = infty_norm(qpwork._primal_residual_in_scaled_l);
				primal_feasibility_eq_lhs = infty_norm(qpwork._primal_residual_eq_scaled);
                primal_feasibility_lhs = max2(primal_feasibility_eq_lhs,primal_feasibility_in_lhs);

				qpwork._ruiz.scale_primal_residual_in_place_eq(VectorViewMut<T>{from_eigen, qpwork._primal_residual_eq_scaled});
}


template <typename T>
void oldNew_global_dual_residual(
			qp::Qpdata<T>& qpmodel,
			qp::Qpresults<T>& qpresults,
			qp::OldNew_Qpworkspace<T>& qpwork,
			T& dual_feasibility_lhs,
			T& dual_feasibility_rhs_0,
			T& dual_feasibility_rhs_1,
        	T& dual_feasibility_rhs_3
		){

			qpwork._dual_residual_scaled = qpwork._g_scaled;
			qpwork._tmp1.noalias() = (qpwork._h_scaled * qpresults._x);
			qpwork._dual_residual_scaled.noalias()+=qpwork._tmp1;
			qpwork._ruiz.unscale_dual_residual_in_place(VectorViewMut<T>{from_eigen, qpwork._tmp1});
			dual_feasibility_rhs_0 = infty_norm(qpwork._tmp1);
			qpwork._tmp2.noalias() = qpwork._a_scaled.transpose() * qpresults._y;
			qpwork._dual_residual_scaled.noalias() += qpwork._tmp2; 
			qpwork._ruiz.unscale_dual_residual_in_place(VectorViewMut<T>{from_eigen, qpwork._tmp2});
			dual_feasibility_rhs_1 = infty_norm(qpwork._tmp2);

			qpwork._tmp3.noalias() = qpwork._c_scaled.transpose() * qpresults._z;
			qpwork._dual_residual_scaled.noalias() += qpwork._tmp3; 
			qpwork._ruiz.unscale_dual_residual_in_place(VectorViewMut<T>{from_eigen,qpwork._tmp3});
			dual_feasibility_rhs_3 = infty_norm(qpwork._tmp3);

			qpwork._ruiz.unscale_dual_residual_in_place(
					VectorViewMut<T>{from_eigen, qpwork._dual_residual_scaled});

			dual_feasibility_lhs = infty_norm(qpwork._dual_residual_scaled);

			qpwork._ruiz.scale_dual_residual_in_place(
					VectorViewMut<T>{from_eigen, qpwork._dual_residual_scaled});

        };


template<typename T> 
T oldNew_SaddlePoint(
			qp::Qpdata<T>& qpmodel,
			qp::Qpresults<T>& qpresults,
			qp::OldNew_Qpworkspace<T>& qpwork
			){

			qpwork._primal_residual_in_scaled_u.noalias() -=  (qpresults._z*qpresults._mu_in_inv); 
			qpwork._primal_residual_in_scaled_l.noalias() -= (qpresults._z*qpresults._mu_in_inv) ; 
			T prim_eq_e = infty_norm(qpwork._primal_residual_eq_scaled) ; 
			qpwork._dual_residual_scaled.noalias() += (qpwork._c_scaled.transpose()*qpresults._z);
			T dual_e = infty_norm(qpwork._dual_residual_scaled);
			T err = max2(prim_eq_e,dual_e);

			qpwork._tmp_u.noalias() = qp::detail::positive_part(qpwork._primal_residual_in_scaled_u) + qp::detail::negative_part(qpwork._primal_residual_in_scaled_l);
			qpwork._l_active_set_n_u.noalias() = ( qpresults._z.array()>T(0.)).matrix();
			qpwork._l_active_set_n_l.noalias() = ( qpresults._z.array()<T(0.)).matrix();
			qpwork._active_part_z.noalias() = (qpwork._l_active_set_n_u).select(qpwork._primal_residual_in_scaled_u, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) +
				   (qpwork._l_active_set_n_l).select(qpwork._primal_residual_in_scaled_l, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) +
				   (!qpwork._l_active_set_n_l.array() && !qpwork._l_active_set_n_u.array()).select(qpwork._tmp_u, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) ;

			err = max2(err,infty_norm(qpwork._active_part_z));

			/*
			T prim_in_e(0.);

			for (isize i = 0 ; i< qpmodel._n_in ; i=i+1){ // try vectorize it 
				if (qpresults._z(i) >0.){
					prim_in_e = max2(prim_in_e,std::abs(qpwork._primal_residual_in_scaled_u(i)));
				}else if (qpresults._z(i) < 0.){
					prim_in_e = max2(prim_in_e,std::abs(qpwork._primal_residual_in_scaled_l(i)));
				}else{
					prim_in_e = max2(prim_in_e,max2(qpwork._primal_residual_in_scaled_u(i),T(0.))) ;
					prim_in_e = max2(prim_in_e, std::abs(std::min(qpwork._primal_residual_in_scaled_l(i),T(0.))));
				}
			}
			err = max2(err,prim_in_e);
			*/
			return err;
}


template<typename T>
void oldNew_newton_step_new(
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T eps,
        const bool VERBOSE
	){

		qpwork._l_active_set_n_u.noalias() = (qpwork._primal_residual_in_scaled_u.array() > 0).matrix();
		qpwork._l_active_set_n_l.noalias() = (qpwork._primal_residual_in_scaled_l.array() < 0).matrix();

		qpwork._active_inequalities.noalias() = qpwork._l_active_set_n_u || qpwork._l_active_set_n_l ; 

		isize num_active_inequalities = qpwork._active_inequalities.count();
		isize inner_pb_dim = qpmodel._dim + qpmodel._n_eq + num_active_inequalities;

		qpwork._rhs.setZero();
		qpwork._dw_aug.setZero();

		qpwork._rhs.topRows(qpmodel._dim).noalias() -=  qpwork._dual_residual_scaled ;

        qp::line_search::oldNew_active_set_change(
					qpmodel,
					qpresults,
					qpwork
		);

        oldNew_iterative_solve_with_permut_fact_new( //
					qpsettings,
					qpmodel,
					qpresults,
					qpwork,
                    eps,
                    inner_pb_dim,
                    VERBOSE
					);

}

template<typename T>
T oldNew_initial_guess(
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
        VectorViewMut<T> ze,
		T eps_int,
        const bool VERBOSE
		){

			auto z_e = ze.to_eigen().eval();

			qpwork._ruiz.unscale_dual_in_place_in(
							VectorViewMut<T>{from_eigen, z_e}); 
			
			qpwork._primal_residual_in_scaled_u.noalias() += (z_e*qpresults._mu_in_inv) ;  // contains now unscaled(Cx+ze/mu_in)

			qpwork._primal_residual_in_scaled_l.noalias() = qpwork._primal_residual_in_scaled_u;
			qpwork._primal_residual_in_scaled_u.noalias() -= qpmodel._u;
			qpwork._primal_residual_in_scaled_l.noalias() -= qpmodel._l;

			qpwork._l_active_set_n_u = (qpwork._primal_residual_in_scaled_u.array() >= 0.).matrix();
			qpwork._l_active_set_n_l = (qpwork._primal_residual_in_scaled_l.array() <= 0.).matrix();

			qpwork._active_inequalities = qpwork._l_active_set_n_u || qpwork._l_active_set_n_l ;   

			
			qpwork._primal_residual_in_scaled_u.noalias() -= (z_e*qpresults._mu_in_inv) ; 
			qpwork._primal_residual_in_scaled_l.noalias() -= (z_e*qpresults._mu_in_inv) ; 

			qpwork._ruiz.scale_primal_residual_in_place_in(
							VectorViewMut<T>{from_eigen, qpwork._primal_residual_in_scaled_u});
			qpwork._ruiz.scale_primal_residual_in_place_in(
							VectorViewMut<T>{from_eigen, qpwork._primal_residual_in_scaled_l});
			
			qpwork._ruiz.scale_dual_in_place_in(
							VectorViewMut<T>{from_eigen, z_e});

			isize num_active_inequalities = qpwork._active_inequalities.count();
			isize inner_pb_dim = qpmodel._dim + qpmodel._n_eq + num_active_inequalities;

			qpwork._rhs.setZero();
			qpwork._active_part_z.setZero();
            qp::line_search::oldNew_active_set_change(
								qpmodel,
								qpresults,
								qpwork
								);
			
			qpwork._rhs.head(qpmodel._dim).noalias() = -qpwork._dual_residual_scaled ;
			qpwork._rhs.segment(qpmodel._dim,qpmodel._n_eq).noalias() = -qpwork._primal_residual_eq_scaled ;
			for (isize i = 0; i < qpmodel._n_in; i++) {
				isize j = qpwork._current_bijection_map(i);
				if (j < qpresults._n_c) {
					if (qpwork._l_active_set_n_u(i)) { 
						qpwork._rhs(j + qpmodel._dim + qpmodel._n_eq) = -qpwork._primal_residual_in_scaled_u(i);
					} else if (qpwork._l_active_set_n_l(i)) {
						qpwork._rhs(j + qpmodel._dim + qpmodel._n_eq) = -qpwork._primal_residual_in_scaled_l(i);
					}
				} else {
					qpwork._rhs.head(qpmodel._dim).noalias() += qpresults._z(i) * qpwork._c_scaled.row(i); // unactive unrelevant columns
				}
			}	

            oldNew_iterative_solve_with_permut_fact_new( //
					qpsettings,
					qpmodel,
					qpresults,
					qpwork,
                    eps_int,
                    inner_pb_dim,
                    VERBOSE
					);
			
			// use active_part_z as a temporary variable to permut back dw_aug newton step
			for (isize j = 0; j < qpmodel._n_in; ++j) {
				isize i = qpwork._current_bijection_map(j);
				if (i < qpresults._n_c) {
					qpwork._active_part_z(j) = qpwork._dw_aug(qpmodel._dim + qpmodel._n_eq + i);
				} else {
					qpwork._active_part_z(j) = -qpresults._z(j);
				}
			}
			qpwork._dw_aug.tail(qpmodel._n_in) = qpwork._active_part_z ;

			
			qpwork._primal_residual_in_scaled_u.noalias() += (z_e*qpresults._mu_in_inv) ; 
			qpwork._primal_residual_in_scaled_l.noalias() += (z_e*qpresults._mu_in_inv) ; 

			qpwork._d_primal_residual_eq.noalias() = (qpwork._a_scaled*qpwork._dw_aug.topRows(qpmodel._dim)- qpwork._dw_aug.middleRows(qpmodel._dim,qpmodel._n_eq) * qpresults._mu_eq_inv).eval() ; // try optimization
			qpwork._d_dual_for_eq.noalias() = (qpwork._h_scaled*qpwork._dw_aug.topRows(qpmodel._dim)+qpwork._a_scaled.transpose()*qpwork._dw_aug.middleRows(qpmodel._dim,qpmodel._n_eq)+qpresults._rho*qpwork._dw_aug.topRows(qpmodel._dim)).eval() ; // idem
			qpwork._Cdx.noalias() = qpwork._c_scaled*qpwork._dw_aug.topRows(qpmodel._dim) ; // idem
			qpwork._dual_residual_scaled.noalias() -= qpwork._c_scaled.transpose()*z_e ; 

			qp::line_search::oldNew_initial_guess_LS(
						qpsettings,
						qpmodel,
						qpresults,
						qpwork
			);
			
			std::cout << "alpha from initial guess " << qpwork._alpha << std::endl;

			qpwork._primal_residual_in_scaled_u.noalias() += (qpwork._alpha*qpwork._Cdx);
			qpwork._primal_residual_in_scaled_l.noalias() += (qpwork._alpha*qpwork._Cdx);
			qpwork._l_active_set_n_u.noalias() = (qpwork._primal_residual_in_scaled_u.array() >= 0.).matrix();
			qpwork._l_active_set_n_l.noalias() = (qpwork._primal_residual_in_scaled_l.array() <= 0.).matrix();
			qpwork._active_inequalities.noalias() = qpwork._l_active_set_n_u || qpwork._l_active_set_n_l ; 

			qpresults._x.noalias() += (qpwork._alpha * qpwork._dw_aug.topRows(qpmodel._dim)) ; 
			qpresults._y.noalias() += (qpwork._alpha * qpwork._dw_aug.middleRows(qpmodel._dim,qpmodel._n_eq)) ; 

			/*
			for (isize i = 0; i< qpmodel._n_in ; ++i){ // try vectorization
				if (qpwork._l_active_set_n_u(i)){
					qpresults._z(i) = std::max(qpresults._z(i)+qpwork._alpha*qpwork._dw_aug(qpmodel._dim+qpmodel._n_eq+i),T(0.)) ; 
				}else if (qpwork._l_active_set_n_l(i)){
					qpresults._z(i) = std::min(qpresults._z(i)+qpwork._alpha*qpwork._dw_aug(qpmodel._dim+qpmodel._n_eq+i),T(0.)) ; 
				} else{
					qpresults._z(i) += qpwork._alpha*qpwork._dw_aug(qpmodel._dim+qpmodel._n_eq+i) ; 
				}
			}
			*/

			qpwork._active_part_z.noalias() = qpresults._z+qpwork._alpha*qpwork._dw_aug.tail(qpmodel._n_in);
			qpwork._tmp_u.noalias() = (qpwork._active_part_z.array() > T(0.)).select(qpwork._active_part_z, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in));
			qpwork._tmp_l.noalias() = (qpwork._active_part_z.array() < T(0.)).select(qpwork._active_part_z, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in));

			qpresults._z.noalias() = (qpwork._l_active_set_n_u).select(qpwork._tmp_u, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) +
				   (qpwork._l_active_set_n_l).select(qpwork._tmp_l, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) +
				   (!qpwork._l_active_set_n_l.array() && !qpwork._l_active_set_n_u.array()).select(qpwork._active_part_z, Eigen::Matrix<T,Eigen::Dynamic,1>::Zero(qpmodel._n_in)) ;
	

			qpwork._primal_residual_eq_scaled.noalias() += (qpwork._alpha*qpwork._d_primal_residual_eq);
			qpwork._dual_residual_scaled.noalias() += qpwork._alpha* (qpwork._d_dual_for_eq) ;
			qpwork._dw_aug.setZero();
			// TODO try for acceleration with a rhs relative error inside
			T err_saddle_point = oldNew_SaddlePoint(
				qpmodel,
				qpresults,
				qpwork
			);
			
			return err_saddle_point;
}


template<typename T>
T oldNew_correction_guess(
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork,
		T eps_int,
        const bool VERBOSE
		){

		T err_in = 1.e6;

		for (i64 iter = 0; iter <= qpsettings._max_iter_in; ++iter) {

			if (iter == qpsettings._max_iter_in) {
				qpresults._n_tot += qpsettings._max_iter_in;
				break;
			}
			
			qp::detail::oldNew_newton_step_new<T>(
											qpsettings,
											qpmodel,
											qpresults,
											qpwork,
											eps_int,
                                            VERBOSE
			);

			qpwork._d_dual_for_eq.noalias() = qpwork._h_scaled * qpwork._dw_aug.head(qpmodel._dim) ; 
			qpwork._d_primal_residual_eq.noalias() = qpwork._a_scaled * qpwork._dw_aug.head(qpmodel._dim) ; 
			qpwork._Cdx.noalias() = qpwork._c_scaled * qpwork._dw_aug.head(qpmodel._dim) ; 

			if (qpmodel._n_in > 0){
				qp::line_search::oldNew_correction_guess_LS(
										qpmodel,
										qpresults,
										qpwork
				);
			}
			if (infty_norm(qpwork._alpha * qpwork._dw_aug.head(qpmodel._dim))< 1.E-11){
				qpresults._n_tot += iter+1;
				std::cout << "infty_norm(alpha_step * dx) " << infty_norm(qpwork._alpha * qpwork._dw_aug.head(qpmodel._dim)) << std::endl;
				break;
			}
			
			qpresults._x.noalias() += (qpwork._alpha *qpwork._dw_aug.head(qpmodel._dim)) ; 
			qpwork._primal_residual_in_scaled_u.noalias() += (qpwork._alpha *qpwork._Cdx) ;
			qpwork._primal_residual_in_scaled_l.noalias() += (qpwork._alpha *qpwork._Cdx); 
			qpwork._primal_residual_eq_scaled.noalias() += qpwork._alpha * qpwork._d_primal_residual_eq;
 			qpresults._y.noalias() = qpresults._mu_eq *  qpwork._primal_residual_eq_scaled  ;

			qpresults._z.noalias() =  (qp::detail::positive_part(qpwork._primal_residual_in_scaled_u) + qp::detail::negative_part(qpwork._primal_residual_in_scaled_l)) *  qpresults._mu_in;

			// see if optimization possible with += qpwork._d_dual_for_eq or replace tmp1 by qpwork._d_dual_for_eq
			qpwork._dual_residual_scaled.noalias() = qpwork._h_scaled *qpresults._x ;
			T rhs_c = max2(qpwork._correction_guess_rhs_g,infty_norm(qpwork._dual_residual_scaled)) ;
			qpwork._tmp2.noalias() = qpwork._a_scaled.transpose() * ( qpresults._y );
			qpwork._dual_residual_scaled.noalias()+= qpwork._tmp2;
			rhs_c = max2(rhs_c,infty_norm(qpwork._tmp2));
			qpwork._tmp3.noalias() = qpwork._c_scaled.transpose() * ( qpresults._z )   ; 
			qpwork._dual_residual_scaled.noalias()+= qpwork._tmp3;
			rhs_c = max2(rhs_c,infty_norm(qpwork._tmp3));
			qpwork._dual_residual_scaled.noalias() +=  qpwork._g_scaled + qpresults._rho* (qpresults._x-qpwork._xe) ; 
			rhs_c += 1.;

			err_in = infty_norm(qpwork._dual_residual_scaled);
			std::cout << "---it in " << iter << " projection norm " << err_in << " alpha " << qpwork._alpha << " rhs " << eps_int * rhs_c  <<  std::endl;

			if (err_in<= eps_int * rhs_c  ){
				qpresults._n_tot +=iter+1;
				break;
			}
		}
	
		return err_in;

}

template <typename T>
QpSolveStats oldNew_qpSolve( //
		qp::Qpsettings<T>& qpsettings,
		qp::Qpdata<T>& qpmodel,
		qp::Qpresults<T>& qpresults,
		qp::OldNew_Qpworkspace<T>& qpwork) {

	using namespace ldlt::tags;
    static constexpr Layout layout = rowmajor;
    static constexpr auto DYN = Eigen::Dynamic;
	using RowMat = Eigen::Matrix<T, DYN, DYN, Eigen::RowMajor>;
    const bool VERBOSE = true;


	T machine_eps = std::numeric_limits<T>::epsilon();

	T bcl_eta_ext_init = pow(T(0.1),qpsettings._alpha_bcl);
	T bcl_eta_ext = bcl_eta_ext_init;
	T bcl_eta_in(1);
	T eps_in_min = std::min(qpsettings._eps_abs,T(1.E-9));
	
	//// 4/ malloc for no allocation
	/// 5/ load maros problems from c++ parser

	RowMat test(2,2); // test it is full of nan for debug
	std::cout << "test " << test << std::endl;

	T primal_feasibility_eq_rhs_0(0);
	T primal_feasibility_in_rhs_0(0);
	T dual_feasibility_rhs_0(0);
	T dual_feasibility_rhs_1(0);
	T dual_feasibility_rhs_3(0);
	T primal_feasibility_lhs(0);
	T primal_feasibility_eq_lhs(0);
	T primal_feasibility_in_lhs(0);
	T dual_feasibility_lhs(0);
	
	for (i64 iter = 0; iter <= qpsettings._max_iter; ++iter) {
		qpresults._n_ext +=1;
		if (iter == qpsettings._max_iter) {
			break;
		}

		// compute primal residual

		qp::detail::oldNew_global_primal_residual(
				qpmodel,
				qpresults,
				qpwork,
				primal_feasibility_lhs,
				primal_feasibility_eq_rhs_0,
				primal_feasibility_in_rhs_0,
				primal_feasibility_eq_lhs,
				primal_feasibility_in_lhs
		);
		qp::detail::oldNew_global_dual_residual(
			qpmodel,
			qpresults,
			qpwork,
			dual_feasibility_lhs,
			dual_feasibility_rhs_0,
			dual_feasibility_rhs_1,
        	dual_feasibility_rhs_3
		);
		
		std::cout << "---------------it : " << iter << " primal residual : " << primal_feasibility_lhs << " dual residual : " << dual_feasibility_lhs << std::endl;
		std::cout << "bcl_eta_ext : " << bcl_eta_ext << " bcl_eta_in : " << bcl_eta_in <<  " rho : " << qpresults._rho << " bcl_mu_eq : " << qpresults._mu_eq << " bcl_mu_in : " << qpresults._mu_in <<std::endl;

		bool is_primal_feasible = primal_feasibility_lhs <= (qpsettings._eps_abs + qpsettings._eps_rel * max2(  max2(primal_feasibility_eq_rhs_0, primal_feasibility_in_rhs_0),  max2(max2( qpwork._primal_feasibility_rhs_1_eq, qpwork._primal_feasibility_rhs_1_in_u ),qpwork._primal_feasibility_rhs_1_in_l ) ));

		bool is_dual_feasible =dual_feasibility_lhs <=(qpsettings._eps_abs + qpsettings._eps_rel * max2( max2(   dual_feasibility_rhs_3, dual_feasibility_rhs_0),
													max2( dual_feasibility_rhs_1, qpwork._dual_feasibility_rhs_2)) );

		if (is_primal_feasible){
			
			if (dual_feasibility_lhs >= qpsettings._refactor_dual_feasibility_threshold && qpresults._rho != qpsettings._refactor_rho_threshold){

				T rho_new(qpsettings._refactor_rho_threshold);
				oldNew_refactorize(
						qpmodel,
						qpresults,
						qpwork,
						rho_new
						);

				qpresults._rho = rho_new;
			}
			if (is_dual_feasible){
				{
				qpwork._ruiz.unscale_primal_in_place(VectorViewMut<T>{from_eigen,qpresults._x}); 
				qpwork._ruiz.unscale_dual_in_place_eq(VectorViewMut<T>{from_eigen,qpresults._y});
				qpwork._ruiz.unscale_dual_in_place_in(VectorViewMut<T>{from_eigen,qpresults._z});
				}
				return {qpresults._n_ext, qpresults._n_mu_change,qpresults._n_tot};
			}
		}
		
		qpwork._xe = qpresults._x; 
		qpwork._ye = qpresults._y; 
		qpwork._ze = qpresults._z; 
		
		const bool do_initial_guess_fact = (primal_feasibility_lhs < qpsettings._eps_IG || qpmodel._n_in == 0 ) ;

		T err_in(0.);

		if (do_initial_guess_fact){

			err_in = qp::detail::oldNew_initial_guess<T>(
							qpsettings,
							qpmodel,
							qpresults,
							qpwork,
							VectorViewMut<T>{from_eigen,qpwork._ze},
							bcl_eta_in,
							VERBOSE

			);
			qpresults._n_tot +=1;

		}

		bool do_correction_guess = (!do_initial_guess_fact && qpmodel._n_in != 0) ||
		                           (do_initial_guess_fact && err_in >= bcl_eta_in && qpmodel._n_in != 0) ;
		
		std::cout << " error from initial guess : " << err_in << " bcl_eta_in " << bcl_eta_in << std::endl;
		
		if ((do_initial_guess_fact && err_in >= bcl_eta_in && qpmodel._n_in != 0)){

			qpwork._dual_residual_scaled.noalias() += -qpwork._c_scaled.transpose()*qpresults._z ; // contains now Hx* + rho(x*-xe) + g + ATy*
			qpwork._dual_residual_scaled.noalias() += qpresults._mu_eq * qpwork._a_scaled.transpose()*qpwork._primal_residual_eq_scaled ; // contains now Hx* + rho(x*-xe) + g + AT(ye+mu_eq*(Ax*-b))
			qpwork._primal_residual_eq_scaled.noalias()  += (qpresults._y*qpresults._mu_eq_inv); // contains now Ax*-b + ye/mu_eq
			qpwork._primal_residual_in_scaled_u.noalias()  += (qpresults._z*qpresults._mu_in_inv);// contains now Cx*-u + ze/mu_eq
			qpwork._primal_residual_in_scaled_l.noalias() += (qpresults._z*qpresults._mu_in_inv); // contains now Cx*-l + ze/mu_eq

			qpwork._active_part_z.noalias() = qpresults._mu_in*( qp::detail::positive_part(qpwork._primal_residual_in_scaled_u) + qp::detail::negative_part(qpwork._primal_residual_in_scaled_l) );
			qpwork._dual_residual_scaled.noalias() +=  qpwork._c_scaled.transpose() * qpwork._active_part_z  ;// contains now Hx + g + AT(y + mu(Ax-b)) + CT([z+mu(Cx-u)]+ + [z+mu(Cx-l)]-)

		}
		if (!do_initial_guess_fact && qpmodel._n_in != 0){ // y=ye, x=xe, 

			qpwork._ruiz.scale_primal_residual_in_place_in(VectorViewMut<T>{from_eigen, qpwork._primal_residual_in_scaled_u}); // contains now scaled(Cx) 
			qpwork._primal_residual_in_scaled_u.noalias() += qpwork._ze*qpresults._mu_in_inv; // contains now scaled(Cx+ze/mu_in)
			qpwork._primal_residual_in_scaled_l.noalias()  = qpwork._primal_residual_in_scaled_u ;
			qpwork._primal_residual_in_scaled_u.noalias()  -= qpwork._u_scaled;
			qpwork._primal_residual_in_scaled_l.noalias()  -= qpwork._l_scaled;

			qpwork._dual_residual_scaled.noalias() += qpresults._mu_eq * qpwork._a_scaled.transpose()*qpwork._primal_residual_eq_scaled  ; // contains now Hx + g + AT(y + mu(Ax-b)) + CTz 
			qpwork._primal_residual_eq_scaled.noalias()  += (qpresults._y*qpresults._mu_eq_inv);
			qpwork._active_part_z.noalias() = qpresults._mu_in*( qp::detail::positive_part(qpwork._primal_residual_in_scaled_u) + qp::detail::negative_part(qpwork._primal_residual_in_scaled_l) );
			qpwork._active_part_z.noalias() -= qpresults._z;
			qpwork._dual_residual_scaled.noalias() +=  qpwork._c_scaled.transpose() * qpwork._active_part_z  ; // contains now Hx + g + AT(y + mu(Ax-b)) + CT([z+mu(Cx-u)]+ + [z+mu(Cx-l)]-)

		}

		if (do_correction_guess){
			
			err_in = qp::detail::oldNew_correction_guess(
						qpsettings,
						qpmodel,
						qpresults,
						qpwork,
						bcl_eta_in,
                        VERBOSE
			);
			std::cout << " error from correction guess : " << err_in << std::endl;
		}
		
		T primal_feasibility_lhs_new(primal_feasibility_lhs) ; 

		qp::detail::oldNew_global_primal_residual(
						qpmodel,
						qpresults,
						qpwork,
						primal_feasibility_lhs_new,
						primal_feasibility_eq_rhs_0,
						primal_feasibility_in_rhs_0,
						primal_feasibility_eq_lhs,
						primal_feasibility_in_lhs
		);

		is_primal_feasible = primal_feasibility_lhs_new <= (qpsettings._eps_abs + qpsettings._eps_rel * max2(  max2(primal_feasibility_eq_rhs_0, primal_feasibility_in_rhs_0),  max2(max2( qpwork._primal_feasibility_rhs_1_eq, qpwork._primal_feasibility_rhs_1_in_u ),qpwork._primal_feasibility_rhs_1_in_l ) ));

		if (is_primal_feasible){
			T dual_feasibility_lhs_new(dual_feasibility_lhs) ; 
		
			qp::detail::oldNew_global_dual_residual(
				qpmodel,
				qpresults,
				qpwork,
				dual_feasibility_lhs_new,
				dual_feasibility_rhs_0,
				dual_feasibility_rhs_1,
				dual_feasibility_rhs_3
			);

			is_dual_feasible =dual_feasibility_lhs <=(qpsettings._eps_abs + qpsettings._eps_rel * max2( max2(   dual_feasibility_rhs_3, dual_feasibility_rhs_0),
													max2( dual_feasibility_rhs_1, qpwork._dual_feasibility_rhs_2)) );

			if (is_dual_feasible){
				{
				qpwork._ruiz.unscale_primal_in_place(VectorViewMut<T>{from_eigen,qpresults._x}); 
				qpwork._ruiz.unscale_dual_in_place_eq(VectorViewMut<T>{from_eigen,qpresults._y});
				qpwork._ruiz.unscale_dual_in_place_in(VectorViewMut<T>{from_eigen,qpresults._z});
				}
				return {qpresults._n_ext, qpresults._n_mu_change,qpresults._n_tot};
			}
		}

		qp::detail::oldNew_BCL_update(
					qpsettings,
					qpmodel,
					qpresults,
					qpwork,
					primal_feasibility_lhs_new,
					bcl_eta_ext,
					bcl_eta_in,
					bcl_eta_ext_init,
					eps_in_min
		);

		// COLD RESTART
		
		T dual_feasibility_lhs_new(dual_feasibility_lhs) ; 
		
		qp::detail::oldNew_global_dual_residual(
			qpmodel,
			qpresults,
			qpwork,
			dual_feasibility_lhs_new,
			dual_feasibility_rhs_0,
			dual_feasibility_rhs_1,
        	dual_feasibility_rhs_3
		);

		if ((primal_feasibility_lhs_new / max2(primal_feasibility_lhs,machine_eps) >= 1.) && (dual_feasibility_lhs_new / max2(primal_feasibility_lhs,machine_eps) >= 1.) && qpresults._mu_in >= 1.E5){
			std::cout << "cold restart" << std::endl;

			qp::detail::oldNew_mu_update(
				qpmodel,
				qpresults,
				qpwork,
				qpsettings._cold_reset_mu_eq_inv,
				qpsettings._cold_reset_mu_in_inv);
			
			qpresults._mu_in = qpsettings._cold_reset_mu_in;
			qpresults._mu_eq = qpsettings._cold_reset_mu_eq;
			qpresults._mu_in_inv = qpsettings._cold_reset_mu_in_inv;
			qpresults._mu_eq_inv = qpsettings._cold_reset_mu_eq_inv;
			qpresults._n_mu_change+=1;

		}
			
	}
	
	return {qpsettings._max_iter, qpresults._n_mu_change, qpresults._n_tot};
}


} // namespace detail

} // namespace qp

#endif /* end of include guard INRIA_LDLT_OLD_NEW_SOLVER_HPP_HDWGZKCLS */
