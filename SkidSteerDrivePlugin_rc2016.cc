/*
 * Copyright (C) 2012-2015 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <string>

#include "gazebo/physics/physics.hh"
#include "gazebo/transport/transport.hh"
#include "SkidSteerDrivePlugin_rc2016.hh"
#include "encoder_msgs.hh"

using namespace gazebo;
GZ_REGISTER_MODEL_PLUGIN(SkidSteerDrivePlugin)

/////////////////////////////////////////////////
SkidSteerDrivePlugin::SkidSteerDrivePlugin()
{
  this->maxForce = 5.0;
  this->wheelRadius = 0.0;
  this->wheelSeparation = 0.0;
  this->vel_lin = this->vel_rot = 0.0;
  this->Enc_Counter =0;
}

/////////////////////////////////////////////////
int SkidSteerDrivePlugin::RegisterJoint(int _index, const std::string &_name)
{
  // Bounds checking on index
  if (_index < 0 or _index >= NUMBER_OF_WHEELS)
  {
    gzerr << "Joint index " << _index <<  " out of bounds [0, "
          << NUMBER_OF_WHEELS << "] in model " << this->model->GetName()
          << "." << std::endl;
    return 1;
  }

  // Find the specified joint and add it to out list
  this->joints[_index] = this->model->GetJoint(_name);
  if (!this->joints[_index])
  {
    gzerr << "Unable to find the " << _name
          <<  " joint in model " << this->model->GetName() << "." << std::endl;
    return 1;
  }

  // Success!
  return 0;
}

/////////////////////////////////////////////////
void SkidSteerDrivePlugin::Load(physics::ModelPtr _model,
                                sdf::ElementPtr   _sdf)
{
  this->model = _model;

  this->node = transport::NodePtr(new transport::Node());
  this->node->Init(this->model->GetWorld()->GetName());

  int err = 0;

  err += RegisterJoint(RIGHT_FRONT, "right_front");
  err += RegisterJoint(RIGHT_REAR,  "right_rear");
  err += RegisterJoint(LEFT_FRONT,  "left_front");
  err += RegisterJoint(LEFT_REAR,   "left_rear");

  if (err > 0)
    return;

  if (_sdf->HasElement("max_force"))
  {
    this->maxForce = _sdf->GetElement("max_force")->Get<double>();
    gzwarn << "The MaxForce API is deprecated in Gazebo, "
           << "and the max_force tag is no longer used in this plugin."
           << std::endl;
  }
  else
    gzwarn << "No MaxForce value set in the model sdf, default value is 5.0.\n";

  // This assumes that front and rear wheel spacing is identical
  this->wheelSeparation = this->joints[RIGHT_FRONT]->GetAnchor(0).Distance(
                          this->joints[LEFT_FRONT]->GetAnchor(0));

  // This assumes that the largest dimension of the wheel is the diameter
  // and that all wheels have the same diameter
  physics::EntityPtr wheelLink = boost::dynamic_pointer_cast<physics::Entity>(
                                        this->joints[RIGHT_FRONT]->GetChild() );
  if(wheelLink)
  {
    math::Box bb = wheelLink->GetBoundingBox();
    this->wheelRadius = bb.GetSize().GetMax() * 0.5;
  }

  // Validity checks...
  if(this->wheelSeparation <= 0)
  {
    gzerr << "Unable to find the wheel separation distance." << std::endl
          << "  This could mean that the right_front link and the left_front "
          << "link are overlapping." << std::endl;
    return;
  }
  if(this->wheelRadius <= 0)
  {
    gzerr << "Unable to find the wheel radius." << std::endl
          << "  This could mean that the sdf is missing a wheel link on "
          << "the right_front joint." << std::endl;
    return;
  }

  this->velSub = this->node->Subscribe(
    std::string("~/") + this->model->GetName() + std::string("/vel_cmd"),
    &SkidSteerDrivePlugin::OnVelMsg, this);
  this->EncPub = this->node->Advertise<encoder_msgs::msgs::Encoder>(
    std::string("~/") + this->model->GetName() + std::string("/Encoders"));
//  EncPub->WaitForConnection();

  this->updateConnection = event::Events::ConnectWorldUpdateBegin(
          boost::bind(&SkidSteerDrivePlugin::OnUpdate, this));
}


/////////////////////////////////////////////////
void SkidSteerDrivePlugin::OnVelMsg(ConstPosePtr &_msg)
{
  // gzmsg << "cmd_vel: " << msg->position().x() << ", "
  //       << msgs::Convert(msg->orientation()).GetAsEuler().z << std::endl;

  vel_lin = _msg->position().x() / this->wheelRadius;
#if(GAZEBO_MAJOR_VERSION == 5)
  vel_rot = -1 * msgs::Convert(_msg->orientation()).GetAsEuler().z
               * (this->wheelSeparation / this->wheelRadius);
#endif
#if(GAZEBO_MAJOR_VERSION == 7)
  vel_rot = -1 * msgs::ConvertIgn(_msg->orientation()).Euler().Z()
               * (this->wheelSeparation / this->wheelRadius);
#endif
}

/////////////////////////////////////////////////
void SkidSteerDrivePlugin::OnUpdate()
{
  encoder_msgs::msgs::Encoder  encmsg;
  this->joints[RIGHT_FRONT]->SetVelocity(0, vel_lin - vel_rot);
  this->joints[RIGHT_REAR ]->SetVelocity(0, vel_lin - vel_rot);
  this->joints[LEFT_FRONT ]->SetVelocity(0, vel_lin + vel_rot);
  this->joints[LEFT_REAR  ]->SetVelocity(0, vel_lin + vel_rot);
//printf("Enc:%f , %f \n", this->joints[RIGHT_FRONT]->GetAngle(0).Radian(), this->joints[LEFT_FRONT]->GetAngle(0).Radian());
  if(0 == Enc_Counter)
  {
    Enc_Counter = 100;
    //enc.set_has_right(true);
    //enc.set_has_left(true);
    encmsg.set_right(this->joints[RIGHT_FRONT]->GetAngle(0).Radian());
    encmsg.set_left(this->joints[LEFT_FRONT]->GetAngle(0).Radian());
    EncPub->Publish(encmsg);
  }
  else
    Enc_Counter--;
}
