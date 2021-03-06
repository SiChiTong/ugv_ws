#include "libMissionControl.h"

MissionControl::MissionControl():pn("~") {
  pn.param<string>("config_folder", config_folder_, "");
  cout << "config_folder :" << config_folder_ << endl;
  n.param<string>("robot_id", robot_id_, "robot_99");
  n.param<string>("community_id", community_id_, "2");

  joy_sub = n.subscribe("joy",10, &MissionControl::JoyCallback,this);
  goal_sub = n.subscribe("goal",10, &MissionControl::GoalCallback,this);
  obstacle_sub = n.subscribe("map_obs_points",10, &MissionControl::ObstacleCallback,this); 
  step_sub = n.subscribe("button",10, &MissionControl::StepNumberCallback,this);
  cmd_sub = n.subscribe("virtual_joystick/cmd_vel",10, &MissionControl::CmdCallback,this);
  control_sub = n.subscribe("control_string",10, &MissionControl::ControlCallback,this);
  map_number_sub = n.subscribe("map_number",1, &MissionControl::MapNumberCallback,this);

  scan_sub = n.subscribe("/lidar_scan",1, &MissionControl::ScanCallback,this);
  remote_control_sub = n.subscribe("/keyboard_control",1, &MissionControl::RemoteControlCallback,this);

  cmd_vel_pub = n.advertise<geometry_msgs::Twist> ("husky_velocity_controller/cmd_vel", 1);
  path_pred_pub = n.advertise<sensor_msgs::PointCloud>("pred_path",1);

  local_goal_pub = n.advertise<sensor_msgs::PointCloud> ("local_goal", 1);
  global_goal_pub = n.advertise<sensor_msgs::PointCloud> ("global_goal", 1);

  local_all_pub = n.advertise<sensor_msgs::PointCloud>("local_all",1);
  local_safe_pub = n.advertise<sensor_msgs::PointCloud>("local_safe",1);
  local_best_pub = n.advertise<sensor_msgs::PointCloud>("local_best",1);
  local_costmap_pub = n.advertise<nav_msgs::OccupancyGrid>("local_costmap",1);

  station_points_pub = n.advertise<sensor_msgs::PointCloud>("station_points",1);
  global_path_pub = n.advertise<sensor_msgs::PointCloud>("global_path",1);
  vehicle_model_pub = n.advertise<visualization_msgs::Marker>("vehicle_model",1);
  vehicle_info_pub = n.advertise<visualization_msgs::Marker>("vehicle_info",1);
  traceback_pub = n.advertise<sensor_msgs::PointCloud>("trace_back",1);
  auto_supervise_state_pub = n.advertise<mission_control::AutoSuperviseState>("auto_supervise_state",1);
  
  RRT_pointcloud_pub = n.advertise<sensor_msgs::PointCloud>("/RRT_pointcloud",1);
  Astar_pointcloud_pub = n.advertise<sensor_msgs::PointCloud>("/Astar_pointcloud",1);
  vehicle_run_state_pub = n.advertise<std_msgs::String>("/vehicle_run_state",1);
  gate_command_pub = n.advertise<std_msgs::String>("/gate_command", 1);
  
  reset_sub = n.subscribe("/reset_control",1,&MissionControl::ResetCallback,this);
  action_sub = n.subscribe("action_index",1,&MissionControl::ActionCallback,this);
  planner_manual_sub = n.subscribe("/planner_manual_set",1,&MissionControl::PlannerManualCallback,this);

  action_state_pub = n.advertise<std_msgs::Int32>("action_state",1);
  plan_str_pub = n.advertise<std_msgs::String>("from_robot_path",1);

}
MissionControl::~MissionControl(){
  cout << "Navigation Node For Closed ~~~" << endl;
}


bool MissionControl::Initialization() {
  if(!ReadConfig()) {
    max_linear_velocity_ = 1.5;
    max_rotation_velocity_ = 1.5;
    max_linear_acceleration_ = 1;
    max_rotation_acceleration_ = 1;

    max_translational_velocity_ = 2;
    min_translational_velocity_ = 0.1;

    controller_linear_scale_ = 1;
    controller_rotation_scale_ = 1;

    Astar_local_map_window_radius_ 		= 30;
    Astar_local_costmap_resolution_ 	= 0.5;
    RRT_iteration_ 						        = 5000;
		RRT_extend_step_ 					        = 0.2;
		RRT_search_plan_num_ 				      = 1;
		RRT_expand_size_ 					        = 2;
		spline_expand_size_ 					        = 2;

    limit_danger_radius_     = 0.5;
    min_danger_longth_       = 0.35;
    min_danger_width_        = 0.25;
    limit_route_radius_      = 5;
    limit_predict_radius_    = 10;
    buffer_radius_           = 2;
    back_safe_radius_        = 1;
    front_safe_radius_       = 2;
    revolute_safe_radius_    = 0.5;
    danger_assist_radius_    = 1;

    spline_search_grid_     = 3;
    RRT_costmap_resolution_ = 0.1;
    spline_costmap_resolution_ = 0.2;
    spline_map_window_radius_  = 4;
    RRT_map_window_radius_     = 8;

    vehicle_radius_ = 0.3;
  }

  isWIFIControl_ = false;
  isJoyControl_  = false;
  isLTEControl_  = false;
  isLTELock_ = false;

  isAction_ = false;
  isLocationUpdate_ = false;
  isReachCurrentGoal_ = false;
  isAdjustGuardPose_ = false;

  isJOYscram_ = false;
  planner_manual_state_ = false;
  map_number_ = 1;

  MyController_.set_speed_scale(controller_linear_scale_);
  MyController_.set_rotation_scale(controller_rotation_scale_);
  MyExecuter_.SetMaxLinearVel(max_linear_velocity_);
  MyController_.setMaxLinearAcceleration(max_linear_acceleration_);
  
  global_path_pointcloud_.points.clear();

  narrow_command_stage_ = 0;
  lookahead_RRT_scale_ = 0.9;
  auto_state_global_ = 0;
  relocation_stage_ = 0;
  temp_max_linear_acceleration_ = max_linear_acceleration_;

  RRT_refind_ = false;
  isRemote_ = false;

  initLift();
  return(true);

  isRemoteFront_ = false; 
  isRemoteBack_ = false; 
  isRemoteLeft_ = true; 
  isRemoteRight_ = false; 
  isRemoteFrontLeft_ = false; 
  isRemoteFrontRight_ = false; 
  isRemoteBackLeft_ = false; 
  isRemoteBackRight_ = false; 
}


void MissionControl::Execute() {
  if(!Initialization()) return;
  PrintConfig();
  
  ros::Rate loop_rate(ROS_RATE_HZ);

  ros::Time planner_timer = ros::Time::now();
  double planner_duration = 0.1;

  cout << "Navigation Node Started" << endl;

  while (ros::ok()) {
    loop_rate.sleep();
    ros::spinOnce();

    auto mission_state = DecisionMaker();

    readVehicleState();

    if(mission_state == static_cast<int>(AutoState::JOY)) {
      vehicle_run_state_1st_.data = "step1: mission_state was JOY.";
      ClearAutoMissionState();
      ApplyJoyControl(mission_state);
      continue;
    }
    else if(mission_state == static_cast<int>(AutoState::LTE)) {
      vehicle_run_state_1st_.data = "step1:> mission_state was LTE.";
      ApplyLTEControl(mission_state);
      continue;
    }
    else if(mission_state == static_cast<int>(AutoState::WIFI)) {
      vehicle_run_state_1st_.data = "step1: mission_state was WIFI.";
      ApplyWIFIControl(mission_state);
      continue;      
    }
    else if(mission_state == static_cast<int>(AutoState::AUTO)) {
      vehicle_run_state_1st_.data = "step1: mission_state was AUTO, " + string("global path pointcloud size was ") + to_string(global_path_pointcloud_.points.size());
      ApplyAutoControl(mission_state);
      continue;
    }
    else if(mission_state == static_cast<int>(AutoState::SLOW)) {
      vehicle_run_state_1st_.data = "step1: mission_state was SLOW.";
      ApplyAutoControl(mission_state);//ApplySlowControl();
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::STOP)) {
      vehicle_run_state_1st_.data = "step1: mission_state was STOP.";
      ApplyStopControl(mission_state);
      continue; 
    }    
    else if(mission_state == static_cast<int>(AutoState::BACK)) {
      vehicle_run_state_1st_.data = "step1: mission_state was BACK.";
      ApplyAutoControl(mission_state); //ApplyBackwardControl();
      continue;
    }
    else if(mission_state == static_cast<int>(AutoState::NARROW)) {
      vehicle_run_state_1st_.data = "step1: mission_state was NARROW.";
      ApplyNarrowControl(mission_state);
      continue; 

    } 
    else if(mission_state == static_cast<int>(AutoState::ACTION)) {
      vehicle_run_state_1st_.data = "step1: mission_state was ACTION.";
      ApplyAction(mission_state);
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::RELOCATION)) {
      vehicle_run_state_1st_.data = "step1: mission_state was RELOCATION.";
      ApplyRelocationControl(mission_state);
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::NOMAP)) {
      vehicle_run_state_1st_.data = "step1: mission_state was NOMAP.";
      ApplyNomapControl(mission_state);
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::WAIT)) {
      vehicle_run_state_1st_.data = "step1: mission_state was WAIT.";
      ApplyWaitControl(mission_state);
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::ROTATION)) {
      vehicle_run_state_1st_.data = "step1: mission_state was ROTATION.";
      ApplyRotationControl(mission_state);
      continue; 
    }
    else if(mission_state == static_cast<int>(AutoState::REMOTE)) {
      vehicle_run_state_1st_.data = "step1: mission_state was REMOTE.";
      ApplyRemoteControl(mission_state);
      continue; 

    }   
    else {
      cout << "Unknown Mission State" << endl;
      continue;
    }
  }
  cout << "Navigation Node For Closed" << endl;

}


// ABCD EFGH IJKL MNOP QRST UVWX YZ
int MissionControl::DecisionMaker() {
  if(isJoyControl_) return static_cast<int>(AutoState::JOY);
  if(planner_manual_state_) {
      if(planner_manual_ == "STOP") return static_cast<int>(AutoState::STOP);
      else if(planner_manual_ == "AUTO") return static_cast<int>(AutoState::AUTO);
      else if(planner_manual_ == "NARROW") return static_cast<int>(AutoState::NARROW);
      else if(planner_manual_ == "NOMAP") return static_cast<int>(AutoState::NOMAP);
      else if(planner_manual_ == "RELOCATION") return static_cast<int>(AutoState::RELOCATION);
      else if(planner_manual_ == "WAIT") return static_cast<int>(AutoState::WAIT);
  }
  if(isAction_) return static_cast<int>(AutoState::ACTION);
  if(isLTEControl_) {
    if((ros::Time::now() - lte_last_timer_).toSec() > 0.1) {
      lte_cmd_.linear.x = 0;
      lte_cmd_.angular.z = 0;
      // cout << "LTE Control Timeout : " << (ros::Time::now() - lte_last_timer_).toSec() << endl;
    }
    return static_cast<int>(AutoState::LTE);
  }
  if(isRemote_) return static_cast<int>(AutoState::REMOTE);
  if(checkMissionStage()){ 
    // if(auto_state_global_ == 1) return static_cast<int>(AutoState::NOMAP);
    // else if(auto_state_global_ == 2) return static_cast<int>(AutoState::RELOCATION);
    // else {
      if(!MySuperviser_.GetAutoMissionState())
        return static_cast<int>(AutoState::NARROW);
      else 
        return static_cast<int>(AutoState::AUTO);  
    // } 
  }

  if(isReachCurrentGoal_ && isAdjustGuardPose_) return static_cast<int>(AutoState::ROTATION);

  return static_cast<int>(AutoState::STOP);
}

void MissionControl::ApplyAction(int mission_state) {
  geometry_msgs::Twist raw_cmd;
  if(lift_mode_ == 0) {
    double action_time = 5;
    if((ros::Time::now() - action_start_timer_).toSec() > action_time) {
      std_msgs::Int32 action_state;
      action_state.data = current_action_index_;
      action_state_pub.publish(action_state);
      isAction_ = false;
    }
  }
  else if(lift_mode_ == 1) {
    raw_cmd = UpLift1Cmd();
  }
  else if(lift_mode_ == 3) {
    raw_cmd = UpLift17Cmd();
  }
  else if (lift_mode_ == 4) {
    raw_cmd = DownLift17Cmd();
  }
  else if (lift_mode_ == 2) {
    raw_cmd = DownLift1Cmd();
  }


  geometry_msgs::Twist safe_cmd;
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();

}

void MissionControl::ApplyJoyControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd = getJoyCommand();
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  if(!updateState()) return;
  return;
}

void MissionControl::ApplyLTEControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd = getLTECommand();
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  if(!updateState()) return;
  return;
}

void MissionControl::ApplyWIFIControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd = getWIFICommand();
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  if(!updateState()) return;
  return;
}

void MissionControl::ApplyStopControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd;
  ZeroCommand(raw_cmd);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  if(!updateState()) return;
  return;
}

