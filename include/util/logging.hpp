#pragma once

#include "io/transport/hosts.hpp"
#include <functional>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <ostream>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace detail {

enum Code : int {
  FG_RED = 31,
  FG_GREEN = 32,
  FG_YELLOW = 33,
  FG_BLUE = 34,
  FG_MAGENTA = 35,
  FG_CYAN = 36,
  FG_DEFAULT = 39,
  FG_LIGHT_RED = 91,
  FG_LIGHT_GREEN = 92,
  FG_LIGHT_YELLOW = 93,
  FG_LIGHT_BLUE = 94,
  FG_LIGHT_MAGENTA = 95,
  FG_LIGHT_CYAN = 96,
  BG_RED = 41,
  BG_GREEN = 42,
  BG_BLUE = 44,
  BG_DEFAULT = 49,
  BOLD = 1,
  UNDERLINED = 4,
  DEFAULT = 0,
};

class Modifier {
  Code code;

public:
  Modifier(Code code) : code(code) {}

  static Modifier reset() { return Modifier(DEFAULT); }

  friend std::ostream &operator<<(std::ostream &os, const Modifier &mod) {
    return os << "\033[" << static_cast<int>(mod.code) << "m";
  }
};

enum class Mode {
  ERROR,
  DEBUG,
  INFO,
};

struct Tag {
  std::vector<Code> codes;
  size_t padding_right = 1;
  std::function<std::string()> metainfo = {};
};

} // namespace detail

class Logger {
  std::unordered_map<std::string, detail::Tag> tags;

public:
  static Logger &instance() {
    static Logger logger;
    return logger;
  }

  void log_error(const std::string &tag, const std::string &message) {
    log(detail::Mode::ERROR, tag, message);
  }

  void log_debug(const std::string &tag, const std::string &message) {
    log(detail::Mode::DEBUG, tag, message);
  }

  void log_info(const std::string &tag, const std::string &message) {
    log(detail::Mode::INFO, tag, message);
  }

private:
  std::mutex mutex_;

  Logger() { register_tags(); }

  void register_tags() {
    tags["EVENTS"] = {{detail::Code::FG_YELLOW}};
    tags["TIMERS"] = {{detail::Code::FG_LIGHT_BLUE}};
    tags["SOCKET"] = {{detail::Code::FG_LIGHT_RED}};
    tags["LOSSCH"] = {{detail::Code::FG_LIGHT_GREEN}};
    tags["RELICH"] = {{detail::Code::FG_LIGHT_MAGENTA}};
    tags["BRB"] = {{detail::Code::FG_LIGHT_CYAN}, 4};
    tags["URB"] = {{detail::Code::FG_BLUE}, 4};
    tags["FIFO"] = {{detail::Code::FG_MAGENTA}, 3};
    tags["LATTIC"] = {{detail::Code::FG_CYAN}};
    tags["SYSTEM"] = {{detail::Code::FG_CYAN}};
    tags["TID"] = {{detail::Code::FG_GREEN, detail::Code::BOLD}, 1, []() {
                     std::stringstream ss;
                     ss << std::this_thread::get_id();
                     return ss.str();
                   }};
    tags["ERROR"] = {{detail::Code::FG_RED, detail::Code::BOLD}};
    tags["DEBUG"] = {{detail::Code::FG_MAGENTA, detail::Code::BOLD}};
    tags["INFO"] = {{detail::Code::FG_BLUE}, 2};
  }

  void print_tag(std::ostream &out, const std::string &tag) {
    auto &tag_info = tags[tag];
    out << "[";
    for (const auto &code : tag_info.codes) {
      out << detail::Modifier(code);
    }
    out << tag;
    out << detail::Modifier::reset();
    if (tag_info.metainfo) {
      out << ": " << tag_info.metainfo();
    }
    out << "]";
    out << std::string(tag_info.padding_right, ' ');
  }

  void log(detail::Mode mode, const std::string &tag,
           const std::string &message) {
    std::lock_guard lock(mutex_);
    switch (mode) {
    case detail::Mode::ERROR:
      print_tag(std::cout, "ERROR");
      break;
    case detail::Mode::DEBUG:
      print_tag(std::cout, "DEBUG");
      break;
    case detail::Mode::INFO:
      print_tag(std::cout, "INFO");
      break;
    default:
      break;
    }
    print_tag(std::cout, "TID");
    print_tag(std::cout, tag);
    std::cout << message << std::endl;
    std::cout.flush();
  }
};

// Level of verbosity
// #define ERROR
// #define INFO
// #define DEBUG

#if defined(ERROR) || defined(INFO) || defined(DEBUG)
#define LOG_ERROR(tag, message) (Logger::instance().log_error(tag, message))
#else
#define LOG_ERROR(tag, message) (true)
#endif

#if defined(INFO) || defined(DEBUG)
#define LOG_INFO(tag, message) (Logger::instance().log_info(tag, message))
#else
#define LOG_INFO(tag, message) (true)
#endif

#ifdef DEBUG
#define LOG_DEBUG(tag, message) (Logger::instance().log_debug(tag, message))
#else
#define LOG_DEBUG(tag, message) (true)
#endif

// Util functions for the logging

inline std::string ToString(sockaddr_in addr_info) {
  return ":" + std::to_string(static_cast<int>(ntohs(addr_info.sin_port)));
}
