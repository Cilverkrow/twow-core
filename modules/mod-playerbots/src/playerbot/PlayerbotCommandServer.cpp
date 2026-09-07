
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotCommandServer.h"
#include "WorldThreadCommandQueue.h"
#include <cstdlib>
#include <future>
#include <iostream>

INSTANTIATE_SINGLETON_1(PlayerbotCommandServer);

#include <boost/bind/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>

using boost::asio::ip::tcp;
typedef boost::shared_ptr<tcp::socket> socket_ptr;

bool ReadLine(socket_ptr sock, std::string* buffer, std::string* line)
{
    // Do the real reading from fd until buffer has '\n'.
    std::string::iterator pos;
    while ((pos = find(buffer->begin(), buffer->end(), '\n')) == buffer->end())
    {
        char buf[1025];
        boost::system::error_code error;
        size_t n = sock->read_some(boost::asio::buffer(buf), error);
        if (n == -1 || error == boost::asio::error::eof)
            return false;
        else if (error)
            throw boost::system::system_error(error); // Some other error.

        buf[n] = 0;
        *buffer += buf;
    }

    *line = std::string(buffer->begin(), pos);
    *buffer = std::string(pos + 1, buffer->end());
    return true;
}

void session(socket_ptr sock)
{
    try
    {
        std::string buffer, request;
        while (ReadLine(sock, &buffer, &request)) {
            // This is a connection thread, not the world thread. Resolving the
            // bot guid here and dereferencing the Player* / PlayerbotAI* it yields
            // was a use-after-free the moment the world despawned that bot on the
            // same tick. Post the bytes instead and let the world thread answer.
            std::future<std::string> pending = sRandomPlayerbotMgr.PostRemoteCommand(request);

            // Bounded, because the world thread may already be shutting down and
            // will then never drain the queue again. Without this the connection
            // thread parks forever on a promise nobody is left to fulfil.
            std::string response;
            if (pending.wait_for(WorldThreadCommandQueue::WaiterTimeout) == std::future_status::ready)
                response = pending.get() + "\n";
            else
                response = "timeout\n";

            boost::asio::write(*sock, boost::asio::buffer(response.c_str(), response.size()));
            request = "";
        }
    }
    catch (std::exception& e)
    {
        sLog.outError("%s",e.what());
    }
    catch (...)
    {
        // Anything outside the std::exception hierarchy would escape this
        // detached connection thread and std::terminate the whole worldserver.
        sLog.outError("Playerbot command server: connection thread threw a non-standard exception");
    }
}

void server(boost::asio::io_context& io_context, short port)
{
    tcp::acceptor a(io_context, tcp::endpoint(tcp::v4(), port));
    for (;;)
    {
        socket_ptr sock(new tcp::socket(io_context));
        a.accept(*sock);
        // One DETACHED thread per connection. ~boost::thread already detached it
        // implicitly when `t` left scope, which made this an undeclared detached
        // thread that no audit of detach() call sites would ever find. Saying it
        // out loud documents the situation; it does not fix it. There is still no
        // stop path: the listener thread is itself detached in Start(), the accept
        // loop below is unconditional, and nothing joins or cancels any of this at
        // shutdown. Converting the acceptor to asio async with a real stop token is
        // a separate change.
        boost::thread t(boost::bind(session, sock));
        t.detach();
    }
}

void Run()
{
    if (!sPlayerbotAIConfig.commandServerPort) {
        return;
    }

    std::ostringstream s; s << "Starting Playerbot Command Server on port " << sPlayerbotAIConfig.commandServerPort;
    sLog.outString("%s",s.str().c_str());

    try
    {
        boost::asio::io_context io_context;
        server(io_context, sPlayerbotAIConfig.commandServerPort);
    }
    catch (std::exception& e)
    {
        sLog.outError("%s",e.what());
    }
    catch (...)
    {
        // Anything outside the std::exception hierarchy would escape this
        // detached listener thread and std::terminate the whole worldserver.
        sLog.outError("Playerbot command server: listener thread threw a non-standard exception");
    }
}

void PlayerbotCommandServer::Start()
{
    std::thread serverThread(Run);
    serverThread.detach();
}
