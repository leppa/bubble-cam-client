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

#include "bubblecamclient.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>

#include <iostream>

#include <QLoggingCategory>
Q_LOGGING_CATEGORY(bubbleCamMainLog, "bubblecam.main", QtWarningMsg)
#define CRITICAL qCCritical(bubbleCamMainLog())
#define INFO qCInfo(bubbleCamMainLog())

static struct
{
    QHostAddress host;
    QString username;
    QString password;
    QString videoFilePath;
    QString audioFilePath;
    quint16 port;
    quint8 channel;
    quint8 stream;
    quint8 verbosity = 3;
    bool debug = false;
} options;

#include <QDateTime>

void parseCommandLine()
{
    QCommandLineParser parser;
    parser.setApplicationDescription("A client for IP cameras that use \"bubble\" protocol.");

    //    QCommandLineOption hostOption({ "a", "address" }, "Address of the camera.", "IP or
    //    hostname"); parser.addOption(hostOption);

    QCommandLineOption videoFile(
        { "V", "video" },
        "File path to save video stream to. Specify '-' to stream to standard output.", "path");
    parser.addOption(videoFile);

    QCommandLineOption audioFile(
        { "A", "audio" },
        "File path to save audio stream to. Specify '-' to stream to standard output.", "path");

    parser.addOption(audioFile);

    QCommandLineOption portOption({ "P", "port" }, "Port to connect to (default 80).", "port",
                                  "80");
    parser.addOption(portOption);

    QCommandLineOption userOption({ "u", "user" }, "Username for authentication (default 'admin').",
                                  "username", "admin");
    parser.addOption(userOption);

    QCommandLineOption passwordOption(
        { "p", "pass" }, "Password for authentication (default empty).", "password", "");
    parser.addOption(passwordOption);

    QCommandLineOption channelOption({ "c", "channel" },
                                     "Channel number to stream (default 0). Camera may have only "
                                     "one channel, but several streams.",
                                     "number", "0");
    parser.addOption(channelOption);

    QCommandLineOption streamOption({ "s", "stream" },
                                    "Stream number / quality of the channel (default 0). Usually, "
                                    "at least two streams are available, with stream 0 having the "
                                    "highest quality.",
                                    "number", "0");
    parser.addOption(streamOption);

    QCommandLineOption quietOption({ "q", "quiet" }, "Suppresses all output.");
    parser.addOption(quietOption);

    QCommandLineOption verboseOption("verbose",
                                     "Makes output verbose (messages are sent to standard error)");
    parser.addOption(verboseOption);

    QCommandLineOption debugOption(
        "debug", "Enable debug output (implies `--verbose`, overrides `--quiet`)");
    parser.addOption(debugOption);

    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("host", "Address of the camera (IP or hostname).");

    parser.process(QCoreApplication::arguments());

    const QStringList args = parser.positionalArguments();
    if (args.count() < 1) {
        CRITICAL << "Please, provide camera address." << endl;
        parser.showHelp(1);
    }
    options.host = QHostAddress(args.at(0));

    bool pathProvided = false;
    if (parser.isSet(videoFile)) {
        options.videoFilePath = parser.value(videoFile);
        pathProvided = true;
    }
    if (parser.isSet(audioFile)) {
        options.audioFilePath = parser.value(audioFile);
        pathProvided = true;
    }
    if (!pathProvided) {
        CRITICAL << "Please, provide either video or audio file path." << endl;
        parser.showHelp(1);
    }
    if (options.videoFilePath == options.audioFilePath) {
        CRITICAL << "Streaming both video and audio into the same file is not yet supported."
                 << endl;
    }

    bool ok;
    options.port = parser.value(portOption).toUShort(&ok);
    if (!ok) {
        CRITICAL << "Invalid port value:" << parser.value(portOption) << endl;
        parser.showHelp(1);
    }

    options.channel = static_cast<quint8>(parser.value(channelOption).toUShort(&ok));
    if (!ok) {
        CRITICAL << "Invalid channel number:" << parser.value(channelOption) << endl;
        parser.showHelp(1);
    }

    options.stream = static_cast<quint8>(parser.value(streamOption).toUShort(&ok));
    if (!ok) {
        CRITICAL << "Invalid stream number:" << parser.value(streamOption) << endl;
        parser.showHelp(1);
    }

    options.username = parser.value(userOption);
    options.password = parser.value(passwordOption);

    const bool verbose = parser.isSet(verboseOption);
    const bool quiet = parser.isSet(quietOption);
    options.debug = parser.isSet(debugOption);
    if (options.debug) {
        options.verbosity = 255;
    } else {
        if (verbose && quiet) {
            CRITICAL << "Options --verbose and --quiet are mutually exclusive." << endl;
            parser.showHelp(1);
        } else if (verbose) {
            options.verbosity = 3;
        } else if (quiet) {
            options.verbosity = 1;
        }
    }
}