void MissionControl::ApplyAutoControl(int mission_state) {
  relocation_stage_ = 0;
  geometry_msgs::Twist safe_cmd;
  if(!setAutoCoefficient(1,spline_costmap_resolution_,spline_map_window_radius_)) return;
  if(!updateState()) return;
  geometry_msgs::Twist raw_cmd = getAutoCommand();
  auto supervise_state = SuperviseDecision(mission_state);
  raw_cmd = ExecuteDecision(raw_cmd,mission_state,supervise_state);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishInfo();
  publishCommand();
}

void MissionControl::ApplyNarrowControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  if(!setAutoCoefficient(1,RRT_costmap_resolution_,RRT_map_window_radius_)) return;
  if(!updateState()) return;
  // if(!updateNarrowState()) return;
  geometry_msgs::Twist raw_cmd = getNarrowCommand();
  auto supervise_state = SuperviseDecision(mission_state);
  raw_cmd = ExecuteDecision(raw_cmd,mission_state,supervise_state);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishInfo();
  publishCommand();
}

void MissionControl::ApplyRelocationControl(int mission_state) {
  if(RelocationDelay()) return;
  geometry_msgs::Twist safe_cmd;
  if(!setAutoCoefficient(1,spline_costmap_resolution_,spline_map_window_radius_)) return;
  if(!updateState()) return;
  geometry_msgs::Twist raw_cmd = getAutoCommand();
  auto supervise_state = SuperviseDecision(mission_state);
  raw_cmd = ExecuteDecision(raw_cmd,mission_state,supervise_state);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  publishInfo();
}

void MissionControl::ApplyNomapControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  RemoveMapObstacle(obstacle_in_base_);
  if(!setAutoCoefficient(1,spline_costmap_resolution_,spline_map_window_radius_)) return;
  if(!updateState()) return;
  geometry_msgs::Twist raw_cmd = getAutoCommand();
  auto supervise_state = SuperviseDecision(mission_state);
  raw_cmd = ExecuteDecision(raw_cmd,mission_state,supervise_state);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  publishInfo();
}

void MissionControl::ApplyWaitControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd;
  ZeroCommand(raw_cmd);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  if(!updateState()) return;
  return;
}

void MissionControl::ApplyRotationControl(int mission_state) {
  geometry_msgs::Twist safe_cmd;
  geometry_msgs::Twist raw_cmd;
  if(!UpdateVehicleLocation()) return;
  RotationCommand(raw_cmd);
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishCommand();
  publishInfo();
}

void MissionControl::ApplyRemoteControl(int mission_state) {
  ros::Time last_auto_time = ros::Time::now();
  geometry_msgs::Twist safe_cmd;
  if(!setAutoCoefficient(1,spline_costmap_resolution_,spline_map_window_radius_)) return;
  if(!UpdateVehicleLocation()) {
    cout << "Update Vehicle Location Failed" << endl;
    return;
  }

  MySuperviser_.AutoObstaclePercept(obstacle_in_base_,MyTools_.cmd_vel());
  geometry_msgs::Twist raw_cmd = getRemoteCommand();
  checkCommandSafety(raw_cmd,safe_cmd,mission_state);
  createCommandInfo(safe_cmd);
  publishInfo();
  publishCommand();
  ros::Time now_auto_time = ros::Time::now();
  if(plan_state_) cout << "spline plan search time : " << (now_auto_time.nsec - last_auto_time.nsec) * pow(10,-6) << "ms" << endl;
}

geometry_msgs::Twist MissionControl::getAutoCommand() {
  geometry_msgs::Twist controller_cmd;
  static bool last_plan_state = true;
  bool now_plan_state = true;

  int global_goal_index = FindCurrentGoalRoute(global_path_pointcloud_,vehicle_in_map_,lookahead_global_meter_);
  global_sub_goal_ = global_path_pointcloud_.points[global_goal_index];

  auto_state_global_ = global_sub_goal_.z;

  geometry_msgs::Point32 global_goal_in_local;
  ConvertPoint(global_sub_goal_,global_goal_in_local,map_to_base_);

  int path_lookahead_index;
  double search_range = spline_map_window_radius_;
  double search_range_min = 1;
  double iteration_scale = 1;

  ros::Time last_auto_time = ros::Time::now();
  while(search_range >= search_range_min) {
    MyPlanner_.set_map_window_radius(search_range);
    if(!MyPlanner_.UpdateCostmap(obstacle_in_base_)) {
      cout << "Update Costmap Failed" << endl;
      return controller_cmd;
    }
    
    if(MyPlanner_.GenerateCandidatePlan(global_goal_in_local,obstacle_in_base_,search_range)) {
      local_costmap_pub.publish(MyPlanner_.costmap_local());
      break;
    }
    else search_range-- ;
  }

  if(search_range < search_range_min) {
    path_lookahead_index = 0;
    plan_state_ = false;
  } else {
    plan_state_ = true;
    if(MySuperviser_.getFrontSafe()) lookahead_local_scale_ = 5;
    else lookahead_local_scale_ = 2;
    path_lookahead_index = MyPlanner_.sub_2_path_best().points.size()/lookahead_local_scale_;
  }

  if(MyPlanner_.sub_2_path_best().points.size() <= 0) {
    controller_cmd.linear.x = 0.1;
    controller_cmd.angular.z = 0;
    return controller_cmd;
  }
  ros::Time now_auto_time = ros::Time::now();
  cout << "spline plan search time : " << (now_auto_time.nsec - last_auto_time.nsec) * pow(10,-6) << "ms" << endl;
  local_sub_goal_ = MyPlanner_.sub_2_path_best().points[path_lookahead_index];
  MyController_.ComputePurePursuitCommand(global_goal_in_local,local_sub_goal_,controller_cmd);
  // cout << "lookahead_local_scale_ : " << lookahead_local_scale_ << endl;

  return controller_cmd;
}

geometry_msgs::Twist MissionControl::getNarrowCommand() {
  //切换规划器时 narrow_command_stage_ 重新复位为0 ,需要在上层决策器中重置。
  if(narrow_command_stage_ == 0) {
    // ros::Time last_Astar_time = ros::Time::now();
    // sensor_msgs::PointCloud Astar_pointcloud_local = NarrowAstarPathfind();
    // ros::Time now_Astar_time = ros::Time::now();
    // cout << "Astar plan time : " << (now_Astar_time.nsec - last_Astar_time.nsec) * pow(10,-6) << "ms" << endl;

    ros::Time last_RRT_time = ros::Time::now();
    sensor_msgs::PointCloud RRT_pointcloud_local = NarrowRRTPathfind();
    ros::Time now_RRT_time = ros::Time::now();
    cout << "RRT plan time : " << (now_RRT_time.nsec - last_RRT_time.nsec) * pow(10,-6) << "ms" << endl;
    // Astar_pointcloud_global_.points.clear();
    // for(int i = Astar_pointcloud_local.points.size()-1; i >= 0; i--) {
    //   if(i == 0 || i % 5 == 0 || i == Astar_pointcloud_local.points.size()-1) {
    //     geometry_msgs::Point32 temp_Astar_point_global;
    //     ConvertPoint(Astar_pointcloud_local.points[i],temp_Astar_point_global,base_to_map_);
    //     Astar_pointcloud_global_.points.push_back(temp_Astar_point_global);
    //   }
    // }

    RRT_pointcloud_global_.points.clear();
    for(int j = 0; j < RRT_pointcloud_local.points.size(); j++) {

      geometry_msgs::Point32 temp_RRT_point_global;
      ConvertPoint(RRT_pointcloud_local.points[j],temp_RRT_point_global,base_to_map_);
      RRT_pointcloud_global_.points.push_back(temp_RRT_point_global);

    }

    //决策Astar or RRT ？ 
    // isAstarPathfind_ = NarrowSubPlannerDecision();
    isAstarPathfind_ = false;
    narrow_command_stage_++;
    if(isAstarPathfind_) {
      // vehicle_run_state_2nd_.data = "step2: NARROW sub planner was Astar, " + string("Astar size was ") + to_string(Astar_pointcloud_local.points.size());
    }else {
      vehicle_run_state_2nd_.data = "step2: NARROW sub planner was RRT, " + string("RRT size was ") + to_string(RRT_pointcloud_local.points.size());
    }
  }

  geometry_msgs::Twist controller_cmd;
  if(narrow_command_stage_ == 1) {
    if(isAstarPathfind_) controller_cmd = PursuitAstarPathCommand();
    else {
      if(RRT_refind_) {
        ros::Time last_RRT_time = ros::Time::now();
        sensor_msgs::PointCloud RRT_pointcloud_local = NarrowRRTPathfind();
        ros::Time now_RRT_time = ros::Time::now();
        cout << "RRT plan time : " << (now_RRT_time.nsec - last_RRT_time.nsec) * pow(10,-6) << "ms" << endl;

        RRT_pointcloud_global_.points.clear();
        for(int j = 0; j < RRT_pointcloud_local.points.size(); j++) {
          geometry_msgs::Point32 temp_RRT_point_global;
          ConvertPoint(RRT_pointcloud_local.points[j],temp_RRT_point_global,base_to_map_);
          RRT_pointcloud_global_.points.push_back(temp_RRT_point_global);

        }
      }
      if(RRT_pointcloud_global_.points.empty()) {
        ClearNarrowMissionState();
      }
      controller_cmd = PursuitRRTPathCommand();
    }    
  }
  return controller_cmd;
}

geometry_msgs::Twist MissionControl::getRemoteCommand() {
  geometry_msgs::Twist controller_cmd;

  int path_lookahead_index;
  double search_range = spline_map_window_radius_;
  double search_range_min = 1;
  double iteration_scale = 1;

  // cout << "remote_goal_ : " << remote_goal_.x << " " << remote_goal_.y << endl;
  ros::Time last_auto_time = ros::Time::now();
  while(search_range >= search_range_min) {
    MyPlanner_.set_map_window_radius(search_range);
    if(!MyPlanner_.UpdateCostmap(obstacle_in_base_)) {
      cout << "Update Costmap Failed" << endl;
      return controller_cmd;
    }
    
    setRemoteGoal(search_range);
    if(MyPlanner_.GenerateCandidatePlan(remote_goal_,obstacle_in_base_,search_range)) {
      local_costmap_pub.publish(MyPlanner_.costmap_local());
      break;
    }
    else search_range-- ;
  }

  if(search_range < search_range_min) {
    path_lookahead_index = 0;
  } else {
    if(MySuperviser_.getFrontSafe()) lookahead_local_scale_ = 5;
    else lookahead_local_scale_ = 2;
    path_lookahead_index = MyPlanner_.sub_2_path_best().points.size()/lookahead_local_scale_;
  }

  if(MyPlanner_.sub_2_path_best().points.size() <= 0) {
    controller_cmd.linear.x = 0.1;
    controller_cmd.angular.z = 0;
    return controller_cmd;
  }
  ros::Time now_auto_time = ros::Time::now();
  // cout << "spline plan search time : " << (now_auto_time.nsec - last_auto_time.nsec) * pow(10,-6) << "ms" << endl;
  local_sub_goal_ = MyPlanner_.sub_2_path_best().points[path_lookahead_index];
  MyController_.ComputePurePursuitCommand(remote_goal_,local_sub_goal_,controller_cmd);
  cout << "lookahead_local_scale_ : " << lookahead_local_scale_ << endl;

  return controller_cmd;
}

bool MissionControl::RelocationDelay() {
  relocation_stage_++;
  if(relocation_stage_ <= 100) return true;
  else return false;
}

void MissionControl::CheckNarrowToAutoState() {
  int global_goal_index = FindCurrentGoalRoute(global_path_pointcloud_,vehicle_in_map_,lookahead_global_meter_);
  geometry_msgs::Point32 global_goal_in_local;
  ConvertPoint(global_path_pointcloud_.points[global_goal_index],global_goal_in_local,map_to_base_);

  double check_search_range = 4;
  ClearAutoMissionState();
  // if(MyPlanner_.GenerateCandidatePlan(global_goal_in_local,obstacle_in_base_,check_search_range)){
  //   ClearAutoMissionState();

  //   vehicle_run_state_3rd_.data = GetLocalTime() + "step3： The route condition was better, NARROW planner had changed to AUTO.";
  //   vehicle_run_state_pub.publish(vehicle_run_state_3rd_);
  // }
}

bool MissionControl::NarrowSubPlannerDecision() {
  if(Astar_pointcloud_global_.points.empty()) return false;
  else{
    if(RRT_pointcloud_global_.points.empty()) return true;
    double Astar_pointcloud_dist = ComputePathDistance(Astar_pointcloud_global_);
    double RRT_pointcloud_dist = ComputePathDistance(RRT_pointcloud_global_);
    // double vehicle_goal_min_dist 
    //   = hypot(Astar_pointcloud_global_.points.front().x-vehicle_in_map_.x,Astar_pointcloud_global_.points.front().y-vehicle_in_map_.y);
    if(Astar_pointcloud_dist < RRT_pointcloud_dist * 2) return true;
    else return false;  
    //后续需要再对RRT的障碍物密集程度做判断
  }
}

