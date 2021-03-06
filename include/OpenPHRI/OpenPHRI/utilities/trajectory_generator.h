/*      File: trajectory_generator.h
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
 * @file trajectory_generator.h
 * @author Benjamin Navarro
 * @brief Definition of the TrajectoryPoint struct and the TrajectoryGenerator class
 * @date April 2017
 * @ingroup OpenPHRI
 */

#pragma once

#include <OpenPHRI/utilities/interpolators_common.h>
#include <OpenPHRI/utilities/object_collection.hpp>
#include <OpenPHRI/utilities/fifth_order_polynomial.h>
#include <vector>

namespace phri {

enum class TrajectoryOutputType {
	Position,
	Velocity,
	Acceleration,
	All
};

enum class TrajectorySynchronization {
	NoSynchronization,
	SynchronizeWaypoints,
	SynchronizeTrajectory
};

template<typename T>
class TrajectoryGenerator {
public:
	TrajectoryGenerator(const TrajectoryPoint<T>& start, double sample_time, TrajectorySynchronization sync = TrajectorySynchronization::NoSynchronization, TrajectoryOutputType output_type = TrajectoryOutputType::All) :
		sample_time_(sample_time),
		output_type_(output_type),
		sync_(sync)
	{
		create(start);
	}

	template<typename U = T>
	void addPathTo(const TrajectoryPoint<T>& to, const T& max_velocity, const T& max_acceleration, typename std::enable_if<std::is_same<U, double>::value>::type* = 0) {
		assert(getComponentCount() == to.size());
		points_.push_back(to);
		SegmentParams params;
		for (size_t i = 0; i < to.size(); ++i) {
			params.max_velocity.push_back(max_velocity);
			params.max_acceleration.push_back(max_acceleration);
		}
		params.minimum_time.resize(1, 0.);
		params.padding_time.resize(1, 0.);
		params.current_time.resize(1, 0.);
		params.isFixedTime = false;
		params.poly_params.resize(to.size());
		segment_params_.push_back(params);
	}

	template<typename U = T>
	void addPathTo(const TrajectoryPoint<T>& to, const T& max_velocity, const T& max_acceleration, typename std::enable_if<not std::is_same<U, double>::value>::type* = 0) {
		assert(getComponentCount() == to.size());
		points_.push_back(to);
		SegmentParams params;
		for (size_t i = 0; i < to.size(); ++i) {
			params.max_velocity.push_back(max_velocity[i]);
			params.max_acceleration.push_back(max_acceleration[i]);
		}
		params.minimum_time.resize(to.size(), 0.);
		params.padding_time.resize(to.size(), 0.);
		params.current_time.resize(to.size(), 0.);
		params.isFixedTime = false;
		params.poly_params.resize(to.size());
		segment_params_.push_back(params);
	}

	void addPathTo(const TrajectoryPoint<T>& to, double duration) {
		assert(getComponentCount() == to.size());
		points_.push_back(to);
		SegmentParams params;
		params.minimum_time.resize(to.size(), duration);
		params.padding_time.resize(to.size(), 0.);
		params.current_time.resize(to.size(), 0.);
		params.isFixedTime = true;
		params.poly_params.resize(to.size());
		segment_params_.push_back(params);
	}

	std::shared_ptr<const T> getPositionOutput() const {
		return position_output_;
	}

	std::shared_ptr<const T> getVelocityOutput() const {
		return velocity_output_;
	}

	std::shared_ptr<const T> getAccelerationOutput() const {
		return acceleration_output_;
	}

	double getCurrentSegmentMinimumTime(size_t component) {
		return segment_params_[current_segement_.at(component)].minimum_time.at(component);
	}

	double getTrajectoryMinimumTime(size_t starting_segment = 0) {
		double total_minimum_time = 0;
		for (size_t segment = starting_segment; segment < getSegmentCount(); ++segment) {
			total_minimum_time += *std::max_element(segment_params_[segment].minimum_time.begin(), segment_params_[segment].minimum_time.end());
		}
		return total_minimum_time;
	}

	double getComponentMinimumTime(size_t component, size_t starting_segment = 0) const {
		double min_time = 0.;
		for (size_t segment = starting_segment; segment < getSegmentCount(); ++segment) {
			min_time += segment_params_[segment].minimum_time[component];
		}
		return min_time;
	}

	double getTrajectoryDuration() const {
		double total = 0.;
		for (size_t i = 0; i < getSegmentCount(); ++i) {
			total += getSegmentDuration(i);
		}
		return total;
	}

