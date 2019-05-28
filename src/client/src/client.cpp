#include <ros/ros.h>
#include <geometry_msgs/Pose.h>
#include <client/commond.h>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
#include <thread>

#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <vector>

#include <move_base_msgs/MoveBaseAction.h>
#include <actionlib/client/simple_action_client.h>
#include <tf/transform_listener.h>

#include "tf/transform_datatypes.h"
#include <tf2/utils.h>

using namespace std;

struct Pose
{
    Pose():x(0), y(0), yaw(0), type(-1){}
    double x;
    double y;
    double yaw;
    int type;//0:起点 1:终点 -1:普通
};


class Client
{
public:

    typedef actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> MoveBaseClient;

    Client();
    ros::NodeHandle node;
    void exec();
    void init_socket();
    void readCommond();
    void gps_callback(const geometry_msgs::Pose::ConstPtr& msg);
    bool commondCallback(client::commond::Request & request, client::commond::Response & response);
    void get_pose_callback();
    void navigation();

    void send_started();
    void send_reachStartPoint();
    void send_realTimePoint(const ros::TimerEvent&);
    void send_reachEndPoint();

    ~Client();

private:
    int sockfd = -1;
    string serveraddrstr;
    string serverportstr;

    string startPoint_longitude;
    string startPoint_latitude;
    string endPoint_longitude;
    string endPoint_latitude;
    string realTimePoint_longitude;
    string realTimePoint_latitude;
    string carId;

    int getRemainTime();
    void dealCommond(int type);
    enum status_T
    {START, END, STARTTOEND, ENDTOSTART, STOP, PARKING, PARKINGTOSTART};
    status_T status{PARKING};

    vector<Pose> pathVector;
    void createPath();
    int moveSinglePath(vector<Pose> &singlePath);
    MoveBaseClient *ac{nullptr};

    tf::StampedTransform base_link_pose;
    double pi = 3.1415;
    bool isSendMsgToServer;
    double remainTimeInMin;
};
int Client::getRemainTime()
{
    return static_cast<int>(remainTimeInMin);
}
void Client::send_started()
{
    if(!isSendMsgToServer)
        return;
    json commond;
    commond["position"]["longitude"] = startPoint_longitude;
    commond["position"]["latitude"] = startPoint_latitude;
    commond["location"] = "started";
    commond["communicationId"] = carId;
    commond["arriveTime"] = std::to_string(getRemainTime());

    string commondString = commond.dump();
    commondString = "$" + commondString + "$";
    write(sockfd, commondString.c_str(), commondString.size());
    ROS_INFO_STREAM(commond);
}

