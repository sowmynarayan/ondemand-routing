# ondemand-routing
ODR routing protocol and a sample time server and client using the protocol

Team Members:

1. Sowmiya Narayan Srinath
2. Sai Santhosh Vaidyam Anandan

An On-Demand shortest-hop Routing (ODR) protocol for networks of fixed but arbitrary and unknown connectivity, 
using PF_PACKET sockets. The implementation is based on (a simplified version of) the AODV algorithm.

Time client and server applications that send requests and replies to each other across the network using ODR. 
An API implemented using Unix domain datagram sockets enables applications to communicate with the ODR mechanism 
running locally at their nodes.
