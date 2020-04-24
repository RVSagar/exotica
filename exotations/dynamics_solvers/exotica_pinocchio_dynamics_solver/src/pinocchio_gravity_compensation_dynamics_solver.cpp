//
// Copyright (c) 2020, University of Oxford
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of  nor the names of its contributors may be used to
//    endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include <exotica_pinocchio_dynamics_solver/pinocchio_gravity_compensation_dynamics_solver.h>

#include <pinocchio/algorithm/aba-derivatives.hpp>
#include <pinocchio/algorithm/aba.hpp>
#include <pinocchio/algorithm/cholesky.hpp>
#include <pinocchio/algorithm/compute-all-terms.hpp>
#include <pinocchio/algorithm/joint-configuration.hpp>
#include <pinocchio/algorithm/rnea-derivatives.hpp>
#include <pinocchio/algorithm/rnea.hpp>
#include <pinocchio/parsers/urdf.hpp>

REGISTER_DYNAMICS_SOLVER_TYPE("PinocchioDynamicsSolverWithGravityCompensation", exotica::PinocchioDynamicsSolverWithGravityCompensation)

namespace exotica
{
void PinocchioDynamicsSolverWithGravityCompensation::AssignScene(ScenePtr scene_in)
{
    constexpr bool verbose = false;
    if (scene_in->GetKinematicTree().GetControlledBaseType() == BaseType::FIXED)
    {
        pinocchio::urdf::buildModel(scene_in->GetKinematicTree().GetRobotModel()->getURDF(), model_, verbose);
    }
    else
    {
        ThrowPretty("Only BaseType::FIXED is currently supported with this DynamicsSolver.");
    }

    num_positions_ = model_.nq;
    num_velocities_ = model_.nv;
    num_controls_ = model_.nv;

    pinocchio_data_.reset(new pinocchio::Data(model_));

    // Pre-allocate data for f, fx, fu
    const int ndx = get_num_state_derivative();
    xdot_analytic_.setZero(ndx);
    fx_.setZero(ndx, ndx);
    fx_.topRightCorner(num_velocities_, num_velocities_).setIdentity();
    fu_.setZero(ndx, num_controls_);
    u_nle_.setZero(num_controls_);
    u_command_.setZero(num_controls_);
    a_.setZero(num_velocities_);
    du_command_dq_.setZero(num_controls_, num_velocities_);
    du_nle_dq_.setZero(num_controls_, num_velocities_);
}

Eigen::VectorXd PinocchioDynamicsSolverWithGravityCompensation::f(const StateVector& x, const ControlVector& u)
{
    // Obtain torque to compensate gravity and dynamic effects (Coriolis)
    u_nle_ = pinocchio::nonLinearEffects(model_, *pinocchio_data_, x.head(num_positions_).eval(), x.tail(num_velocities_).eval());

    // Commanded torque is u_nle_ + u
    u_command_.noalias() = u_nle_ + u;

    pinocchio::aba(model_, *pinocchio_data_, x.head(num_positions_).eval(), x.tail(num_velocities_).eval(), u_command_);
    xdot_analytic_.head(num_velocities_) = x.tail(num_velocities_);
    xdot_analytic_.tail(num_velocities_) = pinocchio_data_->ddq;
    return xdot_analytic_;
}

void PinocchioDynamicsSolverWithGravityCompensation::ComputeDerivatives(const StateVector& x, const ControlVector& u)
{
    Eigen::VectorBlock<const Eigen::VectorXd> q = x.head(num_positions_);
    Eigen::VectorBlock<const Eigen::VectorXd> v = x.tail(num_velocities_);

    // Obtain torque to compensate gravity and dynamic effects (Coriolis)
    u_nle_ = pinocchio::nonLinearEffects(model_, *pinocchio_data_, q, v);

    // Commanded torque is u_nle_ + u
    u_command_.noalias() = u_nle_ + u;

    pinocchio_data_->Minv.setZero();
    pinocchio::computeAllTerms(model_, *pinocchio_data_, q, v);
    pinocchio::cholesky::decompose(model_, *pinocchio_data_);
    pinocchio::cholesky::computeMinv(model_, *pinocchio_data_, pinocchio_data_->Minv);

    // du_command_dq_
    a_.noalias() = pinocchio_data_->Minv * u_command_;
    pinocchio::computeRNEADerivatives(model_, *pinocchio_data_, q, v, a_);
    du_command_dq_.noalias() = pinocchio_data_->Minv * pinocchio_data_->dtau_dq;

    // du_nle_dq_
    a_.noalias() = pinocchio_data_->Minv * u_nle_;
    pinocchio::computeRNEADerivatives(model_, *pinocchio_data_, q, v, a_);
    du_nle_dq_.noalias() = pinocchio_data_->Minv * pinocchio_data_->dtau_dq;

    // du_dq_
    fx_.block(num_velocities_, 0, num_velocities_, num_velocities_).noalias() = du_nle_dq_ - du_command_dq_;

    // Since dtau_du=Identity, the partial derivative of fu is directly Minv.
    fu_.bottomRightCorner(num_velocities_, num_velocities_) = pinocchio_data_->Minv;
}

Eigen::MatrixXd PinocchioDynamicsSolverWithGravityCompensation::fx(const StateVector& x, const ControlVector& u)
{
    Eigen::VectorBlock<const Eigen::VectorXd> q = x.head(num_positions_);
    Eigen::VectorBlock<const Eigen::VectorXd> v = x.tail(num_velocities_);

    // Obtain torque to compensate gravity and dynamic effects (Coriolis)
    u_nle_ = pinocchio::nonLinearEffects(model_, *pinocchio_data_, q, v);

    // Commanded torque is u_nle_ + u
    u_command_.noalias() = u_nle_ + u;

    pinocchio_data_->Minv.setZero();
    pinocchio::computeAllTerms(model_, *pinocchio_data_, q, v);
    pinocchio::cholesky::decompose(model_, *pinocchio_data_);
    pinocchio::cholesky::computeMinv(model_, *pinocchio_data_, pinocchio_data_->Minv);

    // du_command_dq_
    a_.noalias() = pinocchio_data_->Minv * u_command_;
    pinocchio::computeRNEADerivatives(model_, *pinocchio_data_, q, v, a_);
    du_command_dq_.noalias() = pinocchio_data_->Minv * pinocchio_data_->dtau_dq;

    // du_nle_dq_
    a_.noalias() = pinocchio_data_->Minv * u_nle_;
    pinocchio::computeRNEADerivatives(model_, *pinocchio_data_, q, v, a_);
    du_nle_dq_.noalias() = pinocchio_data_->Minv * pinocchio_data_->dtau_dq;

    // du_dq_
    fx_.block(num_velocities_, 0, num_velocities_, num_velocities_).noalias() = du_nle_dq_ - du_command_dq_;

    return fx_;
}

Eigen::MatrixXd PinocchioDynamicsSolverWithGravityCompensation::fu(const StateVector& x, const ControlVector& u)
{
    Eigen::VectorBlock<const Eigen::VectorXd> q = x.head(num_positions_);
    Eigen::VectorBlock<const Eigen::VectorXd> v = x.tail(num_velocities_);

    // Obtain torque to compensate gravity and dynamic effects (Coriolis)
    u_nle_ = pinocchio::nonLinearEffects(model_, *pinocchio_data_, q, v);

    // Commanded torque is u_nle_ + u
    u_command_.noalias() = u_nle_ + u;

    // Since dtau_du=Identity, the partial derivative of fu is directly Minv.
    Eigen::Block<Eigen::MatrixXd> Minv = fu_.bottomRightCorner(num_velocities_, num_velocities_);

    pinocchio::computeAllTerms(model_, *pinocchio_data_, q, v);
    pinocchio::cholesky::decompose(model_, *pinocchio_data_);
    pinocchio::cholesky::computeMinv(model_, *pinocchio_data_, Minv);

    return fu_;
}

Eigen::VectorXd PinocchioDynamicsSolverWithGravityCompensation::StateDelta(const StateVector& x_1, const StateVector& x_2)
{
    if (x_1.size() != num_positions_ + num_velocities_ || x_2.size() != num_positions_ + num_velocities_)
    {
        ThrowPretty("x_1 or x_2 do not have correct size, x1=" << x_1.size() << " x2=" << x_2.size() << " expected " << num_positions_ + num_velocities_);
    }

    Eigen::VectorXd dx(2 * num_velocities_);
    pinocchio::difference(model_, x_2.head(num_positions_), x_1.head(num_positions_), dx.head(num_velocities_));
    dx.tail(num_velocities_) = x_1.tail(num_velocities_) - x_2.tail(num_velocities_);
    return dx;
}

Eigen::MatrixXd PinocchioDynamicsSolverWithGravityCompensation::dStateDelta(const StateVector& x_1, const StateVector& x_2, const ArgumentPosition first_or_second)
{
    if (x_1.size() != num_positions_ + num_velocities_ || x_2.size() != num_positions_ + num_velocities_)
    {
        ThrowPretty("x_1 or x_2 do not have correct size, x1=" << x_1.size() << " x2=" << x_2.size() << " expected " << num_positions_ + num_velocities_);
    }

    if (first_or_second != ArgumentPosition::ARG0 && first_or_second != ArgumentPosition::ARG1)
    {
        ThrowPretty("Can only take derivative w.r.t. x_1 or x_2, i.e., ARG0 or ARG1. Provided: " << first_or_second);
    }

    Eigen::MatrixXd J = Eigen::MatrixXd::Identity(2 * num_velocities_, 2 * num_velocities_);

    if (first_or_second == ArgumentPosition::ARG0)
    {
        pinocchio::dDifference(model_, x_2.head(num_positions_), x_1.head(num_positions_), J.topLeftCorner(num_velocities_, num_velocities_), pinocchio::ArgumentPosition::ARG1);
    }
    else
    {
        pinocchio::dDifference(model_, x_2.head(num_positions_), x_1.head(num_positions_), J.topLeftCorner(num_velocities_, num_velocities_), pinocchio::ArgumentPosition::ARG0);
        J.bottomRightCorner(num_velocities_, num_velocities_) *= -1.0;
    }

    return J;
}

void PinocchioDynamicsSolverWithGravityCompensation::Integrate(const StateVector& x, const StateVector& dx, const double dt, StateVector& xout)
{
    Eigen::VectorXd dx_times_dt = dt * dx;
    pinocchio::integrate(model_, x.head(num_positions_), dx_times_dt.head(num_velocities_), xout.head(num_positions_));
    xout.tail(num_velocities_) = x.tail(num_velocities_) + dx_times_dt.tail(num_velocities_);
}

Eigen::VectorXd PinocchioDynamicsSolverWithGravityCompensation::SimulateOneStep(const StateVector& x, const ControlVector& u)
{
    Eigen::VectorXd xout(num_positions_ + num_velocities_);
    Eigen::VectorXd dx = f(x, u);
    Integrate(x, dx, dt_, xout);
    return xout;
}

}  // namespace exotica
