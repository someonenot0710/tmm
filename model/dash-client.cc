/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2014 TEI of Western Macedonia, Greece
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Dimitrios J. Vergados <djvergad@gmail.com>
 */

#include <ns3/log.h>
#include <ns3/uinteger.h>
#include <ns3/tcp-socket-factory.h>
#include <ns3/simulator.h>
#include <ns3/inet-socket-address.h>
#include <ns3/inet6-socket-address.h>
#include "http-header.h"
#include "dash-client.h"
#include "para.h"
#include <cmath>
#include <fstream>
#include <stdlib.h>
#include <sstream>
#include <iomanip>

NS_LOG_COMPONENT_DEFINE("DashClient");
using namespace std;

vector <int> video_num; //Jerry
vector<vector<int>> video_series;
int init_buffer;
int seg_length;
vector<vector<vector<float>>> probs;
vector<vector<double>> brs;
vector<vector<double>> dists;
vector<vector<vector<double>>> video_size;
vector<vector<vector<float>>> pred_info;
vector<vector<vector<float>>> gt_info;
vector<string> video_names;
vector<vector<int>> user_info;
std::string tile_col, tile_row;
std::string experiment_dir;
std::string feature_dir;
std::string video_info_dir="";
std::string prob_dir="";
std::string prob_overall_dir="";
std::string user_info_file="";
std::string area_dir="";
float rho;
int M;
int tile_num;
std::string cur_pre; //Jerry

namespace ns3
{

  NS_OBJECT_ENSURE_REGISTERED(DashClient);

  int DashClient::m_countObjs = 0;

  TypeId
  DashClient::GetTypeId(void)
  {
    static TypeId tid =
        TypeId("ns3::DashClient").SetParent<Application>().AddConstructor<
            DashClient>().AddAttribute("VideoId",
            "The Id of the video that is played.", UintegerValue(0),
            MakeUintegerAccessor(&DashClient::m_videoId),
            MakeUintegerChecker<uint32_t>(1)).AddAttribute("Remote",
            "The address of the destination", AddressValue(),
            MakeAddressAccessor(&DashClient::m_peer), MakeAddressChecker()).AddAttribute(
            "Protocol", "The type of TCP protocol to use.",
            TypeIdValue(TcpSocketFactory::GetTypeId()),
            MakeTypeIdAccessor(&DashClient::m_tid), MakeTypeIdChecker()).AddAttribute(
            "TargetDt", "The target buffering time", TimeValue(Time("35s")),
            MakeTimeAccessor(&DashClient::m_target_dt), MakeTimeChecker()).AddAttribute(
            "window", "The window for measuring the average throughput (Time)",
            TimeValue(Time("10s")), MakeTimeAccessor(&DashClient::m_window),
            MakeTimeChecker()

            ).AddTraceSource("Tx", "A new packet is created and is sent",
            MakeTraceSourceAccessor(&DashClient::m_txTrace));
    return tid;
  }

  DashClient::DashClient() :
      m_rateChanges(0), m_target_dt("35s"), m_bitrateEstimate(0.0), m_segmentId(
          0), m_socket(0), m_connected(false), m_totBytes(0), m_startedReceiving(
          Seconds(0)), m_sumDt(Seconds(0)), m_lastDt(Seconds(-1)), m_id(
          m_countObjs++), m_requestTime("0s"), m_segment_bytes(0), m_bitRate(
          50000), m_window(Seconds(10)), m_segmentFetchTime(Seconds(0))
  {
    NS_LOG_FUNCTION(this);
    m_parser.SetApp(this); // So the parser knows where to send the received messages
  }

  DashClient::~DashClient()
  {
    NS_LOG_FUNCTION(this);
  }

  Ptr<Socket>
  DashClient::GetSocket(void) const
  {
    NS_LOG_FUNCTION(this);
    return m_socket;
  }

