#include "webhook_listener.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QDebug>

WebhookListener::WebhookListener(QObject* parent)
    : QObject(parent)
{
}

WebhookListener::~WebhookListener()
{
    stop();
}

void WebhookListener::start(int port)
{
    if (m_server) stop();

    m_port = port;
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &WebhookListener::onNewConnection);

    if (m_server->listen(QHostAddress::Any, m_port)) {
        qDebug() << "[webhook] Listening on port" << m_port;
    } else {
        qWarning() << "[webhook] Failed to listen on port" << m_port
                    << ":" << m_server->errorString();
    }
}

void WebhookListener::stop()
{
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

void WebhookListener::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray data = socket->readAll();
            handleRequest(data, socket);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void WebhookListener::handleRequest(const QByteArray& request, QTcpSocket* socket)
{
    // Minimal HTTP parsing
    QString requestStr = QString::fromUtf8(request);
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) {
        sendResponse(socket, 400, "Bad Request");
        return;
    }

    // Parse request line: "POST /webhooks/my-workflow HTTP/1.1"
    QStringList requestLine = lines[0].split(" ");
    if (requestLine.size() < 2) {
        sendResponse(socket, 400, "Bad Request");
        return;
    }

    QString method = requestLine[0];
    QString path = requestLine[1];

    // Only accept POST /webhooks/<id>
    if (method != "POST" || !path.startsWith("/webhooks/")) {
        sendResponse(socket, 404, "Not Found");
        return;
    }

    QString workflowId = path.mid(10); // Remove "/webhooks/"
    if (workflowId.isEmpty()) {
        sendResponse(socket, 400, "Missing workflow ID");
        return;
    }

    // Parse headers (lines between request line and body)
    QJsonObject headers;
    int bodyStart = requestStr.indexOf("\r\n\r\n");
    for (int i = 1; i < lines.size(); ++i) {
        const QString& line = lines[i];
        if (line.isEmpty()) break;  // Empty line = start of body
        int colonIdx = line.indexOf(':');
        if (colonIdx > 0) {
            QString key = line.left(colonIdx).trimmed().toLower();
            QString value = line.mid(colonIdx + 1).trimmed();
            headers[key] = value;
        }
    }

    // Find body (after empty line)
    QJsonObject payload;
    if (bodyStart >= 0) {
        QString body = requestStr.mid(bodyStart + 4);
        QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
        if (doc.isObject()) {
            payload = doc.object();
        }
    }

    qDebug() << "[webhook] Received POST /webhooks/" << workflowId
             << "with" << headers.size() << "headers";
    emit webhookReceived(workflowId, payload, headers);

    QJsonObject response;
    response["status"] = "accepted";
    response["workflowId"] = workflowId;
    sendResponse(socket, 200, QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void WebhookListener::sendResponse(QTcpSocket* socket, int statusCode, const QByteArray& body)
{
    QString statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        default: statusText = "Error"; break;
    }

    QByteArray response;
    response.append(QString("HTTP/1.1 %1 %2\r\n").arg(statusCode).arg(statusText).toUtf8());
    response.append("Content-Type: application/json\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append(QString("Content-Length: %1\r\n").arg(body.size()).toUtf8());
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}