double MissionControl::ComputePathDistance(sensor_msgs::PointCloud Path) {
  double path_dist = 0;
  if(Path.points.size() <= 1) return 999999;
  for(int i = 0; i < Path.points.size() - 1; i++) {
    double temp_dist = hypot(Path.points[i].x-Path.points[i+1].x,Path.points[i].y-Path.points[i+1].y);
    path_dist = path_dist + temp_dist;
  }
  return path_dist;
}

sensor_msgs::PointCloud MissionControl::NarrowAstarPathfind() {
  geometry_msgs::Point32 temp_global_goal = vehicle_in_map_;
  geometry_msgs::Point32 temp_local_goal;
  geometry_msgs::Point32 temp_local_point;
  double last_dist = 0;

  int global_goal_index = FindCurrentGoalRoute(global_path_pointcloud_,vehicle_in_map_,lookahead_global_meter_);
  for(int i = global_goal_index; i < global_path_pointcloud_.points.size(); i++) {

    ConvertPoint(global_path_pointcloud_.points[i],temp_local_point,map_to_base_);

    double temp_dist = hypot(global_path_pointcloud_.points[i].x-vehicle_in_map_.x,global_path_pointcloud_.points[i].y-vehicle_in_map_.y);

    if(temp_dist >= Astar_local_map_window_radius_) continue;
    
    if(temp_dist > last_dist) {
      temp_global_goal = global_path_pointcloud_.points[i];
      last_dist = temp_dist;
    }
  }

  ConvertPoint(temp_global_goal,temp_local_goal,map_to_base_);
  if(fabs(temp_local_goal.x) < 0.001) temp_local_goal.x = 0;
  if(fabs(temp_local_goal.y) < 0.001) temp_local_goal.y = 0;
  
  geometry_msgs::Point32 vehicle_in_local;
  vehicle_in_local.x = 0;
  vehicle_in_local.y = 0;

  vector<sensor_msgs::PointCloud> Astar_path = MyPlanner_.getAstarPath(vehicle_in_local,temp_local_goal);
  if(!Astar_path.empty()) return Astar_path.front();

  sensor_msgs::PointCloud temp_path;
  return temp_path;
}

sensor_msgs::PointCloud MissionControl::NarrowRRTPathfind() {
  int global_goal_index = FindCurrentGoalRoute(global_path_pointcloud_,vehicle_in_map_,lookahead_global_meter_);
  // cout << "global_goal_index :" << global_goal_index << endl;
  global_sub_goal_ = global_path_pointcloud_.points[global_goal_index];
  cout << "global_sub_goal_ : " << global_sub_goal_.x << " , " << global_sub_goal_.y << endl;
  
  geometry_msgs::Point32 global_goal_in_local;
  ConvertPoint(global_sub_goal_,global_goal_in_local,map_to_base_);

  double goal_distance = hypot(global_goal_in_local.x,global_goal_in_local.y);
  if(goal_distance > 6) {
    double shrink_scale = 6.0 / goal_distance;
    global_goal_in_local.x = global_goal_in_local.x * shrink_scale;
    global_goal_in_local.y = global_goal_in_local.y * shrink_scale;
  }
  
  geometry_msgs::Point32 vehicle_in_local;
  vehicle_in_local.x = 0;
  vehicle_in_local.y = 0;

  sensor_msgs::PointCloud temp_pointcloud;

  nav_msgs::Path RRT_path_local = MyPlanner_.getRRTPlan(vehicle_in_local,global_goal_in_local);

  for(int i = 0; i < RRT_path_local.poses.size(); i++) {
    geometry_msgs::Point32 temp_point;
    temp_point.x = RRT_path_local.poses[i].pose.position.x;
    temp_point.y = RRT_path_local.poses[i].pose.position.y;
  
    temp_pointcloud.points.push_back(temp_point);
  }

  temp_pointcloud.header.frame_id = "/base_link";
  temp_pointcloud.header.stamp = ros::Time::now();

  return temp_pointcloud;
}

geometry_msgs::Twist MissionControl::PursuitAstarPathCommand() {

  geometry_msgs::Twist controller_cmd;
  if(Astar_pointcloud_global_.points.empty()) return controller_cmd;

  int path_index = FindCurrentGoalRoute(Astar_pointcloud_global_,vehicle_in_map_,lookahead_global_meter_);

  geometry_msgs::Point32 Astar_sub_goal_local;
  ConvertPoint(Astar_pointcloud_global_.points[path_index],Astar_sub_goal_local,map_to_base_);

  if(MySuperviser_.GetNarrowRefind()) {
    ClearNarrowMissionState(); //Astar路径跟踪时判断是否重Narrow规划
    vehicle_run_state_3rd_.data = GetLocalTime() + "step3： Astar was broken, NARROW planner started to replan.";
    vehicle_run_state_pub.publish(vehicle_run_state_3rd_);
  }

  ConvertPoint(Astar_sub_goal_local,global_sub_goal_,base_to_map_);
  sensor_msgs::PointCloud temp_pointcloud;
  MyRouter_.ModifyRoutePoint(Astar_sub_goal_local,temp_pointcloud,obstacle_in_base_);

  if(temp_pointcloud.points.size() > 0) {
    if(temp_pointcloud.points.back().z == -1) {
      Astar_sub_goal_local = temp_pointcloud.points.back();
      ConvertPoint(Astar_sub_goal_local,global_sub_goal_,base_to_map_);
    }
  }

  if(CheckRouteEndState(Astar_pointcloud_global_.points.back())) {
    ClearAutoMissionState();
    vehicle_run_state_3rd_.data = GetLocalTime() + "step3: Astar route had been completed.";
    vehicle_run_state_pub.publish(vehicle_run_state_3rd_);
    return controller_cmd; 
  }

  double rotation_threshold = 0.6;
  double faceing_angle = atan2(Astar_sub_goal_local.y,Astar_sub_goal_local.x);

  
  int path_lookahead_index;
  double search_range = MyPlanner_.map_window_radius();
  double search_range_min = 2;
  double iteration_scale = 0.8;
  double wait_range = 4;
  wait_plan_state_ = true;
  while(search_range > search_range_min) {
    if(!MyPlanner_.GenerateCandidatePlan(Astar_sub_goal_local,obstacle_in_base_,search_range)) {
      if(search_range < wait_range) wait_plan_state_ = false;
    } else {
      break;
    }
    search_range *= iteration_scale;

    // if(MyPlanner_.path_all_set().size() > 0) local_all_pub.publish(MyTools_.ConvertVectortoPointcloud(MyPlanner_.path_all_set()));
  }
  

  if(search_range <= search_range_min) {
    plan_state_ = false;
    path_lookahead_index = 0;
  } else {
    path_lookahead_index = MyPlanner_.path_best().points.size()/lookahead_local_scale_;
    plan_state_ = true;
  }

  if(fabs(faceing_angle) > rotation_threshold) plan_state_ = true;

  local_sub_goal_ = MyPlanner_.path_best().points[path_lookahead_index];
  MyController_.ComputePurePursuitCommand(Astar_sub_goal_local,local_sub_goal_,controller_cmd);
  return controller_cmd;
}

geometry_msgs::Twist MissionControl::PursuitRRTPathCommand() {

  geometry_msgs::Twist controller_cmd;
  if(RRT_pointcloud_global_.points.empty()) return controller_cmd;

  sensor_msgs::PointCloud RRT_pointcloud_local;
  for(int i = 0; i < RRT_pointcloud_global_.points.size(); i++) {
    geometry_msgs::Point32 temp_RRT_point_local;
    ConvertPoint(RRT_pointcloud_global_.points[i],temp_RRT_point_local,map_to_base_);
    RRT_pointcloud_local.points.push_back(temp_RRT_point_local);
  }

  int path_lookahead_index;

  geometry_msgs::Point32 RRT_check_local_point;
  vector<geometry_msgs::Point32> RRT_check_local;

  path_lookahead_index = RRT_pointcloud_local.points.size() * lookahead_RRT_scale_;
  for(int i = path_lookahead_index; i >= 0; i--) {
    RRT_check_local_point = RRT_pointcloud_local.points[i];
    RRT_check_local.push_back(RRT_check_local_point);
  }

  //判断是否重寻路径
  RRT_refind_ = MySuperviser_.NarrowPlannerRestart(obstacle_in_base_,RRT_check_local,MyPlanner_.costmap_local(),MyTools_.cmd_vel()); //路径上有无障碍物
  
  int init_index = FindCurrentGoalRoute(global_path_pointcloud_,vehicle_in_map_,lookahead_global_meter_); //global_sub_goal 是否发生变化
  static int last_index = init_index;

  if(last_index != init_index) {
    RRT_refind_ = true;
    ClearAutoMissionState(); //RRT每到达一个目标点转为AUTO
  }
  last_index = init_index;

  if(RRT_refind_) {
    vehicle_run_state_3rd_.data = GetLocalTime() + "step3： RRT route appeared obstacle or achived sub goal, RRT started to replan. ";
    vehicle_run_state_pub.publish(vehicle_run_state_3rd_);
    ClearRRTPlanState();
    return controller_cmd;
  }

  MyController_.ComputePurePursuitRRTCommand(RRT_pointcloud_local.points.front(),RRT_pointcloud_local.points[path_lookahead_index],controller_cmd);
  if(CheckRRTRouteState(RRT_pointcloud_global_.points[path_lookahead_index])) lookahead_RRT_scale_ = lookahead_RRT_scale_ * 0.9;
  if(CheckRRTRouteState(RRT_pointcloud_global_.points.front())) {
    RRT_refind_ = true;
    ClearAutoMissionState(); //RRT每到达一个目标点转为AUTO
  }
  return controller_cmd;
}

bool MissionControl::CheckAstarRouteState(geometry_msgs::Point32 Input) {  
    double reach_goal_distance = 2;
    if(hypot(vehicle_in_map_.x-Input.x,vehicle_in_map_.y-Input.y) < reach_goal_distance) {
        return true;
    }else{
        return false;
    }
}

bool MissionControl::CheckRRTRouteState(geometry_msgs::Point32 Input) {  
    double reach_goal_distance = 1;
    if(hypot(vehicle_in_map_.x-Input.x,vehicle_in_map_.y-Input.y) < reach_goal_distance) {
        return true;
    }else{
        return false;
    }
}

bool MissionControl::CheckRouteEndState(geometry_msgs::Point32 Input) {  
    double reach_goal_distance = 1;
    if(hypot(vehicle_in_map_.x-Input.x,vehicle_in_map_.y-Input.y) < reach_goal_distance) {
        return true;
    }else{
        return false;
    }
}

bool MissionControl::CheckNavigationState() {
  if(isReachCurrentGoal_) return false;
  if(global_path_pointcloud_.points.size() <= 0) return false;
  double reach_goal_distance = 2;
  geometry_msgs::Point32 goal_local;
  ConvertPoint(goal_in_map_,goal_local,map_to_base_);

  if(hypot(goal_local.x,goal_local.y) < reach_goal_distance) {
    isReachCurrentGoal_ = true;
    global_path_pointcloud_.points.clear();
    ClearAutoMissionState();
    // cout << "Goal Reached" << endl;
    return false;
  }
  return true;
}


void MissionControl::ComputeGlobalPlan(geometry_msgs::Point32& Goal) {
  if(!isLocationUpdate_) return;
  isReachCurrentGoal_ = false;
  string map_folder = config_folder_ + "/map/";
  MyRouter_.RoutingAnalyze(Goal,vehicle_in_map_,map_folder,map_number_);
  global_path_pointcloud_ = MyRouter_.path_pointcloud();
  isReachCurrentGoal_ = false;
  if(global_path_pointcloud_.points.size() <= 0) {
    global_path_pointcloud_.points.push_back(goal_in_map_);
  } else {
    if(MyRouter_.path().size() > 0) SendMqttRoute(MyRouter_.path());
  }


  // cout << "Path Planned" << endl;
}


void MissionControl::SendMqttRoute(vector<int> Input) {
    string delimiter = "$";
    string id_delimiter = "#";

    string head_str = "GPMAPD";
    string source_str = "robot";
    string community_str = community_id_;
    string id_str = robot_id_.substr(6,2);
    string status_str = "ok";
    string time_str = to_string(ros::Time::now().toNSec()).substr(0,13);
    string type_str = "path";
    string task_str = "path";
    string end_str = "HE";

    string msg_1_str;
    if(Input.size() > 0) msg_1_str += to_string(Input[0]);
    if(Input.size() > 1) {
      for (int i = 1; i < Input.size(); ++i) {
        msg_1_str += id_delimiter;
        msg_1_str += to_string(Input[i]);
      }
    }

    int length_raw = head_str.size() + source_str.size() + community_str.size() +
                     id_str.size() + status_str.size() +
                     time_str.size() + type_str.size() + 
                     task_str.size() + msg_1_str.size() +
                     end_str.size() + 10 * delimiter.size();
    length_raw += static_cast<int>(log10(static_cast<double>(length_raw)));
    length_raw ++;
    if(to_string(length_raw).size() != to_string(length_raw-1).size())

    if(length_raw == 100 || length_raw == 100) length_raw++;
    string length_str = to_string(length_raw);

    string output = head_str + delimiter + 
                    length_str + delimiter + 
                    source_str + delimiter + 
                    community_str + delimiter + 
                    id_str + delimiter + 
                    status_str + delimiter + 
                    time_str + delimiter + 
                    type_str + delimiter + 
                    task_str + delimiter + 
                    msg_1_str + delimiter + 
                    end_str;

    // cout << robot_id_ << " Path Mqtt : " << output << endl;
    std_msgs::String output_msg;
    output_msg.data = output;
    plan_str_pub.publish(output_msg);
}

