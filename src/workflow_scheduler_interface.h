#ifndef WORKFLOW_SCHEDULER_INTERFACE_H
#define WORKFLOW_SCHEDULER_INTERFACE_H

#include <QObject>
#include <QString>
#include "interface.h"

/**
 * @brief Interface for the Workflow Scheduler module
 *
 * Manages deployed workflows, cron/interval scheduling, webhook
 * HTTP endpoints, manual triggers, and execution history.
 */
class WorkflowSchedulerInterface : public PluginInterface
{
public:
    virtual ~WorkflowSchedulerInterface() = default;

    // Deployment
    Q_INVOKABLE virtual QString deployWorkflow(const QString& workflowId,
                                               const QString& workflowJson) = 0;
    Q_INVOKABLE virtual QString undeployWorkflow(const QString& workflowId) = 0;
    Q_INVOKABLE virtual QString listDeployedWorkflows() = 0;

    // Triggering
    Q_INVOKABLE virtual QString triggerWorkflow(const QString& workflowId,
                                                const QString& triggerData) = 0;

    // Status
    Q_INVOKABLE virtual QString getSchedulerStatus() = 0;
    Q_INVOKABLE virtual QString getExecutionHistory(int limit) = 0;

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);
};

#define WorkflowSchedulerInterface_iid "org.logos.WorkflowSchedulerInterface"
Q_DECLARE_INTERFACE(WorkflowSchedulerInterface, WorkflowSchedulerInterface_iid)

#endif // WORKFLOW_SCHEDULER_INTERFACE_H
