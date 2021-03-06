/*
 * This file is part of PowerDNS or weakforced.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once
#include "ext/luawrapper/include/LuaContext.hpp"
#include "misc.hh"
#include <mutex>
#include <thread>
#include "json11.hpp"
#include "login_tuple.hh"
#include "customfunc.hh"

typedef std::tuple<int, std::string, std::string, KeyValVector> AllowReturn;

typedef std::function<AllowReturn(const LoginTuple&)> allow_t;
extern allow_t g_allow;
typedef std::function<void(const LoginTuple&)> report_t;
extern report_t g_report;
typedef std::function<bool(const std::string&, const std::string&, const ComboAddress&)> reset_t;
extern reset_t g_reset;
typedef std::function<std::string(const std::string&)> canonicalize_t;

struct CustomFuncMapObject {
  custom_func_t c_func;
  bool 		c_reportSink;
};

typedef std::map<std::string, CustomFuncMapObject> CustomFuncMap;
extern CustomFuncMap g_custom_func_map;

vector<std::function<void(void)>> setupLua(bool client, bool allow_report, LuaContext& c_lua, allow_t& allow_func, report_t& report_func, reset_t& reset_func, canonicalize_t& canon_func, CustomFuncMap& custom_func_map, const std::string& config);

struct LuaThreadContext {
  std::shared_ptr<LuaContext> lua_contextp;
  std::shared_ptr<std::mutex> lua_mutexp;
  allow_t allow_func;
  report_t report_func;
  reset_t reset_func;
  canonicalize_t canon_func;
  CustomFuncMap custom_func_map;
};

#define NUM_LUA_STATES 6

class LuaMultiThread
{
public:
  LuaMultiThread() : num_states(NUM_LUA_STATES),
		     state_index(0)
  {
    LuaMultiThread(num_states);
  }

  LuaMultiThread(unsigned int nstates) : num_states(nstates),
					 state_index(0)
  {
    for (unsigned int i=0; i<num_states; i++) {
      LuaThreadContext ltc;
      ltc.lua_contextp = std::make_shared<LuaContext>();
      ltc.lua_mutexp = std::make_shared<std::mutex>();
      lua_cv.push_back(ltc);
    }	
  }

  // these are used to setup the allow and report function pointers
  std::vector<LuaThreadContext>::iterator begin() { return lua_cv.begin(); }
  std::vector<LuaThreadContext>::iterator end() { return lua_cv.end(); }

  bool reset(const std::string& type, const std::string& login_value, const ComboAddress& ca_value) {
    auto lt_context = getLuaState();
    // lock the lua state mutex
    std::lock_guard<std::mutex> lock(*(lt_context.lua_mutexp));
    // call the reset function
    return lt_context.reset_func(type, login_value, ca_value);
  }

  AllowReturn allow(const LoginTuple& lt) {
    auto lt_context = getLuaState();
    // lock the lua state mutex
    std::lock_guard<std::mutex> lock(*(lt_context.lua_mutexp));
    // call the allow function
    return lt_context.allow_func(lt);
  }

  void report(const LoginTuple& lt) {
    auto lt_context = getLuaState();
    // lock the lua state mutex
    std::lock_guard<std::mutex> lock(*(lt_context.lua_mutexp));
    // call the report function
    lt_context.report_func(lt);
  }

  std::string canonicalize(const std::string& login) {
    auto lt_context = getLuaState();
    // lock the lua state mutex
    std::lock_guard<std::mutex> lock(*(lt_context.lua_mutexp));
    // call the canonicalize function
    return lt_context.canon_func(login);
  }

  CustomFuncReturn custom_func(const std::string& command, const CustomFuncArgs& cfa, bool& reportSinkReturn) {
    auto lt_context = getLuaState();
    // lock the lua state mutex
    std::lock_guard<std::mutex> lock(*(lt_context.lua_mutexp));
    // call the custom function
    for (const auto& i : lt_context.custom_func_map) {
      if (command.compare(i.first) == 0) {
	reportSinkReturn = i.second.c_reportSink;
	return i.second.c_func(cfa);
      }
    }
    return CustomFuncReturn(false, KeyValVector{});
  }
  
protected:
  LuaThreadContext& getLuaState()
  {
    std::lock_guard<std::mutex> lock(mutx);
    if (state_index >= num_states)
      state_index = 0;
    return lua_cv[state_index++];
  }
private:
  std::vector<LuaThreadContext> lua_cv;
  unsigned int num_states;
  unsigned int state_index;
  std::mutex mutx;
};

extern std::shared_ptr<LuaMultiThread> g_luamultip;
extern int g_num_luastates;
extern unsigned int g_num_sibling_threads;
#define WFORCE_NUM_SIBLING_THREADS 2
