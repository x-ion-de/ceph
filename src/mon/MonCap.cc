// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix.hpp>

#include "MonCap.h"
#include "common/config.h"
#include "common/debug.h"
using std::ostream;
using std::vector;

ostream& operator<<(ostream& out, rwxa_t p)
{ 
  if (p == MON_CAP_ANY)
    return out << "*";

  if (p & MON_CAP_R)
    out << "r";
  if (p & MON_CAP_W)
    out << "w";
  if (p & MON_CAP_X)
    out << "x";
  return out;
}

ostream& operator<<(ostream& out, const MonCapSpec& s)
{
  return out << s.allow;
}

ostream& operator<<(ostream& out, const MonCapMatch& m)
{
  if (m.service.length()) {
    out << "service " << m.service << " ";
  }
  if (m.pool.length()) {
    out << "pool " << m.pool << " ";
  }
  if (m.command.length()) {
    out << "command " << m.command << " ";
  }
  return out;
}

bool MonCapMatch::is_match(const std::string& s, const std::string& p, const std::string& c) const
{
  if (pool.length()) {
    if (pool != p)
      return false;
  }
  if (service.length()) {
    if (service != s)
      return false;
  }
  if (command.length()) {
    if (command != c)
      return false;
  }
  return true;
}

ostream& operator<<(ostream& out, const MonCapGrant& g)
{
  return out << "grant(" << g.match << g.spec << ")";
}


bool MonCap::allow_all() const
{
  for (vector<MonCapGrant>::const_iterator p = grants.begin(); p != grants.end(); ++p)
    if (p->match.is_match(string(), string(), string()) && p->spec.allow_all())
      return true;
  return false;
}

void MonCap::set_allow_all()
{
  grants.clear();
  grants.push_back(MonCapGrant(MonCapMatch(), MonCapSpec(MON_CAP_ANY)));
}

bool MonCap::is_capable(const string& service, const string& pool, const string& command,
			bool op_may_read, bool op_may_write, bool op_may_exec) const
{
  rwxa_t allow = 0;
  for (vector<MonCapGrant>::const_iterator p = grants.begin();
       p != grants.end(); ++p) {
    if (p->match.is_match(service, pool, command)) {
      allow |= p->spec.allow;
      if ((op_may_read && !(allow & MON_CAP_R)) ||
	  (op_may_write && !(allow & MON_CAP_W)) ||
	  (op_may_exec && !(allow & MON_CAP_X)))
	continue;
      return true;
    }
  }
  return false;
}


// grammar
namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
namespace phoenix = boost::phoenix;

template <typename Iterator>
struct MonCapParser : qi::grammar<Iterator, MonCap()>
{
  MonCapParser() : MonCapParser::base_type(moncap)
  {
    using qi::char_;
    using qi::int_;
    using qi::lexeme;
    using qi::alnum;
    using qi::_val;
    using qi::_1;
    using qi::_2;
    using qi::_3;
    using qi::eps;
    using qi::lit;

    quoted_string %=
      lexeme['"' >> +(char_ - '"') >> '"'] | 
      lexeme['\'' >> +(char_ - '\'') >> '\''];
    unquoted_word %= +char_("a-zA-Z0-9_-");
    str %= quoted_string | unquoted_word;

    spaces = +lit(' ');

    // constraint := [thing[=]<name>]
    pool_name %= -(spaces >> lit("pool") >> (lit('=') | spaces) >> str);
    command_name %= -(spaces >> lit("command") >> (lit('=') | spaces) >> str);
    service_name %= -(spaces >> lit("service") >> (lit('=') | spaces) >> str);

    match = (service_name >> pool_name >> command_name)  [_val = phoenix::construct<MonCapMatch>(_1, _2, _3)];

    // rwxa := * | [r][w][x]
    rwxa =
      (spaces >> lit("*")[_val = MON_CAP_ANY]) |
      ( eps[_val = 0] >>
	(
	 spaces >>
	 ( lit('r')[_val |= MON_CAP_R] ||
	   lit('w')[_val |= MON_CAP_W] ||
	   lit('x')[_val |= MON_CAP_X]
	   )
	 )
	);
	 
    // capspec := * | rwx
    capspec = rwxa                                          [_val = phoenix::construct<MonCapSpec>(_1)];

    // grant := allow match capspec
    grant = (*lit(' ') >> lit("allow") >>
	     ((capspec >> match)       [_val = phoenix::construct<MonCapGrant>(_2, _1)] |
	      (match >> capspec)       [_val = phoenix::construct<MonCapGrant>(_1, _2)]) >>
	     *lit(' '));
    // moncap := grant [grant ...]
    grants %= (grant % (*lit(' ') >> (lit(';') | lit(',')) >> *lit(' ')));
    moncap = grants  [_val = phoenix::construct<MonCap>(_1)]; 
  }
  qi::rule<Iterator> spaces;
  qi::rule<Iterator, unsigned()> rwxa;
  qi::rule<Iterator, string()> quoted_string;
  qi::rule<Iterator, string()> unquoted_word;
  qi::rule<Iterator, string()> str;
  qi::rule<Iterator, MonCapSpec()> capspec;
  qi::rule<Iterator, string()> pool_name;
  qi::rule<Iterator, string()> service_name;
  qi::rule<Iterator, string()> command_name;
  qi::rule<Iterator, MonCapMatch()> match;
  qi::rule<Iterator, MonCapGrant()> grant;
  qi::rule<Iterator, std::vector<MonCapGrant>()> grants;
  qi::rule<Iterator, MonCap()> moncap;
};

bool MonCap::parse(const string& str, ostream *err)
{
  MonCapParser<string::const_iterator> g;
  string::const_iterator iter = str.begin();
  string::const_iterator end = str.end();

  bool r = qi::phrase_parse(iter, end, g, ascii::space, *this);
  if (r && iter == end)
    return true;

  // Make sure no grants are kept after parsing failed!
  grants.clear();

  if (err)
    *err << "moncap parse failed, stopped at '" << std::string(iter, end)
	 << "' of '" << str << "'\n";

  return false; 
}

