/*
 * network.h
 *
 *  Created on: 03.02.2025
 *      Author: marvi
 */

#ifndef NETWORK_HPP_
#define NETWORK_HPP_

#include <string>
#include <vector>
#include <memory>

/** FIXME
 * Sockets seem to terminate randomly by some certain errors, leaving the
 * stype field in an invalid state
 */

namespace NetSocket {

bool InetInit();
void InetCleanup();

class INetAddress {

public:
	void* addr;

	INetAddress();
	INetAddress(const INetAddress& other);
	~INetAddress();
	bool fromstr(std::string& addressStr, unsigned int port);
	bool tostr(std::string& addressStr, unsigned int* port) const;
	int compare(const INetAddress& other) const;

	INetAddress& operator=(const INetAddress& other);
	bool operator<(const INetAddress& other) const;
	bool operator>(const INetAddress& other) const;
	bool operator==(const INetAddress& other) const;

};

typedef INetAddress inetaddr;

/**
 * Resolves the supplied host string into a list of network addresses.
 * @param hostStr The host URL string
 * @param portStr The host port string
 * @param lookForUDP If the resolution should happen for TCP or UDP sockets
 * @param addresses An vector to place the resolved addresses in
 * @return true if the resolution was successfull, false otherwise
 */
bool resolveInet(const std::string& hostStr, const std::string& portStr, bool lookForUDP, std::vector<NetSocket::INetAddress>& addresses);

enum SocketType {
	UNBOUND = 0,
	LISTEN_TCP = 1,
	LISTEN_UDP = 2,
	STREAM = 3
};

class Socket {

public:
	virtual ~Socket() = default;

	/**
	 * Creates a new TCP port that can accept incoming connections usign the accept() function
	 * @param localAddress The local address to bind the socket to
	 * @return true if the port was successfully bound, false otherwise
	 */
	virtual bool listen(const inetaddr& localAddress) = 0;

	/**
	 * Attempts to accept an incomming connection and initializes the supplied (unbound) socket for it as TCP stream socket.
	 * This function blocks until an connection is received.
	 * @param clientSocket The unbound socket to use for the incomming connection
	 * @return true if an connection was accepted and the socket was initialized successfully, false otherwise
	 */
	virtual bool accept(Socket &clientSocket) = 0;

	/**
	 * Attempts to establish a connection to an TCP listen socket at the specified address.
	 * @param remoteAddress The address to connect to
	 * @return true if an connection was successfully established, false otherwise
	 */
	virtual bool connect(const inetaddr& remoteAddress) = 0;

	/**
	 * Sends data trough the TCP connection.
	 * @param buffer The buffer holding the data
	 * @param length The length of the data
	 * @return true if the data was sent successfully, false otherwise
	 */
	virtual bool send(const char* buffer, unsigned int length) = 0;

	/**
	 * Receives data trough the TCP connection.
	 * This function might block indefinitely until data is received.
	 * This function might return with zero bytes read, which is not an error.
	 * @param buffer The buffer to write the payload to
	 * @param length The capacity of the buffer
	 * @param received The actual number of bytes received
	 * @return true if the function did return normally (no error occurred), false otherwise
	 */
	virtual bool receive(char* buffer, unsigned int length, unsigned int* received) = 0;

	/**
	 * Creates and new socket configured for UDP transmissions
	 * @return true if the port was successfully bound, false otherwise
	 */
	virtual bool bind(const inetaddr& localAddress) = 0;

	/**
	 * Receives data trough UDP transmissions.
	 * This function might block indefinitely until data is received.
	 * This function might return with zero bytes read, which is not an error.
	 * @param remoteAddress The sender address of the received package
	 * @param buffer The buffer to write the data to
	 * @param length The capacity of the buffer
	 * @param received The actual number of bytes received
	 * @return true if the function did return normally (no error occurred), false otherwise
	 */
	virtual bool receivefrom(inetaddr& remoteAddress, char* buffer, unsigned int length, unsigned int* received) = 0;

	/**
	 * Sends data trough the UDP transmissions
	 * @param remoteAddress The target address to which the data should be send.
	 * @param buffer The buffer holding the data
	 * @param length The length of the data
	 * @return true if the data was sent successfully, false otherwise
	 */
	virtual bool sendto(const inetaddr& remoteAddress, const char* buffer, unsigned int length) = 0;

	/**
	 * Closes the port.
	 */
	virtual void close() = 0;

	/**
	 * Checks if the port is still open and operational.
	 * @return true if the port is still open, false otherwise
	 */
	virtual bool isOpen() = 0;

	virtual SocketType type() = 0;
	virtual int lastError() = 0;

};

NetSocket::Socket* newSocket();

}

#endif /* NETWORK_HPP_ */
