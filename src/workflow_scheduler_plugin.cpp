#include "workflow_scheduler_plugin.h"
#include "cron_parser.h"
#include "webhook_listener.h"
#include "deployment_store.h"
#include "logos_api.h"
#include "logos_api_client.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QDateTime>

WorkflowSchedulerPlugin::WorkflowSchedulerPlugin(QObject* parent)
    : QObject(parent)
    , m_store(new DeploymentStore(this))
    , m_webhookListener(new WebhookListener(this))
{
    qDebug() << "[workflow_scheduler] Constructor";

    // Wire up webhook triggers — pass through the parsed headers
    connect(m_webhookListener, &WebhookListener::webhookReceived,
            this, [this](const QString& workflowId, const QJsonObject& payload,
                         const QJsonObject& headers) {
        QJsonObject triggerData;
        triggerData["body"] = payload;
        triggerData["_headers"] = headers;
        executeDeployedWorkflow(workflowId, "webhook", triggerData);
    });
}

WorkflowSchedulerPlugin::~WorkflowSchedulerPlugin()
{
    // Stop all timers
    for (auto it = m_intervalTimers.begin(); it != m_intervalTimers.end(); ++it) {
        killTimer(it.value());
    }
    qDebug() << "[workflow_scheduler] Destructor";
}

void WorkflowSchedulerPlugin::initLogos(LogosAPI* logosAPIInstance)
{
    if (logos) { delete logos; logos = nullptr; }
    if (logosAPI) { delete logosAPI; logosAPI = nullptr; }
    logosAPI = logosAPIInstance;
    if (logosAPI) {
        logos = new LogosModules(logosAPI);
    }

    // Restore deployed workflows from disk
    auto deployed = m_store->loadAll();
    for (auto it = deployed.begin(); it != deployed.end(); ++it) {
        setupTimersForWorkflow(it.key(), it.value());
        qDebug() << "[workflow_scheduler] Restored deployed workflow:" << it.key();
    }

    // Start webhook listener
    int port = qEnvironmentVariableIntValue("LOGOS_WEBHOOK_PORT");
    if (port == 0) port = 8081;
    m_webhookListener->start(port);

    qDebug() << "[workflow_scheduler] initLogos complete —"
             << deployed.size() << "deployed workflows restored";
}