	double getSegmentMinimumTime(size_t segment, size_t component) const {
		if(segment < getSegmentCount()) {
			return segment_params_[segment].minimum_time[component];
		}
		else {
			return 0.;
		}
	}

	double getSegmentDuration(size_t segment, size_t component) const {
		return segment_params_[segment].minimum_time[component] + segment_params_[segment].padding_time[component];
	}

	double getSegmentDuration(size_t segment) const {
		double max = 0.;
		for (size_t component = 0; component < getComponentCount(); ++component) {
			max = std::max(max, getSegmentDuration(segment, component));
		}
		return max;
	}

	size_t getSegmentCount() const {
		return points_.size()-1;
	}

	size_t getComponentCount() const {
		return points_[0].size();
	}

	void setPaddingTime(size_t segment, size_t component, double time) {
		segment_params_[segment].padding_time[component] = time;
	}

	void setCurrentTime(size_t segment, size_t component, double time) {
		segment_params_[segment].current_time[component] = time;
	}

	bool computeParameters() {
		int segments = getSegmentCount();
		if(segments < 1) {
			return false;
		}

		for (size_t segment = 0; segment < segments; ++segment) {
			TrajectoryPoint<T>& from = points_[segment];
			TrajectoryPoint<T>& to = points_[segment+1];

			auto& params = segment_params_[segment];

			for (size_t component = 0; component < from.size(); ++component) {
				auto& poly_params = params.poly_params[component];
				poly_params = {0, params.minimum_time[component] + params.padding_time[component], from.yrefs_[component], to.yrefs_[component], from.dyrefs_[component], to.dyrefs_[component], from.d2yrefs_[component], to.d2yrefs_[component]};
				FifthOrderPolynomial::computeParameters(poly_params);
				params.current_time[component] = 0.;
			}
		}

		*position_output_ = *points_[0].y;
		*velocity_output_ = *points_[0].dy;
		*acceleration_output_ = *points_[0].d2y;

		return true;
	}

	virtual bool computeTimings(double v_eps = 1e-6, double a_eps = 1e-6) {
		if(getSegmentCount() < 1) {
			return false;
		}

		bool ret = true;

		#pragma omp parallel for
		for (size_t segment = 0; segment < getSegmentCount(); ++segment) {
			auto& from = points_[segment];
			auto& to = points_[segment+1];
			auto& params = segment_params_[segment];
			for (size_t component = 0; component < getComponentCount(); ++component) {
				ret &= computeTimings(from, to, params, segment, component, v_eps, a_eps);
			}
		}

		computePaddingTime();
		computeParameters();

		return ret;
	}