void Client::send_reachStartPoint()
{
    if(!isSendMsgToServer)
        return;
    json commond;
    commond["position"]["longitude"] = startPoint_longitude;
    commond["position"]["latitude"] = startPoint_latitude;
    commond["location"] = "startPoint";
    commond["communicationId"] = carId;
    commond["arriveTime"] = std::to_string(getRemainTime());

    string commondString = commond.dump();
    commondString = "$" + commondString + "$";
    write(sockfd, commondString.c_str(), commondString.size());
    ROS_INFO_STREAM(commond);
}
void Client::send_realTimePoint(const ros::TimerEvent&)
{
    if((status != STARTTOEND) || (status != ENDTOSTART))
        return;
    if(!isSendMsgToServer)
        return;
    json commond;
    commond["position"]["longitude"] = realTimePoint_longitude;
    commond["position"]["latitude"] = realTimePoint_latitude;
    commond["location"] = "running";
    commond["communicationId"] = carId;
    commond["arriveTime"] = std::to_string(getRemainTime());

    string commondString = commond.dump();
    commondString = "$" + commondString + "$";
    write(sockfd, commondString.c_str(), commondString.size());
    ROS_INFO_STREAM(commond);
}
void Client::send_reachEndPoint()
{
    if(!isSendMsgToServer)
        return;
    json commond;
    commond["position"]["longitude"] = endPoint_longitude;
    commond["position"]["latitude"] = endPoint_latitude;
    commond["location"] = "endPoint";
    commond["communicationId"] = carId;

    string commondString = commond.dump();
    commondString = "$" + commondString + "$";
    write(sockfd, commondString.c_str(), commondString.size());
    ROS_INFO_STREAM(commond);
}
void Client::dealCommond(int type)
{
    if(type == 0)//到达start位置
    {
        send_reachStartPoint();
        status = START;
    }
    else if(type == 1)//出发
    {
        if(status == START || status == STOP)
        {
            status = STARTTOEND;
        }
    }
    else if(type == 2) //返回
    {
        if(status == END || status == STOP)
        {
           status = ENDTOSTART;
        }
    }
    else if(type == 3)//导航停止
    {
        if(status == STARTTOEND || status == ENDTOSTART)
        {
            status = STOP;
        }
    }
}
void Client::get_pose_callback()
{   
    tf::TransformListener listener;
    while(true)
    {
        try{
            listener.waitForTransform("map", "base_link", ros::Time(0), ros::Duration(10.0));
            listener.lookupTransform("map", "base_link", ros::Time(0), base_link_pose);
                                //ros::Time(0)表示最近的一帧坐标变换，不能写成ros::Time::now()
        }
        catch (tf::TransformException &ex) {
            ROS_ERROR("%s",ex.what());
        }

        double roll, pitch, yaw;
        tf::Matrix3x3(base_link_pose.getRotation()).getRPY(roll, pitch, yaw);

        //ROS_INFO_STREAM("x="<<base_link_pose.getOrigin().x()<<",y="<<base_link_pose.getOrigin().y() << ", yaw=" << yaw);

        ros::Duration(0.1).sleep();
    }
}

