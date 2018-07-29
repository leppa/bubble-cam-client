/*
 *  BubbleCam Client
 *
 *  Copyright (c) 2018, Oleksii Serdiuk <contacts[at]oleksii[dot]name>
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define QT_NO_CAST_FROM_ASCII

#include "bubblecamclient.h"

#include <QDateTime>
#include <QTcpSocket>
#include <QTimer>

#include <chrono>

#define REQUEST "GET /bubble/live?ch=0&stream=0 HTTP/1.1\r\n\r\n"
#define REPLY_FAIL_TIMEOUT 5 * 1000
#define HEARTBEAT_INTERVAL 10 * 1000

#define DEFAULT_PORT 80
#define DEFAULT_USER "admin"
#define DEFAULT_CHANNEL 0

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(bubbleCamClientLog, "bubblecam.BubbleCamClient", QtWarningMsg)
#define DEBUG qCDebug(bubbleCamClientLog())
#define WARNING qCWarning(bubbleCamClientLog())
#define INFO qCInfo(bubbleCamClientLog())

enum class PackageType : qint8 {
    Message = 0x00,
    Media,
    Heartbeat,
    OpenChannel = 0x04,
    OpenStream = 0x0a
};

enum class MessageType : qint8 {
    Auth = 0x00,
    ChannelRequest,
    PtzControl,
    AuthReply,
    ChannelRequestReply
};

enum class MediaType : qint8 { Audio = 0x00, Idr, PSlice };

template <typename T>
quint32 packageSize();

#pragma pack(push, 1)
struct PackageHeader
{
    const quint8 magic = 0xaa;
    quint32 length_be = 0x00;
    PackageType packageType = PackageType::Message;
    quint32 timestamp_be = 0x00;

    PackageHeader()
    {
        qint64 microsecs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        // We truncate the most significant bits, as they won't fit into 32 bits
        timestamp_be = qToBigEndian<quint32>(static_cast<quint32>(microsecs));
    }
};

struct Message
{
    PackageHeader header;
    quint32 length_be = 0x00;
    MessageType messageType;
    const qint8 reserved[3] = {};
};

struct AuthMessage
{
    Message message;
    char user[20] = {};
    char pass[20] = {};

    AuthMessage()
    {
        message.header.length_be = qToBigEndian<quint32>(packageSize<AuthMessage>());
        message.messageType = MessageType::Auth;
        message.length_be =
            qToBigEndian<quint32>(sizeof(message.messageType) + sizeof(user) + sizeof(pass));
    }
};

struct AuthMessageReply
{
    Message message;
    qint8 verify;
    const quint8 reserved[3] = {};
    qint8 auth[32];

    AuthMessageReply()
    {
        message.header.length_be = qToBigEndian<quint32>(packageSize<AuthMessageReply>());
        message.messageType = MessageType::AuthReply;
        message.length_be = qToBigEndian<quint32>(sizeof(message.messageType) + sizeof(verify)
                                                  + sizeof(reserved) + sizeof(auth));
    }
};

struct OpenStreamMessage
{
    PackageHeader header;
    quint32 channel = 0x00;
    quint32 stream = 0x00;
    quint32 opened = 0x00;
    quint32 reserved = 0x00;

    OpenStreamMessage()
    {
        header.packageType = PackageType::OpenStream;
        header.length_be = qToBigEndian<quint32>(packageSize<OpenStreamMessage>());
    }
};

struct HeartbeatMessage
{
    PackageHeader header;
    qint8 payload = 0x02; // Seems to laways be 0x02

    HeartbeatMessage()
    {
        header.packageType = PackageType::Heartbeat;
        header.length_be = qToBigEndian<quint32>(packageSize<HeartbeatMessage>());
    }
};

struct MediaMessage
{
    PackageHeader header;
    quint32 length_be;
    MediaType mediaType;
    qint8 channelId;
};
#pragma pack(pop)

template <typename T>
quint32 packageSize()
{
    return sizeof(T) - sizeof(PackageHeader::magic) - sizeof(PackageHeader::length_be);
}

BubbleCamClient::BubbleCamClient() {}

BubbleCamClient::ErrorCode BubbleCamClient::startStreaming(const QHostAddress &hostName,
                                                           quint16 port, const QString &user,
                                                           const QString &password, quint8 channel,
                                                           quint8 stream)
{
    if (m_streaming)
        return ErrorCode::AlreadyStreaming;

    if (user.length() > 20 || password.length() > 20)
        return ErrorCode::UsernameOrPasswordTooLong;

    QScopedPointer<QTcpSocket> socket(new QTcpSocket());
    socket->connectToHost(hostName, port);
    if (!socket->waitForConnected())
        return ErrorCode::ConnectionTimeout;

    socket->write(REQUEST);
    if (!socket->waitForBytesWritten())
        return ErrorCode::WriteTimeout;
    if (!socket->waitForReadyRead(REPLY_FAIL_TIMEOUT))
        return ErrorCode::ReadTimeout;

    QByteArray reply = socket->readAll();
    const int i = reply.indexOf('\x00');
    if (i >= 0) {
        reply.truncate(i);
    }
    DEBUG << reply.size() << reply;

    AuthMessage auth;
    strcpy(auth.user, user.toUtf8().constData());
    strcpy(auth.pass, password.toUtf8().constData());

    QByteArray auth_package(reinterpret_cast<char *>(&auth), sizeof(AuthMessage));
    DEBUG << auth_package.size() << auth_package.toHex();
    socket->write(auth_package);
    if (!socket->waitForBytesWritten())
        return ErrorCode::WriteTimeout;
    if (!socket->waitForReadyRead(REPLY_FAIL_TIMEOUT))
        return ErrorCode::ReadTimeout;
    reply = socket->readAll();
    DEBUG << reply.size() << reply.toHex();

    if (!reply.startsWith('\xaa'))
        return ErrorCode::UnexpectedReply;

    {
        const PackageHeader *header = reinterpret_cast<const PackageHeader *>(reply.constData());
        if (header->packageType != PackageType::Message)
            return ErrorCode::UnexpectedReply;
        const Message *message = reinterpret_cast<const Message *>(reply.constData());
        if (message->messageType != MessageType::AuthReply)
            return ErrorCode::UnexpectedReply;
        const AuthMessageReply *authReply =
            reinterpret_cast<const AuthMessageReply *>(reply.constData());
        if (authReply->verify == 0)
            return ErrorCode::AuthenticationFailed;
    }

    OpenStreamMessage open_stream;
    open_stream.channel = channel;
    open_stream.stream = stream;
    open_stream.opened = 0x01;

    QByteArray open_stream_package(reinterpret_cast<char *>(&open_stream),
                                   sizeof(OpenStreamMessage));
    DEBUG << open_stream_package.size() << open_stream_package.toHex();
    socket->write(open_stream_package);
    if (!socket->waitForBytesWritten())
        return ErrorCode::WriteTimeout;
    if (!socket->waitForReadyRead(REPLY_FAIL_TIMEOUT))
        return ErrorCode::OpenStreamFailed;

    m_channel = channel;
    m_stream = stream;
    m_streaming = true;

    m_socket.reset(socket.take());
    connect(m_socket.data(), &QTcpSocket::readyRead, this, &BubbleCamClient::onReadyRead);
    connect(m_socket.data(), &QTcpSocket::disconnected, this, &BubbleCamClient::onDisconnected);
    connect(m_socket.data(), SIGNAL(error(QAbstractSocket::SocketError)),
            SLOT(onError(QAbstractSocket::SocketError)));

    m_heartbeatTimer.reset(new QTimer());
    connect(m_heartbeatTimer.data(), &QTimer::timeout, this,
            &BubbleCamClient::onHeartbeatTimerTimeout);
    m_heartbeatTimer->start(HEARTBEAT_INTERVAL);

    return ErrorCode::NoError;
}

BubbleCamClient::ErrorCode BubbleCamClient::startStreaming(const QHostAddress &hostName,
                                                           const QString &password, quint8 stream)
{
    return startStreaming(hostName, DEFAULT_PORT, QLatin1String(DEFAULT_USER), password,
                          DEFAULT_CHANNEL, stream);
}

BubbleCamClient::ErrorCode BubbleCamClient::startStreaming(const QHostAddress &hostName,
                                                           quint8 stream)
{
    return startStreaming(hostName, DEFAULT_PORT, QLatin1String(DEFAULT_USER), {}, DEFAULT_CHANNEL,
                          stream);
}

void BubbleCamClient::stopStreaming()
{
    if (!m_streaming)
        return;

    m_streaming = false;

    if (m_heartbeatTimer) {
        m_heartbeatTimer->stop();
        m_heartbeatTimer.reset();
    }

    if (m_socket) {
        OpenStreamMessage open_stream;
        open_stream.channel = m_channel;
        open_stream.stream = m_stream;
        open_stream.opened = 0x00;

        QByteArray open_stream_package(reinterpret_cast<char *>(&open_stream),
                                       sizeof(OpenStreamMessage));
        DEBUG << open_stream_package.size() << open_stream_package.toHex();
        m_socket->write(open_stream_package);
        if (!m_socket->waitForBytesWritten()) {
            m_socket->close();
            m_socket.reset();
            return;
        }

        m_socket->disconnectFromHost();

        if (m_socket->state() != QTcpSocket::UnconnectedState) {
            m_socket->waitForDisconnected();
        }
        m_socket.reset();
    }
}

BubbleCamClient::~BubbleCamClient()
{
    if (m_streaming)
        stopStreaming();
}

int BubbleCamClient::processMessage(const QByteArray &data)
{
    const MediaMessage *message = reinterpret_cast<const MediaMessage *>(data.constData());
    const qint32 size = static_cast<qint32>(qFromBigEndian<quint32>(message->length_be));

    DEBUG << "Got message" << qint8(message->header.packageType) << qint8(message->mediaType)
          << size;

    if (message->header.packageType != PackageType::Media) {
        WARNING << "Package not of Media type:" << qint8(message->header.packageType);
        if (bubbleCamClientLog().isDebugEnabled()
            && message->header.packageType == PackageType::Message
            && data.size() >= sizeof(Message)) {
            const Message *message = reinterpret_cast<const Message *>(data.constData());
            DEBUG << qint8(message->messageType) << data.size()
                  << data.left(sizeof(Message)).toHex();
        }
        return emitData(data.left(1));
    }

    const QByteArray messageData = data.mid(sizeof(MediaMessage), size);
    packet_left = size - messageData.size();
    audioActive = message->mediaType == MediaType::Audio;
    if (messageData.isEmpty())
        return sizeof(MediaMessage);

    switch (message->mediaType) {
    case MediaType::Audio:
        DEBUG << "Audio size:" << messageData.size();
        emit audioStream(messageData);
        audioActive = true;
        break;
    case MediaType::Idr:
    case MediaType::PSlice:
        DEBUG << "Video size:" << messageData.size();
        emit videoStream(messageData);
        break;
    default:
        WARNING << "Unknown media type:" << qint8(message->mediaType);
        return emitData(data.left(1));
    }
    return sizeof(MediaMessage) + messageData.size();
}

int BubbleCamClient::emitData(const QByteArray &data)
{
    packet_left = qMax(0, packet_left - data.size());
    if (audioActive) {
        emit audioStream(data);
    } else {
        emit videoStream(data);
    }
    return data.size();
}

void BubbleCamClient::onReadyRead()
{
    QByteArray data = m_socket->readAll();
    //    DEBUG << data.size();

    int offset = 0;
    while (offset < data.size()) {
        const int newOffset = data.indexOf('\xaa', offset + packet_left);
        if (newOffset > offset) {
            emitData(data.left(newOffset));
            offset = newOffset;
        }

        QByteArray mid = data.mid(offset);
        if (mid.startsWith('\xaa')) {
            // We might have a split MediaMessage, wait for more data
            while (mid.size() < sizeof(MediaMessage)) {
                m_socket->waitForReadyRead();
                data.append(m_socket->readAll());
                mid = data.mid(offset);
            }
            offset += processMessage(mid);
        } else {
            offset += emitData(mid);
        }
    }
}

void BubbleCamClient::onDisconnected()
{
    if (m_socket) {
        INFO << "Socket disconnected" << m_socket->errorString();
    } else {
        INFO << "Socket disconnected";
    }
    stopStreaming();
}

void BubbleCamClient::onError(QAbstractSocket::SocketError socketError)
{
    if (m_socket) {
        WARNING << "Socket error" << socketError << m_socket->errorString();
    } else {
        WARNING << "Socket error" << socketError;
    }
    stopStreaming();
}

void BubbleCamClient::onHeartbeatTimerTimeout()
{
    INFO << "Sending heartbeat";

    HeartbeatMessage heartbeat;
    QByteArray heartbeat_package(reinterpret_cast<char *>(&heartbeat), sizeof(HeartbeatMessage));
    DEBUG << heartbeat_package.size() << heartbeat_package.toHex();
    m_socket->write(heartbeat_package);
}
