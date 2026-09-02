#include "scheduler_runtime.h"

#include "cron_parser.h"
#include "deployment_store.h"
#include "webhook_listener.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QTimer>

SchedulerRuntime::SchedulerRuntime(QObject* parent)
    : QObject(parent)
    , m_store(new DeploymentStore(this))
    , m_webhookListener(new WebhookListener(this))
{
    connect(m_webhookListener, &WebhookListener::webhookReceived,
            this, [this](const QString& workflowId, const QJsonObject& payload,
                         const QJsonObject& headers) {
        QJsonObject triggerData;
        triggerData[QStringLiteral("body")]     = payload;
        triggerData[QStringLiteral("_headers")] = headers;
        fire(workflowId, QStringLiteral("webhook"), triggerData);
    });
}

SchedulerRuntime::~SchedulerRuntime()
{
    // Children of this QObject, so Qt would delete them anyway — but stop
    // them first so nothing fires into a half-destroyed handler.
    for (QTimer* timer : m_intervalTimers) timer->stop();
    for (QTimer* timer : m_cronTimers)     timer->stop();
}

void SchedulerRuntime::setTriggerHandler(TriggerHandler handler)
{
    m_handler = std::move(handler);
}

void SchedulerRuntime::fire(const QString& workflowId, const QString& triggerType,
                            const QJsonObject& data)
{
    if (m_handler)
        m_handler(workflowId, triggerType, data);
}

int SchedulerRuntime::start()
{
    const auto deployed = m_store->loadAll();
    for (auto it = deployed.begin(); it != deployed.end(); ++it) {
        setupTimersForWorkflow(it.key(), it.value());
        qDebug() << "[workflow_scheduler] restored deployed workflow:" << it.key();
    }

    int port = qEnvironmentVariableIntValue("LOGOS_WEBHOOK_PORT");
    if (port == 0) port = 8081;
    m_webhookListener->start(port);

    return static_cast<int>(deployed.size());
}

void SchedulerRuntime::setupTimersForWorkflow(const QString& workflowId,
                                              const QJsonObject& workflow)
{
    // Redeploying the same id must not stack a second set of timers on top of
    // the first.
    teardownTimersForWorkflow(workflowId);

    const QJsonArray nodes = workflow.value(QStringLiteral("nodes")).toArray();
    for (const auto& nodeVal : nodes) {
        const QJsonObject node = nodeVal.toObject();
        if (node.value(QStringLiteral("type")).toString() != QStringLiteral("trigger"))
            continue;

        const QJsonObject props = node.value(QStringLiteral("properties")).toObject();

        const int intervalMs = props.value(QStringLiteral("intervalMs")).toInt(0);
        if (intervalMs > 0) {
            auto* timer = new QTimer(this);
            timer->setInterval(intervalMs);
            connect(timer, &QTimer::timeout, this, [this, workflowId]() {
                QJsonObject data;
                data[QStringLiteral("timestamp")] = QDateTime::currentMSecsSinceEpoch();
                data[QStringLiteral("source")]    = QStringLiteral("timer");
                fire(workflowId, QStringLiteral("timer"), data);
            });
            timer->start();
            m_intervalTimers.insert(workflowId, timer);
            qDebug() << "[workflow_scheduler] interval timer for" << workflowId
                     << "every" << intervalMs << "ms";
        }

        const QString cron = props.value(QStringLiteral("cron")).toString();
        if (!cron.isEmpty()) {
            // One 60s ticker per cron workflow, matched against the expression
            // on each tick.
            auto* timer = new QTimer(this);
            timer->setInterval(60000);
            connect(timer, &QTimer::timeout, this, [this, workflowId, cron]() {
                if (!CronParser::matchesNow(cron))
                    return;
                QJsonObject data;
                data[QStringLiteral("timestamp")]  = QDateTime::currentMSecsSinceEpoch();
                data[QStringLiteral("source")]     = QStringLiteral("cron");
                data[QStringLiteral("expression")] = cron;
                fire(workflowId, QStringLiteral("timer"), data);
            });
            timer->start();
            m_cronTimers.insert(workflowId, timer);
            qDebug() << "[workflow_scheduler] cron schedule for" << workflowId << ":" << cron;
        }
    }
}

void SchedulerRuntime::teardownTimersForWorkflow(const QString& workflowId)
{
    if (QTimer* timer = m_intervalTimers.take(workflowId)) {
        timer->stop();
        timer->deleteLater();
    }
    if (QTimer* timer = m_cronTimers.take(workflowId)) {
        timer->stop();
        timer->deleteLater();
    }
}

int SchedulerRuntime::webhookPort() const
{
    return m_webhookListener->port();
}

bool SchedulerRuntime::webhookRunning() const
{
    return m_webhookListener->isRunning();
}