	virtual bool compute() {
		auto check_error =
			[this](size_t component) -> bool {
				return std::abs(position_output_refs_[component] - error_tracking_params_.reference_refs[component]) > error_tracking_params_.threshold_refs[component];
			};

		if(error_tracking_params_) {
			for (size_t component = 0; component < getComponentCount(); ++component) {
				if(check_error(component)) {
					// If we have no synchronization we stop only this component, otherwise we stop all of them
					if(sync_ == TrajectorySynchronization::NoSynchronization) {
						error_tracking_params_.state[component] = ErrorTrackingState::Paused;
						double& dy = velocity_output_refs_[component];
						double& d2y = acceleration_output_refs_[component];
						dy = 0.;
						d2y = 0.;
					}
					else {
						for_each(error_tracking_params_.state.begin(), error_tracking_params_.state.end(), [](ErrorTrackingState state) {state = ErrorTrackingState::Paused;});
						for(double& dy: velocity_output_refs_) {
							dy = 0.;
						}
						for(double& d2y: acceleration_output_refs_) {
							d2y = 0.;
						}
						return false;
					}
				}
			}
		}

		bool all_ok = true;
		for (size_t component = 0; component < getComponentCount(); ++component) {
			auto& current_component_segment = current_segement_[component];
			if(current_component_segment < getSegmentCount()) {
				all_ok = false;
			}
			else {
				continue;
			}

			auto& params = segment_params_[current_component_segment];
			double& current_time = params.current_time[component];

			if (error_tracking_params_.state[component] == ErrorTrackingState::Paused) {
				// Recompute a polynomial with zero initial velocity and acceleration starting from the current positon
				auto recompute_from_here =
					[this](size_t component) {
						auto& params = segment_params_[current_segement_[component]];
						FifthOrderPolynomial::Parameters& poly_params = params.poly_params[component];

						poly_params.yi = error_tracking_params_.reference_refs[component];
						poly_params.dyi = 0.;
						poly_params.d2yi = 0.;

						if(params.isFixedTime) {
							poly_params.xf = params.minimum_time[component];
							FifthOrderPolynomial::computeParameters(poly_params);
						}
						else {
							FifthOrderPolynomial::computeParametersWithConstraints(poly_params, params.max_velocity[component], params.max_acceleration[component], 1e-6, 1e-6);
							params.minimum_time[component] = poly_params.xf;
						}
						params.current_time[component] = 0.;
					};

				if(sync_ == TrajectorySynchronization::NoSynchronization) {
					// If we're still to far, skip to the next component, otherwise resume the generation and recompte from current position is required
					if(check_error(component)) {
						continue;
					}
					else {
						error_tracking_params_.state[component] = ErrorTrackingState::Running;
						if(error_tracking_params_.recompute_when_resumed) {
							recompute_from_here(component);
						}
					}
				}
				else if(error_tracking_params_.recompute_when_resumed) {
					// If we're here it means that all components have returned to the expected output and that trajectories need to be recomputed from the current position
					for (size_t idx = 0; idx < getComponentCount(); ++idx) {
						error_tracking_params_.state[idx] = ErrorTrackingState::Running;
						recompute_from_here(idx);
					}
					// Update the padding times to respect the imposed synchronization
					computePaddingTime();
					for (size_t seg = 0; seg < getSegmentCount(); ++seg) {
						auto& params = segment_params_[seg];
						for (size_t idx = 0; idx < getComponentCount(); ++idx) {
							auto& poly_params = params.poly_params[idx];
							// Update the polynomial length and recompute it
							poly_params.xf = params.minimum_time[idx] + params.padding_time[idx];
							FifthOrderPolynomial::computeParameters(poly_params);
						}
					}
				}
			}

			double dt = getSegmentDuration(current_component_segment, component) - current_time;
			if(dt < 0.) {
				++current_component_segment;
				if(current_component_segment < getSegmentCount()) {
					setCurrentTime(current_component_segment, component, -dt);
				}
			}

			switch(output_type_) {
			case TrajectoryOutputType::Position:
			{
				double& out = position_output_refs_[component];
				out = FifthOrderPolynomial::compute(current_time, params.poly_params[component]);
			}
			break;
			case TrajectoryOutputType::Velocity:
			{
				double& out = velocity_output_refs_[component];
				out = FifthOrderPolynomial::computeFirstDerivative(current_time, params.poly_params[component]);
			}
			break;
			case TrajectoryOutputType::Acceleration:
			{
				double& out = acceleration_output_refs_[component];
				out = FifthOrderPolynomial::computeSecondDerivative(current_time, params.poly_params[component]);
			}
			break;
			case TrajectoryOutputType::All:
			{
				double& pos_out = position_output_refs_[component];
				double& vel_out = velocity_output_refs_[component];
				double& acc_out = acceleration_output_refs_[component];
				pos_out = FifthOrderPolynomial::compute(current_time, params.poly_params[component]);
				vel_out = FifthOrderPolynomial::computeFirstDerivative(current_time, params.poly_params[component]);
				acc_out = FifthOrderPolynomial::computeSecondDerivative(current_time, params.poly_params[component]);
			}
			break;
			}

			current_time += sample_time_;
		}
		return all_ok;
	}

	auto operator()() {
		return compute();
	}

	const TrajectoryPoint<T>& operator [](size_t point) const {
		return points_.at(point);
	}

	TrajectoryPoint<T>& operator [](size_t point) {
		return points_.at(point);
	}

	void setSynchronizationMethod(TrajectorySynchronization sync) {
		sync_ = sync;
	}
	template<typename U = T>
	void enableErrorTracking(const std::shared_ptr<const T>& reference, const T& threshold, bool recompute_when_resumed, typename std::enable_if<std::is_same<U, double>::value>::type* = 0) {
		error_tracking_params_.reference = reference;
		error_tracking_params_.threshold = threshold;
		error_tracking_params_.recompute_when_resumed = recompute_when_resumed;
		error_tracking_params_.reference_refs.push_back(std::cref(*reference));
		error_tracking_params_.threshold_refs.push_back(std::cref(error_tracking_params_.threshold));
	}

	template<typename U = T>
	void enableErrorTracking(const std::shared_ptr<const T>& reference, const T& threshold, bool recompute_when_resumed, typename std::enable_if<not std::is_same<U, double>::value>::type* = 0) {
		error_tracking_params_.reference = reference;
		error_tracking_params_.threshold = threshold;
		error_tracking_params_.recompute_when_resumed = recompute_when_resumed;
		for (size_t i = 0; i < reference->size(); ++i) {
			error_tracking_params_.reference_refs.push_back(std::cref((*error_tracking_params_.reference)[i]));
			error_tracking_params_.threshold_refs.push_back(std::cref(error_tracking_params_.threshold[i]));
		}
	}

