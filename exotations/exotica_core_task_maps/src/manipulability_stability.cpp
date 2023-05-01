//
// Copyright (c) 2018, University of Edinburgh
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

#include <exotica_core_task_maps/manipulability_stability.h>

REGISTER_TASKMAP_TYPE("ManipulabilityStability", exotica::ManipulabilityStability);

namespace exotica
{
void ManipulabilityStability::Update(Eigen::VectorXdRefConst x, Eigen::VectorXdRef phi)
{
    if (phi.rows() != TaskSpaceDim()) ThrowNamed("Wrong size of phi!");

    for (int i = 0; i < n_end_effs_; ++i)
    {
        // const Eigen::MatrixXd& J = kinematics[0].jacobian[i].data.topRows(n_rows_of_jac_);
        // phi(i) = -std::sqrt((J * J.transpose()).determinant());
        // if (debug_) HIGHLIGHT_NAMED("ManipulabilityStability", "phi(i)=" << phi(i));

        const Eigen::MatrixXd& j_right = scene_->GetKinematicTree().Jacobian("hand_right_end_effector_link", KDL::Frame(), "arm_right_1_link", KDL::Frame());
        double manip_right = std::sqrt((j_right * j_right.transpose()).determinant());

        const Eigen::MatrixXd& j_left = scene_->GetKinematicTree().Jacobian("hand_left_end_effector_link", KDL::Frame(), "arm_left_1_link", KDL::Frame());
        double manip_left = std::sqrt((j_left * j_left.transpose()).determinant());

        phi(i) = -std::min(manip_right, manip_left);
        
        if (debug_) HIGHLIGHT_NAMED("ManipulabilityStability", "phi-" << i << ": " <<phi(i));

        // std::cout << " Final Manip: " << final_manip << std::endl;
    }
}

void ManipulabilityStability::Instantiate(const ManipulabilityStabilityInitializer& init)
{
    parameters_ = init;
    n_end_effs_ = frames_.size();

    if (init.OnlyPosition)
    {
        n_rows_of_jac_ = 3;
    }
    else
    {
        n_rows_of_jac_ = 6;
    }
}

int ManipulabilityStability::TaskSpaceDim()
{
    return n_end_effs_;
}
}  // namespace exotica