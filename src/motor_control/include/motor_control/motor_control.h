#include <ros/ros.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2_ros/transform_broadcaster.h>
#include <geometry_msgs/TransformStamped.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>
#include <vector>
#include <chrono>
#include <std_msgs/UInt8.h>
#include <motor_control/motor_commond.h>
#include <motor_control/paramConfig.h>
#include <memory>

#include <serial/serial.h>

using namespace ros;
using namespace std;
using namespace tf2_ros;

class MotorControl
{
public:
	MotorControl();
	void exec();
	void twist_callback(const geometry_msgs::Twist::ConstPtr& msg);
  void twist_joy_callback(const geometry_msgs::Twist::ConstPtr& msg);
	virtual ~MotorControl();

protected:
private:
  serial::Serial ros_ser;
  void serialWrite(unsigned char *data, size_t size);
  bool serialRead(unsigned char *data, size_t size);

  bool motor_in_control{false};
	ros::NodeHandle node;
	ros::Publisher odom_pub_;
  double velocity_left_latest, velocity_right_latest; //最近一次所更新的数据
  int position_left_latest, position_right_latest;

  int position_left_handle, position_right_handle;//倒数第二次记录的数据实际处理的数据

  int RPM_MAX;
  double wheel_length;
  int ratio;//减速比
	double model_param;//理论上是左右两轮的间距
  bool output_tf;
  bool is_publish_odom;
  int wheelcount;
  double round_per_meter;
  const double pulse_per_round = 10000;//pulse per round
  std::string odom_frame_{"odom"}, base_frame_{"base_link"};
  double accumulation_x_{0}, accumulation_y_{0}, accumulation_th_{0};
  geometry_msgs::TransformStamped transformStamped_;
  tf2_ros::TransformBroadcaster br_;
  nav_msgs::Odometry odom_;

	geometry_msgs::Twist current_twist_;
	void send_speed_callback();
  void send_speed_callback(const ros::TimerEvent&);
  void get_odometry_callback(const ros::TimerEvent&);
  bool commondCallback(motor_control::motor_commond::Request & request, motor_control::motor_commond::Response & response);
  void dynamic_callback(motor_control::paramConfig &config, uint32_t level);
  void publish_odom();
  bool canMove_base{true};
  bool handle_feedback(unsigned char *data);
  bool can_send_velocity(unsigned char address, int velocity);
  bool can_send_getvelocity(unsigned char address);
  bool can_send_getposition(unsigned char address);
  bool can_send_enable(unsigned char address);
  bool can_send_disable(unsigned char address);
  bool can_send_clear_error(unsigned char address);
  double can_read_velocity(unsigned char address);
};