void MissionControl::ConvertPoint(geometry_msgs::Point32 Input,geometry_msgs::Point32& Output,tf::Transform Transform) {
  double transform_m[4][4];
  double mv[12];
  Transform.getBasis().getOpenGLSubMatrix(mv);
  tf::Vector3 origin = Transform.getOrigin();

  transform_m[0][0] = mv[0];
  transform_m[0][1] = mv[4];
  transform_m[0][2] = mv[8];
  transform_m[1][0] = mv[1];
  transform_m[1][1] = mv[5];
  transform_m[1][2] = mv[9];
  transform_m[2][0] = mv[2];
  transform_m[2][1] = mv[6];
  transform_m[2][2] = mv[10];

  transform_m[3][0] = transform_m[3][1] = transform_m[3][2] = 0;
  transform_m[0][3] = origin.x();
  transform_m[1][3] = origin.y();
  transform_m[2][3] = origin.z();
  transform_m[3][3] = 1;

  Output.x 
    = Input.x * transform_m[0][0] 
    + Input.y * transform_m[0][1]
    + transform_m[0][3];
  Output.y 
    = Input.x * transform_m[1][0] 
    + Input.y * transform_m[1][1] 
    + transform_m[1][3];   
  Output.z = Input.z;
}


int MissionControl::FindCurrentGoalRoute(sensor_msgs::PointCloud Path,geometry_msgs::Point32 Robot,double Lookahead) {
  int output = -1;
  double min_value = DBL_MAX;
  double min_index_i = -1;
  double min_index_j = -1;

  for (int i = 0; i < Path.points.size()-1; ++i) {
    double lookahead_applied = Lookahead - Lookahead * Path.points[i].z * 0;
    double distance_i 
      = hypot((Path.points[i].x - Robot.x),(Path.points[i].y - Robot.y));
    double distance_j 
      = hypot((Path.points[i+1].x - Robot.x),(Path.points[i+1].y - Robot.y));
    double distance_ij 
      = hypot((Path.points[i].x - Path.points[i+1].x)
      ,(Path.points[i].y - Path.points[i+1].y));
    double curve_ration = Path.points[i].z;

    double value_ij = (distance_i + distance_j - distance_ij) / distance_ij;

    if(value_ij < min_value) {
      min_value = value_ij;
      min_index_i = i;
      min_index_j = i + 1;
      if(distance_j < lookahead_applied && min_index_j < Path.points.size()-1) {
        min_index_j++;
      }
    }
  }
  if(min_index_i == -1) min_index_i = Path.points.size() - 1;
  if(min_index_j == -1) min_index_j = Path.points.size() - 1;

  output = min_index_j;

  return output;
}

void MissionControl::LimitCommand(geometry_msgs::Twist& Cmd_vel,int mission_state) {
  if(mission_state == static_cast<int>(AutoState::AUTO))
    MyController_.getMaxLinearAcceleration(max_linear_acceleration_);
  else
    max_linear_acceleration_ = temp_max_linear_acceleration_;

  static geometry_msgs::Twist last_cmd_vel;
  static geometry_msgs::Twist last_origin_vel;
  last_origin_vel = Cmd_vel;

  // Velocity Limit
  if(fabs(Cmd_vel.linear.x) > fabs(max_linear_velocity_)) Cmd_vel.linear.x = ComputeSign(Cmd_vel.linear.x) * max_linear_velocity_;
  if(fabs(Cmd_vel.angular.z) > fabs(max_rotation_velocity_)) Cmd_vel.angular.z = ComputeSign(Cmd_vel.angular.z) * max_rotation_velocity_;
  if(fabs(Cmd_vel.angular.z) < min_translational_velocity_) Cmd_vel.angular.z = 0;

  // Linear acceleration Limit
  if((Cmd_vel.linear.x * last_cmd_vel.linear.x) < 0) Cmd_vel.linear.x = 0;
  else if(Cmd_vel.linear.x > 0 && Cmd_vel.linear.x < last_cmd_vel.linear.x) {Cmd_vel.linear.x = last_cmd_vel.linear.x - max_linear_acceleration_;}
  else if(fabs(fabs(Cmd_vel.linear.x) - fabs(last_cmd_vel.linear.x)) < max_linear_acceleration_) {Cmd_vel.linear.x = Cmd_vel.linear.x;}
  else if(Cmd_vel.linear.x > last_cmd_vel.linear.x) Cmd_vel.linear.x = last_cmd_vel.linear.x + max_linear_acceleration_;
  else if(Cmd_vel.linear.x < last_cmd_vel.linear.x) Cmd_vel.linear.x = last_cmd_vel.linear.x - max_linear_acceleration_;

  // Angular acceleration limit
  if((Cmd_vel.angular.z * last_cmd_vel.angular.z) < 0 || Cmd_vel.angular.z == 0) Cmd_vel.angular.z = 0;
  else if(Cmd_vel.angular.z > 0 && Cmd_vel.angular.z < last_cmd_vel.angular.z) {Cmd_vel.angular.z = last_cmd_vel.angular.z - max_rotation_acceleration_;}
  else if(fabs(fabs(Cmd_vel.angular.z) - fabs(last_cmd_vel.angular.z)) < max_rotation_acceleration_) {Cmd_vel.angular.z = Cmd_vel.angular.z;}
  else if(Cmd_vel.angular.z > last_cmd_vel.angular.z) Cmd_vel.angular.z = last_cmd_vel.angular.z + max_rotation_acceleration_;
  else if(Cmd_vel.angular.z < last_cmd_vel.angular.z) Cmd_vel.angular.z = last_cmd_vel.angular.z - max_rotation_acceleration_;

  double velocity_rotation_ratio = max_translational_velocity_ / max_rotation_velocity_;
  double max_moving_rotation_velocity = max_rotation_velocity_ * (velocity_rotation_ratio - fabs(Cmd_vel.angular.z / max_rotation_velocity_));
  if(fabs(Cmd_vel.linear.x) > fabs(max_moving_rotation_velocity)) Cmd_vel.linear.x = ComputeSign(Cmd_vel.linear.x) * max_moving_rotation_velocity;

  // mission state limit
  if(fabs(last_origin_vel.linear.x) < fabs(last_cmd_vel.linear.x)) {
    if(mission_state == static_cast<int>(AutoState::JOY)) {
      if(isJOYscram_) {
        if(last_origin_vel.linear.x == 0)
          Cmd_vel.linear.x = 0;    
      } else {
        if(last_origin_vel.linear.x == 0) {
          Cmd_vel.linear.x = last_cmd_vel.linear.x - 2 * max_linear_acceleration_;
          if(Cmd_vel.linear.x < 0) Cmd_vel.linear.x = 0;
        }
      }
    }
    else if(mission_state == static_cast<int>(AutoState::STOP) || mission_state == static_cast<int>(AutoState::ACTION)) {
      if(last_cmd_vel.linear.x == 0) Cmd_vel.linear.x = 0; 
      else {
        Cmd_vel.linear.x = last_cmd_vel.linear.x - 2 * max_linear_acceleration_;
        if(Cmd_vel.linear.x < 0) Cmd_vel.linear.x = 0;
      }
    }
    else if(mission_state == static_cast<int>(AutoState::NARROW)) {
      if(last_origin_vel.linear.x == 0) {
        Cmd_vel.linear.x = last_cmd_vel.linear.x - 4 * max_linear_acceleration_;
        if(Cmd_vel.linear.x < 0) Cmd_vel.linear.x = 0;
      }
    }
  }

  if(mission_state == static_cast<int>(AutoState::AUTO) || mission_state == static_cast<int>(AutoState::NARROW) 
                                                        || mission_state == static_cast<int>(AutoState::REMOTE)) {
    if(MySuperviser_.DangerObstaclepPercept()) {
      Cmd_vel.linear.x = last_cmd_vel.linear.x - 6 * max_linear_acceleration_; 
      if(Cmd_vel.linear.x < 0) Cmd_vel.linear.x = 0;
    }
  }
  last_cmd_vel = Cmd_vel; 
}


void MissionControl::PrintConfig() {
  cout << "==================================" << endl;
  cout << "           Configuration          " << endl;
  cout << "==================================" << endl;

  cout << "Max Linear Velocity            : " << max_linear_velocity_ << endl;
  cout << "Max Rotation Velocity          : " << max_rotation_velocity_ << endl;

  cout << "Max Linear Acceleration        : " << max_linear_acceleration_ << endl;
  cout << "Max Rotation Acceleration      : " << max_rotation_acceleration_ << endl;

  cout << "Max Translational Velocity     : " << max_translational_velocity_ << endl;
  cout << "Min Translational Velocity     : " << min_translational_velocity_ << endl;

  cout << "Controller Linear Scale        : " << controller_linear_scale_ << endl;
  cout << "Controller Rotation Scale      : " << controller_rotation_scale_ << endl;

  cout << "Vehicle Radius                 : " << vehicle_radius_ << endl;

  cout << "Astar_local_map_window_radius  : " << Astar_local_map_window_radius_ << endl;
  cout << "Astar_local_costmap_resolution : " << Astar_local_costmap_resolution_ << endl;
  cout << "RRT_iteration                  : " << RRT_iteration_ << endl;

  cout << "==================================" << endl;
}


bool MissionControl::ReadConfig() {
  std::ifstream yaml_file;
  std::string file_name_node = config_folder_+ "/cfg/navi_config.txt";

  std::string Spline_folder = config_folder_;
  std::string Spline_name = "/cfg/map_to_path_4m";
  std::string Spline_name_1st = "/cfg/map_to_path_1m";
  std::string Spline_name_2nd = "/cfg/map_to_path_2m";
  std::string Spline_name_3rd = "/cfg/map_to_path_3m";
  MyPlanner_.InitSplineCurve(Spline_folder,Spline_name);
  MyPlanner_.InitSplineCurve1st(Spline_folder,Spline_name_1st);
  MyPlanner_.InitSplineCurve2nd(Spline_folder,Spline_name_2nd);
  MyPlanner_.InitSplineCurve3rd(Spline_folder,Spline_name_3rd);
  
  yaml_file.open(file_name_node);
  if(!yaml_file.is_open()) {
    cout<<"Could not find Config File"<<endl;
    cout<<"File Not Exist: "<<file_name_node<<endl;
    return false;
  }

  string str;
  int info_linenum = 0;
  while (std::getline(yaml_file, str)) {
    std::stringstream ss(str);
    string yaml_info;
    ss >> yaml_info;

    if(yaml_info == "max_linear_velocity") { 
      ss >> max_linear_velocity_;
      info_linenum++;
    }
    else if(yaml_info == "max_rotation_velocity") {
      ss >> max_rotation_velocity_;
      info_linenum++;
    }
    else if(yaml_info == "max_linear_acceleration") {
      ss >> max_linear_acceleration_;
      info_linenum++;
    }
    else if(yaml_info == "max_rotation_acceleration") {
      ss >> max_rotation_acceleration_;
      info_linenum++;
    }
    else if(yaml_info == "max_translational_velocity") {
      ss >> max_translational_velocity_;
      info_linenum++;
    }
    else if(yaml_info == "min_translational_velocity") {
      ss >> min_translational_velocity_;
      info_linenum++;
    }
    else if(yaml_info == "controller_linear_scale") {
      ss >> controller_linear_scale_;
      info_linenum++;
    }
    else if(yaml_info == "controller_rotation_scale") {
      ss >> controller_rotation_scale_;
      info_linenum++;
    }
    else if(yaml_info == "vehicle_radius") {
      ss >> vehicle_radius_;
      info_linenum++;
    }
    else if(yaml_info == "Astar_local_map_window_radius") {
      ss >> Astar_local_map_window_radius_;
      info_linenum++;
    }
    else if(yaml_info == "Astar_local_costmap_resolution") {
      ss >> Astar_local_costmap_resolution_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_iteration") {
      ss >> RRT_iteration_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_extend_step") {
      ss >> RRT_extend_step_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_search_plan_num") {
      ss >> RRT_search_plan_num_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_expand_size") {
      ss >> RRT_expand_size_;
      info_linenum++;
    }
    else if(yaml_info == "limit_danger_radius") {
      ss >> limit_danger_radius_;
      info_linenum++;
    }
    else if(yaml_info == "min_danger_longth") {
      ss >> min_danger_longth_;
      info_linenum++;
    }
    else if(yaml_info == "min_danger_width") {
      ss >> min_danger_width_;
      info_linenum++;
    }
    else if(yaml_info == "limit_route_radius") {
      ss >> limit_route_radius_;
      info_linenum++;
    }
    else if(yaml_info == "limit_predict_radius") {
      ss >> limit_predict_radius_;
      info_linenum++;
    }
    else if(yaml_info == "buffer_radius") {
      ss >> buffer_radius_;
      info_linenum++;
    }
    else if(yaml_info == "back_safe_radius") {
      ss >> back_safe_radius_;
      info_linenum++;
    }
    else if(yaml_info == "front_safe_radius") {
      ss >> front_safe_radius_;
      info_linenum++;
    }
    else if(yaml_info == "revolute_safe_radius") {
      ss >> revolute_safe_radius_;
      info_linenum++;
    }
    else if(yaml_info == "danger_assist_radius") {
      ss >> danger_assist_radius_;
      info_linenum++;
    }
    else if(yaml_info == "spline_search_grid") {
      ss >> spline_search_grid_;
      info_linenum++;
    }
    else if(yaml_info == "isJOYscram") {
      ss >> isJOYscram_;
      info_linenum++;
    }
    else if(yaml_info == "spline_costmap_resolution") {
      ss >> spline_costmap_resolution_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_costmap_resolution") {
      ss >> RRT_costmap_resolution_;
      info_linenum++;
    }
    else if(yaml_info == "spline_map_window_radius") {
      ss >> spline_map_window_radius_;
      info_linenum++;
    }
    else if(yaml_info == "RRT_map_window_radius") {
      ss >> RRT_map_window_radius_;
      info_linenum++;
    }
    else if(yaml_info == "spline_expand_size") {
      ss >> spline_expand_size_;
      info_linenum++;
    }
  }

  yaml_file.close(); 
  return true;
}

