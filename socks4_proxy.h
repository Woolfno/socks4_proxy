#ifndef __SOCKS4_PROXY__
#define __SOCKS4_PROXY__

#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif

#include <boost\shared_array.hpp>
#include <boost\asio.hpp>
#include <boost\bind.hpp>
#include <boost\enable_shared_from_this.hpp>
#include <string>
#include <queue>

namespace socks4_proxy
{
	using namespace boost::asio;
	
	struct request
	{
		uint8_t ver;
		uint8_t cmd;
		uint16_t port;
		uint32_t ip;
		char username[32];
	};

	struct response
	{
		uint8_t pad = 0;
		uint8_t code;
		uint8_t pads[6] = { 0 };
	};	

	enum code_response { 
		request_granted = 0x5a,
		request_failed = 0x5b,
		request_failed_no_identd = 0x5c,
		request_failed_bad_user_id = 0x5d
	};

	typedef boost::shared_array<char> buffer_ptr;
	enum { max_length = 1024*4 };
	struct msg
	{
		buffer_ptr data;
		size_t size;

		msg() :size(max_length), data(new char[max_length]) { };
		msg(char * d, size_t s) :size(s)
		{
			data=buffer_ptr(new char[size]);
			memcpy(data.get(), d, size);
		};
	};
	typedef boost::shared_ptr<msg> msg_ptr;

	class session :public boost::enable_shared_from_this<session>,boost::noncopyable
	{
	private:
		ip::tcp::socket client_socket_;		//сокет клиета
		ip::tcp::socket server_socket_;		//сокет внешнего сервера
		bool complete;

		io_service & service;

		request socks_request;
		response socks_response;
				
		std::queue<msg_ptr> client_msgs;	//от клиента серверу
		std::queue<msg_ptr> server_msgs;	//от сервера клиенту		

		void wait_request_on_connection();
		void do_connect(const boost::system::error_code & err, size_t bytes);
		void on_connect(const boost::system::error_code & err);		
		void do_read_from_client();
		void on_read_from_client(msg_ptr msg, const boost::system::error_code & err, size_t bytes);
		void do_read_from_server();		
		void on_read_from_server(msg_ptr msg, const boost::system::error_code & err, size_t bytes);
		size_t request_read_complite(const boost::system::error_code & err, size_t bytes);
		void do_write_to_client();
		void on_write_to_client(msg_ptr msg, const boost::system::error_code & err, size_t bytes);
		void do_write_to_server();
		void on_write_to_server(msg_ptr msg, const boost::system::error_code & err, size_t bytes);

	public:
		session(io_service & service) :
			client_socket_(service),
			server_socket_(service),
			service(service),
			complete(false)
		{
			memset(&socks_request, 0, sizeof(socks_request));
		};

		void start();
		void stop();
		ip::tcp::socket & socket() { return client_socket_; };
	};

	class server: public boost::enable_shared_from_this<server>
	{
	private:
		ip::tcp::acceptor acceptor_;
		io_service & service;
		
		void start_accept();
		void handle_accept(boost::shared_ptr<session> client, const boost::system::error_code & err);

	public:
		server(io_service & service, int port):
			service(service),acceptor_(service, ip::tcp::endpoint(ip::tcp::v4(), port))
		{}
		
		void start() { start_accept(); };
	};

};

#endif