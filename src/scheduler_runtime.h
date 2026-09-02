#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class DeploymentStore;
class QTimer;
class WebhookListener;

/**
 * The Qt-shaped half of the scheduler: the deployment store, the webhook
 * listener, and the per-workflow timers.
 *
 * It is a QObject because timers and the listener are, and a universal
 * module's impl class is not one — the impl owns this and stays Qt-free at
 * its API surface. Firings come back out through the std::function set by
 * setTriggerHandler, so nothing Qt-typed crosses back into the impl.
 */
class SchedulerRuntime : public QObject
{
    Q_OBJECT

public:
    /// (workflowId, triggerType, triggerDataJson)
    using TriggerHandler = std::function<void(const QString&, const QString&, const QJsonObject&)>;

    explicit SchedulerRuntime(QObject* parent = nullptr);
    ~SchedulerRuntime() override;

    void setTriggerHandler(TriggerHandler handler);

    DeploymentStore* store() const { return m_store; }

    /// Restore deployed workflows from disk and start the webhook listener.
    /// @return how many workflows were restored.
    int start();

    void setupTimersForWorkflow(const QString& workflowId, const QJsonObject& workflow);
    void teardownTimersForWorkflow(const QString& workflowId);

    int  webhookPort() const;
    bool webhookRunning() const;
    int  activeIntervalTimerCount() const { return m_intervalTimers.size(); }

private:
    void fire(const QString& workflowId, const QString& triggerType, const QJsonObject& data);

    DeploymentStore* m_store           = nullptr;
    WebhookListener* m_webhookListener = nullptr;
    TriggerHandler   m_handler;

    // QTimer*, not QTimer::timerId(). The previous code stored the id and
    // passed it to killTimer() on the plugin — a timer id belongs to the
    // QObject that started it, so that call did nothing: undeploying a
    // workflow left its timer running and firing forever.
    QHash<QString, QTimer*> m_intervalTimers;
    QHash<QString, QTimer*> m_cronTimers;
};