bool MissionControl::UpdateVehicleLocation() {
  try {
    listener_map_to_base.lookupTransform("/map", "/base_link",  
                             ros::Time(0), stampedtransform);
  }
  catch (tf::TransformException ex) {
    cout << "Waiting For Transform in MissionControl Node" << endl;
    return false;
  }

  isLocationUpdate_ = true;

  vehicle_in_map_.x = stampedtransform.getOrigin().x();
  vehicle_in_map_.y = stampedtransform.getOrigin().y();
  vehicle_in_map_.z = tf::getYaw(stampedtransform.getRotation());

  geometry_msgs::TransformStamped temp_trans;
  temp_trans.header.stamp = ros::Time::now();
  temp_trans.header.frame_id = "/map";
  temp_trans.child_frame_id = "/base_link";

  temp_trans.transform.translation.x = stampedtransform.getOrigin().x();
  temp_trans.transform.translation.y = stampedtransform.getOrigin().y();
  temp_trans.transform.translation.z = stampedtransform.getOrigin().z();
  temp_trans.transform.rotation.x = stampedtransform.getRotation().x();
  temp_trans.transform.rotation.y = stampedtransform.getRotation().y();
  temp_trans.transform.rotation.z = stampedtransform.getRotation().z();
  temp_trans.transform.rotation.w = stampedtransform.getRotation().w();
  transformMsgToTF(temp_trans.transform,base_to_map_);
  map_to_base_ = base_to_map_.inverse();

  geometry_msgs::Point32 shift_local;
  geometry_msgs::Point32 shift_baseline;
  ConvertPoint(shift_local,shift_baseline,base_to_map_);
  geometry_msgs::Point32 shift_map;
  shift_local.x = -0.5;
  shift_local.y = -0.3;
  ConvertPoint(shift_local,shift_map,base_to_map_);

  visualization_msgs::Marker marker;
  visualization_msgs::Marker infoer;
  
  marker.header.frame_id = "map";
  marker.header.stamp = ros::Time::now();
  marker.id = 0;
  marker.type = visualization_msgs::Marker::MESH_RESOURCE;
  marker.action = visualization_msgs::Marker::ADD;
  marker.pose.position.x = stampedtransform.getOrigin().x() + shift_map.x - shift_baseline.x;
  marker.pose.position.y = stampedtransform.getOrigin().y() + shift_map.y - shift_baseline.y;
  marker.pose.position.z = 0;
  marker.pose.orientation.x = stampedtransform.getRotation().x();
  marker.pose.orientation.y = stampedtransform.getRotation().y();
  marker.pose.orientation.z = stampedtransform.getRotation().z();
  marker.pose.orientation.w = stampedtransform.getRotation().w();
  marker.color.a = 1.0;
  marker.color.r = 1;
  marker.color.g = 0.5;
  marker.color.b = 0.5;
  marker.scale.x = 0.5;
  marker.scale.y = 0.5;
  marker.scale.z = 0.5;
  marker.mesh_resource = "package://config/cfg/model.dae";


  infoer.header.frame_id = "/base_link";
  infoer.header.stamp = ros::Time::now();
  infoer.id = 0;
  infoer.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
  infoer.action = visualization_msgs::Marker::ADD;
  infoer.pose.position.z = 5;
  infoer.color.a = 1.0;
  infoer.color.r = 1;
  infoer.color.g = 1;
  infoer.color.b = 1;
  infoer.scale.z = 0.1;
  infoer.text = "ID : " + robot_id_;


  vehicle_model_pub.publish(marker);
  vehicle_info_pub.publish(infoer);

  return true;
}



/** Callbacks **/
void MissionControl::JoyCallback(const sensor_msgs::Joy::ConstPtr& Input) {
  last_timer_ = ros::Time::now();
  static bool isLock = false;
  double axis_deadzone = 0.1;

  (Input->buttons[BUTTON_LB]) ? isJoyControl_ = true : isJoyControl_ = false;

  if (Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_X]) isLock = true;
  if (Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_Y]) isLock = false;

  double axis_x = Input->axes[AXIS_LEFT_UP_DOWN];
  double axis_y = Input->axes[AXIS_RIGHT_LEFT_RIGHT];
  if(fabs(axis_x) < axis_deadzone) axis_x = 0;
  if(fabs(axis_y) < axis_deadzone) axis_y = 0;

  if(isLock) {
    isJoyControl_ = true;
    ZeroCommand(joy_cmd_);
  } else {
    joy_cmd_.linear.x  = axis_x * max_linear_velocity_;
    joy_cmd_.angular.z = axis_y * max_rotation_velocity_;      
  }

  if(Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_BACK]) {
    global_path_pointcloud_.points.clear();
    isAction_ = false;
  }

  if(Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_A]) {
    goal_in_map_.x = -138.251;
    goal_in_map_.y = 16.853;
    goal_in_map_.z = 0;
    ComputeGlobalPlan(goal_in_map_);
    ClearAutoMissionState();
  }

  if(Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_B]) {
    goal_in_map_.x = -106;
    goal_in_map_.y = 63.800;
    goal_in_map_.z = 0;
    ComputeGlobalPlan(goal_in_map_);
    ClearAutoMissionState();
  }

  if(Input->buttons[BUTTON_LB] && Input->buttons[BUTTON_START]) {
    goal_in_map_.x = -103;
    goal_in_map_.y = 44.7;
    goal_in_map_.z = 0;
    isAdjustGuardPose_ = true;
    ComputeGlobalPlan(goal_in_map_);
    ClearAutoMissionState();
  }

  if(Input->buttons[BUTTON_RB]) {
    if(Input->buttons[BUTTON_A]) {
      cout << " Up to Lift 1 floor" << endl;
      global_path_pointcloud_.points.clear();
      isAction_    = true;
      lift_mode_   = 1;
      stage_index_ = 0;
    }
    else if (Input->buttons[BUTTON_B]) {
      cout << "Down to Lift 1 floor " << endl;
      global_path_pointcloud_.points.clear();
      isAction_    = true;
      lift_mode_   = 2;
      stage_index_ = 0;
    }
    else if (Input->buttons[BUTTON_X]) {
      cout << "Up to Lift 17 floor" << endl;
      global_path_pointcloud_.points.clear();
      isAction_    = true;
      lift_mode_   = 3;
      stage_index_ = 0;
    }
    else if (Input->buttons[BUTTON_Y]) {
      cout << "Down to Lift 17 floor" << endl;
      global_path_pointcloud_.points.clear();
      isAction_    = true;
      lift_mode_   = 4;
      stage_index_ = 0;
    }
  }

}

void MissionControl::CmdCallback(const geometry_msgs::Twist::ConstPtr& Input) {
  last_timer_ = ros::Time::now();
  isWIFIControl_ = true;
  geometry_msgs::Twist android_cmd;
  android_cmd.linear.x = Input->linear.x;
  android_cmd.angular.z = Input->angular.z;
  android_cmd.angular.z *= 1.5;
  joy_cmd_ = android_cmd;
}

void MissionControl::ControlCallback(const std_msgs::String::ConstPtr& Input) {
  lte_last_timer_ = ros::Time::now();
  string input_duplicate = Input->data;
  string delimiter = "$";
  string token;
  size_t pos = 0;

  string data_master = "masterlock";
  string data_lock   = "deadlock";
  string data_move   = "move";
  string data_loader = "conveyor";
  string lock_on = "on";
  string lock_off = "off";

  string topic_str;
  string msg_1_str;
  string msg_2_str;

  // cout << "Raw Input : " << input_duplicate << endl;

  if ((pos = input_duplicate.find(delimiter)) != std::string::npos) {
    topic_str = input_duplicate.substr(0, pos);
    // linear_index = stof(token);
    input_duplicate.erase(0, pos + delimiter.length());
  }

  if(topic_str == data_master) {
    if ((pos = input_duplicate.find(delimiter)) != std::string::npos) {
      msg_1_str = input_duplicate.substr(0, pos);
      input_duplicate.erase(0, pos + delimiter.length());
      if(msg_1_str == lock_on) {
        isLTEControl_ = true;
        cout << robot_id_ << " Control On" << endl;
        lte_cmd_.linear.x = 0;
        lte_cmd_.angular.z = 0;
      }
      else if (msg_1_str == lock_off) {
        isLTEControl_ = false;
        cout << robot_id_ << " Control Off" << endl;
        lte_cmd_.linear.x = 0;
        lte_cmd_.angular.z = 0;
      }
    }
    return;    
  }


  if(topic_str == data_lock) {
    if ((pos = input_duplicate.find(delimiter)) != std::string::npos) {
      msg_1_str = input_duplicate.substr(0, pos);
      input_duplicate.erase(0, pos + delimiter.length());
      if(msg_1_str == lock_on) {
        isLTELock_ = true;
        cout << robot_id_ << " Lock On" << endl;
        lte_cmd_.linear.x = 0;
        lte_cmd_.angular.z = 0;
      }
      else if (msg_1_str == lock_off) {
        isLTELock_ = false;
        cout << robot_id_ << " Lock Off" << endl;
        lte_cmd_.linear.x = 0;
        lte_cmd_.angular.z = 0;
      }
    }
    return;    
  }


  if(isLTELock_) {
    lte_cmd_.linear.x = 0;
    lte_cmd_.angular.z = 0;
    return;
  }


  if(topic_str == data_move) {
    double linear_speed;
    double angular_speed;
    if ((pos = input_duplicate.find(delimiter)) != std::string::npos) {
      msg_1_str = input_duplicate.substr(0, pos);
      angular_speed = stof(msg_1_str);
      input_duplicate.erase(0, pos + delimiter.length());
    }
    if ((pos = input_duplicate.find(delimiter)) != std::string::npos) {
      msg_2_str = input_duplicate.substr(0, pos);
      linear_speed = stof(msg_2_str);
      input_duplicate.erase(0, pos + delimiter.length());
    }
    cout << robot_id_ << " Move command Linear " << linear_speed << ", angular " << angular_speed << endl;
    lte_cmd_.linear.x = linear_speed;
    lte_cmd_.angular.z = angular_speed;
    return;    
  }  
}

int MissionControl::SuperviseDecision(int mission_state){
    int raw_state;
    if(mission_state == static_cast<int>(AutoState::AUTO) || mission_state == static_cast<int>(AutoState::RELOCATION)){
      MySuperviser_.GenerateTracebackRoute(global_sub_goal_,vehicle_in_map_);   //1
      raw_state = MySuperviser_.AutoSuperviseDecision(obstacle_in_base_,vehicle_in_map_,MyTools_.cmd_vel(),plan_state_,wait_plan_state_); //2
    }
    else if(mission_state == static_cast<int>(AutoState::NARROW)) {
      if(!isAstarPathfind_) raw_state = MySuperviser_.NarrowSuperviseDecision(obstacle_in_base_,MyTools_.cmd_vel());
      else {
        MySuperviser_.GenerateTracebackRoute(global_sub_goal_,vehicle_in_map_);
        raw_state = MySuperviser_.AstarSuperviseDecision(obstacle_in_base_,vehicle_in_map_,MyTools_.cmd_vel(),plan_state_,wait_plan_state_); 
      }
    }  
    else if(mission_state == static_cast<int>(AutoState::NOMAP)) {
      raw_state = MySuperviser_.AutoSuperviseDecision(obstacle_in_base_,vehicle_in_map_,MyTools_.cmd_vel(),plan_state_,wait_plan_state_);
    }

    return raw_state;
}