  void
  DashClient::DoDispose(void)
  {
    NS_LOG_FUNCTION(this);

    m_socket = 0;
    // chain up
    Application::DoDispose();
  }

// Application Methods
  void
  DashClient::StartApplication(void) // Called at time specified by Start
  {
    NS_LOG_FUNCTION(this);

    // Create the socket if not already

    if (!m_socket)
      {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);

        // Fatal error if socket type is not NS3_SOCK_STREAM or NS3_SOCK_SEQPACKET
        if (m_socket->GetSocketType() != Socket::NS3_SOCK_STREAM
            && m_socket->GetSocketType() != Socket::NS3_SOCK_SEQPACKET)
          {
            NS_FATAL_ERROR("Using HTTP with an incompatible socket type. "
                "HTTP requires SOCK_STREAM or SOCK_SEQPACKET. "
                "In other words, use TCP instead of UDP.");
          }

        if (Inet6SocketAddress::IsMatchingType(m_peer))
          {
            m_socket->Bind6();
          }
        else if (InetSocketAddress::IsMatchingType(m_peer))
          {
            m_socket->Bind();
          }

        m_socket->Connect(m_peer);
        m_socket->SetRecvCallback(MakeCallback(&DashClient::HandleRead, this));
        m_socket->SetConnectCallback(
            MakeCallback(&DashClient::ConnectionSucceeded, this),
            MakeCallback(&DashClient::ConnectionFailed, this));
        m_socket->SetSendCallback(MakeCallback(&DashClient::DataSend, this));
      }
  }

  void
  DashClient::StopApplication(void) // Called at time specified by Stop
  {
    NS_LOG_FUNCTION(this);

    if (m_socket != 0)
      {
        m_socket->Close();
        m_connected = false;
        m_player.m_state = MPEG_PLAYER_DONE;
      }
    else
      {
        NS_LOG_WARN("DashClient found null socket to close in StopApplication");
      }
  }

