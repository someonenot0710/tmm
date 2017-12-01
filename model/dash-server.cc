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

#include "ns3/address.h"
#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/node.h"
#include "ns3/socket.h"
#include "ns3/udp-socket.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/packet.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "dash-server.h"
#include "http-header.h"
#include "mpeg-header.h"
#include <ns3/random-variable-stream.h>
#include <ns3/tcp-socket.h>
#include <ns3/double.h>
#include "para.h"
using namespace std;


namespace ns3
{
  NS_LOG_COMPONENT_DEFINE("DashServer");
  NS_OBJECT_ENSURE_REGISTERED(DashServer);

  TypeId
  DashServer::GetTypeId(void)
  {
    static TypeId tid =
        TypeId("ns3::DashServer").SetParent<Application>().AddConstructor<
            DashServer>().AddAttribute("Local",
            "The Address on which to Bind the rx socket.", AddressValue(),
            MakeAddressAccessor(&DashServer::m_local), MakeAddressChecker()).AddAttribute(
            "Protocol", "The type id of the protocol to use for the rx socket.",
            TypeIdValue(UdpSocketFactory::GetTypeId()),
            MakeTypeIdAccessor(&DashServer::m_tid), MakeTypeIdChecker()).AddTraceSource(
            "Rx", "A packet has been received",
            MakeTraceSourceAccessor(&DashServer::m_rxTrace));
    return tid;
  }

  DashServer::DashServer()
  {
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_totalRx = 0;
  }

  DashServer::~DashServer()
  {
    NS_LOG_FUNCTION(this);
  }

  Ptr<Socket>
  DashServer::GetListeningSocket(void) const
  {
    NS_LOG_FUNCTION(this);
    return m_socket;
  }

  std::list<Ptr<Socket> >
  DashServer::GetAcceptedSockets(void) const
  {
    NS_LOG_FUNCTION(this);
    return m_socketList;
  }

  void
  DashServer::DoDispose(void)
  {
    NS_LOG_FUNCTION(this);
    m_socket = 0;
    m_socketList.clear();

    // chain up
    Application::DoDispose();
  }

// Application Methods
  void
  DashServer::StartApplication()    // Called at time specified by Start
  {
    NS_LOG_FUNCTION(this);
  // Create the socket if not already
    if (!m_socket)
      {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        m_socket->Bind(m_local);
        m_socket->Listen();
        // m_socket->ShutdownSend ();
        if (addressUtils::IsMulticast(m_local))
          {
            Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
            if (udpSocket)
              {
                // equivalent to setsockopt (MCAST_JOIN_GROUP)
                udpSocket->MulticastJoinGroup(0, m_local);
              }
            else
              {
                NS_FATAL_ERROR("Error: joining multicast on a non-UDP socket");
              }
          }
      }

    m_socket->SetRecvCallback(MakeCallback(&DashServer::HandleRead, this));

    m_socket->SetAcceptCallback(
        MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
        MakeCallback(&DashServer::HandleAccept, this));
    m_socket->SetCloseCallbacks(
        MakeCallback(&DashServer::HandlePeerClose, this),
        MakeCallback(&DashServer::HandlePeerError, this));
  }

