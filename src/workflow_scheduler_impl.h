#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "logos_module_context.h"

/**
 * @brief The Workflow Scheduler module.
 *
 * Deploys workflows and runs them unattended: interval and cron timers,
 * inbound webhooks, and a rolling execution history.
 *
 * Unlike the engine, every module this one calls is a DECLARED dependency
 * (workflow_engine), so it dispatches through the generated typed accessor
 * `modules().workflow_engine` rather than a runtime client.
 *
 * The Qt-shaped machinery — store, listener, timers — lives in
 * SchedulerRuntime; this class stays Qt-free.
 */
class WorkflowSchedulerImpl : public LogosModuleContext
{
public:
    WorkflowSchedulerImpl();
    ~WorkflowSchedulerImpl();

    /// Deploy a workflow and arm its triggers. Returns {"status","workflowId","webhookUrl"}.
    std::string deployWorkflow(const std::string& workflowId, const std::string& workflowJson);

    /// Remove a deployment and disarm its triggers.
    std::string undeployWorkflow(const std::string& workflowId);

    /// JSON array of the deployed workflows.
    std::string listDeployedWorkflows();

    /// Fire a deployed workflow by hand, with trigger data.
    std::string triggerWorkflow(const std::string& workflowId, const std::string& triggerData);

    /// {"deployedWorkflows","activeIntervalTimers","webhookPort","webhookRunning"}.
    std::string getSchedulerStatus();

    /// The most recent runs, newest first, capped at `limit`.
    std::string getExecutionHistory(int64_t limit);

logos_events:
    /// A workflow was deployed and its triggers armed.
    void schedulerWorkflowDeployed(const std::string& workflowId);

    /// A trigger fired and the run is being handed to the engine.
    void schedulerWorkflowTriggered(const std::string& workflowId,
                                    const std::string& triggerType);

    /// A triggered run came back from the engine.
    void schedulerExecutionCompleted(const std::string& workflowId, bool success);

protected:
    /// Deployments are restored and the webhook listener started here, not in
    /// the constructor: restoring means calling the engine, which is only
    /// wired once the module has its context.
    void onContextReady() override;

private:
    struct State;
    std::unique_ptr<State> m_state;

    void executeDeployedWorkflow(const std::string& workflowId,
                                 const std::string& triggerType,
                                 const std::string& triggerDataJson);
};
