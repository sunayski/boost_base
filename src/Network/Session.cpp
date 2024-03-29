#include "Session.hpp"
#include "AApplication.h"
#include "Misc/Timer.hpp"
#include "Misc/Debug.hpp"

#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>

Session::Session(boost::asio::io_context &io)
    : Session(io, TCPSocket(io)) {
}

Session::Session(boost::asio::io_context &io, TCPSocket &&sock)
    : StrandHolder(io),
      m_socket(std::move(sock)),
      m_writing(false),
      m_closeOnWrite(false),
      m_closed(false),
      m_started(false) {
    debug_print(boost::format("Session::Session %1%") % this);
}

Session::~Session() {
    onDestroy(m_started);
    debug_print(boost::format("Session::~Session %1%") % this);
}

void Session::start() {
    auto self = shared_from_this();
    post([self]() {
        if (self->m_closed)
            return;

        self->preStart();
        self->startImpl();
    });
}

bool Session::started() const {
    STRAND_ASSERT(this);
    return m_started;
}

void Session::preStart() {
    m_started = true;
    onStart();
    m_socket.init();
}

void Session::startImpl() {
}

void Session::readAll(std::size_t size) {
    auto self = shared_from_this();
    startReceiveTimeout();
    boost::asio::async_read(m_socket, boost::asio::buffer(m_readBuffer, size),
                            strand().wrap([self, size](const boost::system::error_code &ec,
                                          std::size_t bytes_transferred) {
        self->emitReceivedData(self->m_readBuffer.data(), bytes_transferred, ec, size);
    }));
}

void Session::readSome(std::size_t maxSize) {
    const std::size_t size = std::min(maxSize, m_readBuffer.size());
    auto self = shared_from_this();
    startReceiveTimeout();
    m_socket.async_read_some(
                boost::asio::buffer(m_readBuffer, size),
                strand().wrap([self](const boost::system::error_code &ec,
                              std::size_t bytes_transferred) {
        self->emitReceivedData(self->m_readBuffer.data(), bytes_transferred, ec);
    }));
}

void Session::setReceiveTimeout(int sec) {
    m_receiveTimer.reset(new Timer(io(), sec));
    m_receiveTimer->setStrand(this, false);

    m_receiveTimer->onTimeout.connect([this] (Timer*) {
        STRAND_ASSERT(this);

        if (m_closed)
            return;

        debug_print(boost::format("Session receive timeout %1%") % this);
        close();
    });
}

void Session::startReceiveTimeout() {
    if (m_receiveTimer.get() == nullptr)
        return;

    m_receiveTimer->startTimer();
}

void Session::emitReceivedData(const uint8_t *ptr, std::size_t size,
                               const boost::system::error_code &ec,
                               const boost::optional<std::size_t>& expected) {
    //debug_print(boost::format("Session::emitReceivedData: %1% %2% %3% %4% %5%") % this % expected % exact % size % ec);

    if (m_receiveTimer.get() != nullptr) {
        m_receiveTimer->stopTimer();
    }

    if (m_closed)
        return;

    if (ec || (expected.has_value() && (expected.value() != size))) {
        errorHandler(ec);
        return;
    }

    onData(ptr, size);
}

void Session::writeAll(const uint8_t *ptr, std::size_t size) {
    auto self = shared_from_this();
    post([self, ptr, size]() {
        if (self->m_closed)
            return;

        self->doWrite(ptr, size);
    });
}

void Session::doWrite(const uint8_t *ptr, std::size_t size) {
    if (m_writing) {
        AAP->log(boost::format("Session::doWrite m_writing == true, FIX IT !!!!! %1%") % this);
        return;
    }

    m_writing = true;
    auto self = shared_from_this();

    boost::asio::async_write(
                m_socket, boost::asio::buffer(ptr, size),
                strand().wrap([self, size](boost::system::error_code ec,
                              std::size_t bytes_transferred) {
        if (self->m_closed) {
            return;
        }

        if (ec || (size != bytes_transferred)) {
            self->errorHandler(ec);
            return;
        }

        self->m_writing = false;
        self->onWriteDone();

        if (self->m_closeOnWrite) {
            self->closeOnWrite();
        }
    }));
}

TCPSocket& Session::socket() {
    return m_socket;
}

void Session::startSSL(bool client, TEvent r) {
    auto self = shared_from_this();

    socket().initSSL(client ? boost::asio::ssl::stream_base::handshake_type::client :
                              boost::asio::ssl::stream_base::handshake_type::server,
                     [self, r](bool success) {
        if (!success) {
            self->close();
            return;
        }

        r();
    });
}

void Session::errorHandler(const boost::system::error_code& ec) {
    if (!isDisconnect(ec)) {
        debug_print(boost::format("Session::errorHandler %1% %2%") % this % ec.message().c_str());
        onError(ec);
    }

    close();
}

bool Session::isDisconnect(const boost::system::error_code& code) {
    if ((boost::asio::error::eof == code) ||
            (boost::asio::error::connection_reset == code))
        return true;

    return false;
}

void Session::close() {
    auto self = shared_from_this();

    post([self]() {
        if (self->m_closed)
            return;

        self->m_socket.close();
        self->m_closed = true;
        self->onClose();
        self->disconnectAllSlots();
    });
}

void Session::disconnectAllSlots() {
    onData.disconnect_all_slots();
    onError.disconnect_all_slots();
    onWriteDone.disconnect_all_slots();
    onClose.disconnect_all_slots();
    onStart.disconnect_all_slots();
    // onDestroy не дисконектим, он должен вызываться в деструкторе
}

void Session::closeOnWrite() {
    auto self = shared_from_this();

    post([self]() {
        self->m_closeOnWrite = true;

        //debug_print("Session::closeOnWrite %p %i", self.get(), self->m_writing);

        if (!self->m_writing)
            self->close();
    });
}
