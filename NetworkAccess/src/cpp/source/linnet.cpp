#ifdef PLATFORM_LIN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include "netsocket.hpp"

/*
 * On linux read functions may never return if the socket is closed or other special conditions occur
 * So we have to define an timeout and use poll() to wait for read-readiness on the socket
 */
#define READ_SOCKET_TIMEOUT 2000

bool NetSocket::InetInit() {
	return true;
}

void NetSocket::InetCleanup() {}

void printError(const char* format) {
	int errorCode = errno;
	if (errorCode == 0) return;
	printf(format, errorCode, strerror(errorCode));
}

typedef union {
		sockaddr sockaddrU;
		sockaddr_in sockaddr4;
		sockaddr_in6 sockaddr6;
} addr_t;

NetSocket::INetAddress::INetAddress() {
	this->addr = new addr_t;
}

NetSocket::INetAddress::~INetAddress() {
	delete (addr_t*) this->addr;
}

NetSocket::INetAddress::INetAddress(const NetSocket::INetAddress& other) {
	this->addr = new addr_t;
	*((addr_t*) this->addr) = *((addr_t*) other.addr);
}

NetSocket::INetAddress& NetSocket::INetAddress::operator=(const NetSocket::INetAddress& other) {
	if (this == &other)
		return *this;
	*((addr_t*) this->addr) = *((addr_t*) other.addr);
	return *this;
}

int NetSocket::INetAddress::compare(const INetAddress& other) const {
	int i = (int) ((addr_t*) this->addr)->sockaddrU.sa_family - (int) ((addr_t*) other.addr)->sockaddrU.sa_family;
	if (i != 0) {
		return i;
	} else if (((addr_t*) this->addr)->sockaddrU.sa_family == AF_INET6) {
		return memcmp(&((addr_t*) this->addr)->sockaddr6, &((addr_t*) other.addr)->sockaddr6, sizeof(sockaddr_in6));
	} else {
		return memcmp(&((addr_t*) this->addr)->sockaddr4, &((addr_t*) other.addr)->sockaddr4, sizeof(sockaddr_in));
	}
}

bool NetSocket::INetAddress::operator<(const NetSocket::INetAddress& other) const {
	return compare(other) < 0;
}

bool NetSocket::INetAddress::operator>(const NetSocket::INetAddress& other) const {
	return compare(other) > 0;
}

bool NetSocket::INetAddress::operator==(const NetSocket::INetAddress& other) const {
	return compare(other) == 0;
}

bool NetSocket::INetAddress::fromstr(std::string& addressStr, unsigned int port) {
	if (inet_pton(AF_INET, addressStr.c_str(), &((addr_t*) this->addr)->sockaddr4.sin_addr) == 1) {
		((addr_t*) this->addr)->sockaddr4.sin_family = AF_INET;
		((addr_t*) this->addr)->sockaddr4.sin_port = htons(port);
		return true;
	} else if (inet_pton(AF_INET6, addressStr.c_str(), &((addr_t*) this->addr)->sockaddr6.sin6_addr) == 1) {
		((addr_t*) this->addr)->sockaddr6.sin6_family = AF_INET6;
		((addr_t*) this->addr)->sockaddr6.sin6_port = htons(port);
		return true;
	} else {
		printf("INetAddress:fromstr:inet_pton() failed for AF_INET and AF_INET6!\n");
		return false;
	}
}

bool NetSocket::INetAddress::tostr(std::string& addressStr, unsigned int* port) const {
	if (((addr_t*) this->addr)->sockaddrU.sa_family == AF_INET) {
		char addrStr[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &((addr_t*) this->addr)->sockaddr4.sin_addr, addrStr, INET_ADDRSTRLEN) == 0) {
			printError("error %d in INetAddress:tostr:inet_ntop(): %s\n");
			return false;
		}
		*port = htons(((addr_t*) this->addr)->sockaddr4.sin_port);
		addressStr = std::string(addrStr);
		return true;
	} else if (((addr_t*) this->addr)->sockaddrU.sa_family == AF_INET6) {
		char addrStr[INET6_ADDRSTRLEN];
		if (inet_ntop(((addr_t*) this->addr)->sockaddr6.sin6_family, &((addr_t*) this->addr)->sockaddr6.sin6_addr, addrStr, INET6_ADDRSTRLEN) == 0) {
			printError("error %d in INetAddress:tostr:inet_ntop(): %s\n");
			return false;
		}
		*port = htons(((addr_t*) this->addr)->sockaddr6.sin6_port);
		addressStr = std::string(addrStr);
		return true;
	} else {
		printf("INetAddress:tostr:str_to_inet() with non AF_INET or AF_INET6 address!\n");
		return false;
	}
}

