#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>
#include <QString>
#include <QMap>
#include <QPair>
#include <QUrl>
#include <QDebug>
#include "debug.h"
#if QT_VERSION >= 0x050000
#include <QUrlQuery>
#endif

#include "o2/o2replyserver.h"

O2ReplyServer::O2ReplyServer(QObject *parent): QTcpServer(parent)
{
    connect(this, &QTcpServer::newConnection, this, &O2ReplyServer::onIncomingConnection);
    replyContent_ = "<HTML></HTML>";
}

void O2ReplyServer::onIncomingConnection()
{
    QTcpSocket *socket = nextPendingConnection();
    connect(socket, &QIODevice::readyRead, this, &O2ReplyServer::onBytesReady, Qt::UniqueConnection);
    connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
}

void O2ReplyServer::onBytesReady()
{
    qCDebug(TOMBOYNOTESRESOURCE_LOG) << "O2ReplyServer::onBytesReady";
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) {
        return;
    }
    QByteArray reply;
    reply.append("HTTP/1.0 200 OK \r\n");
    reply.append("Content-Type: text/html; charset=\"utf-8\"\r\n");
    reply.append(QByteArray("Content-Length: ") + QByteArray::number(replyContent_.size()) + "\r\n\r\n");
    reply.append(replyContent_);
    socket->write(reply);

    QByteArray data = socket->readAll();
    QMap<QString, QString> queryParams = parseQueryParams(&data);
    socket->disconnectFromHost();
    close();
    Q_EMIT verificationReceived(queryParams);
}

QMap<QString, QString> O2ReplyServer::parseQueryParams(QByteArray *data)
{
    qCDebug(TOMBOYNOTESRESOURCE_LOG) << "O2ReplyServer::parseQueryParams";

    QString splitGetLine = QString(*data).split("\r\n").first();
    splitGetLine.remove("GET ");
    splitGetLine.remove("HTTP/1.1");
    splitGetLine.remove("\r\n");
    splitGetLine.prepend("http://localhost");
    QUrl getTokenUrl(splitGetLine);

    QList< QPair<QString, QString> > tokens;
#if QT_VERSION < 0x050000
    tokens = getTokenUrl.queryItems();
#else
    QUrlQuery query(getTokenUrl);
    tokens = query.queryItems();
#endif
    QMultiMap<QString, QString> queryParams;
    QPair<QString, QString> tokenPair;
    foreach (tokenPair, tokens) {
        // FIXME: We are decoding key and value again. This helps with Google OAuth, but is it mandated by the standard?
        QString key = QUrl::fromPercentEncoding(tokenPair.first.trimmed().toLatin1());
        QString value = QUrl::fromPercentEncoding(tokenPair.second.trimmed().toLatin1());
        queryParams.insert(key, value);
    }
    return queryParams;
}

QByteArray O2ReplyServer::replyContent()
{
    return replyContent_;
}

void O2ReplyServer::setReplyContent(const QByteArray &value)
{
    replyContent_ = value;
}