geometry_msgs::Twist MissionControl::ExecuteDecision(geometry_msgs::Twist Input,int mission_state,int supervise_state){
  geometry_msgs::Twist raw_cmd;
  if(mission_state == static_cast<int>(AutoState::AUTO) || mission_state == static_cast<int>(AutoState::NOMAP) 
                                                  || mission_state == static_cast<int>(AutoState::RELOCATION))
  {
    if(supervise_state == static_cast<int>(AUTO::P)){
        raw_cmd = MyExecuter_.ApplyAutoP(Input);
    }else if(supervise_state == static_cast<int>(AUTO::R1)){
        raw_cmd = MyExecuter_.ApplyAutoR1(Input);
    }else if(supervise_state == static_cast<int>(AUTO::R2)){
        raw_cmd = MyExecuter_.ApplyAutoR2(Input);
    }else if(supervise_state == static_cast<int>(AUTO::R3)){
        raw_cmd = MyExecuter_.ApplyAutoR3(Input);
    }else if(supervise_state == static_cast<int>(AUTO::N)){
        raw_cmd = MyExecuter_.ApplyAutoN(Input);
    }else if(supervise_state == static_cast<int>(AUTO::D1)){
        raw_cmd = MyExecuter_.ApplyAutoD1(Input);
    }else if(supervise_state == static_cast<int>(AUTO::D2)){
        raw_cmd = MyExecuter_.ApplyAutoD2(Input);
    }else if(supervise_state == static_cast<int>(AUTO::D3)){
        raw_cmd = MyExecuter_.ApplyAutoD3(Input);
    }else if(supervise_state == static_cast<int>(AUTO::D4)){
        raw_cmd = MyExecuter_.ApplyAutoD4(Input);
    }else if(supervise_state == static_cast<int>(AUTO::AUTO)){
        raw_cmd = MyExecuter_.ApplyAutoAUTO(Input);
    }else if(supervise_state == static_cast<int>(AUTO::REVOLUTE1)){
        raw_cmd = MyExecuter_.ApplyAutoRevolute1(Input);
        MyExecuter_.ComputeRevoluteCommand(vehicle_in_map_,MySuperviser_.GetRevolutePose(), PI/4, raw_cmd);
    }else if(supervise_state == static_cast<int>(AUTO::REVOLUTE2)){
        raw_cmd = MyExecuter_.ApplyAutoRevolute2(Input);
        MyExecuter_.ComputeRevoluteCommand(vehicle_in_map_,MySuperviser_.GetRevolutePose(), -PI/2, raw_cmd);
    }else if(supervise_state == static_cast<int>(AUTO::TRACEBACK)){
        raw_cmd = TracebackRoute(MySuperviser_.GetVehicleTraceback());
    }
  }

  if(mission_state == static_cast<int>(AutoState::NARROW))
  {
    if(!isAstarPathfind_){
      if(supervise_state == static_cast<int>(NARROW::P)){
        raw_cmd = MyExecuter_.ApplyNarrowP(Input);
      }else if(supervise_state == static_cast<int>(NARROW::D1)){
        raw_cmd = MyExecuter_.ApplyNarrowD1(Input);
      }else if(supervise_state == static_cast<int>(NARROW::R1)){
        raw_cmd = MyExecuter_.ApplyNarrowR1(Input);
      }else if(supervise_state == static_cast<int>(NARROW::R2)){
        raw_cmd = MyExecuter_.ApplyNarrowR2(Input);
      }
    }else{
      if(supervise_state == static_cast<int>(AUTO::P)){
        raw_cmd = MyExecuter_.ApplyAutoP(Input);
      }else if(supervise_state == static_cast<int>(AUTO::R1)){
        raw_cmd = MyExecuter_.ApplyAutoR1(Input);
      }else if(supervise_state == static_cast<int>(AUTO::R2)){
        raw_cmd = MyExecuter_.ApplyAutoR2(Input);
      }else if(supervise_state == static_cast<int>(AUTO::R3)){
        raw_cmd = MyExecuter_.ApplyAutoR3(Input);
      }else if(supervise_state == static_cast<int>(AUTO::N)){
        raw_cmd = MyExecuter_.ApplyAutoN(Input);
      }else if(supervise_state == static_cast<int>(AUTO::D1)){
        raw_cmd = MyExecuter_.ApplyAutoD1(Input);
      }else if(supervise_state == static_cast<int>(AUTO::D2)){
        raw_cmd = MyExecuter_.ApplyAutoD2(Input);
      }else if(supervise_state == static_cast<int>(AUTO::D3)){
        raw_cmd = MyExecuter_.ApplyAutoD3(Input);
      }else if(supervise_state == static_cast<int>(AUTO::D4)){
        raw_cmd = MyExecuter_.ApplyAutoD4(Input);
      }else if(supervise_state == static_cast<int>(AUTO::AUTO)){
        raw_cmd = MyExecuter_.ApplyAutoAUTO(Input);
      }else if(supervise_state == static_cast<int>(AUTO::REVOLUTE1)){
        raw_cmd = MyExecuter_.ApplyAutoRevolute1(Input);
        MyExecuter_.ComputeRevoluteCommand(vehicle_in_map_,MySuperviser_.GetRevolutePose(), PI/4, raw_cmd);
      }else if(supervise_state == static_cast<int>(AUTO::REVOLUTE2)){
        raw_cmd = MyExecuter_.ApplyAutoRevolute2(Input);
        MyExecuter_.ComputeRevoluteCommand(vehicle_in_map_,MySuperviser_.GetRevolutePose(), -PI/2, raw_cmd);
      }else if(supervise_state == static_cast<int>(AUTO::TRACEBACK)){
        raw_cmd = TracebackRoute(MySuperviser_.GetVehicleTraceback());
      }
    }
  }
  return raw_cmd;
}

geometry_msgs::Twist MissionControl::TracebackRoute(vector<geometry_msgs::Point32> Input){
    geometry_msgs::Twist raw_cmd;
    static double lookahead_scale = 0.5;
    int lookahead_index = Input.size() * lookahead_scale;
    geometry_msgs::Point32 vehicle_pose_back_local;
    geometry_msgs::Point32 vehicle_pose_backend_local;

    ConvertPoint(Input[lookahead_index], vehicle_pose_back_local, map_to_base_);
    ConvertPoint(Input.front(), vehicle_pose_backend_local, map_to_base_);

    MyController_.ComputePurePursuitCommand(vehicle_pose_backend_local,vehicle_pose_back_local,raw_cmd);
    
    if(CheckTracebackState(Input[lookahead_index])) lookahead_scale = lookahead_scale * 0.5;
    if(CheckTracebackState(Input.front())) lookahead_scale = 0.5;

    return raw_cmd;
}

