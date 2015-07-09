/** -*- mode: c++ -*-
 *
 * stdoutclient.cpp - Simple client designed to work with stdoutserver
 * to demonstrate the use of Boost ASIO.
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

#include <cassert>
#include <iostream>
#include <iterator>
#include <vector>
#include <algorithm>

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>

using namespace std;
using boost::asio::ip::tcp;
using boost::asio::io_service;
using boost::system::error_code;
using boost::bind;

/**
 * Function object that reads a 32 bit number as a series of 8 bit
 * values by reading the LSB and shifting to the right by 8 on each
 * application.
 */
struct LsbFirstShifter
{
public:
  LsbFirstShifter(const uint32_t init)
    : value_(init)
  {
  }

  uint8_t
  operator()()
  {
    const uint8_t result = static_cast<uint8_t>(value_ & 255);
    value_ >>= 8;
    return result;
  }

private:
  uint32_t value_;
};

/**
 * Helper class that encapsulates the behavior of resolving an address
 * with DNS and then connecting to it, all asynchronously.
 */
class async_connector
{
public:
  // Pass the error code and the socket to the final handler.
  typedef boost::function<void(const error_code &, tcp::socket &)> ConnectionHandler;

  async_connector(io_service &);

  /**
   * Start the process of connecting to the server asynchronously.
   *
   * @param server - name of server to connect to.
   *
   * @param portOrServiceName - number of port or name of service to
   *        connect to.
   *
   * @param connectHandler - handler functor to call when the
   *        connection has been completed.
   */
  void
  async_connect(const string,
		const string,
		ConnectionHandler &);

private:

  /**
   * Helper function that avoids repetition of the call to
   * asio::async_connect().
   *
   * @param iter - connection endpoint iterator referencing the next
   *        endpoint to try connecting to.
   *
   * @param connectHandler - handler functor to call when the
   *        connection has been completed.
   */
  void
  doAsyncConnect(tcp::resolver::iterator,
		 ConnectionHandler &);

  /**
   * Handler for completion of DNS resolution, either error or
   * success.
   *
   * @param connectHandler - handler functor to call when the
   *        connection has been completed.
   *
   * @param err - code associated with any error that occurred during
   *        the resolution process.
   *
   * @param iter - connection endpoint iterator referencing the first
   *        endpoint to try connecting to.
   */
  void
  handleResolution(ConnectionHandler &,
		   const error_code &,
		   tcp::resolver::iterator);

  /**
   * Handler function for completion of connection, either error or
   * success.
   *
   * @param connectHandler - handler functor to call when the
   *        connection has been completed.
   *
   * @param err - code associated with any error that occurred during
   *        the resolution process.
   *
   * @param iter - connection endpoint iterator referencing the first
   *        endpoint to try connecting to.
   */
  void
  handleConnectionComplete(ConnectionHandler &,
			   const error_code &,
			   tcp::resolver::iterator);

  tcp::resolver resolver_;
  tcp::socket socket_;
  const string server_;
};

async_connector::
async_connector(io_service &svc)
  : resolver_(svc), socket_(svc)
{
}

void
async_connector::
async_connect(const string server,
	      const string portOrServiceName,
	      ConnectionHandler &connectHandler)
{
  tcp::resolver::query query(server, portOrServiceName);
  // Resolve the address associated with the (server, port/service)
  // pair asynchronously and call async_connector::handleResolution
  // when the resolution is complete.
  resolver_.async_resolve(query,
			  boost::bind(&async_connector::handleResolution,
				      this,
				      connectHandler,
				      // Calls resolution handler with
				      // error code.
				      boost::asio::placeholders::error,
				      // Calls resolution handler with
				      // endpoint iterator.
				      boost::asio::placeholders::iterator));
}

void
async_connector::
doAsyncConnect(tcp::resolver::iterator iter,
	       ConnectionHandler &connectHandler)
{
  // Extract the next endpoint from the iterator.
  tcp::endpoint endpoint = *iter;
  socket_.async_connect(endpoint,
			bind(&async_connector::handleConnectionComplete,
			     this,
			     connectHandler,
			     // Calls connect handler with
			     // error code.
			     boost::asio::placeholders::error,
			     // Increment iterator for next
			     // attempt (in case this one
			     // fails).
			     ++iter));
}

