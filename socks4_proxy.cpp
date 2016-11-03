#include "socks4_proxy.h"

#include <iostream>
#include <string>
#include <boost\make_shared.hpp>

void socks4_proxy::session::wait_request_on_connection()
{
	msg_ptr msg(new msg());
	client_msgs.push(msg);
	async_read(client_socket_,buffer(msg->data.get(),msg->size),
		boost::bind(&session::request_read_complite,shared_from_this(), _1,_2),
		boost::bind(&session::do_connect, shared_from_this(),_1,_2));
}

size_t socks4_proxy::session::request_read_complite(const boost::system::error_code & err, size_t bytes)
{
	if (err) return 1;
	if (bytes < 9) return 1;
	msg_ptr msg = client_msgs.front();
	bool found = msg->data.get()[bytes-1] == 0;
	return found ? 0:1;
}

void socks4_proxy::session::do_connect(const boost::system::error_code & err,size_t bytes)
{	
	if (!err)
	{
		msg_ptr msg = client_msgs.front();
		client_msgs.pop();
		memcpy(&socks_request, msg->data.get(), sizeof(socks_request));
		socks_request.port = htons(socks_request.port);
		socks_request.ip = htonl(socks_request.ip);
		server_socket_.async_connect(
			ip::tcp::endpoint(ip::address_v4(socks_request.ip), socks_request.port),
			boost::bind(&session::on_connect, shared_from_this(), _1));
	}
	else
	{
		std::cerr << "Error wait on connaction"<<err.message()<<std::endl;
		stop();
	}
}

void socks4_proxy::session::on_connect(const boost::system::error_code & err)
{
	if (!err)
	{		
		memset(&socks_response, 0, sizeof(socks_response));
		socks_response.code = code_response::request_granted;
		msg_ptr msg(new msg());
		msg->size = sizeof(socks_response);
		memcpy(msg->data.get(), &socks_response, sizeof(socks_response));
		server_msgs.push(msg);

		std::cout << "Connection: " << ip::address_v4(socks_request.ip).to_string() << std::endl;

		do_write_to_client();
		service.post(boost::bind(&session::do_read_from_client, shared_from_this()));
		service.post(boost::bind(&session::do_read_from_server, shared_from_this()));
	}
	else
	{
		std::cerr << "Error connection: "<<err.message()<<std::endl;
		stop();
	}
}

void socks4_proxy::session::do_read_from_client()
{
	msg_ptr m = msg_ptr(new msg());
	client_socket_.async_read_some(buffer(m->data.get(),max_length),
		boost::bind(&session::on_read_from_client,shared_from_this(), m, _1, _2));
}

void socks4_proxy::session::on_read_from_client(msg_ptr msg, const boost::system::error_code & err, size_t bytes)
{
	if (err)
	{
		std::cerr << "error read_from_client: "<<err.message()<<std::endl;
		std::cout << "on_read_from_client: stop()" << std::endl;
		stop();
		return;
	}

	msg->size = bytes;
	client_msgs.push(msg);
	
	service.post(boost::bind(&session::do_read_from_client,shared_from_this()));
	service.post(boost::bind(&session::do_write_to_server, shared_from_this()));
}

void socks4_proxy::session::do_read_from_server()
{
	if (complete) return;

	msg_ptr m = msg_ptr(new msg());	
	server_socket_.async_read_some(buffer(m->data.get(), max_length),
		boost::bind(&session::on_read_from_server, shared_from_this(), m, _1, _2));
}

void socks4_proxy::session::on_read_from_server(msg_ptr msg, const boost::system::error_code & err, size_t bytes)
{
	if (err)
	{
		std::cout << "error read_from_server: " << err.message() << std::endl;
		if (err == error::eof)
		{
			std::cout << "server socket close\n";
			complete = true;
			server_socket_.close();
		}
		else
		{
			std::cout << "on_read_from_server: stop()" << std::endl;
			stop();
			return;
		}		
	}
		
	msg->size = bytes;
	server_msgs.push(msg);

	if (!complete)
		service.post(boost::bind(&session::do_read_from_server, shared_from_this()));
	service.post(boost::bind(&session::do_write_to_client, shared_from_this()));
}

void socks4_proxy::session::do_write_to_client()
{
	if (server_msgs.empty())
	{
		if (complete)
		{
			std::cout << "do_write_to_client: stop()" << std::endl;
			stop();
			return;
		}
		return;
	}
		
	msg_ptr m = server_msgs.front();	
	server_msgs.pop();
	async_write(client_socket_,buffer(m->data.get(), m->size), boost::bind(&session::on_write_to_client, shared_from_this(), m, _1, _2));
}

void socks4_proxy::session::on_write_to_client(msg_ptr msg, const boost::system::error_code & err,size_t bytes)
{		
	std::cout << "on_write_to_client complete :"<<complete 
		<<" count queue: "<<server_msgs.size()
		<<" write bytes: "<<bytes
		<<" msg size: "<<msg->size<<std::endl;
	
	if (bytes!=0 && bytes < msg->size)
	{
		memcpy(msg->data.get(), msg->data.get() + bytes, msg->size - bytes);
		msg->size -= bytes;
		server_msgs.push(msg);
		service.post(boost::bind(&session::do_write_to_client, shared_from_this()));
	}
	if (complete && server_msgs.empty())
		stop();
}

void socks4_proxy::session::do_write_to_server()
{
	if (client_msgs.empty() || complete) return;	

	msg_ptr m = client_msgs.front();
	client_msgs.pop();
	async_write(server_socket_, buffer(m->data.get(), m->size), boost::bind(&session::on_write_to_server, shared_from_this(), m, _1, _2));
}

void socks4_proxy::session::on_write_to_server(msg_ptr msg, const boost::system::error_code & err, size_t bytes)
{	
	if (bytes < msg->size)
	{
		memcpy(msg->data.get(), msg->data.get() + bytes, msg->size - bytes);
		msg->size -= bytes;
		client_msgs.push(msg);
		service.post(boost::bind(&session::do_write_to_server, shared_from_this()));
	}
}

void socks4_proxy::session::start()
{
	wait_request_on_connection();
}

void socks4_proxy::session::stop()
{
	boost::system::error_code err;
	if (client_socket_.is_open())
	{
		std::cout << "Client disconnected:"
			<< client_socket_.remote_endpoint(err).address().to_string() << ":" << client_socket_.remote_endpoint().port()
			<< std::endl;
	}
	client_socket_.shutdown(ip::tcp::socket::shutdown_both,err);
	client_socket_.close(err);
	server_socket_.close(err);
	
	client_msgs = std::queue<msg_ptr>();
	server_msgs = std::queue<msg_ptr>();
}

void socks4_proxy::server::start_accept()
{	
	boost::shared_ptr<session> new_session = boost::make_shared<session>(service);
	acceptor_.async_accept(new_session->socket(), boost::bind(&server::handle_accept,shared_from_this(),new_session,_1));
}

void socks4_proxy::server::handle_accept(boost::shared_ptr<session> new_session,const boost::system::error_code & err)
{
	if (!err)
	{
		std::cout << "client connected: " 
			<< new_session->socket().remote_endpoint().address().to_string()<<":"
			<< new_session->socket().remote_endpoint().port()
			<< std::endl;		
		new_session->start();
	}
	start_accept();
}