  void
  DashServer::StopApplication()     // Called at time specified by Stop
  {
    NS_LOG_FUNCTION(this);
    while (!m_socketList.empty()) //these are accepted sockets, close them
      {
        Ptr<Socket> acceptedSocket = m_socketList.front();
        m_socketList.pop_front();
        acceptedSocket->Close();
      }
    if (m_socket)
      {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket> >());
      }
  }

  void
  DashServer::HandleRead(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
      {
        if (packet->GetSize() == 0)
          { //EOF
            break;
          }
        m_totalRx += packet->GetSize();

        HTTPHeader header;
        packet->RemoveHeader(header);
//	uint32_t ID = header.GetSegmentId();// Jerry
//      uint32_t video = header.GetVideoId(); //Jerry
//	std::cout<<"VidID: "<<video<<"  "<<"segID: "<<ID<<std::endl; // Jerry
        SendSegment(header.GetVideoId(), header.GetResolution(),
            header.GetSegmentId(),header.GetPacketNum(), socket);

        if (InetSocketAddress::IsMatchingType(from))
          {
            NS_LOG_INFO(
                "At time " << Simulator::Now ().GetSeconds () << "s packet sink received " << packet->GetSize () << " bytes from " << InetSocketAddress::ConvertFrom(from).GetIpv4 () << " port " << InetSocketAddress::ConvertFrom (from).GetPort () << " total Rx " << m_totalRx << " bytes");
          }
        else if (Inet6SocketAddress::IsMatchingType(from))
          {
            NS_LOG_INFO(
                "At time " << Simulator::Now ().GetSeconds () << "s packet sink received " << packet->GetSize () << " bytes from " << Inet6SocketAddress::ConvertFrom(from).GetIpv6 () << " port " << Inet6SocketAddress::ConvertFrom (from).GetPort () << " total Rx " << m_totalRx << " bytes");
          }
        m_rxTrace(packet, from);
      }
  }

  void
  DashServer::HandlePeerClose(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);
  }

  void
  DashServer::HandlePeerError(Ptr<Socket> socket)
  {
    NS_LOG_FUNCTION(this << socket);
  }

  void
  DashServer::HandleAccept(Ptr<Socket> s, const Address& from)
  {
    NS_LOG_FUNCTION(this << s << from);
    s->SetRecvCallback(MakeCallback(&DashServer::HandleRead, this));
    s->SetSendCallback(MakeCallback(&DashServer::DataSend, this));
    m_socketList.push_back(s);

  }

  void
  DashServer::DataSend(Ptr<Socket> socket, uint32_t)
  {
    NS_LOG_FUNCTION(this);
    for (std::map<Ptr<Socket>, std::queue<Packet> >::iterator iter =
        m_queues.begin(); iter != m_queues.end(); ++iter)
      {
        HTTPHeader httpHeader;
        MPEGHeader mpegHeader;

        if (iter->second.size())
          {
            Ptr<Packet> frame = iter->second.front().Copy();

            frame->RemoveHeader(mpegHeader);
            frame->RemoveHeader(httpHeader);

            NS_LOG_INFO(
                "VidId: " << httpHeader.GetVideoId() << " rxAv= " << iter->first->GetRxAvailable() << " queue= "<< iter->second.size() << " res= " << httpHeader.GetResolution());
          }
      }
    while (!m_queues[socket].empty())
      {
//	std::cout<<"socket: "<<socket<<"  m_queue_size: "<<m_queues[socket].size()<<std::endl; //Jerry
        int bytes;
        Ptr<Packet> frame = m_queues[socket].front().Copy();
//	std::cout<<"1,"<<socket<<","<<Simulator::Now().GetSeconds()<<std::endl; //tmm
        if ((bytes = socket->Send(frame)) != (int) frame->GetSize())
          {
	    std::cout<<"Could not send frame"<<std::endl;
            NS_LOG_INFO("Could not send frame");
            if (bytes != -1)
              {
                NS_FATAL_ERROR("Oops, we sent half a frame :(");
              }
            break;
          }
//	std::cout<<"m_queue: "<<m_queues[socket].size()<<std::endl; //Jerry
        m_queues[socket].pop();
      }

    NS_LOG_INFO("DATA WAS JUST SENT!!!");

  }

  void
  DashServer::SendSegment(uint32_t video_id, uint32_t resolution,
      uint32_t segment_id,uint32_t packet_num, Ptr<Socket> socket)
  {
//   std::cout<<"video_id: "<<video_id<<"  send segment!!!!!!"<<std::endl;
	/*Jerry*/ 
	/*
    static int co=0;
    if(co==0){
	cout<<"in server side"<<endl;
    for (size_t j=0,max=video_num.size();j!=max;j++){
	cout<<video_num[j]<<endl;
    }
	co++;
	}
   */
	
    v_num[video_id]++; //Jerry

    int avg_packetsize = resolution / (50 * 8);

    HTTPHeader http_header_tmp;
    MPEGHeader mpeg_header_tmp;

    Ptr<UniformRandomVariable> frame_size_gen = CreateObject<UniformRandomVariable> ();
    /*Jerry*/
    frame_size_gen->SetAttribute ("Min", DoubleValue (1288));
    /*
    frame_size_gen->SetAttribute ("Max", DoubleValue (
        std::max(
            std::min(2 * avg_packetsize, MPEG_MAX_MESSAGE)
                - (int) (mpeg_header_tmp.GetSerializedSize()
                    + http_header_tmp.GetSerializedSize()), 1)));
    */

    frame_size_gen->SetAttribute ("Max", DoubleValue (1288)); // Jerry
    
    //video_num[v_num[video_id]]
    for (uint32_t f_id = 0; f_id < packet_num; f_id++) //MPEG_FRAMES_PER_SEGMENT
      {
        uint32_t frame_size = (unsigned) frame_size_gen->GetValue();
	
//	std::cout<<"f_id: "<<f_id<<std::endl;
//	std::cout<<"f_id"<<f_id<<"  " << "size" << frame_size <<std::endl; //Jerry

        HTTPHeader http_header;
        http_header.SetMessageType(HTTP_RESPONSE);
        http_header.SetVideoId(video_id);
        http_header.SetResolution(resolution);
        http_header.SetSegmentId(segment_id);

        MPEGHeader mpeg_header;
        mpeg_header.SetFrameId(f_id);
        mpeg_header.SetPlaybackTime(
            MilliSeconds(
                (f_id + (segment_id * MPEG_FRAMES_PER_SEGMENT))
                    * MPEG_TIME_BETWEEN_FRAMES)); //50 fps

//	cout<<Simulator::Now()<<":  "<<(f_id + (segment_id * MPEG_FRAMES_PER_SEGMENT))* MPEG_TIME_BETWEEN_FRAMES<<endl;

//	mpeg_header.SetPlaybackTime(Seconds(1)); //Jerry

        mpeg_header.SetType('B');
        mpeg_header.SetSize(frame_size);

        Ptr<Packet> frame = Create<Packet>(frame_size);
        frame->AddHeader(http_header);
        frame->AddHeader(mpeg_header);
        NS_LOG_INFO(
            "SENDING PACKET " << f_id << " " << frame->GetSize() << " res=" << http_header.GetResolution() << " size=" << mpeg_header.GetSize() << " avg=" << avg_packetsize);

        m_queues[socket].push(*frame);
      }
    DataSend(socket, 0);
  }

} // Namespace ns3