void toStdErr(const char *severity, const char *category, const char *message, bool showSeverity)
{
    if (options.debug) {
        std::cerr << "[";
        const auto width = std::cerr.width(8);
        std::cerr << severity;
        std::cerr.width(width);
        std::cerr << "] " << category << ": " << message << std::endl;
    } else if (showSeverity) {
        std::cerr << severity << ": " << message << std::endl;
    } else {
        std::cerr << message << std::endl;
    }
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    const QByteArray localMsg = msg.toLocal8Bit();
    switch (type) {
    case QtDebugMsg:
        toStdErr("DEBUG", context.category, localMsg.constData(), false);
        break;
    case QtInfoMsg:
        toStdErr("INFO", context.category, localMsg.constData(), false);
        break;
    case QtWarningMsg:
        toStdErr("WARNING", context.category, localMsg.constData(), true);
        break;
    case QtCriticalMsg:
        toStdErr("CRITICAL", context.category, localMsg.constData(), true);
        break;
    case QtFatalMsg:
        toStdErr("FATAL", context.category, localMsg.constData(), true);
        abort();
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app);

    qInstallMessageHandler(messageHandler);

    QCoreApplication::setApplicationName(QLatin1String("BubbleCam Client"));
    QCoreApplication::setApplicationVersion(QLatin1String("0.1.0"));

    parseCommandLine();

    switch (options.verbosity) {
    case 0:
        QLoggingCategory::setFilterRules("*=false");
        break;
    case 1:
        QLoggingCategory::setFilterRules(
            "*.debug=false\n*.info=false\n*.warning=false\n*.critical=true");
        break;
    case 2:
        QLoggingCategory::setFilterRules(
            "*.debug=false\n*.info=false\n*.warning=true\n*.critical=true");
        break;
    case 3:
        QLoggingCategory::setFilterRules(
            "*.debug=false\n*.info=true\n*.warning=true\n*.critical=true");
        break;
    case 4:
        QLoggingCategory::setFilterRules(
            "*.debug=true\n*.info=false\n*.warning=true\n*.critical=true");
        break;
    default:
        QLoggingCategory::setFilterRules("*=true");
        break;
    }

    BubbleCamClient client;
    BubbleCamClient::ErrorCode error =
        client.startStreaming(options.host, options.port, options.username, options.password,
                              options.channel, options.stream);
    if (error != BubbleCamClient::ErrorCode::NoError) {
        CRITICAL << "Failed to start stream:" << error;
        return 1;
    }
    INFO << "Successfully started stream";

    QFile v;
    if (options.videoFilePath == "-") {
        v.open(stdout, QFile::WriteOnly);
    } else {
        v.setFileName(options.videoFilePath);
        v.open(QFile::WriteOnly);
    }

    QObject::connect(&client, &BubbleCamClient::videoStream,
                     [&v](const QByteArray &data) { v.write(data); });

    QFile a;
    if (!options.audioFilePath.isEmpty()) {
        if (options.audioFilePath == "-") {
            a.open(stdout, QFile::WriteOnly);
        } else {
            a.setFileName(options.audioFilePath);
            a.open(QFile::WriteOnly);
        }

        QObject::connect(&client, &BubbleCamClient::audioStream,
                         [&a](const QByteArray &data) { a.write(data.mid(36)); });
    }

    const int ret = app.exec();

    client.stopStreaming();
    v.close();
    a.close();

    return ret;
}