QString WorkflowSchedulerPlugin::deployWorkflow(const QString& workflowId,
                                                 const QString& workflowJson)
{
    QJsonDocument doc = QJsonDocument::fromJson(workflowJson.toUtf8());
    QJsonObject workflow = doc.object();

    m_store->save(workflowId, workflow);
    setupTimersForWorkflow(workflowId, workflow);

    emit eventResponse("schedulerWorkflowDeployed", QVariantList() << workflowId);

    QJsonObject result;
    result["status"] = "deployed";
    result["workflowId"] = workflowId;
    result["webhookUrl"] = QString("http://localhost:%1/webhooks/%2")
                               .arg(m_webhookListener->port())
                               .arg(workflowId);
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString WorkflowSchedulerPlugin::undeployWorkflow(const QString& workflowId)
{
    teardownTimersForWorkflow(workflowId);
    m_store->remove(workflowId);

    QJsonObject result;
    result["status"] = "undeployed";
    result["workflowId"] = workflowId;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString WorkflowSchedulerPlugin::listDeployedWorkflows()
{
    QJsonArray list;
    auto deployed = m_store->loadAll();
    for (auto it = deployed.begin(); it != deployed.end(); ++it) {
        QJsonObject entry;
        entry["workflowId"] = it.key();
        entry["name"] = it.value()["name"].toString();
        entry["webhookUrl"] = QString("http://localhost:%1/webhooks/%2")
                                  .arg(m_webhookListener->port())
                                  .arg(it.key());
        list.append(entry);
    }
    return QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
}

QString WorkflowSchedulerPlugin::triggerWorkflow(const QString& workflowId,
                                                  const QString& triggerData)
{
    QJsonDocument trigDoc = QJsonDocument::fromJson(triggerData.toUtf8());
    executeDeployedWorkflow(workflowId, "manual", trigDoc.object());

    QJsonObject result;
    result["status"] = "triggered";
    result["workflowId"] = workflowId;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString WorkflowSchedulerPlugin::getSchedulerStatus()
{
    QJsonObject status;
    status["deployedWorkflows"] = m_store->loadAll().size();
    status["activeIntervalTimers"] = m_intervalTimers.size();
    status["webhookPort"] = m_webhookListener->port();
    status["webhookRunning"] = m_webhookListener->isRunning();
    return QString::fromUtf8(QJsonDocument(status).toJson(QJsonDocument::Compact));
}

QString WorkflowSchedulerPlugin::getExecutionHistory(int limit)
{
    QJsonArray history;
    int count = qMin(limit, m_executionHistory.size());
    for (int i = 0; i < count; ++i) {
        history.append(m_executionHistory[i]);
    }
    return QString::fromUtf8(QJsonDocument(history).toJson(QJsonDocument::Compact));
}

void WorkflowSchedulerPlugin::executeDeployedWorkflow(const QString& workflowId,
                                                       const QString& triggerType,
                                                       const QJsonObject& triggerData)
{
    if (!logosAPI) {
        qWarning() << "[workflow_scheduler] No LogosAPI — cannot execute";
        return;
    }

    auto* engineClient = logosAPI->getClient("workflow_engine");
    if (!engineClient) {
        qWarning() << "[workflow_scheduler] workflow_engine not available";
        return;
    }

    QJsonObject workflow = m_store->load(workflowId);
    if (workflow.isEmpty()) {
        qWarning() << "[workflow_scheduler] Workflow not found:" << workflowId;
        return;
    }

    QString workflowJson = QString::fromUtf8(QJsonDocument(workflow).toJson(QJsonDocument::Compact));
    QString triggerJson = QString::fromUtf8(QJsonDocument(triggerData).toJson(QJsonDocument::Compact));

    emit eventResponse("schedulerWorkflowTriggered",
                      QVariantList() << workflowId << triggerType);

    QVariant result = engineClient->invokeRemoteMethod(
        "workflow_engine", "executeWorkflowWithTrigger",
        {workflowJson, triggerJson});

    // Record in history
    QJsonDocument resultDoc = QJsonDocument::fromJson(result.toString().toUtf8());
    QJsonObject record = resultDoc.object();
    record["workflowId"] = workflowId;
    record["triggerType"] = triggerType;
    record["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    m_executionHistory.prepend(record);
    while (m_executionHistory.size() > MAX_HISTORY) {
        m_executionHistory.removeLast();
    }

    emit eventResponse("schedulerExecutionCompleted",
                      QVariantList() << workflowId << record["success"].toBool());
}

void WorkflowSchedulerPlugin::setupTimersForWorkflow(const QString& workflowId,
                                                      const QJsonObject& workflow)
{
    // Scan nodes for trigger types
    QJsonArray nodes = workflow["nodes"].toArray();
    for (const auto& nodeVal : nodes) {
        QJsonObject node = nodeVal.toObject();
        if (node["type"].toString() != "trigger") continue;

        QJsonObject props = node["properties"].toObject();

        // Interval timer
        int intervalMs = props["intervalMs"].toInt(0);
        if (intervalMs > 0) {
            auto* timer = new QTimer(this);
            timer->setInterval(intervalMs);
            connect(timer, &QTimer::timeout, this, [this, workflowId]() {
                QJsonObject data;
                data["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                data["source"] = "timer";
                executeDeployedWorkflow(workflowId, "timer", data);
            });
            timer->start();
            m_intervalTimers[workflowId] = timer->timerId();
            qDebug() << "[workflow_scheduler] Started interval timer for"
                     << workflowId << "every" << intervalMs << "ms";
        }

        // Cron schedule
        QString cron = props["cron"].toString();
        if (!cron.isEmpty()) {
            // The cron ticker runs every 60s and checks all cron schedules
            // For simplicity, we use a 60s QTimer and match against CronParser
            auto* timer = new QTimer(this);
            timer->setInterval(60000);
            connect(timer, &QTimer::timeout, this, [this, workflowId, cron]() {
                if (CronParser::matchesNow(cron)) {
                    QJsonObject data;
                    data["timestamp"] = QDateTime::currentMSecsSinceEpoch();
                    data["source"] = "cron";
                    data["expression"] = cron;
                    executeDeployedWorkflow(workflowId, "timer", data);
                }
            });
            timer->start();
            m_cronTimers[workflowId] = timer->timerId();
            qDebug() << "[workflow_scheduler] Started cron schedule for"
                     << workflowId << ":" << cron;
        }
    }
}

void WorkflowSchedulerPlugin::teardownTimersForWorkflow(const QString& workflowId)
{
    if (m_intervalTimers.contains(workflowId)) {
        killTimer(m_intervalTimers.take(workflowId));
    }
    if (m_cronTimers.contains(workflowId)) {
        killTimer(m_cronTimers.take(workflowId));
    }
}