bool NetSocket::resolveInet(const std::string& hostStr, const std::string& portStr, bool lookForUDP, std::vector<NetSocket::INetAddress>& addresses) {

	struct addrinfo hints {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = lookForUDP ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_protocol = lookForUDP ? IPPROTO_UDP : IPPROTO_TCP;

	struct addrinfo *info = 0, *ptr = 0;
	if (::getaddrinfo(hostStr.c_str(), portStr.c_str(), &hints, &info) != 0) {
		printError("error %d in Socket:resolveInet:getaddrinfo(): %s\n");
		return false;
	}

	for (ptr = info; ptr != 0; ptr = ptr->ai_next) {
		addresses.emplace_back();
		if (ptr->ai_family == AF_INET6) {
			((addr_t*) addresses.back().addr)->sockaddr6 = *((sockaddr_in6*) ptr->ai_addr);
		} else if (ptr->ai_family == AF_INET) {
			((addr_t*) addresses.back().addr)->sockaddr4 = *((sockaddr_in*) ptr->ai_addr);
		}
	}

	::freeaddrinfo(info);
	return true;
}

class SocketLin : public NetSocket::Socket {

public:
	NetSocket::SocketType stype;
	int handle;
	struct pollfd pollfd;
	unsigned short addrType;

	SocketLin() {
		this->stype = NetSocket::UNBOUND;
		this->handle = -1;
		this->addrType = 0;
	}

	~SocketLin() override {
		if (this->stype != NetSocket::UNBOUND) {
			close();
		}
	}

	NetSocket::SocketType type() override {
		return this->stype;
	}

	int lastError() override {
		return errno;
	}

	bool getINet(NetSocket::INetAddress& address) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call getINet() on unbound socket!\n");
			return false;
		}

		unsigned int addrlen = sizeof(addr_t);
		if (::getpeername(this->handle, &((addr_t*) address.addr)->sockaddrU, &addrlen) == -1) {
			printError("error %d in Socket:getINet:getpeername(): %s\n");
			return false;
		}

		return true;
	}

	bool setNagle(bool enableBuffering) override {
		if (this->stype != NetSocket::STREAM) {
			printf("tried to call setNagle() on non stream socket!\n");
			return false;
		}

		unsigned int optval = enableBuffering ? 0 : 1;
		if (::setsockopt(this->handle, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(int)) == -1) {
			printError("error %d in Socket:setNagle:setsockopt(TCP_NODELAY): %s\n");
			return false;
		}

		return true;
	}

	bool getNagle(bool* enableBuffering) override {
		if (this->stype != NetSocket::STREAM) {
			printf("tried to call getNagle() on non stream socket!\n");
			return false;
		}

		unsigned int optlen = sizeof(int);
		int optval = 0;
		if (::getsockopt(this->handle, IPPROTO_TCP, TCP_NODELAY, &optval, &optlen) == -1) {
			printError("error %d in Socket:getNagle:setsockopt(TCP_NODELAY): %s\n");
			return false;
		}

		*enableBuffering = optval == 1 ? false : true;
		return true;
	}

	bool listen(const NetSocket::INetAddress& address) override {
		if (this->stype != NetSocket::UNBOUND) {
			printf("tried to call listen() on already bound socket!\n");
			return false;
		}

		this->addrType = ((addr_t*) address.addr)->sockaddrU.sa_family;
		this->handle = ::socket(((addr_t*) address.addr)->sockaddrU.sa_family, SOCK_STREAM, IPPROTO_TCP);
		if (this->handle == -1) {
			printError("error %d in Socket:listen:socket(): %s\n");
			return false;
		}

		if (::bind(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6)) != 0) {
			printError("error %d in Socket:listen:bind(): %s\n");
			::close(this->handle);
			this->handle = -1;
			return false;
		}

		if (::listen(this->handle, SOMAXCONN) != 0) {
			printError("error %d in Socket:listen:listen(): %s\n");
			::close(this->handle);
			this->handle = -1;
			return false;
		}

		this->stype = NetSocket::LISTEN_TCP;
		return true;
	}

	bool bind(const NetSocket::INetAddress& address) override {
		if (this->stype != NetSocket::UNBOUND) {
			printf("tried to call listen() on already bound socket!\n");
			return false;
		}

		this->addrType = ((addr_t*) address.addr)->sockaddrU.sa_family;
		this->handle = ::socket(((addr_t*) address.addr)->sockaddrU.sa_family, SOCK_DGRAM, IPPROTO_UDP);
		if (this->handle == -1) {
			printError("error %d in Socket:bind:socket(): %s\n");
			return false;
		}

		if (::bind(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6)) != 0) {
			printError("error %d in Socket:bind:bind(): %s\n");
			::close(this->handle);
			this->handle = -1;
			return false;
		}

		this->stype = NetSocket::LISTEN_UDP;
		return true;
	}

	bool accept(NetSocket::Socket& socket) override {
		if (this->stype != NetSocket::LISTEN_TCP) {
			printf("tried to call accept() on non LISTEN_TCP socket!\n");
			return false;
		}
		if (((SocketLin&) socket).stype != NetSocket::UNBOUND) {
			printf("tried to call accept() with already bound socket!\n");
			return false;
		}

		int clientSocket = ::accept(this->handle, NULL, NULL);
		if (clientSocket == -1) {
			printError("error %d in Socket:accept:accept(): %s\n");
			return false;
		}

		((SocketLin&) socket).addrType = this->addrType;
		((SocketLin&) socket).handle = clientSocket;
		((SocketLin&) socket).stype = NetSocket::STREAM;
		return true;
	}

	bool setTimeouts(unsigned long readTimeout, unsigned long writeTimeout) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call setTimeouts() on unbound socket!\n");
			return false;
		}

		struct timeval rcvTimeout = {
				.tv_sec = readTimeout / 1000,
				.tv_usec = (readTimeout % 1000) * 1000
		};
		struct timeval sndTimeout = {
				.tv_sec = readTimeout / 1000,
				.tv_usec = (readTimeout % 1000) * 1000
		};
		bool b1 = setsockopt(this->handle, SOL_SOCKET, SO_SNDTIMEO, &sndTimeout, sizeof(struct timeval)) == 0;
		bool b2 = setsockopt(this->handle, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(struct timeval)) == 0;
		return b1 & b2;
	}

	bool getTimeouts(unsigned long* readTimeout, unsigned long* writeTimeout) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call getTimeouts() on unbound socket!\n");
			return false;
		}

		socklen_t optlen = 0;
		struct timeval rcvTimeout = {0};
		struct timeval sndTimeout = {0};
		bool b1 = getsockopt(this->handle, SOL_SOCKET, SO_SNDTIMEO, &sndTimeout, &optlen) == 0;
		bool b2 = getsockopt(this->handle, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, &optlen) == 0;
		if (b1) *writeTimeout = sndTimeout.tv_sec * 1000 + (sndTimeout.tv_usec / 1000);
		if (b2) *readTimeout = rcvTimeout.tv_sec * 1000 + (rcvTimeout.tv_usec / 1000);
		return b1 & b2;
	}

	bool connect(const NetSocket::INetAddress& address, unsigned long timeout) override {
		if (this->stype != NetSocket::UNBOUND) {
			printf("tried to call connect() on already bound socket!\n");
			return false;
		}

		this->handle = ::socket(((addr_t*) address.addr)->sockaddrU.sa_family, SOCK_STREAM, IPPROTO_TCP);
		this->pollfd.fd = this->handle;
		this->pollfd.events = POLLIN | POLLOUT;
		if (this->handle == -1) {
			printError("error %d in Socket:connect:socket(): %s\n");
			return false;
		}

		int flags = ::fcntl(this->handle, F_GETFL, 0);
		if (::fcntl(this->handle, F_SETFL, flags | O_NONBLOCK) == -1) {
			printError("error %d in Socket:connect:fcntl(O_NONBLOCK=1): %s\n");
			::close(this->handle);
			return false;
		}

		int result = ::connect(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
		if (result == -1) {
			if (errno == EINPROGRESS || errno == EAGAIN) {

				this->pollfd.revents = 0;
				result = ::poll(&this->pollfd, 1U, (int) timeout);
				if (result == 0) {
					::close(this->handle);
					this->handle = -1;
					return false; // timed out
				}

			} else {
				printError("error %d in Socket:connect:connect(): %s\n");
				::close(this->handle);
				this->handle = -1;
				return false;
			}
		}

		if (::fcntl(this->handle, F_SETFL, flags ^ O_NONBLOCK) == -1) {
			printError("error %d in Socket:connect:fcntl(O_NONBLOCK=0): %s\n");
			::close(this->handle);
			return false;
		}

		this->stype = NetSocket::STREAM;
		return true;
	}

	void close() override {
		if (this->stype == NetSocket::UNBOUND) return;
		::close(this->handle);
		this->handle = -1;
		this->stype = NetSocket::UNBOUND;
	}

	bool isOpen() override {
		return this->stype != NetSocket::UNBOUND && this->handle != -1;
	}

	bool send(const char* buffer, unsigned int length) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call send() on unbound socket!\n");
			return false;
		} else if (this->stype != NetSocket::STREAM) {
			printf("tried to call send() on non STREAM socket!\n");
			return false;
		}

		if (::send(this->handle, buffer, length, 0) == -1) {
			if (errno == ETIMEDOUT)
				return true; // timed out
			else if (errno == ECONNRESET)
				return false; // connection closed
			printError("error %d in Socket:send:send(): %s\n");
			return false;
		}

		return true;
	}

	bool receive(char* buffer, unsigned int length, unsigned int* received) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call send() on unbound socket!\n");
			return false;
		} else if (this->stype != NetSocket::STREAM) {
			printf("tried to call send() on non STREAM socket!\n");
			return false;
		}

		struct pollfd fd = {
			.fd = this->handle,
			.events = POLLIN,
			.revents = 0
		};
		int result = 0;
		while ((result = ::poll(&fd, 1UL, READ_SOCKET_TIMEOUT)) == 0 && isOpen()) {
			if (result < 0) {
				printError("error %d in Socket:receive:poll(): %s\n");
				return false;
			}
		}

		result = ::recv(this->handle, buffer, length, 0);
		if (result == 0) {
			return false; // connection closed
		} else if (result < 0) {
			if (errno == ETIMEDOUT)
				return true; // timed out
			else if (errno == ECONNRESET)
				return false; // connection closed
			printError("error %d in Socket:receive:recv(): %s\n");
			return false;
		} else {
			*received = result;
		}

		return true;
	}

	bool receivefrom(NetSocket::INetAddress& address, char* buffer, unsigned int length, unsigned int* received) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call receivefrom() on unbound socket!\n");
			return false;
		} else if (this->stype != NetSocket::LISTEN_UDP) {
			printf("tried to call receivefrom() on non LISTEN_UDP socket!\n");
			return false;
		}

		timeval rcvtimeout;
		rcvtimeout.tv_sec = 1;
		rcvtimeout.tv_usec = 0;

		struct pollfd fd = {
			.fd = this->handle,
			.events = POLLIN,
			.revents = 0
		};
		int result = 0;
		while ((result = ::poll(&fd, 1UL, READ_SOCKET_TIMEOUT)) == 0 && isOpen()) {
			if (result < 0) {
				printError("error %d in Socket:receivefrom:poll(): %s\n");
				return false;
			}
		}

		socklen_t senderAdressLen = sizeof(sockaddr_in6);
		result = ::recvfrom(this->handle, buffer, length, 0, &((addr_t*) address.addr)->sockaddrU, &senderAdressLen);
		if (result == 0) {
			return false; // connection closed
		} else if (result == -1) {
			if (errno == ETIMEDOUT)
				return true; // timed out
			else if (errno == ECONNRESET)
				return false; // connection closed
			printError("error %d in Socket:receivefrom:recvfrom(): %s\n");
			return false;
		} else {
			*received = result;
		}

		return true;

	}

	bool sendto(const NetSocket::INetAddress& address, const char* buffer, unsigned int length) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call sendto() on unbound socket!\n");
			return false;
		} else if (this->stype != NetSocket::LISTEN_UDP) {
			printf("tried to call sendto() on non LISTEN_UDP socket!\n");
			return false;
		}

		if (((addr_t*) address.addr)->sockaddrU.sa_family != this->addrType) {
			printf("tried to call receivefrom() with invalid address type for this socket!\n");
			return false;
		}

		int result = ::sendto(this->handle, buffer, length, 0, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
		if (result == -1) {
			if (errno == ETIMEDOUT)
				return true; // timed out
			else if (errno == ECONNRESET)
				return false; // connection closed
			printError("error %d in Socket:sendto:sendto(): %s\n");
			return false;
		}

		return true;
	}

};

NetSocket::Socket* NetSocket::newSocket() {
	return new SocketLin();
}

#endif
