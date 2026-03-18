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
| `getExecutionHistory(limit)` | `QString` (JSON array) | Recent execution log with timing and results |

All parameters and return values are JSON-encoded strings.

---

## Trigger Types

Trigger type is determined by the `_trigger` property on the workflow's trigger node.

**`timer`** — fires on a cron expression or millisecond interval:
```json
{ "_trigger": "timer", "cron": "0 9 * * 1-5" }
{ "_trigger": "timer", "intervalMs": 60000 }
```

**`webhook`** — fires when an HTTP POST is received on `/webhooks/<workflowId>`:
```json
{ "_trigger": "webhook" }
```

**`manual`** — only fires when `triggerWorkflow()` is called explicitly:
```json
{ "_trigger": "manual" }
```

---

## Internal Components

### `CronParser`

Parses 5-field cron expressions (`minute hour dayOfMonth month dayOfWeek`) and evaluates whether they match the current time. Ported from `bridge/scheduler.js`. Supports `*`, exact values, ranges (`N-M`), lists (`N,M`), and steps (`*/N`).

### `DeploymentStore`

Persists deployed workflow JSON to `~/.local/share/logos/deployed-workflows/`. On startup, the scheduler calls `loadAll()` to restore all previously deployed workflows and re-arm their schedules — no manual re-deployment needed after a restart.

### `WebhookListener`

A lightweight HTTP server built on `QTcpServer` that listens on port 8081 (configurable). Accepts `POST /webhooks/<workflowId>`, parses the JSON body and HTTP headers, and emits `webhookReceived` to fire the matching workflow. Handles only the minimum HTTP parsing needed for webhook payloads; not a general-purpose HTTP server.

---

## Architecture

```
logos-workflow-engine   (scheduler calls executeWorkflowWithTrigger)
        ▲
        │
logos-workflow-scheduler  ← you are here
        │
        ├── CronParser     (evaluates cron expressions)
        ├── DeploymentStore (persists to ~/.local/share/logos/deployed-workflows/)
        └── WebhookListener (QTcpServer on :8081)
```

The scheduler has no dependency on the registry or canvas — it only needs the engine to run executions.

---

## Project Structure

```
logos-workflow-scheduler/
├── src/
│   ├── workflow_scheduler_interface.h      # Public interface (Q_INVOKABLE declarations)
│   ├── workflow_scheduler_plugin.h/.cpp    # Plugin implementation + timer loop
│   ├── cron_parser.h/.cpp                 # 5-field cron expression evaluator
│   ├── deployment_store.h/.cpp            # Disk persistence for deployed workflows
│   └── webhook_listener.h/.cpp           # QTcpServer-based HTTP webhook receiver
├── generated_code/                         # Auto-generated LogosAPI/SDK scaffolding
├── CMakeLists.txt                          # Requires Qt6Network
├── flake.nix
├── module.yaml                             # depends on: workflow_engine
└── metadata.json
```

---

## Building

### With Nix (recommended)

```bash
nix build
```

### With CMake

Requires Qt6 with Network module.

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/logos-core
make -j$(nproc)
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

Deployed workflows are written to `~/.local/share/logos/deployed-workflows/<workflowId>.json`. The scheduler reloads and re-arms all persisted workflows on startup, so cron schedules and webhook listeners resume automatically after a process restart or system reboot.

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
