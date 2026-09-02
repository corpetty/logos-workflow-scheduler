#include "workflow_scheduler_impl.h"

#include "deployment_store.h"
#include "scheduler_runtime.h"

#include "logos_sdk.h"

#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <memory>

namespace {

constexpr int kMaxHistory = 100;

std::string compact(const QJsonObject& obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

std::string compact(const QJsonArray& arr)
{
    return QJsonDocument(arr).toJson(QJsonDocument::Compact).toStdString();
}

QString qstr(const std::string& s)
{
    return QString::fromStdString(s);
}

QJsonObject parseObject(const std::string& json)
{
    return QJsonDocument::fromJson(QByteArray::fromStdString(json)).object();
}

} // namespace

struct WorkflowSchedulerImpl::State {
    std::unique_ptr<SchedulerRuntime> runtime;
    QJsonArray                        executionHistory;
};

WorkflowSchedulerImpl::WorkflowSchedulerImpl()
    : m_state(std::make_unique<State>())
{
    m_state->runtime = std::make_unique<SchedulerRuntime>();
    m_state->runtime->setTriggerHandler(
        [this](const QString& workflowId, const QString& triggerType,
               const QJsonObject& data) {
            executeDeployedWorkflow(workflowId.toStdString(),
                                    triggerType.toStdString(),
                                    compact(data));
        });
}

WorkflowSchedulerImpl::~WorkflowSchedulerImpl() = default;

void WorkflowSchedulerImpl::onContextReady()
{
    const int restored = m_state->runtime->start();
    qDebug() << "[workflow_scheduler] ready —" << restored << "deployed workflows restored";
}

std::string WorkflowSchedulerImpl::deployWorkflow(const std::string& workflowId,
                                                  const std::string& workflowJson)
{
    const QString    id       = qstr(workflowId);
    const QJsonObject workflow = parseObject(workflowJson);

    m_state->runtime->store()->save(id, workflow);
    m_state->runtime->setupTimersForWorkflow(id, workflow);

    schedulerWorkflowDeployed(workflowId);

    QJsonObject result;
    result[QStringLiteral("status")]     = QStringLiteral("deployed");
    result[QStringLiteral("workflowId")] = id;
    result[QStringLiteral("webhookUrl")] =
        QStringLiteral("http://localhost:%1/webhooks/%2")
            .arg(m_state->runtime->webhookPort())
            .arg(id);
    return compact(result);
}

std::string WorkflowSchedulerImpl::undeployWorkflow(const std::string& workflowId)
{
    const QString id = qstr(workflowId);

    m_state->runtime->teardownTimersForWorkflow(id);
    m_state->runtime->store()->remove(id);

    QJsonObject result;
    result[QStringLiteral("status")]     = QStringLiteral("undeployed");
    result[QStringLiteral("workflowId")] = id;
    return compact(result);
}

std::string WorkflowSchedulerImpl::listDeployedWorkflows()
{
    QJsonArray list;
    const auto deployed = m_state->runtime->store()->loadAll();
    for (auto it = deployed.begin(); it != deployed.end(); ++it) {
        QJsonObject entry;
        entry[QStringLiteral("workflowId")] = it.key();
        entry[QStringLiteral("name")]       = it.value().value(QStringLiteral("name")).toString();
        entry[QStringLiteral("webhookUrl")] =
            QStringLiteral("http://localhost:%1/webhooks/%2")
                .arg(m_state->runtime->webhookPort())
                .arg(it.key());
        list.append(entry);
    }
    return compact(list);
}

std::string WorkflowSchedulerImpl::triggerWorkflow(const std::string& workflowId,
                                                   const std::string& triggerData)
{
    executeDeployedWorkflow(workflowId, "manual", triggerData);

    QJsonObject result;
    result[QStringLiteral("status")]     = QStringLiteral("triggered");
    result[QStringLiteral("workflowId")] = qstr(workflowId);
    return compact(result);
}

std::string WorkflowSchedulerImpl::getSchedulerStatus()
{
    QJsonObject status;
    status[QStringLiteral("deployedWorkflows")]    =
        static_cast<int>(m_state->runtime->store()->loadAll().size());
    status[QStringLiteral("activeIntervalTimers")] =
        m_state->runtime->activeIntervalTimerCount();
    status[QStringLiteral("webhookPort")]          = m_state->runtime->webhookPort();
    status[QStringLiteral("webhookRunning")]       = m_state->runtime->webhookRunning();
    return compact(status);
}

std::string WorkflowSchedulerImpl::getExecutionHistory(int64_t limit)
{
    QJsonArray history;
    const int count = std::min<int>(limit, m_state->executionHistory.size());
    for (int i = 0; i < count; ++i)
        history.append(m_state->executionHistory[i]);
    return compact(history);
}

void WorkflowSchedulerImpl::executeDeployedWorkflow(const std::string& workflowId,
                                                    const std::string& triggerType,
                                                    const std::string& triggerDataJson)
{
    const QString id = qstr(workflowId);

    const QJsonObject workflow = m_state->runtime->store()->load(id);
    if (workflow.isEmpty()) {
        qWarning() << "[workflow_scheduler] workflow not found:" << id;
        return;
    }

    schedulerWorkflowTriggered(workflowId, triggerType);

    // workflow_engine is a DECLARED dependency, so this is the generated
    // typed accessor rather than a runtime client.
    logos::CallError err;
    const std::string resultJson = modules().workflow_engine.executeWorkflowWithTrigger(
        compact(workflow), triggerDataJson, &err);

    QJsonObject record;
    if (!err.code.empty()) {
        qWarning() << "[workflow_scheduler] engine call failed for" << id << ":"
                   << QString::fromStdString(err.message);
        record[QStringLiteral("success")] = false;
        record[QStringLiteral("error")]   = QString::fromStdString(
            err.message.empty() ? err.code : err.message);
    } else {
        record = parseObject(resultJson);
    }

    record[QStringLiteral("workflowId")]  = id;
    record[QStringLiteral("triggerType")] = qstr(triggerType);
    record[QStringLiteral("timestamp")]   = QDateTime::currentMSecsSinceEpoch();

    m_state->executionHistory.prepend(record);
    while (m_state->executionHistory.size() > kMaxHistory)
        m_state->executionHistory.removeLast();

    schedulerExecutionCompleted(workflowId,
                                record.value(QStringLiteral("success")).toBool());
}
