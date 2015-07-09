/** -*- mode: c++ -*-
 *
 * stdoutserver.cpp - Super simple server designed to demonstrate the
 * use of Boost ASIO by dumping the incoming contents to stdout.
 *
 * Copyright (C) 2015 Brian Davis
 * All Rights Reserved
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Author: Brian Davis <bd@jollyrogerlabs.com>
 * 
 */

// #include <cstdint>
#include <stdint.h>
#include <iostream>
#include <list>
#include <algorithm>
#include <iterator>
#include <numeric>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace std;
using boost::asio::ip::tcp;

using boost::asio::io_service;
using boost::asio::buffer;
using boost::asio::async_read;
using boost::system::error_code;
using boost::bind;
using boost::asio::placeholders::error;


/**
 * Helper template function that simply calls "operator delete" on a
 * pointer.
 */
template<typename T>
void
deleter(T *ptr)
{
  delete ptr;
}

/**
 * Function that shifts in incoming 32 bit unsigned integer in LSB
 * first format.
 */
uint32_t
LsbFirstShifter(const uint32_t current,
		const uint8_t next)
{
  return (current >> 8) | (static_cast<uint32_t>(next) << (8 * (sizeof(uint32_t) - 1)));
}

/**
 * Bulk of application logic goes here.
 */
class StdoutSession
{
public:
  StdoutSession(io_service &);

  /**
   * Return a reference to the socket instance, which is owned by this
   * class.
   */
  tcp::socket &getSocket();

  /**
   * Print session startup message and start reading the header from
   * the client.
   */
  void start();

private:
  enum {
    HEADER_LENGTH = sizeof(uint32_t)
  };

  /**
   * Called when the header read is complete.  Calculate the body size
   * based on the header data and start reading the body.
   */
  void handleHeader(const error_code &);

  /**
   * Called when the body has been read.
   */
  void handleBody(const error_code &);

  /**
   * Shut down the session.
   */
  void shutdown();

  io_service &svc_;
  tcp::socket socket_;
  vector<char> data_;
};

StdoutSession::
StdoutSession(io_service &svc)
  : svc_(svc), socket_(svc)
{
}

tcp::socket &
StdoutSession::
getSocket()
{
  return socket_;
}

void
StdoutSession::
start()
{
  // start session
  cerr << "New session connected on port "
       << socket_.local_endpoint().port() << endl;
  data_.resize(HEADER_LENGTH);
  // Start async header read.
  async_read(socket_,
	     // NOTE: read size is implicit in the size of the
	     // vector.
	     buffer(data_),
	     bind(&StdoutSession::handleHeader,
		  this,
		  // Calls handleHeader with error code.
		  boost::asio::placeholders::error));
}

void
StdoutSession::
handleHeader(const error_code &err)
{
  if (err) {
    cerr << "Error reading message header: " << err << endl;
    shutdown();
    return;
  }

  // Calculate body size.
  const uint32_t messageSize =
    accumulate(data_.begin(), data_.end(), 0, LsbFirstShifter);
  data_.resize(messageSize);

  // Start async body read.
  async_read(socket_,
	     // NOTE: read size is implicit in the size of the
	     // vector.
	     buffer(data_),
	     bind(&StdoutSession::handleBody,
		  this,
		  // Calls handleBody with error code.
		  error));
}

void
StdoutSession::
handleBody(const error_code &err)
{
  if (err) {
    cerr << "Error reading message header: " << err << endl;
    shutdown();
    return;
  }

  // Send data to stdout.
  copy(data_.begin(), data_.end(), ostream_iterator<char>(cout, ""));
  cout << endl;

  // Close the socket and shut down the session.
  shutdown();
}

void
StdoutSession::
shutdown()
{
  socket_.close();
  // Post a separate job to delete this object once the call to
  // shutdown() is complete.  Not completely necessary because it
  // should be safe to delete it here, but this demonstrates the use
  // of the post() function.
  svc_.post(bind(&deleter<StdoutSession>, this));
}

/**
 * Class that handles the process of listening to a socket, accepting
 * incoming connections, and starting the related StdoutSession
 * instance.
 */
class StdoutServer
{
public:
  StdoutServer(io_service &, const uint16_t);

  void start();

private:
  void acceptNext();

  void
  handleAccept(StdoutSession *,
	       const error_code &);

  io_service &svc_;
  tcp::acceptor acceptor_;
};

StdoutServer::
StdoutServer(io_service &svc,
	     const uint16_t listenPort)
  : svc_(svc),
    acceptor_(svc, tcp::endpoint(tcp::v4(), listenPort))
{
}

void
StdoutServer::
start()
{
  cerr << "stdout service listening on port "
       << acceptor_.local_endpoint().port()
       << endl;
  acceptNext();
}

void
StdoutServer::
acceptNext()
{
  StdoutSession *session = new StdoutSession(svc_);
  acceptor_.async_accept(session->getSocket(),
			 bind(&StdoutServer::handleAccept,
			      this, session, error));
}

void
StdoutServer::
handleAccept(StdoutSession *session,
	     const error_code &err)
{
  if (err) {
    cerr << "Error when handling connection accept: "
	 << err.message() << endl;
    delete session;
    return;
  }
  session->start();
  acceptNext();
}

int
main(const int argc,
     const char *argv[])
{
  assert(2 == argc);
  try {
    io_service svc;
    StdoutServer server(svc, atoi(argv[1]));
    server.start();
    svc.run();
  }
  catch (std::exception &exc) {
    cerr << "Exception at top level: " << exc.what() << endl;
  }
  catch (...) {
    cerr << "Unknown exception type at top level" << endl;
  }
}