	void enableErrorTracking(const T* reference, const T& threshold, bool recompute_when_resumed) {
		enableErrorTracking(std::shared_ptr<const T>(reference, [](const T* p){}), threshold, recompute_when_resumed);
	}

	void disableErrorTracking() {
		error_tracking_params_.reference.reset();
		error_tracking_params_.state.resize(getComponentCount());
		for_each(error_tracking_params_.state.begin(), error_tracking_params_.state.end(), [](ErrorTrackingState state){state = ErrorTrackingState::Running;});
	}

	void reset() {
		for (size_t component = 0; component < getComponentCount(); ++component) {
			current_segement_[component] = 0;
			for(auto& param: segment_params_) {
				param.current_time[component] = 0.;
			}
		}
	}

	virtual void removeAllPoints() {
		for(auto& segment: current_segement_) {
			segment = 0;
		}
		points_.resize(1);
		segment_params_.clear();
	}

	static size_t getComputeTimingsIterations() {
		return FifthOrderPolynomial::compute_timings_total_iter;
	}

	static void resetComputeTimingsIterations() {
		FifthOrderPolynomial::compute_timings_total_iter = 0;
	}

protected:
	struct SegmentParams {
		std::vector<double> max_velocity;
		std::vector<double> max_acceleration;
		bool isFixedTime;
		std::vector<double> minimum_time;
		std::vector<double> current_time;
		std::vector<double> padding_time;
		std::vector<FifthOrderPolynomial::Parameters> poly_params;
	};

	enum class ErrorTrackingState {
		Running,
		Paused
	};

	struct ErrorTracking {
		operator bool() const {
			return static_cast<bool>(reference);
		}

		std::shared_ptr<const T> reference;
		T threshold;
		std::vector<ErrorTrackingState> state;
		bool recompute_when_resumed;
		std::vector<std::reference_wrapper<const double>> reference_refs;
		std::vector<std::reference_wrapper<const double>> threshold_refs;
	};

	TrajectoryGenerator(double sample_time, TrajectorySynchronization sync = TrajectorySynchronization::NoSynchronization, TrajectoryOutputType output_type = TrajectoryOutputType::All) :
		sample_time_(sample_time),
		output_type_(output_type),
		sync_(sync)
	{
	}

	void create(const TrajectoryPoint<T>& start) {
		current_segement_.resize(start.size(), 0);
		points_.push_back(start);
		position_output_ = std::make_shared<T>();
		velocity_output_ = std::make_shared<T>();
		acceleration_output_ = std::make_shared<T>();
		createRefs();
		disableErrorTracking();
	}

	template<typename U = T>
	void createRefs(typename std::enable_if<std::is_same<U, double>::value>::type* = 0) {
		position_output_refs_.clear();
		velocity_output_refs_.clear();
		acceleration_output_refs_.clear();
		position_output_refs_.push_back(std::ref(*position_output_));
		velocity_output_refs_.push_back(std::ref(*velocity_output_));
		acceleration_output_refs_.push_back(std::ref(*acceleration_output_));
	}

	template<typename U = T>
	void createRefs(typename std::enable_if<not std::is_same<U, double>::value>::type* = 0) {
		position_output_refs_.clear();
		velocity_output_refs_.clear();
		acceleration_output_refs_.clear();
		auto& position_vec = *position_output_;
		auto& velocity_vec = *velocity_output_;
		auto& acceleration_vec = *acceleration_output_;
		position_vec.resize(current_segement_.size());
		velocity_vec.resize(current_segement_.size());
		acceleration_vec.resize(current_segement_.size());
		for (size_t i = 0; i < static_cast<size_t>(position_vec.size()); ++i) {
			position_output_refs_.push_back(std::ref(position_vec[i]));
			velocity_output_refs_.push_back(std::ref(velocity_vec[i]));
			acceleration_output_refs_.push_back(std::ref(acceleration_vec[i]));
		}
	}

