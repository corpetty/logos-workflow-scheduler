# logos-workflow-scheduler Specification

FURPS analysis of the workflow scheduler module (v1.0.0).

---

## Functionality

### Workflow Deployment and Undeployment

The scheduler manages the full lifecycle of deployed workflows. `deployWorkflow(workflowId, workflowJson)` accepts a serialized workflow, persists it to disk, arms any trigger defined in its nodes, and returns a JSON response containing the assigned webhook URL. `undeployWorkflow(workflowId)` tears down all active timers for that workflow, removes it from the persistent store, and returns a confirmation.

Deploying a workflow that already exists overwrites the previous version: the old timers are replaced and the file on disk is updated.

### Trigger Types

Three trigger types are supported, determined by the `_trigger` property on nodes of type `"trigger"` within the workflow JSON:

| Trigger | Activation | Configuration |
|---------|-----------|---------------|
| **timer** (cron) | Evaluated once per minute by a 60-second QTimer; fires when `CronParser::matchesNow()` returns true | `"cron": "0 9 * * 1-5"` (standard 5-field) |
| **timer** (interval) | QTimer fires at the specified millisecond interval | `"intervalMs": 60000` |
| **webhook** | HTTP POST to `/webhooks/<workflowId>` on the webhook listener port | `"_trigger": "webhook"` (no additional config) |
| **manual** | Explicit call to `triggerWorkflow(workflowId, triggerData)` | `"_trigger": "manual"` |

A single workflow can contain multiple trigger nodes; the scheduler arms all of them independently.

### Execution History Tracking

Every execution (regardless of trigger type) is recorded in an in-memory `QJsonArray`. Each record includes the `workflowId`, `triggerType`, millisecond-precision `timestamp`, the engine's `success` flag, and any `outputs` returned by the engine. The array is maintained in reverse chronological order (newest first) and capped at `MAX_HISTORY = 100` entries. History does not persist across process restarts.

### Persistent Storage

`DeploymentStore` writes each deployed workflow as an individual JSON file under `~/.local/share/logos/deployed-workflows/<workflowId>.json`. On module initialization (`initLogos`), the store's `loadAll()` method reads every file in that directory and the scheduler re-arms timers for all restored workflows. This allows cron schedules and webhook endpoints to survive process restarts without manual re-deployment.

---

## Usability

### 6-Method JSON API

The public API consists of six `Q_INVOKABLE` methods, all accepting and returning JSON-encoded `QString` values:

| Method | Purpose |
|--------|---------|
| `deployWorkflow(workflowId, workflowJson)` | Deploy and activate |
| `undeployWorkflow(workflowId)` | Remove and deactivate |
| `listDeployedWorkflows()` | Enumerate deployed workflows |
| `triggerWorkflow(workflowId, triggerData)` | Manual trigger with optional payload |
| `getSchedulerStatus()` | Runtime status snapshot |
| `getExecutionHistory(limit)` | Recent execution records |

All methods are callable via `logoscore -c`, from other Logos modules through `LogosAPI`, or programmatically through the generated SDK wrappers.

### Cron Expression Support

The `CronParser` class handles standard 5-field cron expressions (`minute hour dayOfMonth month dayOfWeek`). Supported syntax includes wildcard (`*`), exact values, ranges (`N-M`), comma-separated lists (`N,M,O`), and step values (`*/N`). A `validate()` method provides error messages for malformed expressions before deployment.

### Curl-Compatible Webhook Endpoint

Webhook-triggered workflows are accessible via plain HTTP POST. The URL format `http://localhost:<port>/webhooks/<workflowId>` works directly with `curl`, scripting tools, and external services (CI/CD pipelines, GitHub webhooks, etc.). The JSON body and HTTP headers are forwarded to the engine as trigger data.

### Deploy-and-Trigger in One Step

A workflow with a `manual` trigger can be deployed and immediately triggered in sequence:

```bash
logoscore -c "workflow_scheduler.deployWorkflow('wf1', '...')" \
          -c "workflow_scheduler.triggerWorkflow('wf1', '{}')"
```

Timer and webhook triggers activate immediately upon deployment with no additional step required.

