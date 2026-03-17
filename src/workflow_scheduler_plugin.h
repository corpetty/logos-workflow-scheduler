#ifndef WORKFLOW_SCHEDULER_PLUGIN_H
#define WORKFLOW_SCHEDULER_PLUGIN_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include "workflow_scheduler_interface.h"
#include "logos_api.h"
#include "logos_sdk.h"

class CronParser;
class WebhookListener;
class DeploymentStore;

class WorkflowSchedulerPlugin : public QObject, public WorkflowSchedulerInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID WorkflowSchedulerInterface_iid FILE "metadata.json")
    Q_INTERFACES(WorkflowSchedulerInterface PluginInterface)

public:
    explicit WorkflowSchedulerPlugin(QObject* parent = nullptr);
    ~WorkflowSchedulerPlugin() override;

    // PluginInterface
    QString name() const override { return "workflow_scheduler"; }
    QString version() const override { return "1.0.0"; }

    // WorkflowSchedulerInterface
    Q_INVOKABLE QString deployWorkflow(const QString& workflowId,
                                       const QString& workflowJson) override;
    Q_INVOKABLE QString undeployWorkflow(const QString& workflowId) override;
    Q_INVOKABLE QString listDeployedWorkflows() override;
    Q_INVOKABLE QString triggerWorkflow(const QString& workflowId,
                                        const QString& triggerData) override;
    Q_INVOKABLE QString getSchedulerStatus() override;
    Q_INVOKABLE QString getExecutionHistory(int limit) override;

    // LogosAPI initialization
    Q_INVOKABLE void initLogos(LogosAPI* logosAPIInstance);

signals:
    void eventResponse(const QString& eventName, const QVariantList& args);

private:
    void executeDeployedWorkflow(const QString& workflowId, const QString& triggerType,
                                 const QJsonObject& triggerData);
    void setupTimersForWorkflow(const QString& workflowId, const QJsonObject& workflow);
    void teardownTimersForWorkflow(const QString& workflowId);

    LogosModules* logos = nullptr;
    DeploymentStore* m_store = nullptr;
    WebhookListener* m_webhookListener = nullptr;

    // Active timer handles
    QMap<QString, int> m_intervalTimers;  // workflowId -> QTimer id
    QMap<QString, int> m_cronTimers;

    // Execution history (last N records)
    QJsonArray m_executionHistory;
    static constexpr int MAX_HISTORY = 100;
};

#endif // WORKFLOW_SCHEDULER_PLUGIN_H
