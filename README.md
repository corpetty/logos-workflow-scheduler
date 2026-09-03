# logos-workflow-scheduler

A native [Logos](https://logos.co) module that manages deployed workflows and triggers them automatically. Supports cron/interval scheduling, HTTP webhook endpoints, and manual triggers. Deployed workflows are persisted to disk and their schedules resume on startup.

This is **Module 4** of the [Logos Legos](https://github.com/corpetty/logos-legos) v2 native architecture — the deployment and automation layer.

---

## What It Does

The scheduler sits at the end of the dependency chain. You deploy a serialized workflow to it, configure a trigger (cron, interval, webhook, or manual), and the scheduler takes ownership: persisting it to disk, running the schedule, and calling the engine whenever the trigger fires. It runs independently of the canvas — workflows can be deployed via the API or `logoscore` CLI without any UI.

---

## API

### Deployment

| Method | Returns | Description |
|---|---|---|
| `deployWorkflow(workflowId, workflowJson)` | `QString` (JSON) | Deploy a workflow and activate its trigger. Persists to disk. |
| `undeployWorkflow(workflowId)` | `QString` (status) | Remove a deployed workflow and cancel its schedule |
| `listDeployedWorkflows()` | `QString` (JSON array) | All deployed workflows with trigger config and next-fire time |

### Triggering

| Method | Returns | Description |
|---|---|---|
| `triggerWorkflow(workflowId, triggerData)` | `QString` (JSON) | Manually fire a deployed workflow with optional JSON payload |

### Status

| Method | Returns | Description |
|---|---|---|
| `getSchedulerStatus()` | `QString` (JSON) | Active schedules, webhook port, uptime |
| `getExecutionHistory(limit)` | `QString` (JSON array) | Recent execution log with timing and results (capped at MAX_HISTORY=100 entries) |

All parameters and return values are JSON-encoded strings.

### Events

The scheduler declares three typed events, generated from `logos_events:` in the impl header:

| Event | Fired |
|---|---|
| `schedulerWorkflowDeployed(workflowId)` | A workflow was deployed and its triggers armed |
| `schedulerWorkflowTriggered(workflowId, triggerType)` | A trigger fired and the run is being handed to the engine |
| `schedulerExecutionCompleted(workflowId, success)` | A triggered run came back from the engine |

### Example Return Values

**`listDeployedWorkflows()`**
```json
[
  {
    "workflowId": "my-workflow",
    "name": "My Workflow",
    "webhookUrl": "http://localhost:8081/webhooks/my-workflow"
  }
]
```

**`getSchedulerStatus()`**
```json
{
  "deployedWorkflows": 3,
  "activeIntervalTimers": 1,
  "webhookPort": 8081,
  "webhookRunning": true
}
```

**`getExecutionHistory(limit)`**
```json
[
  {
    "workflowId": "my-workflow",
    "triggerType": "timer",
    "timestamp": 1711036800000,
    "success": true,
    "outputs": {}
  }
]
```

History is stored in memory as a ring buffer capped at **100 entries** (`MAX_HISTORY`). When the buffer is full, the oldest entry is evicted. History does not persist across restarts.

---

## Trigger Types

On deploy, the scheduler scans the workflow's `nodes` array for any node with `"type": "trigger"` and reads its `properties`:

**`timer`** — a node whose properties carry a cron expression or a millisecond interval arms the matching timer. Both may be set on the same node; each gets its own timer:
```json
{ "type": "trigger", "properties": { "cron": "0 9 * * 1-5" } }
{ "type": "trigger", "properties": { "intervalMs": 60000 } }
```
A cron expression is evaluated on a 60-second ticker against `CronParser::matchesNow`, so the finest cron granularity is one minute regardless of the expression.

**`webhook`** — every deployed workflow gets a webhook endpoint at `/webhooks/<workflowId>` unconditionally; there is no per-node opt-in. `deployWorkflow()`'s response includes the URL.

**`manual`** — any deployed workflow can be fired directly by calling `triggerWorkflow(workflowId, triggerData)`, independent of what trigger nodes it declares.

Redeploying an already-deployed `workflowId` tears down its previous timers before arming the new ones, rather than stacking a second set on top.

---

## Internal Components

### `CronParser`

Parses 5-field cron expressions (`minute hour dayOfMonth month dayOfWeek`) and evaluates whether they match the current time. Ported from `bridge/scheduler.js`. Supports `*`, exact values, ranges (`N-M`), lists (`N,M`), and steps (`*/N`).

### `DeploymentStore`

Persists deployed workflow JSON to `~/.local/share/logos/deployed-workflows/`. On startup, the scheduler calls `loadAll()` to restore all previously deployed workflows and re-arm their schedules — no manual re-deployment needed after a restart.

### `WebhookListener`

A lightweight HTTP server built on `QTcpServer` that listens on port 8081 by default. The port is configurable via the `LOGOS_WEBHOOK_PORT` environment variable. Accepts `POST /webhooks/<workflowId>`, parses the JSON body and HTTP headers, and emits `webhookReceived` to fire the matching workflow. Handles only the minimum HTTP parsing needed for webhook payloads; not a general-purpose HTTP server.

### `SchedulerRuntime`

The Qt-shaped half of the module — it owns `DeploymentStore`, `WebhookListener`, and the per-workflow `QTimer`s, and is the only part of this module that is a `QObject`. The impl class (`WorkflowSchedulerImpl`) stays Qt-free at its public API, as `interface: "universal"` requires; `SchedulerRuntime` hands trigger firings back to it through a plain `std::function`, so nothing Qt-typed crosses into the impl. Timers are tracked by `QTimer*`, not by `QTimer::timerId()` — a timer id belongs to the `QObject` that started it, and a previous version of this module stored the id and called `killTimer()` on the wrong object, so undeploying a workflow never actually stopped its timer.

---

## Architecture

```
logos-workflow-engine   (declared dependency; scheduler calls executeWorkflowWithTrigger
        ▲                through the generated typed accessor, modules().workflow_engine)
        │
logos-workflow-scheduler  ← you are here
        │
        ├── SchedulerRuntime (owns the Qt-shaped machinery below)
        │       ├── CronParser     (evaluates cron expressions)
        │       ├── DeploymentStore (persists to ~/.local/share/logos/deployed-workflows/)
        │       └── WebhookListener (QTcpServer on :8081)
        │
        └── WorkflowSchedulerImpl (the public API; Qt-free)
```

The scheduler has no dependency on the registry or canvas — it only needs the engine to run executions, and unlike the engine's own dispatch, this is a call to a *declared* dependency, so it goes through the generated typed accessor rather than a runtime client.

---

## Project Structure

```
logos-workflow-scheduler/
├── src/
│   ├── workflow_scheduler_impl.h    # The public API — this IS the module
│   ├── workflow_scheduler_impl.cpp
│   ├── scheduler_runtime.h          # The Qt-shaped machinery: store, listener, timers
│   ├── scheduler_runtime.cpp
│   ├── cron_parser.h/.cpp           # 5-field cron expression evaluator
│   ├── deployment_store.h/.cpp      # Disk persistence for deployed workflows
│   └── webhook_listener.h/.cpp      # QTcpServer-based HTTP webhook receiver
├── generated_code/                  # Generated glue + derived .lidl contract (build output)
├── CMakeLists.txt                   # Requires Qt6Network
├── flake.nix
└── metadata.json                    # Module descriptor: name, type, interface, dependencies
```

`generated_code/` no longer ships committed scaffolding — `interface: "universal"` means the `*Plugin`/`*Interface` glue and the `.lidl` contract are generated at build time from `workflow_scheduler_impl.h`.

---

## Building

### With Nix (recommended)

The scheduler depends on `logos-workflow-engine` at build time — the builder derives a typed `modules().workflow_engine` accessor from the engine's published `.lidl` contract. The flake wires this up automatically — Nix fetches the engine (and transitively the registry) from GitHub.

```bash
nix build
```

Build order matters: **registry → engine → scheduler**. The engine and registry must be pushed to GitHub before the scheduler can build.

Output: `result/lib/workflow_scheduler_plugin.so`, plus a derived `result/lib/workflow_scheduler.lidl` (installed to `share/logos/`).

### With CMake

Requires `logos-module-builder`'s CMake helpers (`LogosModule.cmake`) and the generator tools (`logos-cpp-generator`, `logos-qt-generator`) on your `PATH`, Qt6 with the Network module, and `LOGOS_MODULE_BUILDER_ROOT` set. The Nix build drives codegen through `logos-module-builder`'s `preConfigure`; a bare CMake build needs the equivalent generator invocation run first.

```bash
mkdir build && cd build
cmake .. -GNinja
ninja
```

Output: `build/modules/workflow_scheduler_plugin.so`

---

## Usage

```bash
# Deploy a workflow
logoscore -c "workflow_scheduler.deployWorkflow('my-workflow', '$(cat workflow.json)')"

# List deployed workflows
logoscore -c "workflow_scheduler.listDeployedWorkflows()"

# Manually trigger a deployed workflow
logoscore -c "workflow_scheduler.triggerWorkflow('my-workflow', '{\"source\":\"manual\"}')"

# Check scheduler status
logoscore -c "workflow_scheduler.getSchedulerStatus()"

# View last 20 executions
logoscore -c "workflow_scheduler.getExecutionHistory(20)"

# Undeploy
logoscore -c "workflow_scheduler.undeployWorkflow('my-workflow')"
```

### Webhook trigger

Once a webhook-triggered workflow is deployed, send a POST to fire it:

```bash
curl -X POST http://localhost:8081/webhooks/my-workflow \
  -H "Content-Type: application/json" \
  -d '{"event": "push", "repo": "logos-legos"}'
```

---

## Persistence

Deployed workflows are written to `~/.local/share/logos/deployed-workflows/<workflowId>.json` as individual JSON files, one per workflow. The scheduler reloads and re-arms all persisted workflows on startup via `DeploymentStore::loadAll()`, so cron schedules and webhook listeners resume automatically after a process restart or system reboot.

The storage directory is determined by `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)` with the `deployed-workflows` subdirectory appended. On Linux this resolves to `~/.local/share/logos/deployed-workflows/`.

---

## Related Modules

| Module | Role |
|---|---|
| [logos-workflow-engine](https://github.com/corpetty/logos-workflow-engine) | Called by this scheduler to execute triggered workflows |
| [logos-workflow-canvas](https://github.com/corpetty/logos-workflow-canvas) | Authors and exports the workflow JSON that gets deployed here |
| [logos-workflow-registry](https://github.com/corpetty/logos-workflow-registry) | No direct dependency, but provides the node types used in deployed workflows |
| [logos-legos](https://github.com/corpetty/logos-legos) | Parent repo with v1 prototype and full architecture docs |

See [logos-legos/docs/NATIVE-ARCHITECTURE.md](https://github.com/corpetty/logos-legos/blob/main/docs/NATIVE-ARCHITECTURE.md) for the full v2 design.

---

## License

MIT
