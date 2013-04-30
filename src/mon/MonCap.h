// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef CEPH_MONCAP_H
#define CEPH_MONCAP_H

#include <ostream>
using std::ostream;

#include "include/types.h"

static const __u8 MON_CAP_R     = (1 << 1);      // read
static const __u8 MON_CAP_W     = (1 << 2);      // write
static const __u8 MON_CAP_X     = (1 << 3);      // execute
static const __u8 MON_CAP_ANY   = 0xff;          // *

typedef __u8 rwxa_t;

ostream& operator<<(ostream& out, rwxa_t p);

struct MonCapSpec {
  rwxa_t allow;

  MonCapSpec() : allow(0) {}
  MonCapSpec(rwxa_t v) : allow(v) {}

  bool allow_all() const {
    return allow == MON_CAP_ANY;
  }
};

ostream& operator<<(ostream& out, const MonCapSpec& s);


struct MonCapMatch {
  std::string service;
  std::string pool;
  std::string command;

  MonCapMatch() {}
  MonCapMatch(std::string s, std::string p, std::string c) : service(s), pool(p), command(c) {}

  /**
   * check if given request parameters match our constraints
   *
   * @param service
   * @param pool
   * @param command
   * @return true if we match, false otherwise
   */
  bool is_match(const std::string& service, const std::string& pool, const std::string& command) const;
};

ostream& operator<<(ostream& out, const MonCapMatch& m);


struct MonCapGrant {
  MonCapMatch match;
  MonCapSpec spec;

  MonCapGrant() {}
  MonCapGrant(MonCapMatch m, MonCapSpec s) : match(m), spec(s) {}
};

ostream& operator<<(ostream& out, const MonCapGrant& g);


struct MonCap {
  std::vector<MonCapGrant> grants;

  MonCap() {}
  MonCap(std::vector<MonCapGrant> g) : grants(g) {}

  bool allow_all() const;
  void set_allow_all();
  bool parse(const std::string& str, ostream *err=NULL);

  /**
   * check if we are capable of something
   *
   * This method actually checks a description of a particular operation against
   * what the capability has specified.  Currently that is just rwx with matches
   * against pool, pool auid, and object name prefix.
   *
   * @param service service name
   * @param pool name of the pool we are accessing
   * @param command command id
   * @param op_may_read whether the operation may need to read
   * @param op_may_write whether the operation may need to write
   * @param op_may_exec whether the operation may exec
   * @return true if the operation is allowed, false otherwise
   */
  bool is_capable(const string& service, const string& pool, const string& command,
		  bool op_may_read, bool op_may_write, bool op_may_exec) const;
};

static inline ostream& operator<<(ostream& out, const MonCap& cap)
{
  return out << "moncap" << cap.grants;
}

#endif
