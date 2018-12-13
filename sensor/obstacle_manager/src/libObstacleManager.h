#ifndef LIB_OBSTACLEMANAGER_H
#define LIB_OBSTACLEMANAGER_H

#include <ros/ros.h>
#include <iostream>
#include <cmath>
#include <vector>
#include <time.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/transform_broadcaster.h>


#include <sensor_msgs/PointCloud.h>
#include <geometry_msgs/Point32.h>

using std::vector;
using std::cout;
using std::endl;

const int ROS_RATE_HZ   = 20;

const vector<double> R11 = {4.619579111799e-01, 1.120235603441e-01, 8.797986191318e-01};
const vector<double> R12 = {-8.847258557293e-01, -1.123975717332e-02, 4.659762097605e-01};
const vector<double> R13 = {6.208903689347e-02, -9.936419827013e-01, 9.391784554130e-02};
const vector<double> T1  = {6.566721252215e-02, 1.007875013048e-01, -3.012084046145e-01 };
//const vector<double> T1  = {2.566721252215e-02, 1.607875013048e-01, -3.012084046145e-01 };

const vector<double> R21 = {0.911020853031573, 0.00519415403124045, 0.412327571362292};
const vector<double> R22 = {-0.00168139473597834, 0.999959143617621, -0.00888166691871711};
const vector<double> R23 = {-0.412356857895401, 0.00739809836464467, 0.910992365438492};
const vector<double> T2  = {-10.6632863659008, 12.5020142707178, 2.33286297823336};

const vector<double> R31 = {0.911020853031573,-0.00168139473597833,-0.412356857895401};
const vector<double> R32 = {0.00519415403124034,0.999959143617621,0.00739809836464456};
const vector<double> R33 = {0.412327571362291,-0.00888166691871699,0.910992365438493};
const vector<double> T3  = {10.6632863659008,-12.5020142707178,-2.33286297823336};


class ObstacleManager
{
public:
	ObstacleManager();
	~ObstacleManager();

	void Manager();
	void Mission();

private:
	/** Node Handles **/
	ros::NodeHandle n;
	ros::NodeHandle pn;

	/** Publishers **/
	ros::Publisher cam_obs_pub;

	/** Subscribers **/
	ros::Subscriber cam_obs_sub;

	tf2_ros::TransformBroadcaster br1_;
	tf2_ros::TransformBroadcaster br2_;
	tf2_ros::TransformBroadcaster br3_;

	sensor_msgs::PointCloud camera_obs_points_;
	bool isBuild_;

	void tfPublish();
	void obstaclePublish();

	void cam_obs_pub_callback(const sensor_msgs::PointCloud::ConstPtr& input) {
		camera_obs_points_.points.clear();
		geometry_msgs::Point32 point;
		double point_step = 0.05;

  	camera_obs_points_.header.frame_id  = input->header.frame_id;

  	if(isBuild_) {
  		isBuild_ = false;
			for (int i = 0; i < input->points.size() - 2; i+=3) {
				if(fabs(input->points[i+1].x-input->points[i+2].x) > 2) continue;
				if(fabs(input->points[i+1].y-input->points[i+2].y) > 2) continue;

				for (double r = input->points[i+1].x; r < input->points[i+2].x; r+=point_step) {
					for (double c = input->points[i+1].y; c < input->points[i+2].y; c+=point_step) {
						point.x = r;
						point.y = c;
						point.z = input->points[i].z;
						camera_obs_points_.points.push_back(point);
					}
				}			
			}
			isBuild_ = true;
		}

	}
	
};


#endif

		/*
		#calib_time: Mon Dec 10 04:13:16 2018
		#description: use R/T/S to transform campoint c into laser coordinates l=R*S*c+T

		R: 
		4.619579111799e-01 1.120235603441e-01 8.797986191318e-01 
		-8.847258557293e-01 -1.123975717332e-02 4.659762097605e-01 
		6.208903689347e-02 -9.936419827013e-01 9.391784554130e-02

		T: 
		2.566721252215e-02 1.607875013048e-01 -3.012084046145e-01

		S: 
		1.000000000000e+00



		roll  tan^-1 (32/33)
		pitch tan^-1 ( -31/sqrt(32^2+33^))
		yaw   tan^-1 (21/11)

		

		double LeMat1[9] = { 
			0.911020853031573, 0.00519415403124045, 0.412327571362292,
			-0.00168139473597834, 0.999959143617621, -0.00888166691871711,
			-0.412356857895401, 0.00739809836464467, 0.910992365438492 };  //B 
		double LeVector1[4] = { -10.6632863659008, 12.5020142707178, 2.33286297823336 ,1000 };


	"RightRotationMatrix": [0.911020853031573,-0.00168139473597833,-0.412356857895401,
	0.00519415403124034,0.999959143617621,0.00739809836464456,
	0.412327571362291,-0.00888166691871699,0.910992365438493],
	"RightTranslationVector": [10.6632863659008,-12.5020142707178,-2.33286297823336],



		*/