int Client::moveSinglePath(vector<Pose> &singlePath)
{
	int goalIndex = 0;
	while(1 && (status == STARTTOEND || status == ENDTOSTART))
	{
        //判断车是否在起点(看车头朝向)  朝向大于90度时需要增加一个位置调整角度
        double roll, pitch, yaw;
        tf::Matrix3x3(base_link_pose.getRotation()).getRPY(roll, pitch, yaw);

        double diff_abs = std::abs(yaw - singlePath[goalIndex].yaw);

        // if((diff_abs > pi/2) && (diff_abs < pi*3/2))
        // {
        //     Pose pose;
        //     pose.x = base_link_pose.getOrigin().x();
        //     pose.y = base_link_pose.getOrigin().y();

        //     double diff = yaw - singlePath[goalIndex].yaw;
            
        //     if(((diff <= 0) && (diff >= -1 * pi)) || ((diff <= 2 * pi) && (diff >= pi)))//当前位置在目标的右边 需要左转  +pi/2
        //     {
        //         pose.yaw = (yaw < pi/2) ? (yaw + pi/2) : (yaw + pi/2 - 2*pi);
        //     }
        //     else//当前位置在目标的左边 需要右转  -pi/2
        //     {
        //         pose.yaw = (yaw > -1*pi/2) ? (yaw - pi/2) : (yaw - pi/2 + 2*pi);
        //     }

        //     auto i = singlePath.begin() + goalIndex;
        //     singlePath.insert(i, pose);
        // }

        //设置目标
	    move_base_msgs::MoveBaseGoal goal;
	    goal.target_pose.header.frame_id = "map";
	    goal.target_pose.header.stamp = ros::Time::now();
	    goal.target_pose.pose.position.x = singlePath[goalIndex].x;
	    goal.target_pose.pose.position.y = singlePath[goalIndex].y;
	    goal.target_pose.pose.orientation = tf::createQuaternionMsgFromYaw(singlePath[goalIndex].yaw);
	    ROS_INFO("Sending goal  x=%f, y=%f, yaw=%f", singlePath[goalIndex].x,singlePath[goalIndex].y, singlePath[goalIndex].yaw);
	    while(1)
	    {
	        ac->sendGoal(goal);
	        // Wait for the action to return
	        while(!ac->waitForResult(ros::Duration(1.0)))//一直检测当前的状态
	        {
                if(status == STOP)
                {
                    //取消导航
                    ac->cancelGoal();
                    while(status == STOP)
                    {
                        ros::Duration(1.0).sleep();
                        ROS_INFO("stopping");
                    }
                }
                double roll, pitch, yaw;
                tf::Matrix3x3(base_link_pose.getRotation()).getRPY(roll, pitch, yaw);

                //计算剩余时间
                double remainDistance = 0;
                remainDistance += std::sqrt((base_link_pose.getOrigin().x() - singlePath[goalIndex].x) * (base_link_pose.getOrigin().x() - singlePath[goalIndex].x)
                                      + (base_link_pose.getOrigin().y() - singlePath[goalIndex].y) * (base_link_pose.getOrigin().y() - singlePath[goalIndex].y));
                for(int goalPoint = goalIndex; goalPoint < singlePath.size() - 1; goalPoint++)
                {
                    remainDistance += std::sqrt((singlePath[goalIndex + 1].x - singlePath[goalIndex].x) * (singlePath[goalIndex + 1].x - singlePath[goalIndex].x)
                                      + (singlePath[goalIndex + 1].y - singlePath[goalIndex].y) * (singlePath[goalIndex + 1].y - singlePath[goalIndex].y));
                }
                remainTimeInMin = remainDistance / 0.5 / 60;

                ROS_INFO_STREAM("running" << "x:" << base_link_pose.getOrigin().x() << " y:" << base_link_pose.getOrigin().y() << " yaw:" << yaw << " remainTime: " << remainDistance / 0.5);
	        }
	        ROS_INFO("finish one point");

	        if(ac->getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
	        {
	            ROS_INFO("reach one point");
	            if(goalIndex == singlePath.size() - 1)//如果是走到了最后一个点,发送消息到后台
	            {
                    return 0;
	            }
	            else //下一个点
	            {
	                goalIndex++;
	                break;
	            }
	        }
	    }
	}
}

void Client::navigation()
{
    status_T status_last;
    status_T status_current;

    while(1)
    {
        if(status == STARTTOEND)
        {
            vector<Pose> singlePathVector;
            for(int i = 1; i < pathVector.size(); i++)
      		{
      			singlePathVector.push_back(pathVector[i]);
      			if(pathVector[i].type == 1)//该点为终点，退出循环
      				break;
      		}
            
 			//开始行走
            if(moveSinglePath(singlePathVector) == 0)
            {
                ROS_INFO("reach end and change status to end");
                status = END;
                send_reachEndPoint();
            }
        }


        if(status == ENDTOSTART)
        {
        	vector<Pose> singlePathVector;
            int endpoint = 0;
            for(int i = 0; i < pathVector.size(); i++)
      		{
                if(pathVector[i].type == 1)//如果是终点
                {
                    endpoint = i;
                    break;
                }
      		}
            
            for(int i = endpoint + 1; i < pathVector.size(); i++)
            {
                singlePathVector.push_back(pathVector[i]);
                if(pathVector[i].type == 0)//该点为起点，退出循环
                    break;
            }

 			//开始行走
            if(moveSinglePath(singlePathVector) == 0)
            {
                ROS_INFO("reach start and change status to start");
                status = START;
                send_reachStartPoint();
            }

        }
        ros::Duration(0.1).sleep();
    }
}

void Client::createPath()
{
    json path_json;
    string path_file;
    ros::param::get("~path_file", path_file);
    std::ifstream i;
    i.open(path_file);
    i >> path_json;
    int number = path_json["number"];
    for(int i = 0; i < number; i++)
    {
        Pose pose;
        pose.x = path_json["pose"][i]["x"];
        pose.y = path_json["pose"][i]["y"];
        pose.yaw = path_json["pose"][i]["yaw"];
        pose.type = path_json["pose"][i]["type"];
        pathVector.push_back(pose);
        ROS_INFO("%f, %f, %f, %d  ", pose.x, pose.y, pose.yaw, pose.type);
    }

}
bool Client::commondCallback(client::commond::Request & request, client::commond::Response & response)
{
    ROS_INFO("here  %d", request.type);
    dealCommond(request.type);
}

Client::Client()
{
    ros::param::get("~ipaddress", serveraddrstr);
    ros::param::get("~port", serverportstr);
    ros::param::get("~online", isSendMsgToServer);
    ros::param::get("~carId", carId);

    realTimePoint_longitude = "";
    realTimePoint_latitude = "";
}
void Client::init_socket()
{
    /*1.创建socket*/
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0)
    {
		perror("socket error");
		exit(1);
    }
    
    /*向serveraddr中填入服务器端ip、port和地址族类型(IPV4)*/
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(atoi(serverportstr.data()));
    //将IP地址转换成网络字节序后填入serveraddr中
    inet_pton(AF_INET, serveraddrstr.data(), &serveraddr.sin_addr.s_addr);
 
    /*2.客户端调用connect函数连接到服务器端*/
    if(connect(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
    {
        ROS_INFO("connect server fail!");
    }
}
void Client::readCommond()
{
    char buf[1024] = {0};
    int size;
    while((size = read(sockfd, buf, sizeof(buf))) > 0)
    {
        try{
            json commond = json::parse(buf);
            ROS_INFO_STREAM(commond);
            if(commond["communicationId"] != carId)
                continue;
            auto commandType = commond["commandType"];
            if(commandType == "connection")
            {
                startPoint_longitude = commond["startPoint"]["longitude"];
                startPoint_latitude = commond["startPoint"]["latitude"];
                endPoint_longitude = commond["endPoint"]["longitude"];
                endPoint_latitude = commond["endPoint"]["latitude"];

                //起点终点停车点先忽略，本地已知
                //车辆需要走到起点，到达起点后  通过手柄控制发送 location = startPoint的消息体
                ROS_INFO("car has connect to server, running to start");
                send_reachStartPoint();
                status = START;
            }
            else if(commandType == "start")
            {
                //开始向终点导航
                if(status == START || status == STOP)
                {
                    status = STARTTOEND;
                }
                send_started();
                ROS_INFO("running to end");
            }
            else if(commandType == "back")
            {
                //返回到起点
                if(status == END || status == STOP)
                {
                    status = ENDTOSTART;
                }
                ROS_INFO("running to start");
            }
            else if(commandType == "parking")
            {
                //暂时不处理
            }

            memset(buf, 0, sizeof(buf));
        }
        catch (json::exception& e)
        {
            // output exception information
            ROS_INFO_STREAM("parse failure");
            continue;
        }
    }
}
void Client::gps_callback(const geometry_msgs::Pose::ConstPtr& msg)
{
    double latitude = std::stod(startPoint_latitude);
    double longitude = std::stod(startPoint_longitude);
    
    if(std::abs(msg->position.x - latitude ) > 1 || std::abs(msg->position.y - longitude ) > 1)
    {
        realTimePoint_latitude = startPoint_latitude;
        realTimePoint_longitude = startPoint_longitude;
    }
    else
    {
        realTimePoint_latitude = std::to_string(msg->position.x);
        realTimePoint_longitude = std::to_string(msg->position.y);
    }
}

void Client::exec()
{
    createPath();

    ac = new MoveBaseClient("move_base", true);

    ac->waitForServer();
    init_socket();

    string startCode = "$@@@@" + carId + "$";
    const char* startCode_ = startCode.c_str();
    write(sockfd, startCode_, strlen(startCode_));

    std::thread receive_thread(&Client::readCommond, this);
    receive_thread.detach();

    std::thread navigation_thread(&Client::navigation, this);
    navigation_thread.detach();

    std::thread pose_thread(&Client::get_pose_callback, this);
    pose_thread.detach();

    ros::Subscriber cmd_sub = node.subscribe<geometry_msgs::Pose>("/gps", 10, &Client::gps_callback, this);
    ros::ServiceServer service = node.advertiseService("commond", &Client::commondCallback, this);
    ros::Timer sendRealTimePointTimer = node.createTimer(ros::Duration(5), &Client::send_realTimePoint, this);
    ros::spin();
}

Client::~Client()
{
    close(sockfd);
}
int main(int argc, char **argv)
{
    ros::init(argc, argv, "client");
    Client client;
    client.exec();
    return 0;
}