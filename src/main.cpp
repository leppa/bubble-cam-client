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

#include <QtEndian>

#include <QCoreApplication>
#include <QDateTime>
#include <QTcpSocket>
#include <QFile>

#include <chrono>

#include <QDebug>

#define HOST "192.168.1.26"
#define PORT 80
#define USER "admin"
#define PASS ""

#define REQUEST "GET /bubble/live?ch=0&stream=0 HTTP/1.1\r\n\r\n"

template <typename T>
quint32 packageSize()
{
    return sizeof(T) - sizeof(PackageHeader::magic) - sizeof(PackageHeader::length_be);
}

#pragma pack(push, 1)
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

struct PackageHeader
{
    const quint8 magic = 0xaa;
    quint32 length_be = 0x00;
    PackageType packageType = PackageType::Message;
    quint32 timestamp_be = 0x00;

    PackageHeader()
    {
        // Should be microseconds since epoch, but it's not possible to fit them into 32 bits
        auto secs = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
        timestamp_be = qToBigEndian<quint32>(secs);
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

struct MediaMessage
{
    PackageHeader header;
    quint32 length_be;
    MediaType mediaType;
    qint8 channelId;
};
#pragma pack(pop)

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    QTcpSocket socket;
    socket.connectToHost(QLatin1String(HOST), PORT);
    if (!socket.waitForConnected())
        return 1;

    socket.write(REQUEST);
    if (!socket.waitForBytesWritten())
        return 2;
    if (!socket.waitForReadyRead())
        return 3;

    QByteArray reply = socket.readAll();
    const int i = reply.indexOf('\x00');
    if (i >= 0) {
        reply.truncate(i);
    }
    qDebug() << reply.size() << reply;

    AuthMessage auth;
    strcpy(auth.user, USER);
    strcpy(auth.pass, PASS);

    QByteArray auth_package(reinterpret_cast<char *>(&auth), sizeof(AuthMessage));
    qDebug() << auth_package.length() << auth_package.toHex(' ');
    socket.write(auth_package);
    if (!socket.waitForBytesWritten())
        return 2;
    if (!socket.waitForReadyRead())
        return 3;
    reply = socket.readAll();
    qDebug() << reply.size() << reply.toHex(' ');

    if (!reply.startsWith('\xaa'))
        return 4;

    {
        const PackageHeader *header = reinterpret_cast<const PackageHeader *>(reply.constData());
        if (header->packageType != PackageType::Message)
            return 4;
        const Message *message = reinterpret_cast<const Message *>(reply.constData());
        if (message->messageType != MessageType::AuthReply)
            return 4;
        const AuthMessageReply *authReply =
            reinterpret_cast<const AuthMessageReply *>(reply.constData());
        if (authReply->verify == 0)
            return 4;
    }

    OpenStreamMessage open_stream;
    open_stream.channel = 0x00;
    open_stream.stream = 0x00;
    open_stream.opened = 0x01;

    QByteArray open_stream_package(reinterpret_cast<char *>(&open_stream),
                                   sizeof(OpenStreamMessage));
    qDebug() << open_stream_package.length() << open_stream_package.toHex(' ');
    socket.write(open_stream_package);
    if (!socket.waitForBytesWritten())
        return 2;
    if (!socket.waitForReadyRead())
        return 3;
    reply = socket.readAll();
    qDebug() << reply.size() << reply.toHex(' ');

    {
        const PackageHeader *header = reinterpret_cast<const PackageHeader *>(reply.constData());
        if (header->packageType != PackageType::Media)
            return 4;
        const MediaMessage *message = reinterpret_cast<const MediaMessage *>(reply.constData());
        if (message->mediaType != MediaType::Idr) {
            return 4;
        }
    }

    QFile f(QLatin1String("test.h264"));
    f.open(QFile::WriteOnly | QFile::Truncate);

    while (socket.waitForReadyRead()) {
        reply = socket.readAll();
        if (reply.size() == 16 && reply.startsWith(static_cast<const unsigned char>(0xaa))) {
            qDebug() << "Skipping" << reply.toHex(' ');
        }
        f.write(reply);
    }

    return 0;
}
