/*
 * Chaos Fork: STUN server for UDP hole punching.
 */

#include "chaos/stun_server.h"
#include "output.h"
#include <chrono>
#include <cstring>

namespace Chaos {

namespace {

constexpr size_t kStunHeaderSize = 20;
constexpr uint16_t kBindingRequest = 0x0001;
constexpr uint16_t kBindingSuccess = 0x0101;
constexpr uint16_t kAttrMappedAddress = 0x0001;
constexpr uint16_t kAttrXorMappedAddress = 0x0020;
constexpr uint32_t kMagicCookie = 0x2112A442;
constexpr uint64_t kReportIntervalMs = 2000;
constexpr size_t kMaxTrackedEndpoints = 256;
constexpr int kMaxDatagramsPerUpdate = 32;

uint64_t NowMs() {
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

void WriteU16BE(uint8_t* p, uint16_t v) {
	p[0] = static_cast<uint8_t>(v >> 8);
	p[1] = static_cast<uint8_t>(v & 0xFF);
}

void WriteU32BE(uint8_t* p, uint32_t v) {
	p[0] = static_cast<uint8_t>(v >> 24);
	p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
	p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
	p[3] = static_cast<uint8_t>(v & 0xFF);
}

uint16_t ReadU16BE(const uint8_t* p) {
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t ReadU32BE(const uint8_t* p) {
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
		(static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

bool SetNonBlocking(StunSocket s) {
#ifdef _WIN32
	u_long mode = 1;
	return ioctlsocket(s, FIONBIO, &mode) == 0;
#else
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) return false;
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void CloseSocket(StunSocket s) {
#ifdef _WIN32
	closesocket(s);
#else
	close(s);
#endif
}

} // namespace

StunServer::~StunServer() {
	Stop();
}

bool StunServer::Start(uint16_t port) {
	Stop();
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == STUN_INVALID_SOCK) {
		Output::Warning("STUN: Failed to create socket");
		return false;
	}

	int reuse = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		Output::Warning("STUN: Failed to bind port {}", port);
		CloseSocket(sock);
		sock = STUN_INVALID_SOCK;
		return false;
	}

	if (!SetNonBlocking(sock)) {
		Output::Warning("STUN: Failed to set non-blocking mode");
		CloseSocket(sock);
		sock = STUN_INVALID_SOCK;
		return false;
	}

	last_reported.clear();
	Output::Debug("STUN: Listening on UDP port {}", port);
	return true;
}

void StunServer::Stop() {
	if (sock != STUN_INVALID_SOCK) {
		CloseSocket(sock);
		sock = STUN_INVALID_SOCK;
	}
	last_reported.clear();
}

void StunServer::Update() {
	if (sock == STUN_INVALID_SOCK) return;

	uint8_t buf[1500];
	for (int i = 0; i < kMaxDatagramsPerUpdate; ++i) {
		sockaddr_in src;
		std::memset(&src, 0, sizeof(src));
		StunSockLen src_len = static_cast<StunSockLen>(sizeof(src));
		int received = recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
			reinterpret_cast<sockaddr*>(&src), &src_len);
		if (received <= 0) break;
		HandleDatagram(buf, static_cast<size_t>(received), src.sin_addr.s_addr, ntohs(src.sin_port));
	}
}

void StunServer::HandleDatagram(const uint8_t* data, size_t len, uint32_t src_ip, uint16_t src_port) {
	if (len < kStunHeaderSize) return;
	const uint16_t msg_type = ReadU16BE(data);
	const uint16_t msg_len = ReadU16BE(data + 2);
	const uint32_t cookie = ReadU32BE(data + 4);
	if (msg_type != kBindingRequest) return;
	if (cookie != kMagicCookie) return;
	if (static_cast<size_t>(msg_len) + kStunHeaderSize > len) return;

	uint8_t transaction_id[12];
	std::memcpy(transaction_id, data + 8, 12);
	SendBindingResponse(src_ip, src_port, transaction_id, src_ip, src_port);

	// Report each endpoint at most once per interval to bound punch traffic
	const uint64_t now = NowMs();
	const auto key = std::make_pair(src_ip, src_port);
	auto it = last_reported.find(key);
	if (it != last_reported.end() && now - it->second < kReportIntervalMs) return;
	if (last_reported.size() >= kMaxTrackedEndpoints) last_reported.clear();
	last_reported[key] = now;
	if (on_endpoint) on_endpoint(src_ip, src_port);
}

void StunServer::SendBindingResponse(uint32_t dst_ip, uint16_t dst_port,
		const uint8_t transaction_id[12], uint32_t mapped_ip, uint16_t mapped_port) {
	// Header (20) + MAPPED-ADDRESS (12) + XOR-MAPPED-ADDRESS (12)
	uint8_t pkt[44];
	std::memset(pkt, 0, sizeof(pkt));
	WriteU16BE(pkt + 0, kBindingSuccess);
	WriteU16BE(pkt + 2, 24);
	WriteU32BE(pkt + 4, kMagicCookie);
	std::memcpy(pkt + 8, transaction_id, 12);

	// mapped_ip is kept in network byte order as received
	uint8_t ip_bytes[4];
	std::memcpy(ip_bytes, &mapped_ip, 4);

	WriteU16BE(pkt + 20, kAttrMappedAddress);
	WriteU16BE(pkt + 22, 8);
	pkt[24] = 0;
	pkt[25] = 0x01; // IPv4
	WriteU16BE(pkt + 26, mapped_port);
	std::memcpy(pkt + 28, ip_bytes, 4);

	static const uint8_t magic_bytes[4] = {0x21, 0x12, 0xA4, 0x42};
	WriteU16BE(pkt + 32, kAttrXorMappedAddress);
	WriteU16BE(pkt + 34, 8);
	pkt[36] = 0;
	pkt[37] = 0x01; // IPv4
	WriteU16BE(pkt + 38, static_cast<uint16_t>(mapped_port ^ 0x2112));
	for (int i = 0; i < 4; ++i) pkt[40 + i] = static_cast<uint8_t>(ip_bytes[i] ^ magic_bytes[i]);

	sockaddr_in dst;
	std::memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = dst_ip;
	dst.sin_port = htons(dst_port);
	sendto(sock, reinterpret_cast<const char*>(pkt), sizeof(pkt), 0,
		reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
}

} // namespace Chaos