bool MissionControl::CheckTracebackState(geometry_msgs::Point32 Input) { 
    double reach_goal_distance = 1;
    geometry_msgs::Point32 Input_local;
    ConvertPoint(Input,Input_local,map_to_base_);
    if(hypot(Input_local.x,Input_local.y) < reach_goal_distance) {
        return true;
    }else{
        return false;
    }
}




  // ================= Lift ================= 
  void MissionControl::ScanCallback(const sensor_msgs::LaserScan::ConstPtr& Input) {
    scan_points_.points.clear();
    geometry_msgs::Point32 temp_point;
    for (int i = 0; i < Input->ranges.size(); ++i) {
      double current_angle = Input->angle_min + i * Input->angle_increment;
      if(Input->ranges[i] <= Input->range_min || Input->ranges[i] >= Input->range_max) continue;
      temp_point.x = Input->ranges[i] * cos(current_angle);
      temp_point.y = Input->ranges[i] * sin(current_angle);
      scan_points_.points.push_back(temp_point);
    }
    scan_points_.header.frame_id = "/base_link";
    scan_points_.header.stamp = ros::Time::now();
  }

  void MissionControl::initLift() {
    lift_mode_ = 0;
    stage_index_ = 0;
  }

  geometry_msgs::Twist MissionControl::UpLift1Cmd() {
    geometry_msgs::Twist output_cmd;

    if(stage_index_ == 0) { 
      if(GoStraight(output_cmd)) stage_index_ = 2;
     }
     else if(stage_index_ == 2) {
      if(Turn(output_cmd)) stage_index_ = 3;
      action_msg_.data = "close";
		  gate_command_pub.publish(action_msg_);
     } 
     else if(stage_index_ == 3) {
      if(EnterLift(output_cmd)) stage_index_ = 4;
    }
    else if(stage_index_ == 4) {
      initLift();
      cout << "Entered the 1st floor LIFT" << endl;
    }
    return output_cmd;
  }


  geometry_msgs::Twist MissionControl::DownLift1Cmd() {
    geometry_msgs::Twist output_cmd;

    if(stage_index_ == 0) {
      if(GetOffLift(output_cmd)) stage_index_ = 2;
    }
    else if(stage_index_ == 2) {
      if(Turn(output_cmd)) stage_index_ = 3;
    } 
    else if(stage_index_ == 3) {
      if(StepBack(output_cmd)) stage_index_ = 4;
    }
    else if(stage_index_ == 4) {
      initLift();
      cout << "get off the 1st floor LIFT" << endl;
    }
    return output_cmd;
  }

  geometry_msgs::Twist MissionControl::UpLift17Cmd() {
    geometry_msgs::Twist output_cmd;

    if(stage_index_ == 0) {
      if(GoStraight(output_cmd)) stage_index_ = 2;
    }
    else if(stage_index_ == 2) {
      if(Turn(output_cmd)) stage_index_ = 3;
    } 
    else if(stage_index_ == 3) {
      if(EnterLift(output_cmd)) stage_index_ = 4;
    }
    else if(stage_index_ == 4) {
        initLift();
        cout << "Entered the 17th floor LIFT" << endl;
    }
    return output_cmd;
  }

  geometry_msgs::Twist MissionControl::DownLift17Cmd() {
    geometry_msgs::Twist output_cmd;

    if(stage_index_ == 0) {
      if(GetOffLift(output_cmd)) stage_index_ = 1;
    }
    else if(stage_index_ == 1) {
      if(Turn(output_cmd)) stage_index_ = 2;
    } 
    else if(stage_index_ == 2) {
      if(StepBack(output_cmd)) stage_index_ = 3;
    } 
    else if(stage_index_ == 3) {
        initLift();
        cout << "get off the 17th floor LIFT" << endl;
    }
    return output_cmd;
  }

  
  bool MissionControl::GoStraight(geometry_msgs::Twist& Output) {
    cout << "=== GoStraight ===" << endl;
    y_coordinate_ = 0;
    RansacFrontWall(scan_points_);
    cout << "length is " << y_coordinate_ << endl;
    geometry_msgs::Twist output_cmd;
    double linear_speed = 0;
    double angular_speed = 0;
    double standard_linear_velocity = 0.2;
    static int state = 0;
    double wait_turn_point;
    double standard_tolerance = 0.1;
    double standard_angular_velocity = 0.2;

    (lift_mode_ == 3) ? wait_turn_point = 3.75 :  wait_turn_point = 3.95;
    if (y_coordinate_ < wait_turn_point && y_coordinate_ > 3) state = 2;
    
    if (state == 0) {
      linear_speed = standard_linear_velocity;
      if (k_wall_< 0) angular_speed = -standard_angular_velocity;
      else angular_speed = standard_angular_velocity;
      if (k_wall_ < standard_tolerance && k_wall_ > -standard_tolerance) state = 1;
    }
    else if (state == 1){
      linear_speed = standard_linear_velocity;
      angular_speed =0;
      if (fabs(k_wall_) > 0.3) state = 0;
    }

    if (state == 2) {
      linear_speed = 0;
      if (k_wall_< 0) angular_speed = -standard_angular_velocity;
      else angular_speed = standard_angular_velocity;
      if (k_wall_ < standard_tolerance && k_wall_ > -standard_tolerance) state = 3;
    }

    goStraightObstacleDete(scan_points_);
    if (collision_distance_ != 0 || std::isnan(y_coordinate_)) {
      linear_speed = 0;
      angular_speed = 0;
    }

    output_cmd.linear.x = linear_speed;
    output_cmd.angular.z = angular_speed;
    cout << "linear_speed is " << linear_speed << endl;
    cout << "angular_speed is " << angular_speed << endl;
    cout << "state is " << state << endl;
    Output = output_cmd;
    if (state == 3) {
      beginturn_ = ros::Time::now();
      state = 0;
      return true;
    }
    else return false;
  }


  bool MissionControl::StepBack(geometry_msgs::Twist& Output) {
    cout << "===StepBack===" << endl;

    DistanceAroundandMirror(scan_points_);
    geometry_msgs::Twist output_cmd;
    double linear_speed = 0;
    double angular_speed = 0;

    linear_speed_ = linear_speed; 
    angular_speed_ = angular_speed;
    Retreat(scan_points_);

    if (angular_speed != angular_speed_) angular_speed = -angular_speed_;
    if (linear_speed != linear_speed_) linear_speed = linear_speed_;

    goStraightObstacleDete(scan_points_);
    if (collision_distance_ != 0) {
      linear_speed = 0;
      angular_speed = 0;
    }
    (lift_mode_ == 4) ? output_cmd.linear.x = linear_speed : output_cmd.linear.x = -linear_speed;

    output_cmd.angular.z = angular_speed;
    cout << "linear_speed is " << output_cmd.linear.x << endl;
    cout << "angular_speed is " << output_cmd.angular.z << endl;
    Output = output_cmd;
    if (lift_mode_ == 4) {
      cout << "h3 = " << h3_ << endl;
      if (h3_ > 6) return true;
    }
    else {
      cout << "h1 = " << h1_ << endl;
      if (h1_ > 6.5) return true;
    }
    return false;
  }

  bool MissionControl::Turn(geometry_msgs::Twist& Output) {
    cout << "=== Turn ===" << endl;
    geometry_msgs::Twist output_cmd;
    ros::Time beginturn = ros::Time::now();
    double linear_speed = 0;
    double angular_speed = 0;
    double standard_linear_velocity = 0.2;
    int state = 0;
    (lift_mode_ == 4) ? angular_speed = -0.38 : angular_speed = 0.38;
    (lift_mode_ == 3) ? linear_speed = 0.9 :  linear_speed = standard_linear_velocity;

    cout << beginturn.toSec() << " , " << beginturn_.toSec() << endl;
    if (fabs(beginturn.toSec() - beginturn_.toSec()) > 4.5) state = 1;
      
    output_cmd.linear.x = linear_speed;
    output_cmd.angular.z = angular_speed;
    cout << "linear_speed is " << linear_speed << endl;
    cout << "angular_speed is " << angular_speed << endl;
    
    Output = output_cmd;
    if(state == 1) return true;
    return false;
  }

  bool MissionControl::EnterLift(geometry_msgs::Twist& Output) {
    cout << "===EnterLift===" << endl;
    y_coordinate_ = 0;
    
    DistanceAroundandMirror(scan_points_);
    DetectObstacles(scan_points_);

    cout << "obstacle = " << angle_left_ << " , " << angle_right_ << endl;
    if (lift_mode_ == 3) {
      if (y_coordinate_ == 0) mirror_num_ = 0;
      else mirror_num_ = 100;
      h1_ = h1_ + 2;
    }
    cout << "mirror_num is " << mirror_num_ << endl;
    cout << "h1_ = " << h1_ << endl;
    cout << "y_coordinate_ = " << y_coordinate_ << endl;
    elevator_wall_ = length_up_ + length_down_ + h3_;
    cout << "elevator_wall_ = " << elevator_wall_ << endl;
    geometry_msgs::Twist output_cmd;
    static int state = 2;
    double linear_speed = 0;
    double angular_speed = 0;
    double standard_linear_velocity = 0.2;
    
    if (state == 0){
      if (h1_ < 2.5){
        if (angle_right_ == 1 || angle_left_ == 1 || ob_lift_ == 1) {
          linear_speed = 0;
          angular_speed = 0;
        }
      }
      else if (h1_ >= 2.5){
        if (angle_right_ == 1 && angle_left_ == 0) {
          angular_speed= -0.2;
          linear_speed = -0;
        }
        else if (angle_left_ == 1 && angle_right_ == 0) {
          angular_speed = 0.2;
          linear_speed = 0;
        }
        else if ((angle_left_ == 1 && angle_right_ == 1) || ob_lift_ == 1){
          angular_speed = 0;
          linear_speed = 0;
        }
      }
      if (angle_right_ == 0 && angle_left_ == 0 && ob_lift_ == 0) {
        linear_speed = standard_linear_velocity;
        angular_speed = 0;
      }
      if (elevator_wall_ < 3) state = 4;
      if (y_coordinate_ == 0 && mirror_num_ <= 15) state = 2;
    }
    else if (state == 2){
      if (y_coordinate_ != 0 && mirror_num_ > 15) state = 0;
      else {
        if (h1_ > 3 && elevator_wall_ > 3) state = 3;
        else {
          linear_speed = 0;
          angular_speed = 0;
        }
      }
      if (elevator_wall_ < 3 || h1_ > 4) state = 4;
    }
    else if (state == 3){
      if (h1_ >= 3.2){
        linear_speed = -standard_linear_velocity;
        angular_speed = 0;

        if (y_coordinate_ != 0 && ob_lift_ == 0 && angle_right_ == 0 && angle_left_ == 0) state = 2;
      } 
      else if (h1_ < 3.2) state = 2;
    }
    else if (state == 4) {
      cout << " special mode " << endl;
      if (angle_right_ == 0 && angle_left_ == 0 && ob_lift_ == 0) {
        angular_speed= 0;
        linear_speed = standard_linear_velocity;
      }
      else if (angle_right_ == 1 && angle_left_ == 0) {
        angular_speed= -0.2;
        linear_speed = -0;
      }
      else if (angle_left_ == 1 && angle_right_ == 0) {
        angular_speed = 0.2;
        linear_speed = 0;
      }
      else if ((angle_left_ == 1 && angle_right_ == 1) || ob_lift_ == 1) {
        angular_speed = 0;
        linear_speed = 0;
        if ((h3_ > 1.1 && h3_ < 1.9) || (h1_ > 4 && h1_ < 4.7)) state = 3;
      }
      if (elevator_wall_ <= 2.3 && elevator_wall_ >= 1.6 && h3_ <= 0.8 && length_up_ + h3_  >= 1.35 && (length_up_ < 0.9 || length_down_ < 0.9) && h3_ < 0.75) state = 1;
      else if (elevator_wall_ <= 1.6 && h3_ < 0.75 && length_up_ + length_down_ < 1.2) state = 1;
      if (h1_ > 4.7 && h3_ < 0.75) state = 1;

      if (elevator_wall_ > 3 && h1_ < 4) state = 2;
    }

    goStraightObstacleDete(scan_points_);
    if (collision_distance_ != 0) {
      linear_speed = 0;
      angular_speed = 0;
    }

    output_cmd.linear.x = linear_speed;
    output_cmd.angular.z = angular_speed;
    cout << "linear_speed is " << linear_speed << endl;
    cout << "angular_speed is " << angular_speed << endl;
    cout << "state is " << state << endl; 
    Output = output_cmd;

    if (state == 1) {
      state = 2;
      return true;
    }
    return false;
  }

  bool MissionControl::GetOffLift(geometry_msgs::Twist& Output) {
    DistanceAroundandMirror(scan_points_);
    cout << "h1 = " << h1_ << endl;
    geometry_msgs::Twist output_cmd;
    double linear_speed = 0;
    double angular_speed = 0;
    double wait_turn_point;
    (lift_mode_ == 4) ? wait_turn_point = 1.25 :  wait_turn_point = 1.6;

    linear_speed_ = linear_speed; 
    angular_speed_ = angular_speed;
    Retreat(scan_points_);

    if (angular_speed != angular_speed_) angular_speed = -angular_speed_;
    if (linear_speed != linear_speed_) linear_speed = linear_speed_;

    goStraightObstacleDete(scan_points_);
    if (collision_distance_ != 0) {
      linear_speed = 0;
      angular_speed = 0;
    }

    output_cmd.linear.x = linear_speed;
    output_cmd.angular.z = angular_speed;
    cout << "linear_speed is " << linear_speed << endl;
    cout << "angular_speed is " << angular_speed << endl;
    Output = output_cmd;

    if (h1_ < wait_turn_point) {
      beginturn_ = ros::Time::now();
      return true;
    }
    return false;
  }

  void MissionControl::RansacFrontWall(sensor_msgs::PointCloud Input) {
    sensor_msgs::PointCloud rightwall_point_selected;
    int cont;
    y_coordinate_ = 0;
    double temp, sequence,value_y;
    double ave_pointcloud_interval[3][20];
    
    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i];
      if (pointis_1.x > 2.8) rightwall_point_selected.points.push_back(pointis_1);
    }

    int size = rightwall_point_selected.points.size();
    int point_cloud_interval = size / 20; 
    for (int j = 0; j < 20; ++j){
      double ave_interval_x = 0,ave_interval_y = 0;
      for (int m = 0; m < point_cloud_interval; ++m)  {
        ave_interval_x = ave_interval_x + rightwall_point_selected.points[point_cloud_interval * j + m].x;
        ave_interval_y = ave_interval_y + rightwall_point_selected.points[point_cloud_interval * j + m].y;
      }
      ave_interval_x = ave_interval_x / point_cloud_interval;
      ave_interval_y = ave_interval_y / point_cloud_interval;
      ave_pointcloud_interval[0][j] = ave_interval_x;
      ave_pointcloud_interval[1][j] = ave_interval_y;
      ave_pointcloud_interval[2][j] = j;
    }

    for (int i = 0; i < 19; ++i){
      for (int j = 0; j < 20 - i; ++j){
        if (ave_pointcloud_interval[0][j + 1] > ave_pointcloud_interval[0][j]){
          temp = ave_pointcloud_interval[0][j];
          ave_pointcloud_interval[0][j] = ave_pointcloud_interval[0][j + 1];
          ave_pointcloud_interval[0][j + 1] = temp;
          value_y = ave_pointcloud_interval[1][j];
          ave_pointcloud_interval[1][j] = ave_pointcloud_interval[1][j + 1];
          ave_pointcloud_interval[1][j + 1] = value_y;
          sequence = ave_pointcloud_interval[2][j];
          ave_pointcloud_interval[2][j] = ave_pointcloud_interval[2][j + 1];
          ave_pointcloud_interval[2][j + 1] = sequence;
        }
      } 
    }

    std::vector<double> x;
    std::vector<double> y;
    x.push_back(ave_pointcloud_interval[0][0]);
    x.push_back(ave_pointcloud_interval[0][1]);
    y.push_back(ave_pointcloud_interval[1][1]);
    y.push_back(ave_pointcloud_interval[1][2]);
    double t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    double a, b;
    a = (ave_pointcloud_interval[1][0] - ave_pointcloud_interval[1][1]) / (ave_pointcloud_interval[0][0] - ave_pointcloud_interval[0][1]);
    b = ave_pointcloud_interval[1][0] - a * ave_pointcloud_interval[0][0];

    for (int i = 2; i < 20; ++i){
      double sigma = 0.15;
      double d = fabs((a * ave_pointcloud_interval[0][i] - ave_pointcloud_interval[1][i] + b) / (hypot(a,-1)));
      if (d <= sigma)  {
        x.push_back(ave_pointcloud_interval[0][i]);
        y.push_back(ave_pointcloud_interval[1][i]);
      }
      else;
    }
        t1 = 0, t2 = 0, t3 = 0, t4 = 0;
        for (int i = 0; i < x.size(); ++i){
          t1 += x[i] * x[i];
          t2 += x[i];
          t3 += x[i] * y[i];
          t4 += y[i];
        }
        a = (t3 * x.size() - t2 * t4) / (t1 * x.size() - t2 * t2);
        b = (t1 * t4 - t2 * t3) / (t1 * x.size() - t2 * t2);
        cout << "y = " << a << "x + " << b << endl;

    k_wall_ = -1 / a;

    y_coordinate_ = ave_pointcloud_interval[0][0];
    if (y_coordinate_ < 2.8 || k_wall_ > 5) collision_distance_ = 2;

    rightwall_point_selected.header.frame_id = "/base_link";
    rightwall_point_selected.header.stamp = ros::Time::now();
    // pointcloud_pub.publish(rightwall_point_selected);

    x.clear();
    y.clear();
  }
   
  void MissionControl::DetectObstacles(sensor_msgs::PointCloud Input) {
    sensor_msgs::PointCloud rightwall_point_selected;
    elevator_wall_ = length_up_ + length_down_ + length_right_;
    angle_right_ = 0;
    angle_left_ = 0;
    double ob_lift_1 = 0,ob_lift_2 = 0,ob_lift_3 = 0,ob_lift_4 = 0,ob_lift_0 = 0,ob_lift_5 = 0;
    ob_lift_ = 0;

    for (int i = 0; i < Input.points.size(); ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i]; 
      if ((pointis_1.x < 0.5 && pointis_1.x > 0 && pointis_1.y < 0.3 && pointis_1.y > 0)) angle_right_ = 1;
      if ((pointis_1.x < 0.5 && pointis_1.x > 0 && pointis_1.y < 0 && pointis_1.y > -0.3)) angle_left_ = 1;
    
      if (pointis_1.x > -0.08 && pointis_1.x < 0.08 && pointis_1.y > 0) length_up_ = fabs(pointis_1.y);
      if (pointis_1.x > -0.08 && pointis_1.x < 0.08 && pointis_1.y < 0) length_down_ = fabs(pointis_1.y);
      if (pointis_1.y > -0.08 && pointis_1.y < 0.08 && pointis_1.x < 0) length_left_ = fabs(pointis_1.x);
    }
    cout << "h3 = " << h3_ << endl; 

    if (h3_ > 1){
      for (int i = 0; i < Input.points.size(); ++i) {
        geometry_msgs::Point32 pointis_lift = Input.points[i];  
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < 0.6 && pointis_lift.y > 0.45))  {
          ob_lift_0 = 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        }
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < 0.45 && pointis_lift.y > 0.25))  {
          ob_lift_1 = 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        }
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < 0.2 && pointis_lift.y > 0))  {
          ob_lift_2= 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        }
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < 0 && pointis_lift.y > -0.2))  {
          ob_lift_3 = 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        }
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < -0.25 && pointis_lift.y > -0.45))  {
          ob_lift_4 = 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        }
        if ((pointis_lift.x < h3_ - 1 && pointis_lift.x > 0 && pointis_lift.y < -0.45 && pointis_lift.y > -0.6))  {
          ob_lift_5 = 1;
          rightwall_point_selected.points.push_back(pointis_lift);
        } 
      }
      cout << "ob_lift is " << ob_lift_0 << " , "<< ob_lift_1 << " , " << ob_lift_2 << " , " << ob_lift_3 << " , " << ob_lift_4 << " , " << ob_lift_5 << endl;
      if ((ob_lift_1 == 1 && ob_lift_2 == 1) || (ob_lift_2 == 1 && ob_lift_3 == 1) || (ob_lift_3 == 1 && ob_lift_4 == 1)) ob_lift_ = 1;
      else ob_lift_ = 0;
      if (ob_lift_0 == 0 || ob_lift_5 == 0) ob_lift_ = 0;
      if ((ob_lift_1 == 0 && ob_lift_2 == 0) || (ob_lift_2 == 0 && ob_lift_3 == 0) || (ob_lift_3 == 0 && ob_lift_4 == 0)) ob_lift_ = 0;
      cout << "ob_lift_ is " << ob_lift_ << endl;
    }
    else ob_lift_ = 0;
  }

  void MissionControl::Retreat(sensor_msgs::PointCloud Input) {
    double ob_lift_2 = 0,ob_lift_3 = 0;

    for (int i = 0; i < Input.points.size(); ++i) {
      geometry_msgs::Point32 pointis_lift = Input.points[i];

      if ((pointis_lift.x < 0 && pointis_lift.x > -0.7 && pointis_lift.y < 0.2 && pointis_lift.y > 0)) {
        ob_lift_2= 1;
      }
      if ((pointis_lift.x < 0 && pointis_lift.x > -0.7 && pointis_lift.y < 0 && pointis_lift.y > -0.2)) {
        ob_lift_3 = 1;
      }
    }
    cout << ob_lift_2 << " , " << ob_lift_3 << endl;

    if ((ob_lift_2 == 1 && ob_lift_3 == 1)) {
      linear_speed_ = 0;
      angular_speed_ = 0;
    }
    else if (ob_lift_3 == 1){
      linear_speed_ = 0;
      angular_speed_ = 0.33;
    }
    else if (ob_lift_2 == 1){
      linear_speed_ = 0;
      angular_speed_ = -0.33;
    }
    else if (ob_lift_2 == 0 && ob_lift_3 == 0) {
      linear_speed_ = -0.2;
      angular_speed_ = 0;
    }
  }

  void MissionControl::DistanceAroundandMirror(sensor_msgs::PointCloud Input) {
    sensor_msgs::PointCloud rightwall_point_selected;
    h1_ = 0;
    h2_ = 0;
    h3_ = 0;
    elevator_wall_ = length_up_ + length_down_ + length_right_;
    cout << "=== DistanceAroundandMirror ===" << endl;
    
    int cont;
    double temp, sequence,value_y;
    double ave_pointcloud_interval[3][20];
    double ave_pointcloud_interval2[2][20];
    double ave_pointcloud_interval3[2][20];

    rightwall_point_selected.points.clear();
    
    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i];
      if (pointis_1.x > 0 && pointis_1.y < 1 && pointis_1.y > -1.5) rightwall_point_selected.points.push_back(pointis_1);
    }

    if (!rightwall_point_selected.points.empty()){
      int size = rightwall_point_selected.points.size();
      int point_cloud_interval = size / 20; 
      for (int j = 0; j < 20; ++j){
        double ave_interval_x = 0,ave_interval_y = 0;
        for (int m = 0; m < point_cloud_interval; ++m)  {
          ave_interval_x = ave_interval_x + rightwall_point_selected.points[point_cloud_interval * j + m].x;
          ave_interval_y = ave_interval_y + rightwall_point_selected.points[point_cloud_interval * j + m].y;
        }
        ave_interval_x = ave_interval_x / point_cloud_interval;
        ave_interval_y = ave_interval_y / point_cloud_interval;
        ave_pointcloud_interval2[0][j] = ave_interval_x;
        ave_pointcloud_interval2[1][j] = ave_interval_y;
      }

      for (int i = 0; i < 20; ++i){
        for (int j = 0; j < 20 - i; ++j){
          if (ave_pointcloud_interval2[0][j + 1] > ave_pointcloud_interval2[0][j]){
            temp = ave_pointcloud_interval2[0][j];
            ave_pointcloud_interval2[0][j] = ave_pointcloud_interval2[0][j + 1];
            ave_pointcloud_interval2[0][j + 1] = temp;
            value_y = ave_pointcloud_interval2[1][j];
            ave_pointcloud_interval2[1][j] = ave_pointcloud_interval2[1][j + 1];
            ave_pointcloud_interval2[1][j + 1] = value_y;
          }
        } 
      }
      h3_ = fabs(ave_pointcloud_interval2[0][0]);
    }
    else h3_ = 0;
    rightwall_point_selected.points.clear();

    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i];
      if (pointis_1.x < 0 && pointis_1.y < 1 && pointis_1.y > -1.5) rightwall_point_selected.points.push_back(pointis_1);
    }

    if (!rightwall_point_selected.points.empty()){
      int size = rightwall_point_selected.points.size();
      int point_cloud_interval = size / 20; 
      for (int j = 0; j < 20; ++j){
        double ave_interval_x = 0,ave_interval_y = 0;
        for (int m = 0; m < point_cloud_interval; ++m)  {
          ave_interval_x = ave_interval_x + rightwall_point_selected.points[point_cloud_interval * j + m].x;
          ave_interval_y = ave_interval_y + rightwall_point_selected.points[point_cloud_interval * j + m].y;
        }
        ave_interval_x = ave_interval_x / point_cloud_interval;
        ave_interval_y = ave_interval_y / point_cloud_interval;
        ave_pointcloud_interval2[0][j] = ave_interval_x;
        ave_pointcloud_interval2[1][j] = ave_interval_y;
      }

      for (int i = 0; i < 20; ++i){
        for (int j = 0; j < 20 - i; ++j){
          if (ave_pointcloud_interval2[0][j + 1] < ave_pointcloud_interval2[0][j]){
            temp = ave_pointcloud_interval2[0][j];
            ave_pointcloud_interval2[0][j] = ave_pointcloud_interval2[0][j + 1];
            ave_pointcloud_interval2[0][j + 1] = temp;
            value_y = ave_pointcloud_interval2[1][j];
            ave_pointcloud_interval2[1][j] = ave_pointcloud_interval2[1][j + 1];
            ave_pointcloud_interval2[1][j + 1] = value_y;
          }
        } 
      }
      h1_ = fabs(ave_pointcloud_interval2[0][0]);
    }
    else h1_ = 0;
    rightwall_point_selected.points.clear();

    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i];
      if (pointis_1.x > 0 && pointis_1.y > -1.5 && pointis_1.y < 1.5 && pointis_1.x < 6.5 - h1_) rightwall_point_selected.points.push_back(pointis_1);
    }

    int size = rightwall_point_selected.points.size();
    int point_cloud_interval = size / 20; 
    for (int j = 0; j < 20; ++j){
      double ave_interval_x = 0,ave_interval_y = 0;
      for (int m = 0; m < point_cloud_interval; ++m)  {
        ave_interval_x = ave_interval_x + rightwall_point_selected.points[point_cloud_interval * j + m].x;
        ave_interval_y = ave_interval_y + rightwall_point_selected.points[point_cloud_interval * j + m].y;
      }
      ave_interval_x = ave_interval_x / point_cloud_interval;
      ave_interval_y = ave_interval_y / point_cloud_interval;
      ave_pointcloud_interval[0][j] = ave_interval_x;
      ave_pointcloud_interval[1][j] = ave_interval_y;
      ave_pointcloud_interval[2][j] = j;
    }

    for (int i = 0; i < 19; ++i){
      for (int j = 0; j < 20 - i; ++j){
        if (ave_pointcloud_interval[0][j + 1] > ave_pointcloud_interval[0][j]){
          temp = ave_pointcloud_interval[0][j];
          ave_pointcloud_interval[0][j] = ave_pointcloud_interval[0][j + 1];
          ave_pointcloud_interval[0][j + 1] = temp;
          value_y = ave_pointcloud_interval[1][j];
          ave_pointcloud_interval[1][j] = ave_pointcloud_interval[1][j + 1];
          ave_pointcloud_interval[1][j + 1] = value_y;
          sequence = ave_pointcloud_interval[2][j];
          ave_pointcloud_interval[2][j] = ave_pointcloud_interval[2][j + 1];
          ave_pointcloud_interval[2][j + 1] = sequence;
        }
      } 
    }
    length_right_ = ave_pointcloud_interval[0][0];
    if (ave_pointcloud_interval[0][1] - ave_pointcloud_interval[0][18] > 1.6) y_coordinate_ = ave_pointcloud_interval[0][0] - ave_pointcloud_interval[0][18];
    else if (ave_pointcloud_interval[0][1] > 2 && ave_pointcloud_interval[0][1] < 2.4 && ave_pointcloud_interval[0][0] - ave_pointcloud_interval[0][19] > 1) y_coordinate_ = 1;

    rightwall_point_selected.points.clear();

    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_1 = Input.points[i];
      if (pointis_1.y < 0) rightwall_point_selected.points.push_back(pointis_1);
    }

    if (!rightwall_point_selected.points.empty()){
      int size = rightwall_point_selected.points.size();
      // for (int i = 0; i < size ; ++i) {
      double  point_cloud_interval = size / 20; 
      for (int j = 0; j < 20; ++j){
        double ave_interval_x = 0,ave_interval_y = 0;
        for (int m = 0; m < point_cloud_interval; ++m)  {
          ave_interval_x = ave_interval_x + rightwall_point_selected.points[point_cloud_interval * j + m].x;
          ave_interval_y = ave_interval_y + rightwall_point_selected.points[point_cloud_interval * j + m].y;
        }
        ave_interval_x = ave_interval_x / point_cloud_interval;
        ave_interval_y = ave_interval_y / point_cloud_interval;
        ave_pointcloud_interval3[0][j] = ave_interval_x;
        ave_pointcloud_interval3[1][j] = ave_interval_y;
      }

      for (int i = 0; i < 20; ++i){
        for (int j = 0; j < 20 - i; ++j){
          if (ave_pointcloud_interval3[1][j + 1] < ave_pointcloud_interval3[1][j]){
            temp = ave_pointcloud_interval3[0][j];
            ave_pointcloud_interval3[0][j] = ave_pointcloud_interval3[0][j + 1];
            ave_pointcloud_interval3[0][j + 1] = temp;
            value_y = ave_pointcloud_interval3[1][j];
            ave_pointcloud_interval3[1][j] = ave_pointcloud_interval3[1][j + 1];
            ave_pointcloud_interval3[1][j + 1] = value_y;
          }
        } 
      }
      h2_ = fabs(ave_pointcloud_interval3[1][0]);
    }
    else h2_ = 0;

    sensor_msgs::PointCloud mirror_point_selected;

    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis_0 = Input.points[i];
      if (pointis_0.x < 7 - h1_ && pointis_0.x > 5 - h1_  && pointis_0.y < 4.1 - h2_ && pointis_0.y > -(h2_ - 2)) mirror_point_selected.points.push_back(pointis_0);
    }
    mirror_num_ = mirror_point_selected.points.size();

    rightwall_point_selected.header.frame_id = "/base_link";
    rightwall_point_selected.header.stamp = ros::Time::now();
    // pointcloud_pub.publish(rightwall_point_selected);
  }

  void MissionControl::goStraightObstacleDete(sensor_msgs::PointCloud Input) {
    collision_distance_ = 0;
    for (int i = 0; i < Input.points.size() ; ++i) {
      geometry_msgs::Point32 pointis = Input.points[i];
      double length = hypot(pointis.x,pointis.y);
      if (lift_mode_ == 4) {
        if (pointis.y < 0.25 && pointis.y > -0.25 && pointis.x < 0 && pointis.x > -0.5) {
          cout << "pointis " << pointis.x << " , " << pointis.y << endl;
          collision_distance_ = 1;
        }
      }
      else{
        if (pointis.y < 0.25 && pointis.y > -0.25 && pointis.x < 0.1 && pointis.x > -0.5) {
          cout << "pointis " << pointis.x << " , " << pointis.y << endl;
          collision_distance_ = 1;
        }
      }
    }
    if (collision_distance_ != 0) cout << "WARMING (* O *) !!!" << endl;
  }



