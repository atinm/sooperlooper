#ifndef DEBUG_H
#define DEBUG_H

#include <iostream>
#include <type_traits>
#include <array>
#include <string>
#include <sstream>
#include <stdexcept>

template<typename TF>
void write_debug_output( std::ostream & out, TF const& f ) {
  out << f;
}

struct __tracer__ {
  std::ostream & out;
  const char *file;
  const int line;
  const char *func;
  __tracer__( std::ostream & out, char const * file, int line, char const * func )
    : out( out ), file(file), line(line), func(func) {
    out << file << ":" << line << ": " << func << "(";
  }
  __tracer__( std::ostream & out, char const * file, int line )
    : out( out ), file(file), line(line), func(0) {
    out << " - " << file << ":" << line << ": ";
  }
  ~__tracer__() {
    if (func)
      out << "== " << func << " exit" << std::endl;
    else
      out << std::endl;
  }

  template<typename TF, typename ... TR>
  void write( TF const& f, TR const& ... rest ) {
    write_debug_output( out, f );
    out << " ";
    write( rest... );
  }
  template<typename TF>
  void write( TF const& f ) {
    write_debug_output( out, f );
    out << ")" << std::endl;
  }
  void write() {
    //handle the empty params case
    out << ")" << std::endl;
  }

  template<typename TF, typename ... TR>
  void log( TF const& f, TR const& ... rest ) {
    write_debug_output( out, f );
    out << " ";
    log( rest... );
  }
  template<typename TF>
  void log( TF const& f ) {
    write_debug_output( out, f );
  }
  void log() {
    //handle the empty params case
  }
};

#ifdef DEBUG
#define TRACE(...) __tracer__ __trace__( std::cerr, __FILE__, __LINE__, __func__ ); __trace__.write( __VA_ARGS__ )
#define LOG(...) do { __tracer__( std::cerr, __FILE__, __LINE__ ).log( __VA_ARGS__ ); } while(0)
#else
#define TRACE(...)
#define LOG(...)
#endif

#endif /* DEBUG_H */
