
#ifdef PLATFORM_WIN

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <netsocket.hpp>

bool NetSocket::InetInit() {

	WSADATA wsaData;

	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) {
		printf("WinSock2 startup failed: %d\n", result);
		return false;
	}

	return true;

}

void NetSocket::InetCleanup() {

	WSACleanup();

}

void printError(const char* format) {
	DWORD errorCode = GetLastError();
	if (errorCode == 0) return;
	LPSTR msg;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msg, 0, NULL) > 0) {
		printf(format, errorCode, msg);
		LocalFree(msg);
	}
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
		return memcmp(&((addr_t*) this->addr)->sockaddr6, &((addr_t*) other.addr)->sockaddr6, sizeof(SOCKADDR_IN6));
	} else {
		return memcmp(&((addr_t*) this->addr)->sockaddr4, &((addr_t*) other.addr)->sockaddr4, sizeof(SOCKADDR_IN));
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
			printError("Error %lu in INetAddress:tostr:inet_ntop(): %s");
			return false;
		}
		*port = htons(((addr_t*) this->addr)->sockaddr4.sin_port);
		addressStr = std::string(addrStr);
		return true;
	} else if (((addr_t*) this->addr)->sockaddrU.sa_family == AF_INET6) {
		char addrStr[INET6_ADDRSTRLEN];
		if (inet_ntop(((addr_t*) this->addr)->sockaddr6.sin6_family, &((addr_t*) this->addr)->sockaddr6.sin6_addr, addrStr, INET6_ADDRSTRLEN) == 0) {
			printError("Error %lu in INetAddress:tostr:inet_ntop(): %s");
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
		printError("Error %lu in Socket:resolveInet:getaddrinfo(): %s");
		return false;
	}

	for (ptr = info; ptr != 0; ptr = ptr->ai_next) {
		addresses.emplace_back();
		if (ptr->ai_family == AF_INET6) {
			((addr_t*) addresses.back().addr)->sockaddr6 = *((SOCKADDR_IN6*) ptr->ai_addr);
		} else if (ptr->ai_family == AF_INET) {
			((addr_t*) addresses.back().addr)->sockaddr4 = *((SOCKADDR_IN*) ptr->ai_addr);
		}
	}

	::freeaddrinfo(info);
	return true;
}

class SocketWin : public NetSocket::Socket {

public:
	NetSocket::SocketType stype;
	SOCKET handle;
	unsigned short addrType;

	SocketWin() {
		this->stype = NetSocket::UNBOUND;
		this->handle = INVALID_SOCKET;
		this->addrType = 0;
	}

	~SocketWin() override {
		if (this->stype != NetSocket::UNBOUND) {
			close();
		}
	}

	NetSocket::SocketType type() override {
		return this->stype;
	}

	int lastError() override {
		return GetLastError();
	}