// Private helpers
  void
  DashClient::RequestSegment()
  {
    NS_LOG_FUNCTION(this);

    if (m_connected == false)
      {
        return;
      }

    //yibin
	double sum_bytes=0;
    cout<<m_id<<": "<<m_segmentId<<endl;
    std::stringstream s_mid;
    s_mid << m_id;
 //   char cmd[300];
    std::stringstream tmpu;
    tmpu << std::setw(2) << std::setfill('0') << user_info[m_id][1];
    std::string cur_user = tmpu.str();
    std::string u;
    u="user"+cur_user;
	int i;
/*
    std::string prob_file;
	prob_file=experiment_dir+"prob_"+s_mid.str();
	// python predict.py $feature_dir $v user$(printf %02d $u) $n prob
    sprintf(cmd,"python scripts/predict.py %s %s %s %d %s",feature_dir.c_str(),video_names[user_info[m_id][0]].c_str(),u.c_str(),(m_segmentId+1)*30,prob_file.c_str());
    system(cmd);
    ifstream prob(prob_file);
    vector<float> tmp_probs;
    std::string tmp_prob;
	int tile_no=0;
    while(prob >> tmp_prob){
//		cout<<tmp_prob<<endl;
		float p=stof(tmp_prob.c_str());
        tmp_probs.push_back(p);
		// check threshold
		if (p>=rho){
			sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][tile_no];
		}
		tile_no++;
    }
    prob.close();
    probs[m_id][m_segmentId]=tmp_probs;*/
	// header files
	if(cur_pre=="pre"){
	if(m_segmentId==0){
		sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][0];
		sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][1];
	}else if(m_segmentId==1){ // first segment -> no predicted prob. -> using ground truth
		for(i=0;i<tile_num; i++){
			if(gt_info[m_id][(m_segmentId-1)][i]>=rho){ //0 or rho?
                sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][i];
            }
		}
		probs[m_id][m_segmentId]=gt_info[m_id][(m_segmentId-1)];
	}else{ // has predicted prob.
		for(i=0; i<tile_num; i++){
			if(pred_info[m_id][(m_segmentId-2)][i]>=rho){
				sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][i];
			}
		}
		probs[m_id][m_segmentId]=pred_info[m_id][(m_segmentId-2)];
	}
	}

	if (cur_pre=="cur"){
        if(m_segmentId==0){
                sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][0];
                sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][1];
        }else if(m_segmentId==1){ // first segment -> no predicted prob. -> using ground truth
                for(i=0;i<tile_num; i++){
                        if(gt_info[m_id][(m_segmentId-1)][i]>=rho){ //0 or rho?
                sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][i];
            }
                }
                probs[m_id][m_segmentId]=gt_info[m_id][(m_segmentId-1)];
        }else{ // has predicted prob.
                for(i=0; i<tile_num; i++){
                        if(gt_info[m_id][(m_segmentId-2)][i]>=rho){
                                sum_bytes+=video_size[user_info[m_id][0]][m_segmentId][i];
                        }
                }
                probs[m_id][m_segmentId]=gt_info[m_id][(m_segmentId-2)];
        }
	}

    sum_bytes =  sum_bytes/8.0;
    sum_bytes = sum_bytes/1288.0;

    int packet_number=0;
    if (sum_bytes-int(sum_bytes) ==0)
		packet_number = int(sum_bytes);
    else
	    packet_number = int(sum_bytes)+1;
    
    //yibin
    video_series[m_id][m_segmentId]=packet_number;

    Ptr<Packet> packet = Create<Packet>(100);
    HTTPHeader httpHeader;
    httpHeader.SetSeq(1);
    httpHeader.SetMessageType(HTTP_REQUEST);
    httpHeader.SetVideoId(m_videoId);
    httpHeader.SetResolution(m_bitRate);
    httpHeader.SetSegmentId(m_segmentId++);
    httpHeader.SetPacketNum(packet_number); //tmm video_series[m_id][v_num]
    packet->AddHeader(httpHeader);
   
   
    int res = 0;
    if (((unsigned) (res = m_socket->Send(packet))) != packet->GetSize())
      {
        NS_FATAL_ERROR(
            "Oh oh. Couldn't send packet! res=" << res << " size=" << packet->GetSize());
      }
	 
    m_requestTime = Simulator::Now();
    m_segment_bytes = 0;

  }

  void
  DashClient::HandleRead(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);

    m_parser.ReadSocket(socket);

  }

  void
  DashClient::ConnectionSucceeded(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);
    NS_LOG_LOGIC("DashClient Connection succeeded");
    m_connected = true;
    RequestSegment();
  }

  void
  DashClient::ConnectionFailed(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);NS_LOG_LOGIC(
        "DashClient, Connection Failed");
  }

  void
  DashClient::DataSend(Ptr<Socket>, uint32_t)
  {
    NS_LOG_FUNCTION(this);

    if (m_connected)
      { // Only send new data if the connection has completed

        NS_LOG_INFO("Something was sent");

      }
    else
      {
        NS_LOG_INFO("NOT CONNECTED!!!!");
      }
  }

  void
  DashClient::MessageReceived(Packet message)
  {
    
//    std::cout<<message.GetUid()<<": "<<Simulator::Now().GetSeconds()<<std::endl; //Jerry

    MPEGHeader mpegHeader;
    HTTPHeader httpHeader;
    //yibin
    frame_num++;
    //user_info[m_id][2]++;
   // cout<<m_id<<":"<<frame_num<<endl;
    // Send the frame to the player
    m_player.ReceiveFrame(&message);
    m_player.GetID(m_id);
    m_segment_bytes += message.GetSize();
    m_totBytes += message.GetSize();
    std::cout<<"2,"<<m_id<<","<<Simulator::Now().GetSeconds()<<std::endl; //tmm    
//    std::cout<<"ID: "<<m_id<<"  frame_num: "<<frame_num<<" video_num: "<<video_num[v_num]<<std::endl;
 
    if(frame_num==video_series[m_id][v_num]){
    //if(user_info[m_id][2]==video_series[m_id][m_segmentId]){
//	std::cout<<"ID: "<<m_id<<"  seg: "<<seg_num<<"  frame: "<<frame_num<<"   Sim Time: "<<Simulator::Now().GetSeconds()<<" ------- "<<"total_bytes: "<<m_totBytes<<std::endl;
        //yibin
        
        
	    v_num++;
	    if(v_num!=(int)video_series[m_id].size())
	        tmp_num=frame_num;
	    frame_num=0;
        seg_num++;
    }
    //cout<<seg_num<<","<<m_segmentId<<endl;
//    if (seg_num==(61/seg_length)) 
	if (seg_num==(5/seg_length))
    //cout<<m_id<<","<<m_segmentId<<","<<m_totBytes<<endl;
    //cout<<60/(uint32_t)seg_length<<endl;
    //if(m_segmentId==(60/(uint32_t)seg_length))
        DashClient::StopApplication();

    message.RemoveHeader(mpegHeader);
    message.RemoveHeader(httpHeader);
    
    // Calculate the buffering time
 
   switch (m_player.m_state)
      {
    case MPEG_PLAYER_PLAYING:
      m_sumDt += m_player.GetRealPlayTime(mpegHeader.GetPlaybackTime());
      break;
    case MPEG_PLAYER_PAUSED:
      break;
    case MPEG_PLAYER_DONE:
      return;
    case MPEG_PLAYER_NOT_STARTED: //add by Jerry
      break;
    default:
      NS_FATAL_ERROR("WRONG STATE");
      }
    // If we received the last frame of the segment
//    if (mpegHeader.GetFrameId() == (unsigned) video_num[v_num-1] - 1) //MPEG_FRAMES_PER_SEGMENT
      
      if(v_num>0 && (tmp_num == video_series[m_id][v_num-1]))
      {
	      tmp_num=0;
//	std::cout<<"ID: "<<m_id<<"   request segment------"<<std::endl;
//	std::cout<<mpegHeader.GetFrameId()<<std::endl; //Jerry	
//	uint32_t segment_size = m_segment_bytes; //Jerry

        m_segmentFetchTime = Simulator::Now() - m_requestTime;
        NS_LOG_INFO(
            Simulator::Now().GetSeconds() << " bytes: " << m_segment_bytes << " segmentTime: " << m_segmentFetchTime.GetSeconds() << " segmentRate: " << 8 * m_segment_bytes / m_segmentFetchTime.GetSeconds());
        // Feed the bitrate info to the player
        AddBitRate(Simulator::Now(),
            8 * m_segment_bytes / m_segmentFetchTime.GetSeconds());

        Time currDt = m_player.GetRealPlayTime(mpegHeader.GetPlaybackTime());
        // And tell the player to monitor the buffer level
        LogBufferLevel(currDt);

        uint32_t old = m_bitRate;
        //  double diff = m_lastDt >= 0 ? (currDt - m_lastDt).GetSeconds() : 0;

        Time bufferDelay;

        //m_player.CalcNextSegment(m_bitRate, m_player.GetBufferEstimate(), diff,
        //m_bitRate, bufferDelay);

        uint32_t prevBitrate = m_bitRate;
	//std::cout<<"prevBitrate: "<<prevBitrate<<"  "<< "m_bitRate: "<<m_bitRate<<std::endl;	Jerry
	/*
	pFile = fopen ("myfile.txt","a");
   	{
    	  fputs ("fopen example",pFile);
    	  fclose (pFile);
    	}
	*/
        DashClient::CalcNextSegment(prevBitrate, m_bitRate, bufferDelay); //need to un comment
	
        if (prevBitrate != m_bitRate)
          {
            m_rateChanges++;
          }

        if (bufferDelay == Seconds(0))
          {
            RequestSegment();
          }
        else
          {
            m_player.SchduleBufferWakeup(bufferDelay, this);
          }
/*
        std::cout << Simulator::Now().GetSeconds() << " Node: " << m_id
            << " newBitRate: " << m_bitRate << " oldBitRate: " << old
            << " estBitRate: " << GetBitRateEstimate() << " interTime: "
            << m_player.m_interruption_time.GetSeconds() << " T: "
            << currDt.GetSeconds() << " dT: "
            << (m_lastDt >= 0 ? (currDt - m_lastDt).GetSeconds() : 0)
            << " del: " << bufferDelay << std::endl;
*/

//	if (seg_num==60) DashClient::StopApplication(); //Jerry

/*	std::cout << Simulator::Now().GetSeconds() << " Node: " << m_id
            << " newBitRate: " << m_bitRate << " oldBitRate: " << old
            << " estBitRate: " << GetBitRateEstimate() << " interTime: "
            << m_player.m_interruption_time.GetSeconds() << " T: "
            << currDt.GetSeconds() << " dT: "
            << (m_lastDt >= 0 ? (currDt - m_lastDt).GetSeconds() : 0)
            << " del: " << bufferDelay << " bytes: " << m_segment_bytes << " segmentTime: " 
	    << m_segmentFetchTime.GetSeconds()<< std::endl;
*/
        NS_LOG_INFO(
            "==== Last frame received. Requesting segment " << m_segmentId);


        NS_LOG_INFO(
            "!@#$#@!$@#\t" << Simulator::Now().GetSeconds() << " old: " << old << " new: " << m_bitRate << " t: " << currDt.GetSeconds() << " dt: " << (currDt - m_lastDt).GetSeconds());

        m_lastDt = currDt;

      }

  }

  void
  DashClient::CalcNextSegment(uint32_t currRate, uint32_t & nextRate,
      Time & delay)
  {
  /*  pFile = fopen ("myfile.txt","a");
    {
    fputs ("fopen example",pFile);
    fclose (pFile);
    }
  */
    nextRate = currRate;
    delay = Seconds(0);
  }

  void
  DashClient::GetStats()
  {
    std::cout << " InterruptionTime: "
        << m_player.m_interruption_time.GetSeconds() << " interruptions: "
        << m_player.m_interrruptions << " avgRate: "
        << (1.0 * m_player.m_totalRate) / m_player.m_framesPlayed
        << " minRate: " << m_player.m_minRate << " AvgDt: "
        << m_sumDt.GetSeconds() / m_player.m_framesPlayed << " changes: "
        << m_rateChanges << std::endl;

  }

  void
  DashClient::LogBufferLevel(Time t)
  {
    m_bufferState[Simulator::Now()] = t;
    for (std::map<Time, Time>::iterator it = m_bufferState.begin();
        it != m_bufferState.end(); ++it)
      {
        if (it->first < (Simulator::Now() - m_window))
          {
            m_bufferState.erase(it->first);
          }
      }
  }

  double
  DashClient::GetBufferEstimate()
  {
    double sum = 0;
    int count = 0;
    for (std::map<Time, Time>::iterator it = m_bufferState.begin();
        it != m_bufferState.end(); ++it)
      {
        sum += it->second.GetSeconds();
        count++;
      }
    return sum / count;
  }

  double
  DashClient::GetBufferDifferential()
  {
    std::map<Time, Time>::iterator it = m_bufferState.end();

    if (it == m_bufferState.begin())
      {
        // Empty buffer
        return 0;
      }
    it--;
    Time last = it->second;

    if (it == m_bufferState.begin())
      {
        // Only one element
        return 0;
      }
    it--;
    Time prev = it->second;
    return (last - prev).GetSeconds();
  }

  double
  DashClient::GetSegmentFetchTime()
  {
    return m_segmentFetchTime.GetSeconds();
  }

  void
  DashClient::AddBitRate(Time time, double bitrate)
  {
    m_bitrates[time] = bitrate;
    double sum = 0;
    int count = 0;
    for (std::map<Time, double>::iterator it = m_bitrates.begin();
        it != m_bitrates.end(); ++it)
      {
        if (it->first < (Simulator::Now() - m_window))
          {
            m_bitrates.erase(it->first);
          }
        else
          {
            sum += it->second;
            count++;
          }
      }
    m_bitrateEstimate = sum / count;
  }

} // Namespace ns3