	static bool computeTimings(const TrajectoryPoint<T>& from, const TrajectoryPoint<T>& to, SegmentParams& params, size_t segment, size_t component, double v_eps, double a_eps) {
		bool ok = true;
		if(params.isFixedTime) {
			params.poly_params[component] = FifthOrderPolynomial::Constraints {0, params.minimum_time[component], from.yrefs_[component], to.yrefs_[component], from.dyrefs_[component], to.dyrefs_[component], from.d2yrefs_[component], to.d2yrefs_[component]};
		}
		else {
			auto error_msg =
				[&ok, segment, component] (const std::string& where, const std::string& what) {
					throw std::runtime_error(OPEN_PHRI_ERROR(where + " " + what + " for segment " + std::to_string(segment+1) + ", component " + std::to_string(component+1) + " is higher than the maximum"));
				};

			FifthOrderPolynomial::Parameters poly_params = FifthOrderPolynomial::Constraints {0, 1., from.yrefs_[component], to.yrefs_[component], from.dyrefs_[component], to.dyrefs_[component], from.d2yrefs_[component], to.d2yrefs_[component]};
			// std::cout << "Segment " << segment+1 << ", component " << component+1 << "\n";
			// std::cout << "\tfrom (" << from.yrefs_[component] << "," << from.dyrefs_[component] << "," << from.d2yrefs_[component] << ")\n";
			// std::cout << "\tto (" << to.yrefs_[component] << "," << to.dyrefs_[component] << "," << to.d2yrefs_[component] << ")\n";
			auto error = FifthOrderPolynomial::computeParametersWithConstraints(poly_params, params.max_velocity[component], params.max_acceleration[component], v_eps, a_eps);
			switch(error) {
			case FifthOrderPolynomial::ConstraintError::InitialVelocity:
				error_msg("initial", "velocity");
				ok = false;
				break;
			case FifthOrderPolynomial::ConstraintError::FinalVelocity:
				error_msg("final", "velocity");
				ok = false;
				break;
			case FifthOrderPolynomial::ConstraintError::InitialAcceleration:
				error_msg("initial", "acceleration");
				ok = false;
				break;
			case FifthOrderPolynomial::ConstraintError::FinalAcceleration:
				error_msg("final", "acceleration");
				ok = false;
				break;
			default:
				break;
			}

			if(ok) {
				params.poly_params[component] = poly_params;
				params.minimum_time[component] = params.poly_params[component].xf;
			}
		}

		return ok;
	}

	void computePaddingTime(size_t component) {
		size_t starting_segment = current_segement_[component];
		if(sync_ == TrajectorySynchronization::SynchronizeWaypoints) {
			for (size_t segment = starting_segment; segment < getSegmentCount(); ++segment) {
				double max_time = 0.;
				for (size_t component_idx = 0; component_idx < getComponentCount(); ++component_idx) {
					max_time = std::max(max_time, getSegmentMinimumTime(segment, component_idx));
				}
				setPaddingTime(segment, component, max_time - getSegmentMinimumTime(segment, component));
			}
		}
		else if(sync_ == TrajectorySynchronization::SynchronizeTrajectory) {
			double padding = (getTrajectoryMinimumTime(starting_segment) - getComponentMinimumTime(component, starting_segment))/double(getSegmentCount()-starting_segment);
			for (size_t segment = starting_segment; segment < getSegmentCount(); ++segment) {
				setPaddingTime(segment, component, padding);
			}
		}
		else {
			for (size_t segment = starting_segment; segment < getSegmentCount(); ++segment) {
				setPaddingTime(segment, component, 0.);
			}
		}
	}

	void computePaddingTime() {
		for (size_t component = 0; component < getComponentCount(); ++component) {
			computePaddingTime(component);
		}
	}


	std::vector<TrajectoryPoint<T>> points_;
	std::vector<SegmentParams> segment_params_;
	std::vector<size_t> current_segement_;
	ErrorTracking error_tracking_params_;
	double sample_time_;
	std::shared_ptr<T> position_output_;
	std::shared_ptr<T> velocity_output_;
	std::shared_ptr<T> acceleration_output_;
	std::vector<std::reference_wrapper<double>> position_output_refs_;
	std::vector<std::reference_wrapper<double>> velocity_output_refs_;
	std::vector<std::reference_wrapper<double>> acceleration_output_refs_;
	TrajectoryOutputType output_type_;
	TrajectorySynchronization sync_;
};

template<typename T>
using TrajectoryGeneratorPtr = std::shared_ptr<TrajectoryGenerator<T>>;
template<typename T>
using TrajectoryGeneratorConstPtr = std::shared_ptr<const TrajectoryGenerator<T>>;

extern template class TrajectoryGenerator<double>;
extern template class TrajectoryGenerator<VectorXd>;
extern template class TrajectoryGenerator<Vector6d>;

} // namespace phri
