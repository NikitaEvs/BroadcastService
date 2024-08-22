#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <algorithm>
#include <cctype>
#include <locale>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

class Parser {
public:
  struct Host {
    Host() {}
    Host(size_t id, std::string &ip_or_hostname, unsigned short port)
        : id{id}, port{htons(port)} {

      if (isValidIpAddress(ip_or_hostname.c_str())) {
        ip = inet_addr(ip_or_hostname.c_str());
      } else {
        ip = ipLookup(ip_or_hostname.c_str());
      }
    }

    std::string ipReadable() const {
      in_addr tmp_ip;
      tmp_ip.s_addr = ip;
      return std::string(inet_ntoa(tmp_ip));
    }

    unsigned short portReadable() const { return ntohs(port); }

    unsigned long id;
    in_addr_t ip;
    unsigned short port;

  private:
    bool isValidIpAddress(const char *ipAddress) {
      struct sockaddr_in sa;
      int result = inet_pton(AF_INET, ipAddress, &(sa.sin_addr));
      return result != 0;
    }

    in_addr_t ipLookup(const char *host) {
      struct addrinfo hints, *res;
      char addrstr[128];
      void *ptr;

      memset(&hints, 0, sizeof(hints));
      hints.ai_family = PF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags |= AI_CANONNAME;

      if (getaddrinfo(host, NULL, &hints, &res) != 0) {
        throw std::runtime_error(
            "Could not resolve host `" + std::string(host) +
            "` to IP: " + std::string(std::strerror(errno)));
      }

      while (res) {
        inet_ntop(res->ai_family, res->ai_addr->sa_data, addrstr, 128);

        switch (res->ai_family) {
        case AF_INET:
          ptr =
              &(reinterpret_cast<struct sockaddr_in *>(res->ai_addr))->sin_addr;
          inet_ntop(res->ai_family, ptr, addrstr, 128);
          freeaddrinfo(res);
          return inet_addr(addrstr);
          break;
        // case AF_INET6:
        //     ptr = &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
        //     break;
        default:
          break;
        }
        res = res->ai_next;
      }

      freeaddrinfo(res);

      throw std::runtime_error("No host resolves to IPv4");
    }
  };

public:
  Parser(const int argc, char const *const *argv, bool withConfig = true)
      : argc{argc}, argv{argv}, withConfig{withConfig}, parsed{false} {}

  void parse() {
    if (!parseInternal()) {
      help(argc, argv);
    }

    parsed = true;
  }

  unsigned long id() const {
    checkParsed();
    return id_;
  }

  const char *hostsPath() const {
    checkParsed();
    return hostsPath_.c_str();
  }

  const char *outputPath() const {
    checkParsed();
    return outputPath_.c_str();
  }

  const char *configPath() const {
    checkParsed();
    if (!withConfig) {
      throw std::runtime_error("Parser is configure to ignore the config path");
    }

    return configPath_.c_str();
  }

  std::vector<Host> hosts() {
    std::ifstream hostsFile(hostsPath());
    std::vector<Host> hosts;

    if (!hostsFile.is_open()) {
      std::ostringstream os;
      os << "`" << hostsPath() << "` does not exist.";
      throw std::invalid_argument(os.str());
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(hostsFile, line)) {
      lineNum += 1;

      std::istringstream iss(line);

      trim(line);
      if (line.empty()) {
        continue;
      }

      unsigned long id;
      std::string ip;
      unsigned short port;

      if (!(iss >> id >> ip >> port)) {
        std::ostringstream os;
        os << "Parsing for `" << hostsPath() << "` failed at line " << lineNum;
        throw std::invalid_argument(os.str());
      }

      hosts.push_back(Host(id, ip, port));
    }

    if (hosts.size() < 2UL) {
      std::ostringstream os;
      os << "`" << hostsPath() << "` must contain at least two hosts";
      throw std::invalid_argument(os.str());
    }

    auto comp = [](const Host &x, const Host &y) { return x.id < y.id; };
    auto result = std::minmax_element(hosts.begin(), hosts.end(), comp);
    size_t minID = (*result.first).id;
    size_t maxID = (*result.second).id;
    if (minID != 1UL || maxID != static_cast<unsigned long>(hosts.size())) {
      std::ostringstream os;
      os << "In `" << hostsPath()
         << "` IDs of processes have to start from 1 and be compact";
      throw std::invalid_argument(os.str());
    }

    std::sort(hosts.begin(), hosts.end(),
              [](const Host &a, const Host &b) -> bool { return a.id < b.id; });

    return hosts;
  }

private:
  bool parseInternal() {
    if (!parseID()) {
      return false;
    }

    if (!parseHostPath()) {
      return false;
    }

    if (!parseOutputPath()) {
      return false;
    }

    if (!parseConfigPath()) {
      return false;
    }

    return true;
  }

  void help(const int, char const *const *argv) {
    auto configStr = "CONFIG";
    std::cerr << "Usage: " << argv[0]
              << " --id ID --hosts HOSTS --output OUTPUT";

    if (!withConfig) {
      std::cerr << "\n";
    } else {
      std::cerr << " CONFIG\n";
    }

    exit(EXIT_FAILURE);
  }

  bool parseID() {
    if (argc < 3) {
      return false;
    }

    if (std::strcmp(argv[1], "--id") == 0) {
      if (isPositiveNumber(argv[2])) {
        try {
          id_ = std::stoul(argv[2]);
        } catch (std::invalid_argument const &e) {
          return false;
        } catch (std::out_of_range const &e) {
          return false;
        }

        return true;
      }
    }

    return false;
  }

  bool parseHostPath() {
    if (argc < 5) {
      return false;
    }

    if (std::strcmp(argv[3], "--hosts") == 0) {
      hostsPath_ = std::string(argv[4]);
      return true;
    }

    return false;
  }

  bool parseOutputPath() {
    if (argc < 7) {
      return false;
    }

    if (std::strcmp(argv[5], "--output") == 0) {
      outputPath_ = std::string(argv[6]);
      return true;
    }

    return false;
  }

  bool parseConfigPath() {
    if (!withConfig) {
      return true;
    }

    if (argc < 8) {
      return false;
    }

    configPath_ = std::string(argv[7]);
    return true;
  }

  bool isPositiveNumber(const std::string &s) const {
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) {
                           return !std::isdigit(c);
                         }) == s.end();
  }

  void checkParsed() const {
    if (!parsed) {
      throw std::runtime_error("Invoke parse() first");
    }
  }

  void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                    [](int ch) { return !std::isspace(ch); }));
  }

  void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](int ch) { return !std::isspace(ch); })
                .base(),
            s.end());
  }

  void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
  }

private:
  const int argc;
  char const *const *argv;
  bool withConfig;

  bool parsed;

  unsigned long id_;
  std::string hostsPath_;
  std::string outputPath_;
  std::string configPath_;
};
