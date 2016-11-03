#include "socks4_proxy.h"

#include <boost\asio.hpp>

int main()
{
	boost::asio::io_service service;
	boost::shared_ptr<socks4_proxy::server> srv(new socks4_proxy::server(service, 1080));
	srv->start();
	service.run();
	return 0;
}