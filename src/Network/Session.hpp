#ifndef SESSION_H
#define SESSION_H

#include <boost/signals2.hpp>
#include <boost/array.hpp>

#include "Misc/EnableSharedFromThisVirtual.hpp"
#include "Misc/StrandHolder.hpp"
#include "Network/TCPSocket.hpp"

class Timer;

class Session : public enable_shared_from_this_virtual<Session>, public StrandHolder {
public:
    typedef std::function<void()> TEvent;

    Session(boost::asio::io_context &io);
    Session(boost::asio::io_context &io, TCPSocket&& sock);
    virtual ~Session();

    void start();
    void close();

    bool started() const;

    void startSSL(bool client, TEvent);

    void setReceiveTimeout(int sec);

    virtual void readSome(std::size_t maxSize = READ_BUFFER_SIZE);
    virtual void readAll(std::size_t size);
    virtual void writeAll(const uint8_t *ptr, std::size_t size);

    TCPSocket& socket();

    boost::signals2::signal<void (const uint8_t *ptr, std::size_t size)> onData;
    boost::signals2::signal<void (const boost::system::error_code& ec)> onError;
    boost::signals2::signal<void ()> onWriteDone;
    boost::signals2::signal<void ()> onStart;
    boost::signals2::signal<void ()> onClose;

    boost::signals2::signal<void (bool)> onDestroy;

protected:
    virtual void startImpl();
    void closeOnWrite();

private:
    static constexpr int READ_BUFFER_SIZE = 64 * 2 * 1024;

    void preStart();
    void errorHandler(const boost::system::error_code&);
    bool isDisconnect(const boost::system::error_code&);
    void doWrite(const uint8_t *ptr, std::size_t size);
    void disconnectAllSlots();
    void startReceiveTimeout();
    void emitReceivedData(const uint8_t *ptr, std::size_t size,
                          const boost::system::error_code &ec,
                          const boost::optional<std::size_t>& expected = boost::none);

    TCPSocket m_socket;
    std::array<uint8_t, READ_BUFFER_SIZE> m_readBuffer;
    bool m_writing;
    bool m_closeOnWrite;
    bool m_closed;
    bool m_started;
    std::unique_ptr<Timer> m_receiveTimer;
};

#endif // SESSION_H
