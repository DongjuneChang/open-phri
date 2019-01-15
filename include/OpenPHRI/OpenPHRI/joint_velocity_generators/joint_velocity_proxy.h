/*      File: joint_velocity_proxy.h
*       This file is part of the program open-phri
*       Program description : OpenPHRI: a generic framework to easily and safely control robots in interactions with humans
*       Copyright (C) 2017 -  Benjamin Navarro (LIRMM). All Right reserved.
*
*       This software is free software: you can redistribute it and/or modify
*       it under the terms of the LGPL license as published by
*       the Free Software Foundation, either version 3
*       of the License, or (at your option) any later version.
*       This software is distributed in the hope that it will be useful,
*       but WITHOUT ANY WARRANTY without even the implied warranty of
*       MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*       LGPL License for more details.
*
*       You should have received a copy of the GNU Lesser General Public License version 3 and the
*       General Public License version 3 along with this program.
*       If not, see <http://www.gnu.org/licenses/>.
*/


/**
 * @file joint_velocity_proxy.h
 * @author Benjamin Navarro
 * @brief Definition of the JointVelocityProxy class
 * @date April 2017
 * @ingroup OpenPHRI
 */

#pragma once

#include <OpenPHRI/joint_velocity_generators/joint_velocity_generator.h>
#include <OpenPHRI/definitions.h>

namespace phri {

/** @brief Generates a joint_velocity based on an externally managed one.
 *  @details Can be useful to add a joint_velocity generated by an external library.
 */
class JointVelocityProxy : public JointVelocityGenerator {
public:
	explicit JointVelocityProxy(VectorXdConstPtr joint_velocity);
	explicit JointVelocityProxy(
		VectorXdConstPtr joint_velocity,
		const std::function<void(void)>& update_func);

	virtual ~JointVelocityProxy() = default;

protected:
	virtual void update(VectorXd& velocity) override;

	VectorXdConstPtr joint_velocity_;
	std::function<void(void)> update_func_;
};

using JointVelocityProxyPtr = std::shared_ptr<JointVelocityProxy>;
using JointVelocityProxyConstPtr = std::shared_ptr<const JointVelocityProxy>;

} // namespace phri
