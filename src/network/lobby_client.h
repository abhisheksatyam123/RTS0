#ifndef RTSO_LOBBY_CLIENT_H
#define RTSO_LOBBY_CLIENT_H

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// windef.h defines near and far (16-bit pointer keywords): the code uses them as names.
#undef near
#undef far
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace rtso {

struct LobbyRoomInfo {
	std::string id;
	std::string host_ip;
	uint16_t host_port = 20595;
	int players = 1;
	int max_players = 8;
	std::string map_id = "default";
};

inline bool lobby_http_exchange(const std::string &host, uint16_t port,
                                const std::string &request, std::string &body) {
	body.clear();
#ifdef _WIN32
	SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == INVALID_SOCKET) return false;
#else
	int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0) return false;
#endif
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
#ifdef _WIN32
		closesocket(fd);
#else
		::close(fd);
#endif
		return false;
	}
	if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
#ifdef _WIN32
		closesocket(fd);
#else
		::close(fd);
#endif
		return false;
	}
#ifdef _WIN32
	send(fd, request.data(), (int)request.size(), 0);
#else
	::send(fd, request.data(), request.size(), 0);
#endif
	char buf[4096];
	std::string raw;
	while (true) {
#ifdef _WIN32
		int n = recv(fd, buf, sizeof(buf), 0);
#else
		ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
#endif
		if (n <= 0) break;
		raw.append(buf, buf + n);
		if (raw.size() > 65536) break;
	}
#ifdef _WIN32
	closesocket(fd);
#else
	::close(fd);
#endif
	size_t hdr = raw.find("\r\n\r\n");
	body = (hdr == std::string::npos) ? raw : raw.substr(hdr + 4);
	return !body.empty() || raw.find("201") != std::string::npos || raw.find("200") != std::string::npos;
}

inline bool lobby_http_get(const std::string &host, uint16_t port, const std::string &path, std::string &body) {
	std::ostringstream req;
	req << "GET " << path << " HTTP/1.0\r\nHost: " << host << "\r\nConnection: close\r\n\r\n";
	return lobby_http_exchange(host, port, req.str(), body);
}

inline bool lobby_http_post_json(const std::string &host, uint16_t port, const std::string &path,
                                 const std::string &json, std::string &body) {
	std::ostringstream req;
	req << "POST " << path << " HTTP/1.0\r\nHost: " << host
	    << "\r\nContent-Type: application/json\r\nContent-Length: " << json.size()
	    << "\r\nConnection: close\r\n\r\n" << json;
	return lobby_http_exchange(host, port, req.str(), body);
}

inline std::string json_string_field(const std::string &obj, const char *key) {
	std::string pat = std::string("\"") + key + "\"";
	size_t i = obj.find(pat);
	if (i == std::string::npos) return {};
	i = obj.find(':', i);
	if (i == std::string::npos) return {};
	i = obj.find('"', i);
	if (i == std::string::npos) return {};
	size_t j = obj.find('"', i + 1);
	if (j == std::string::npos) return {};
	return obj.substr(i + 1, j - i - 1);
}

inline int json_int_field(const std::string &obj, const char *key, int fallback) {
	std::string pat = std::string("\"") + key + "\"";
	size_t i = obj.find(pat);
	if (i == std::string::npos) return fallback;
	i = obj.find(':', i);
	if (i == std::string::npos) return fallback;
	i++;
	while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
	return std::atoi(obj.c_str() + i);
}

inline std::vector<LobbyRoomInfo> parse_lobby_rooms(const std::string &json) {
	std::vector<LobbyRoomInfo> out;
	size_t pos = 0;
	while (true) {
		size_t a = json.find('{', pos);
		if (a == std::string::npos) break;
		size_t b = json.find('}', a);
		if (b == std::string::npos) break;
		std::string obj = json.substr(a, b - a + 1);
		pos = b + 1;
		LobbyRoomInfo r;
		r.id = json_string_field(obj, "id");
		r.host_ip = json_string_field(obj, "host_ip");
		r.map_id = json_string_field(obj, "map_id");
		r.host_port = static_cast<uint16_t>(json_int_field(obj, "host_port", 20595));
		r.players = json_int_field(obj, "players", 1);
		r.max_players = json_int_field(obj, "max_players", 8);
		if (!r.id.empty() || !r.host_ip.empty())
			out.push_back(r);
	}
	return out;
}

} // namespace rtso

#endif