	bool getINet(NetSocket::INetAddress& address) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call listen() on unbound socket!\n");
			return false;
		}

		int addrlen = sizeof(addr_t);
		if (::getpeername(this->handle, &((addr_t*) address.addr)->sockaddrU, &addrlen) == SOCKET_ERROR) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error %d in Socket:getINet:getpeername(): %s");
			return false;
		}

		return true;
	}

	bool setNagle(bool enableBuffering) override {
		if (this->stype != NetSocket::STREAM) {
			printf("tried to call setNagle() on non stream socket!\n");
			return false;
		}

		DWORD optval = enableBuffering ? 0 : 1;
		if (::setsockopt(this->handle, IPPROTO_TCP, TCP_NODELAY, (const char*) &optval, sizeof(DWORD)) == SOCKET_ERROR) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:setNagle:setsockopt(TCP_NODELAY): %s");
			return false;
		}

		return true;
	}

	bool getNagle(bool* enableBuffering) override {
		if (this->stype != NetSocket::STREAM) {
			printf("tried to call getNagle() on non stream socket!\n");
			return false;
		}

		int optlen = sizeof(DWORD);
		DWORD optval = 0;
		if (::getsockopt(this->handle, IPPROTO_TCP, TCP_NODELAY, (char*) &optval, &optlen) == SOCKET_ERROR) {
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:getNagle:setsockopt(TCP_NODELAY): %s");
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
		if (this->handle == INVALID_SOCKET) {
			printError("error 0x%x in Socket:listen:socket(): %s");
			return false;
		}

		int result = ::bind(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
		if (result == SOCKET_ERROR) {
			printError("error 0x%x in Socket:listen:bind(): %s");
			::closesocket(this->handle);
			this->handle = INVALID_SOCKET;
			return false;
		}

		result = ::listen(this->handle, SOMAXCONN);
		if (result == SOCKET_ERROR) {
			printError("error 0x%x in Socket:listen:listen(): %s");
			::closesocket(this->handle);
			this->handle = INVALID_SOCKET;
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
		if (this->handle == INVALID_SOCKET) {
			printError("error 0x%x in Socket:bind:socket(): %s");
			return false;
		}

		if (::bind(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6)) == SOCKET_ERROR) {
			printError("error 0x%x in Socket:bind:bind(): %s");
			::closesocket(this->handle);
			this->handle = INVALID_SOCKET;
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
		if (((SocketWin&) socket).stype != NetSocket::UNBOUND) {
			printf("tried to call accept() with already bound socket!\n");
			return false;
		}

		SOCKET clientSocket = ::accept(this->handle, NULL, NULL);
		if (clientSocket == INVALID_SOCKET) {
			printError("error 0x%x in Socket:accept:accept(): %s");
			return false;
		}

		((SocketWin&) socket).addrType = this->addrType;
		((SocketWin&) socket).handle = clientSocket;
		((SocketWin&) socket).stype = NetSocket::STREAM;
		return true;
	}

	bool setTimeouts(unsigned long readTimeout, unsigned long writeTimeout) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call setTimeouts() on unbound socket!\n");
			return false;
		}

		DWORD rcvTimeout = readTimeout;
		DWORD sndTimeout = writeTimeout;
		bool b1 = setsockopt(this->handle, SOL_SOCKET, SO_RCVTIMEO, (const char*) &rcvTimeout, sizeof(DWORD)) == 0;
		bool b2 = setsockopt(this->handle, SOL_SOCKET, SO_SNDTIMEO, (const char*) &sndTimeout, sizeof(DWORD)) == 0;
		return b1 && b2;
	}

	bool getTimeouts(unsigned long* readTimeout, unsigned long* writeTimeout) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call getTimeouts() on unbound socket!\n");
			return false;
		}

		int optlen = 0;
		DWORD rcvTimeout = 0;
		DWORD sndTimeout = 0;
		bool b1 = getsockopt(this->handle, SOL_SOCKET, SO_RCVTIMEO, (char*) &rcvTimeout, &optlen) == 0;
		bool b2 = getsockopt(this->handle, SOL_SOCKET, SO_SNDTIMEO, (char*) &sndTimeout, &optlen) == 0;
		if (b1) *readTimeout = rcvTimeout;
		if (b2) *writeTimeout = sndTimeout;
		return b1 && b2;
	}

	bool connect(const NetSocket::INetAddress& address, unsigned long timeout) override {
		if (this->stype != NetSocket::UNBOUND) {
			printf("tried to call connect() on already bound socket!\n");
			return false;
		}

		this->handle = ::socket(((addr_t*) address.addr)->sockaddrU.sa_family, SOCK_STREAM, IPPROTO_TCP);
		if (this->handle == INVALID_SOCKET) {
			printError("error 0x%x in Socket:connect:socket(): %s");
			return false;
		}

		unsigned long nonblock = 1;
		if (::ioctlsocket(this->handle, FIONBIO, &nonblock) == SOCKET_ERROR) {
			printError("error 0x%x in Socket:connect:ioctlsocket(FIONBIO=1): %s");
			::closesocket(this->handle);
			this->handle = INVALID_SOCKET;
			return false;
		}

		fd_set fdsetRW, fdsetE;
		fdsetRW.fd_count = 1;
		fdsetRW.fd_array[0] = this->handle;
		fdsetE.fd_count = 1;
		fdsetE.fd_array[0] = this->handle;

		int result = ::connect(this->handle, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
		if (result == SOCKET_ERROR) {

			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				printError("error 0x%x in Socket:connect:connect(): %s");
				::closesocket(this->handle);
				this->handle = INVALID_SOCKET;
				return false; // error, could not start connect
			} else {
				TIMEVAL timeoutVal = {
					.tv_sec = timeout / 1000,
					.tv_usec = (timeout % 1000) * 1000
				};
				result = ::select(0, &fdsetRW, &fdsetRW, &fdsetE, &timeoutVal);
				if (result == 0) {
					::closesocket(this->handle);
					this->handle = INVALID_SOCKET;
					return false; // timed out
				} else if (result == SOCKET_ERROR) {
					printError("error 0x%x in Socket:connect:select: %s");
					::closesocket(this->handle);
					this->handle = INVALID_SOCKET;
					return false; // error while waiting
				} else if (FD_ISSET(this->handle, &fdsetE)) {
					int err = 0, optlen = sizeof(int);
					::getsockopt(this->handle, SOL_SOCKET, SO_ERROR, (char*) &err, &optlen);
					WSASetLastError(err);
					printError("error 0x%x in Socket:connect:connect(): %s");
					::closesocket(this->handle);
					this->handle = INVALID_SOCKET;
					return false; // returned but with error
				}
			}

		}

		nonblock = 0;
		if (::ioctlsocket(this->handle, FIONBIO, &nonblock) == SOCKET_ERROR) {
			printError("error 0x%x in Socket:connect:ioctlsocket(FIONBIO=0): %s");
			::closesocket(this->handle);
			this->handle = INVALID_SOCKET;
			return false;
		}

		this->stype = NetSocket::STREAM;
		return true;
	}

	void close() override {
		if (this->stype == NetSocket::UNBOUND) return;
		::closesocket(this->handle);
		this->handle = INVALID_SOCKET;
		this->stype = NetSocket::UNBOUND;
	}

	bool isOpen() override {
		return this->stype != NetSocket::UNBOUND && this->handle != INVALID_SOCKET;
	}

	bool send(const char* buffer, unsigned int length) override {
		if (this->stype == NetSocket::UNBOUND) {
			printf("tried to call send() on unbound socket!\n");
			return false;
		} else if (this->stype != NetSocket::STREAM) {
			printf("tried to call send() on non STREAM socket!\n");
			return false;
		}

		int result = ::send(this->handle, buffer, length, 0);
		if (result == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT)
				return true; // timed out
			else if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAECONNABORTED)
				return false; // connection closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:send:send(): %s");
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

		int result = ::recv(this->handle, buffer, length, 0);
		if (result == 0) {
			return false; // connection closed
		} else if (result == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT)
				return true; // timed out
			else if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAECONNABORTED)
				return false; // connection closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:receive:recv(): %s");
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

		int senderAdressLen = sizeof(SOCKADDR_IN6);
		int result = ::recvfrom(this->handle, buffer, length, 0, &((addr_t*) address.addr)->sockaddrU, &senderAdressLen);
		if (result == 0) {
			return false; // connection closed
		} else if (result == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT)
				return true; // timed out
			else if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAECONNABORTED)
				return false; // connection closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:receivefrom:recvfrom(): %s");
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
			printf("tried to call sendto() with invalid address type for this socket!\n");
			return false;
		}

		int result = ::sendto(this->handle, buffer, length, 0, &((addr_t*) address.addr)->sockaddrU, ((addr_t*) address.addr)->sockaddrU.sa_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6));
		if (result == SOCKET_ERROR) {
			if (WSAGetLastError() == WSAETIMEDOUT)
				return true; // timed out
			else if (WSAGetLastError() == WSAECONNRESET || WSAGetLastError() == WSAECONNABORTED)
				return false; // connection closed
			if (GetLastError() == ERROR_INVALID_HANDLE) {
				close();
				return false;
			}
			printError("error 0x%x in Socket:sendto:sendto(): %s");
			return false;
		}

		return true;
	}

};

NetSocket::Socket* NetSocket::newSocket() {
	return new SocketWin();
}

#endif