void
async_connector::
handleResolution(ConnectionHandler &connectHandler,
		 const error_code &err,
		 tcp::resolver::iterator iter)
{
  if (err) {
    cerr << "Error when resolving: " << err. message() << endl;
    // Pass the error code and socket to the connection handler.
    connectHandler(err, socket_);
  }
  else {
    doAsyncConnect(iter, connectHandler);
  }
}

void
async_connector::
handleConnectionComplete(ConnectionHandler &connectHandler,
			 const error_code &err,
			 tcp::resolver::iterator iter)
{
  if (err) {
    if (tcp::resolver::iterator() != iter) {
      // Connection attempt failed for the previous endpoint in the
      // list, try the current one.
      socket_.close();
      doAsyncConnect(iter, connectHandler);
      // NOTE: doAsyncConnect has supplied the io_service with new
      // work, so it will not return when control passes out of this
      // function.
    }
    else {
      // Connection attempt failed for all endpoints, or some other
      // error occurred.
      cerr << "Error when connecting: " << err.message() << endl;
    }
  }
  else {
    // Success; call handler, passing it the error code and socket.
    connectHandler(err, socket_);
  }
}

/**
 * Bulk of application logic goes here.
 */
class StdoutClient
{
public:
  // this typedef makes it possible for operator() of this class to be
  // called by boost::bind without further type resolution
  typedef void result_type;

  StdoutClient(io_service &svc)
    : svc_(svc)
  {
  }

  /**
   * This function is called when the connection is completed.  It
   * builds the outgoing buffer and starts an asynchronous send
   * operation to send it out.
   */
  void
  operator()(const error_code &err,
	     tcp::socket &socket)
  {
    // connected
    const tcp::endpoint remote = socket.remote_endpoint();
    cerr << "Stdout client now connected to server at address "
	 << remote.address().to_string() << " on port "
	 << remote.port() << endl;
    const char *HELLO = "Hello stdout server!";
    const uint32_t size = strlen(HELLO);
    data_.resize(0);
    // generate the header
    generate_n(back_inserter(data_), sizeof(size), LsbFirstShifter(size));

    const size_t dbgHeaderSize = data_.size();

    // generate the body
    copy(HELLO, HELLO + size, back_inserter(data_));

    cerr << "Writing " << dbgHeaderSize << " bytes for header, "
	 << (data_.size() - dbgHeaderSize) << " bytes for body, totaling "
	 << data_.size() << " bytes" << endl;

    // send to server
    boost::asio::async_write(socket, boost::asio::buffer(data_),
			     bind(&StdoutClient::handleWriteComplete, this,
				  boost::asio::placeholders::error,
				  boost::asio::placeholders::bytes_transferred,
				  // Pass the socket
				  boost::ref(socket)));
  }

private:

  /**
   * This function is called when the asynchronous send is complete.
   */
  void
  handleWriteComplete(const error_code &err,
		      const size_t bytesWritten,
		      tcp::socket &socket)
  {
    if (err) {
      cerr << "Error on write: " << err << endl;
    }
    else {
      cerr << "Successfully wrote " << bytesWritten << " bytes" << endl;

      // give the server some time to work, closing the connection too
      // early will cause an error on the other end.
      sleep(1);
    }

    // shut down the connection in all cases
    socket.close();
  }

  io_service &svc_;
  vector<uint8_t> data_;
};

int
main(const int argc,
     const char *argv[])
{
  assert(3 == argc);
  try {
    io_service svc;
    StdoutClient client(svc);
    async_connector connector(svc);
    async_connector::ConnectionHandler handler = bind(client, _1, _2);
    connector.async_connect(argv[1], argv[2], handler);
    svc.run();
  }
  catch (std::exception &exc) {
    cerr << "Exception at top level: " << exc.what() << endl;
  }
  catch (...) {
    cerr << "Unknown exception type at top level" << endl;
  }
}
