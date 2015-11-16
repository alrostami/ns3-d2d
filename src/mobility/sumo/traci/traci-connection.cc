
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>    // for sockaddr_in
#include <sys/types.h>    // for socket
#include <sys/socket.h>    // for socket
#include <stdio.h>        // for printf
#include <stdlib.h>        // for exit
#include <string.h>        // for bzero

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

#define SOCKET int
inline int initsocketlibonce() {return 0;}
inline void closesocket(int fd) {close(fd);}
inline int sock_errno()  {return errno;}
#define SOCKETERR(x)  x
#define INVALID_SOCKET -1
#define SOCKET_ERROR   -1

//#include <cenvir.h>
//#include <cexception.h>
#include <algorithm>
#include <functional>

#include "./traci-connection.h"
#include "./traci-constants.h"


//#include "simcore/dassert.h"
#include <cassert>
#define ASSERT(CONDITION) \
    assert(CONDITION)

#include <iostream>
#include <stdarg.h>
#include <stdio.h>
void opp_error(const char *format, ...){
	va_list args;
	char err_msg[1024];
	va_start(args, format);
	vsnprintf(err_msg, sizeof err_msg, format, args);
	va_end(args);
	std::cerr<<(err_msg)<<std::endl;
	std::abort();
}




SOCKET socket(void* ptr)
{
	ASSERT(ptr);
	return *static_cast<SOCKET*>(ptr);
}

TraCIConnection::TraCIConnection(void* ptr) : socketPtr(ptr) {
	ASSERT(socketPtr);
}

TraCIConnection::~TraCIConnection() {
	if (socketPtr) {
		closesocket(socket(socketPtr));
		delete static_cast<SOCKET*>(socketPtr);
	}
}

