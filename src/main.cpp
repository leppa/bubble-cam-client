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

#include <QCoreApplication>
#include <QFile>

#include <QDebug>

#define HOST "192.168.1.26"
#define PORT 80
#define USER "admin"
#define PASS ""

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    BubbleCamClient client;
    qDebug() << client.startStreaming(QHostAddress(QLatin1String(HOST)), QLatin1String(PASS), 0);

    QFile v(QLatin1String("test.h264"));
    qDebug() << v.open(QFile::WriteOnly | QFile::Truncate);
    QObject::connect(&client, &BubbleCamClient::videoStream,
                     [&v](const QByteArray &data) { v.write(data); });

    //    QFile a(QLatin1String("test.g711"));
    //    a.open(QFile::WriteOnly | QFile::Truncate);
    //    QObject::connect(&client, &BubbleCamClient::audioStream,
    //                     [&a](const QByteArray &data) { a.write(data); });

    const int ret = app.exec();

    client.stopStreaming();
    v.close();
    //    a.close();

    return ret;
}