---

## Reliability

### Persistent Storage Survives Restarts

The `DeploymentStore` writes workflow state to disk synchronously on every `deployWorkflow` call. On module load, `initLogos` reads all persisted files and re-arms every timer. No deployed workflow is lost across restarts.

### Auto-Restart of Timer Schedules

During `initLogos`, the scheduler iterates over all workflows returned by `m_store->loadAll()` and calls `setupTimersForWorkflow` for each. Cron and interval timers are re-created exactly as they were at original deployment time. The webhook listener is also restarted on the configured port.

### Graceful Handling of Engine Unavailability

When a trigger fires but the workflow engine module is not loaded or not reachable:

1. `logosAPI->getClient("workflow_engine")` returns `nullptr`.
2. The scheduler logs a warning: `"workflow_engine not available"`.
3. No execution record is written to history.
4. The timer continues running -- subsequent trigger firings will retry the engine lookup.

The scheduler does not crash or enter an error state when the engine is absent. Timers remain armed and will succeed once the engine becomes available.

---

## Performance

### Timer-Based Cron Evaluation

Cron schedules use a 60-second `QTimer` that calls `CronParser::matchesNow()` on each tick. This is a lightweight string-parse-and-compare operation against the current local time. The timer runs on the Qt event loop with no dedicated thread. Each deployed cron workflow adds one `QTimer` to the event loop.

Interval timers use `QTimer::setInterval(intervalMs)` with a direct connection to the execution path. Timer resolution is limited by the Qt event loop granularity (typically 1 ms on Linux).

### Webhook Listener on Qt Network Event Loop

The `WebhookListener` is built on `QTcpServer`, which is integrated into the Qt event loop. Incoming connections are handled asynchronously via `QTcpSocket` signals. There is no thread pool or worker model -- all parsing happens on the main thread. This is appropriate for webhook payloads but not suitable for high-throughput HTTP traffic.

### History Capped at 100 Entries

The execution history array is bounded by `MAX_HISTORY = 100`. When the cap is reached, the oldest entry is evicted via `removeLast()` after each `prepend()`. This prevents unbounded memory growth. The `getExecutionHistory(limit)` method returns at most `min(limit, currentSize)` entries from the front of the array.

---

## Supportability

### Dependency on workflow_engine

The scheduler's only module dependency is `workflow_engine`, declared in both `module.yaml` and `metadata.json`. The engine is called via `LogosAPI::getClient("workflow_engine")` and its `executeWorkflowWithTrigger` remote method. The scheduler does not depend on the registry or canvas modules.

Build-time dependency: the engine's generated API headers are required for compilation. The flake.nix wires this via nix input follows.

### Persistent State in User Data Directory

All mutable state lives under `~/.local/share/logos/deployed-workflows/`. Individual workflow files use the `<workflowId>.json` naming convention. This location follows the XDG Base Directory specification (via `QStandardPaths::AppDataLocation`). Clearing this directory resets the scheduler to a clean state.

### Qt6Network Dependency

The webhook listener requires the `Qt6Network` CMake package (declared in `module.yaml` under `cmake.find_packages`). This pulls in `QTcpServer` and `QTcpSocket`. No other external or system libraries are required beyond the standard Logos SDK and Qt6 base.

### Configuration

| Setting | Mechanism | Default |
|---------|-----------|---------|
| Webhook port | `LOGOS_WEBHOOK_PORT` environment variable | 8081 |
| Storage path | `QStandardPaths::AppDataLocation` + `deployed-workflows/` | `~/.local/share/logos/deployed-workflows/` |
| History limit | `MAX_HISTORY` compile-time constant | 100 |

### Events Emitted

The scheduler emits three events via `eventResponse`:

| Event | Arguments | When |
|-------|-----------|------|
| `schedulerWorkflowDeployed` | `[workflowId]` | After successful deployment |
| `schedulerWorkflowTriggered` | `[workflowId, triggerType]` | Before engine execution |
| `schedulerExecutionCompleted` | `[workflowId, success]` | After engine returns |

These events are observable by other modules and by the Logos app UI for status updates.
