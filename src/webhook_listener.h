#ifndef WEBHOOK_LISTENER_H
#define WEBHOOK_LISTENER_H

#include <QObject>
#include <QTcpServer>
#include <QJsonObject>

/**
 * @brief Lightweight HTTP server for receiving webhook triggers
 *
 * Listens for POST /webhooks/<workflowId> and emits webhookReceived
 * with the parsed JSON body. Handles only the bare minimum of HTTP
 * parsing needed for webhook payloads.
 */
class WebhookListener : public QObject
{
    Q_OBJECT

public:
    explicit WebhookListener(QObject* parent = nullptr);
    ~WebhookListener() override;

    void start(int port = 8081);
    void stop();

    int port() const { return m_port; }
    bool isRunning() const { return m_server && m_server->isListening(); }

signals:
    /**
     * @brief Emitted when a webhook POST is received
     * @param workflowId Extracted from URL path
     * @param payload Parsed JSON body
     * @param headers Parsed HTTP headers as key-value pairs
     */
    void webhookReceived(const QString& workflowId, const QJsonObject& payload,
                         const QJsonObject& headers);

private slots:
    void onNewConnection();

private:
    void handleRequest(const QByteArray& request, class QTcpSocket* socket);
    void sendResponse(QTcpSocket* socket, int statusCode, const QByteArray& body);

    QTcpServer* m_server = nullptr;
    int m_port = 8081;
};

#endif // WEBHOOK_LISTENER_H
