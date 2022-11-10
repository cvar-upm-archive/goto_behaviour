/*!*******************************************************************************************
 *  \file       goto_base.hpp
 *  \brief      Base class for goto plugins header
 *  \authors    Miguel Fernández Cortizas
 *              Pedro Arias Pérez
 *              David Pérez Saura
 *              Rafael Pérez Seguí
 *
 *  \copyright  Copyright (c) 2022 Universidad Politécnica de Madrid
 *              All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ********************************************************************************/

#ifndef GOTO_BASE_HPP
#define GOTO_BASE_HPP

#include <as2_core/utils/frame_utils.hpp>
#include <as2_core/utils/tf_utils.hpp>
#include "as2_core/names/actions.hpp"
#include "as2_core/names/topics.hpp"
#include "as2_core/node.hpp"

#include <message_filters/subscriber.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <message_filters/time_synchronizer.h>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include "rclcpp_action/rclcpp_action.hpp"

#include <as2_msgs/action/go_to_waypoint.hpp>

#include <Eigen/Dense>
#include "tf2_geometry_msgs/tf2_geometry_msgs.h"

#include "as2_behavior/behavior_server.hpp"

namespace goto_base {
class GotoBase {
public:
  using GoalHandleGoto = rclcpp_action::ServerGoalHandle<as2_msgs::action::GoToWaypoint>;

  void initialize(as2::Node *node_ptr, float max_speed, float goal_threshold) {
    node_ptr_       = node_ptr;
    desired_speed_  = max_speed;
    goal_threshold_ = goal_threshold;

    pose_sub_ = std::make_shared<message_filters::Subscriber<geometry_msgs::msg::PoseStamped>>(
        node_ptr_, as2_names::topics::self_localization::pose,
        as2_names::topics::self_localization::qos.get_rmw_qos_profile());
    twist_sub_ = std::make_shared<message_filters::Subscriber<geometry_msgs::msg::TwistStamped>>(
        node_ptr_, as2_names::topics::self_localization::twist,
        as2_names::topics::self_localization::qos.get_rmw_qos_profile());
    synchronizer_ = std::make_shared<message_filters::Synchronizer<approximate_policy>>(
        approximate_policy(5), *(pose_sub_.get()), *(twist_sub_.get()));
    synchronizer_->registerCallback(&GotoBase::state_callback, this);

    node_ptr_->declare_parameter<std::string>("frame_id_pose", "");
    node_ptr_->get_parameter("frame_id_pose", frame_id_pose_);
    frame_id_pose_ = as2::tf::generateTfName(node_ptr, frame_id_pose_);

    node_ptr_->declare_parameter<std::string>("frame_id_twist", "");
    node_ptr_->get_parameter("frame_id_twist", frame_id_twist_);
    frame_id_twist_ = as2::tf::generateTfName(node_ptr, frame_id_twist_);

    this->ownInit();
  };

  virtual bool on_activate(
      const std::shared_ptr<const as2_msgs::action::GoToWaypoint::Goal> goal) = 0;

  virtual bool on_modify(
      const std::shared_ptr<const as2_msgs::action::GoToWaypoint::Goal> goal) = 0;

  virtual bool on_deactivate(const std::shared_ptr<std::string> &message) = 0;

  virtual bool on_pause(const std::shared_ptr<std::string> &message)  = 0;
  virtual bool on_resume(const std::shared_ptr<std::string> &message) = 0;

  virtual as2_behavior::ExecutionStatus on_run(
      const std::shared_ptr<const as2_msgs::action::GoToWaypoint::Goal> goal,
      std::shared_ptr<as2_msgs::action::GoToWaypoint::Feedback> &feedback_msg,
      std::shared_ptr<as2_msgs::action::GoToWaypoint::Result> &result_ms) = 0;

  virtual void on_excution_end(const as2_behavior::ExecutionStatus &state) = 0;

  virtual ~GotoBase(){};

protected:
  GotoBase(){};

  // To initialize needed publisher for each plugin
  virtual void ownInit(){};

  float getActualYaw() {
    float actual_yaw;
    pose_mutex_.lock();
    actual_yaw = as2::frame::getYawFromQuaternion(actual_q_);
    pose_mutex_.unlock();
    return actual_yaw;
  };

  bool checkGoalCondition() {
    if (distance_measured_) {
      if (fabs(actual_distance_to_goal_) < goal_threshold_) return true;
    }
    return false;
  };

private:
  // TODO: if onExecute is done with timer no atomic attributes needed
  void state_callback(const geometry_msgs::msg::PoseStamped::ConstSharedPtr pose_msg,
                      const geometry_msgs::msg::TwistStamped::ConstSharedPtr twist_msg) {
    pose_mutex_.lock();

    actual_position_ = {pose_msg->pose.position.x, pose_msg->pose.position.y,
                        pose_msg->pose.position.z};

    actual_q_ = {pose_msg->pose.orientation.x, pose_msg->pose.orientation.y,
                 pose_msg->pose.orientation.z, pose_msg->pose.orientation.w};

    this->actual_distance_to_goal_ = (actual_position_ - desired_position_).norm();
    this->actual_speed_ = Eigen::Vector3d(twist_msg->twist.linear.x, twist_msg->twist.linear.y,
                                          twist_msg->twist.linear.z)
                              .norm();
    distance_measured_ = true;
    pose_mutex_.unlock();
  };

protected:
  as2::Node *node_ptr_;
  float goal_threshold_;

  std::mutex pose_mutex_;
  Eigen::Vector3d actual_position_;
  tf2::Quaternion actual_q_;

  std::atomic<bool> distance_measured_;
  std::atomic<float> actual_distance_to_goal_;
  std::atomic<float> actual_speed_;

  Eigen::Vector3d desired_position_;
  float desired_speed_;
  bool ignore_yaw_;

  std::string frame_id_pose_  = "";
  std::string frame_id_twist_ = "";

private:
  std::shared_ptr<message_filters::Subscriber<geometry_msgs::msg::PoseStamped>> pose_sub_;
  std::shared_ptr<message_filters::Subscriber<geometry_msgs::msg::TwistStamped>> twist_sub_;
  typedef message_filters::sync_policies::ApproximateTime<geometry_msgs::msg::PoseStamped,
                                                          geometry_msgs::msg::TwistStamped>
      approximate_policy;
  std::shared_ptr<message_filters::Synchronizer<approximate_policy>> synchronizer_;
};  // GotoBase class

}  // namespace goto_base

#endif  // GOTO_BASE_HPP
