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

#ifndef BUBBLECAMCLIENT_H
#define BUBBLECAMCLIENT_H

#include <QtEndian>

#include <QObject>
#include <QHostAddress>

#include <chrono>

class QTcpSocket;
class QTimer;
class QFile;
class BubbleCamClient : public QObject
{
    Q_OBJECT

public:
    enum struct ErrorCode : quint8 {
        NoError = 0x00,
        AlreadyStreaming,
        UsernameOrPasswordTooLong,
        ConnectionTimeout = 0x10,
        ReadTimeout,
        WriteTimeout,
        UnexpectedReply,
        AuthenticationFailed = 0x80,
        OpenStreamFailed
    };
    Q_ENUM(ErrorCode)

public:
    BubbleCamClient();

    ErrorCode startStreaming(const QHostAddress &hostName, quint16 port = 80,
                             const QString &user = QLatin1String("admin"),
                             const QString &password = {}, quint8 channel = 0, quint8 stream = 0);
    ErrorCode startStreaming(const QHostAddress &hostName, const QString &password,
                             quint8 stream = 0);
    ErrorCode startStreaming(const QHostAddress &hostName, quint8 stream);

    void stopStreaming();

    virtual ~BubbleCamClient();

signals:
    void videoStream(const QByteArray &data);
    void audioStream(const QByteArray &data);

private slots:
    void onReadyRead();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError socketError);
    void onHeartbeatTimerTimeout();

private:
    bool m_streaming = false;
    quint8 m_channel;
    quint8 m_stream;
    QScopedPointer<QTcpSocket> m_socket;
    QScopedPointer<QTimer> m_heartbeatTimer;

    qint32 packet_left = 0;
    bool audioActive = false;

    int processMessage(const QByteArray &data);
    int emitData(const QByteArray &data);
};

#endif // BUBBLECAMCLIENT_H
