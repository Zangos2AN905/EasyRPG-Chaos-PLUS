/*
 * Chaos Fork: STUN server for UDP hole punching.
 */

#ifndef EP_CHAOS_STUN_SERVER_H
#define EP_CHAOS_STUN_SERVER_H

#include <cstdint>
#include <cstddef>
#include <functional>
#include <map>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET StunSocket;
typedef int StunSockLen;
#define STUN_INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int StunSocket;
typedef socklen_t StunSockLen;
#define STUN_INVALID_SOCK (-1)
#endif

namespace Chaos {

/**
 * Minimal RFC 5389 STUN Binding responder used for UDP hole punching.
 *
 * Runs on the hosting side (game port + 1) and answers Binding Requests with
 * the requester's observed address (MAPPED-ADDRESS and XOR-MAPPED-ADDRESS).
 * Every observed endpoint is reported through a callback so the game host can
 * punch UDP holes toward it from its ENet socket.
 */
class StunServer {
public:
	using EndpointCallback = std::function<void(uint32_t ip, uint16_t port)>;

	~StunServer();

	bool Start(uint16_t port);
	void Stop();
	void Update();
	bool IsRunning() const { return sock != STUN_INVALID_SOCK; }

	void SetEndpointCallback(EndpointCallback cb) { on_endpoint = std::move(cb); }

private:
	void HandleDatagram(const uint8_t* data, size_t len, uint32_t src_ip, uint16_t src_port);
	void SendBindingResponse(uint32_t dst_ip, uint16_t dst_port,
		const uint8_t transaction_id[12], uint32_t mapped_ip, uint16_t mapped_port);

	StunSocket sock = STUN_INVALID_SOCK;
	EndpointCallback on_endpoint;
	// Endpoint -> last report time in ms, for deduplicating punch requests
	std::map<std::pair<uint32_t, uint16_t>, uint64_t> last_reported;
};

} // namespace Chaos

#endif