TraCIConnection* TraCIConnection::connect(const char* host, int port) {
	//MYDEBUG << "TraCIScenarioManager connecting to TraCI server" << endl;

	//if (initsocketlibonce() != 0) opp_error("Could not init socketlib");

	in_addr addr;
	struct hostent* host_ent;
	struct in_addr saddr;

	saddr.s_addr = inet_addr(host);
	if (saddr.s_addr != static_cast<unsigned int>(-1)) {
		addr = saddr;
	} else if ((host_ent = gethostbyname(host))) {
		addr = *((struct in_addr*) host_ent->h_addr_list[0]);
	} else {
		opp_error("Invalid TraCI server address: %s", host);
		return 0;
	}

	sockaddr_in address;
	memset((char*) &address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = addr.s_addr;

	SOCKET* socketPtr = new SOCKET();
	*socketPtr = ::socket(AF_INET, SOCK_STREAM, 0);
	if (*socketPtr < 0) opp_error("Could not create socket to connect to TraCI server");

	if (::connect(*socketPtr, (sockaddr const*) &address, sizeof(address)) < 0) {
		opp_error("Could not connect to TraCI server. Make sure it is running and not behind a firewall. Error message: %d: %s", sock_errno(), strerror(sock_errno()));
	}

	{
		int x = 1;
		::setsockopt(*socketPtr, IPPROTO_TCP, TCP_NODELAY, (const char*) &x, sizeof(x));
	}

	return new TraCIConnection(socketPtr);
}

TraCIBuffer TraCIConnection::query(uint8_t commandId, const TraCIBuffer& buf) {
	sendMessage(makeTraCICommand(commandId, buf));

	TraCIBuffer obuf(receiveMessage());
	uint8_t cmdLength; obuf >> cmdLength;
	uint8_t commandResp; obuf >> commandResp;
	ASSERT(commandResp == commandId);
	uint8_t result; obuf >> result;
	std::string description; obuf >> description;
	if (result == RTYPE_NOTIMPLEMENTED) opp_error("TraCI server reported command 0x%2x not implemented (\"%s\"). Might need newer version.", commandId, description.c_str());
	if (result == RTYPE_ERR) opp_error("TraCI server reported error executing command 0x%2x (\"%s\").", commandId, description.c_str());
	ASSERT(result == RTYPE_OK);
	return obuf;
}

TraCIBuffer TraCIConnection::queryOptional(uint8_t commandId, const TraCIBuffer& buf, bool& success, std::string* errorMsg) {
	sendMessage(makeTraCICommand(commandId, buf));

	TraCIBuffer obuf(receiveMessage());
	uint8_t cmdLength; obuf >> cmdLength;
	uint8_t commandResp; obuf >> commandResp;
	ASSERT(commandResp == commandId);
	uint8_t result; obuf >> result;
	std::string description; obuf >> description;
	success = (result == RTYPE_OK);
	if (errorMsg) *errorMsg = description;
	return obuf;
}

std::string TraCIConnection::receiveMessage() {
	if (!socketPtr) opp_error("Not connected to TraCI server");

	uint32_t msgLength;
	{
		char buf2[sizeof(uint32_t)];
		uint32_t bytesRead = 0;
		while (bytesRead < sizeof(uint32_t)) {
			int receivedBytes = ::recv(socket(socketPtr), reinterpret_cast<char*>(&buf2) + bytesRead, sizeof(uint32_t) - bytesRead, 0);
			if (receivedBytes > 0) {
				bytesRead += receivedBytes;
			} else if (receivedBytes == 0) {
				opp_error("Connection to TraCI server closed unexpectedly. Check your server's log");
			} else {
				if (sock_errno() == EINTR) continue;
				if (sock_errno() == EAGAIN) continue;
				opp_error("Connection to TraCI server lost. Check your server's log. Error message: %d: %s", sock_errno(), strerror(sock_errno()));
			}
		}
		TraCIBuffer(std::string(buf2, sizeof(uint32_t))) >> msgLength;
	}

	uint32_t bufLength = msgLength - sizeof(msgLength);
	char buf[bufLength];
	{
		//MYDEBUG << "Reading TraCI message of " << bufLength << " bytes" << endl;
		uint32_t bytesRead = 0;
		while (bytesRead < bufLength) {
			int receivedBytes = ::recv(socket(socketPtr), reinterpret_cast<char*>(&buf) + bytesRead, bufLength - bytesRead, 0);
			if (receivedBytes > 0) {
				bytesRead += receivedBytes;
			} else if (receivedBytes == 0) {
				opp_error("Connection to TraCI server closed unexpectedly. Check your server's log");
			} else {
				if (sock_errno() == EINTR) continue;
				if (sock_errno() == EAGAIN) continue;
				opp_error("Connection to TraCI server lost. Check your server's log. Error message: %d: %s", sock_errno(), strerror(sock_errno()));
			}
		}
	}
	return std::string(buf, bufLength);
}

void TraCIConnection::sendMessage(std::string buf) {
	if (!socketPtr) opp_error("Not connected to TraCI server");

	{
		uint32_t msgLength = sizeof(uint32_t) + buf.length();
		TraCIBuffer buf2 = TraCIBuffer();
		buf2 << msgLength;
		uint32_t bytesWritten = 0;
		while (bytesWritten < sizeof(uint32_t)) {
			size_t sentBytes = ::send(socket(socketPtr), buf2.str().c_str() + bytesWritten, sizeof(uint32_t) - bytesWritten, 0);
			if (sentBytes > 0) {
				bytesWritten += sentBytes;
			} else {
				if (sock_errno() == EINTR) continue;
				if (sock_errno() == EAGAIN) continue;
				opp_error("Connection to TraCI server lost. Check your server's log. Error message: %d: %s", sock_errno(), strerror(sock_errno()));
			}
		}
	}

	{
		//MYDEBUG << "Writing TraCI message of " << buf.length() << " bytes" << endl;
		uint32_t bytesWritten = 0;
		while (bytesWritten < buf.length()) {
			size_t sentBytes = ::send(socket(socketPtr), buf.c_str() + bytesWritten, buf.length() - bytesWritten, 0);
			if (sentBytes > 0) {
				bytesWritten += sentBytes;
			} else {
				if (sock_errno() == EINTR) continue;
				if (sock_errno() == EAGAIN) continue;
				opp_error("Connection to TraCI server lost. Check your server's log. Error message: %d: %s", sock_errno(), strerror(sock_errno()));
			}
		}
	}
}

std::string makeTraCICommand(uint8_t commandId, const TraCIBuffer& buf) {
	if (sizeof(uint8_t) + sizeof(uint8_t) + buf.str().length() > 0xFF) {
		uint32_t len = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint8_t) + buf.str().length();
		return (TraCIBuffer() << static_cast<uint8_t>(0) << len << commandId).str() + buf.str();
	}
	uint8_t len = sizeof(uint8_t) + sizeof(uint8_t) + buf.str().length();
	return (TraCIBuffer() << len << commandId).str() + buf.str();
}

void TraCIConnection::setNetbounds(TraCICoord netbounds1, TraCICoord netbounds2, int margin) {
	this->netbounds1 = netbounds1;
	this->netbounds2 = netbounds2;
	this->margin = margin;
}
