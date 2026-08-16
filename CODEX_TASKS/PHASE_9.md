# Phase 9 — Final Documentation / Demo / Portfolio / Resume

Repository:

/home/chaos/projects/cpp_robot_middleware

GitHub:

https://github.com/LiuKaiDev/cpp_robot_middleware

Branch:

main

Completed:

Phase 0 — Project Skeleton / Packaging
Phase 1 — UDS Baseline
Phase 2 — Registry / Discovery / mwctl
Phase 3 — Shared Memory V1
Phase 4 — Memory Pool / Message Lifecycle
Phase 5 — Ring Buffer / Backpressure / LoanedSample / SampleView
Phase 6 — Heartbeat / Crash Recovery
Phase 7 — ROS2 Adapter
Phase 8 — Automated Benchmark
Phase 8.1 — Profiling / Evidence-Based Optimization

Current Phase 8.1 commits:

4caf333234c644fd7405db4fb93a9039939587ad
chore: organize repository artifacts and work directories

971129a4495fcd870efe05b51d2dfe8e0087a0a9
perf: reduce measured SHM control-plane overhead

77f7adeb08df36ba4b39dd03f72453d1a7988a62
perf: add phase 8 profiling evidence

This task implements ONLY:

Phase 9 — Documentation / Demo / Portfolio / Resume

This is the final planned project phase.

Do NOT start new architecture work.

Do NOT add speculative performance optimization.

Do NOT implement advanced optional features merely to make the project appear more impressive.

==================================================
1. Git Preflight
==================================================

Before modifying anything:

cd /home/chaos/projects/cpp_robot_middleware

pwd
git status -sb
git branch --show-current
git remote -v

git fetch origin

git rev-parse HEAD
git rev-parse origin/main

git log --oneline -12

Requirements:

branch = main
working tree clean
HEAD == origin/main

Expected HEAD:

77f7adeb08df36ba4b39dd03f72453d1a7988a62

If HEAD != origin/main:

STOP.

Do NOT:

reset
rebase
amend
force push
rewrite Phase 0-8.1 history


==================================================
2. Read The Whole Project Before Finalizing It
==================================================

Read:

AGENTS.md
PROJECT_PLAN.md
README.md
START_HERE.md
GITHUB_REPO_INFO.md

CODEX_TASKS/PHASE_0.md
...
CODEX_TASKS/PHASE_8.md
CODEX_TASKS/PHASE_8_1.md

docs/reports/PHASE_0_REPORT.md
...
docs/reports/PHASE_8_REPORT.md
docs/reports/PHASE_8_1_REPORT.md

Read all current technical documentation under:

docs/

Especially:

CONTROL_PLANE
DATA_PLANE
MEMORY_POOL
QUEUES_AND_LOANING
FAILURE_MODEL
ROS2_ADAPTER
BENCHMARK
DEVELOPMENT_WORKFLOW

Inspect:

benchmark/
middleware/
registry/
ros2_adapter/
examples/
tools/
tests/

Then execute:

find . -maxdepth 3 -type f | sort


==================================================
3. Before Editing, Produce Final Project Audit
==================================================

Before touching files, output:

Current Project State

Completed Functional Areas

Current Test Counts

Current Benchmark Evidence

Current Documentation Inventory

Current Demo Capabilities

Current Known Limitations

Missing Mandatory Requirements

Files Planned To Add

Files Planned To Modify

Files Planned To Delete

Files Planned To Preserve

Then continue automatically unless a serious missing feature or repository inconsistency is found.


==================================================
4. Final Mandatory Feature Audit
==================================================

Compare the ACTUAL repository against PROJECT_PLAN's mandatory feature checklist.

Verify at least:

C++17 Middleware Core

Publisher API

Subscriber API

Unix Domain Socket baseline

Registry Daemon

Topic Discovery

Node Registration

mwctl

Shared Memory Transport

Shared Memory RAII

Memory Pool

Chunk Lifecycle

Multi Subscriber

Reference Counting

Subscriber Queue

Backpressure

Loaned Sample

Runtime Metrics

Heartbeat

Crash Detection

Resource Cleanup

ROS2 Adapter

Automated Benchmark

ROS2 Baseline

Unit Tests

Integration Tests

README

Architecture Documentation

Demo

Do not check boxes based only on old reports.

Inspect actual source/tests/current behavior.

Generate a final checklist.

If a mandatory item is genuinely missing:

DO NOT hide it.

If it is a small closure gap belonging to an already completed subsystem:

implement the minimum correct closure fix,
add tests,
run full regression.

Examples might include a missing final CLI surface for already-existing runtime metrics.

Do NOT use this as permission to implement advanced optional features.


==================================================
5. Optional Features Are NOT Required
==================================================

Do NOT add simply for portfolio appearance:

SPSC lock-free queue

eventfd notification

epoll wait-set redesign

SCM_RIGHTS

memfd_create migration

per-thread memory cache

CPU affinity

scheduler experiments

PointCloud2 bridge

multi-publisher topic

TCP transport

custom ROS2 RMW

DDS / RTPS implementation

These remain optional/future work unless already genuinely implemented.

README must not imply they exist.


==================================================
6. Repository Hygiene Convention Remains Mandatory
==================================================

All temporary Phase 9 work must live under:

.work/phase_9/

Suggested:

.work/phase_9/
├── build_debug/
├── build_release/
├── build_asan/
├── build_ubsan/
├── ros2/
│   ├── build/
│   ├── install/
│   └── log/
├── demo/
└── logs/

Do NOT create root:

build_final
build_phase9
install_final
log_final
etc.

At the end:

remove .work/phase_9/


==================================================
7. Clean Old Full Raw Benchmark Data
==================================================

Inspect:

benchmark/results/

Preserve permanently:

benchmark/results/phase8_reference/

benchmark/results/phase8_1_reference/

These contain compact historical/reference evidence.

Inspect full raw result directories such as:

benchmark/results/phase8_full_cf35309/

and any Phase 8.1 full raw run directory.

For each full raw directory:

git ls-files -- <path>

Confirm whether tracked/untracked.

If:

- untracked/ignored,
- compact reference data is safely preserved,
- docs/reports no longer require raw files,
- Phase 8.1 profiling is finished,

then it may be deleted to reduce local clutter.

Before deleting:

record its size.

Do NOT delete committed reference datasets.

Do NOT delete raw data if final documentation still requires something that has not been extracted yet.

Report exactly what was removed.


==================================================
8. Phase 9 Documentation Goal
==================================================

The final repository should explain the project to someone who has never seen previous Phase reports.

A recruiter / interviewer / engineer should be able to understand:

What problem this project solves

Why it exists

Architecture

Control Plane

Data Plane

Public API

Shared Memory Model

Memory Pool

Message Lifecycle

Multi-Subscriber Sharing

Backpressure

Loaned Sample

Failure Model

ROS2 Adapter

Benchmark Method

Benchmark Results

Profiling Findings

Known Limitations

How to build

How to run

How to reproduce demos


==================================================
9. Final README — Mandatory Structure
==================================================

Rewrite / consolidate README into a polished final project README.

It must contain logically equivalent sections:

1. Project Overview

2. Why This Project

3. Key Features

4. Architecture

5. Control Plane

6. Data Plane

7. Publisher / Subscriber API

8. Shared Memory Layout

9. Message Lifecycle

10. Memory Pool

11. Multi-Subscriber Sharing

12. Backpressure

13. Failure Model

14. Build

15. Quick Start

16. mwctl

17. ROS2 Adapter

18. Benchmark Methodology

19. Benchmark Results

20. Performance Analysis / Profiling

21. Demo

22. Known Limitations

23. Future Work

Exact numbering may differ if layout improves readability.

Do NOT make README enormous by copying every technical document.

README is the high-quality entry point.

Detailed explanations should link into docs/.


==================================================
10. README Accuracy Rule
==================================================

Every technical claim in README must be supported by:

actual implementation

current tests

current Phase reports

benchmark/reference artifacts

Do not write generic marketing claims.

Forbidden unsupported claims include:

production-grade

real-time guaranteed

fully zero-copy

lock-free

distributed middleware

DDS replacement

ROS2 replacement

faster than ROS2 in general

faster than Fast DDS in general

crash-proof

zero packet loss

Use precise measured language.


==================================================
11. Zero-Copy Wording
==================================================

Maintain the established distinction.

Correct:

The native SHM LoanedSample -> SampleView path was verified to avoid middleware payload copies between publisher fill and subscriber access.

Incorrect:

The entire middleware is zero-copy.

Also make clear:

Publisher::publish(data,size)
uses the SHM Copy path.

ROS2 Adapter performs serialization/deserialization
and contains Adapter-level copy boundaries.

UDS is not zero-copy.


==================================================
12. Benchmark Results Must Use Latest Reference
==================================================

The final benchmark summary must primarily use:

benchmark/results/phase8_1_reference/

because Phase 8.1 retained an optimization and reran the complete matrix.

Do NOT accidentally use old pre-optimization Phase 8 numbers as the final headline results.

Keep:

benchmark/results/phase8_reference/

as historical baseline evidence.

README should label reference environment clearly.

Extract final numbers programmatically from:

phase8_1_reference

and/or:

docs/reports/PHASE_8_1_REPORT.md

Do not manually type numbers from memory.


==================================================
13. Benchmark Honesty
==================================================

Final benchmark narrative must include BOTH strengths and tradeoffs.

For example, based on actual current data determine and report:

where UDS remains competitive

where SHM starts to win

large-message behavior

SHM Copy vs SHM Loan

1->1 vs 1->4 scaling

CPU tradeoffs

RSS tradeoffs

ROS2 baseline behavior

backpressure behavior

Do not cherry-pick only winning SHM cases.


==================================================
14. Phase 8.1 Optimization Must Be Explained
==================================================

Explain the measured Phase 8.1 finding accurately:

Small-message SHM performance was dominated by avoidable per-message synchronous registry/discovery work.

The retained optimization introduced:

bounded 1 ms SHM discovery reuse
with immediate invalidation on failure

and:

nonblocking draining of redundant complete wake frames

But verify implementation/docs before using this wording.

Include representative before/after data derived from Phase 8.1 report.

Do NOT describe perf/strace findings that were never collected.

Explicitly state:

perf unavailable
strace unavailable

and that Phase 8.1 used available /proc and benchmark evidence.

Do not upgrade coarse evidence into symbol-level profiling claims.


==================================================
15. Final Architecture Documentation
==================================================

Create or finalize:

docs/ARCHITECTURE.md

This should serve as the canonical architecture document.

Include:

system overview

component boundaries

Middleware Core

mw_registryd

Publisher

Subscriber

UDS transport

SHM transport

Memory Pool

Subscriber Queue

ROS2 Adapter

Benchmark subsystem

Control Plane vs Data Plane

process/thread model

resource ownership overview

dependency direction


==================================================
16. Architecture Diagram
==================================================

Add a clear architecture diagram.

Mermaid is acceptable and preferred because GitHub renders it directly.

Example logical structure:

Applications
     │
Publisher / Subscriber API
     │
Middleware Core
 ┌───┴─────────────────────┐
 │                         │
Control Plane          Data Plane
 │                         │
UDS Registry      UDS / SHM Copy / SHM Loan
 │                         │
mw_registryd        Memory Pool / Queue
                           │
                       Subscribers

ROS2 Adapter sits OUTSIDE Middleware Core.

Make the actual diagram reflect real implementation.

Do not place rclcpp inside Middleware Core.


==================================================
17. Protocol Documentation
==================================================

Create/finalize:

docs/PROTOCOL.md

Consolidate high-level protocol information from existing docs.

Include:

ControlHeader

control versioning

request_id

Node registration

Topic advertisement/subscription

Discovery

Data frame / notification concepts

UDS baseline framing

SHM handle/notification

Heartbeat

Release/recovery control messages

Protocol safety checks

Do not duplicate every implementation detail unnecessarily.


==================================================
18. Memory Model Documentation
==================================================

Create/finalize:

docs/MEMORY_MODEL.md

Include:

Shared Memory ownership

Pool lifetime

Size classes

ChunkHeader

ChunkHandle

pool_id

chunk_index

generation

ref_count

Free List

logical Chunk identity

why virtual pointers differ across processes

SHM Copy path

SHM Loan path

cleanup behavior


==================================================
19. Message Lifecycle Documentation
==================================================

Create/finalize:

docs/MESSAGE_LIFECYCLE.md

Explain:

FREE
↓
LOANED
↓
PUBLISHED
↓
RELEASED
↓
FREE

Include:

ordinary publish

loan without publish

loan then publish

multi-subscriber ref_count

SampleView lifetime

DROP_NEWEST

DROP_OLDEST

BLOCK_WITH_TIMEOUT

subscriber crash reference recovery

publisher crash cleanup


==================================================
20. Failure Model Documentation
==================================================

Keep:

docs/FAILURE_MODEL.md

but ensure it reflects final implementation.

Must clearly distinguish:

normal exit

control socket failure

heartbeat timeout

ALIVE

SUSPECTED

DEAD

publisher SIGKILL

subscriber SIGKILL

robust mutex recovery

outstanding SampleView recovery

stale generation protection

resource cleanup

reconnect

Known limitation:

no automatic recovery after registry daemon itself is lost

Do not overclaim beyond tested scenarios.


==================================================
21. ROS2 Adapter Documentation
==================================================

Keep/finalize:

docs/ROS2_ADAPTER.md

Ensure it clearly says:

Middleware Core does not depend on ROS2

Adapter uses installed mw package

ROS2 Jazzy tested

Supported:

std_msgs/msg/String
geometry_msgs/msg/Twist
sensor_msgs/msg/Image

Both directions supported

Image large-message case verified

Adapter serialization copies exist

Adapter is NOT a custom RMW

Adapter is NOT end-to-end zero-copy


==================================================
22. Benchmark Documentation
==================================================

Finalize:

docs/BENCHMARK.md

Use latest optimized reference as final primary result.

Include:

Environment

WSL2 caveat

Intel Core i5-8300H reference machine

GCC / ROS2 version

RMW implementation

Release build

Transport definitions

UDS

SHM Copy

SHM Loan

direct ROS2 baseline

Message matrix

Topologies

Latency definition

Throughput definitions

CPU/RSS methods

Repetitions

Backpressure experiment

Phase 8.1 optimization

Limitations


==================================================
23. Benchmark Charts
==================================================

Use current latest committed chart data.

Prefer charts from:

benchmark/results/phase8_1_reference/

If Phase 8.1 reference does not already contain all final chart files:

regenerate them from committed aggregate results.

Do NOT rerun the entire benchmark merely to make plots.

Required final visualizations should include at least:

Latency vs Message Size

Throughput vs Message Size

CPU vs Message Size

Subscriber Count vs Throughput

README should display/select a small useful subset.

Do not flood README with every chart.


==================================================
24. Documentation Navigation
==================================================

Create a coherent docs navigation section in README.

Recommended links:

Architecture

Protocol

Control Plane

Data Plane

Memory Model

Message Lifecycle

Memory Pool

Queues and Loaning

Failure Model

ROS2 Adapter

Benchmark

Profiling / Phase 8.1 Report

Demo

Reports


==================================================
25. Historical Phase Reports
==================================================

Keep:

docs/reports/PHASE_0_REPORT.md
...
docs/reports/PHASE_8_1_REPORT.md

Do not move them back to root.

They are development/acceptance history.

README should not force readers through all reports.

They belong in an:

Engineering History / Reports

link.


==================================================
26. Demo Goal
==================================================

Create:

docs/DEMO.md

and reproducible demo scripts.

Do not write commands that do not actually exist.

Inspect current binaries and CLI first.

Every command included in final demo documentation must be:

executed
or
validated with --help / equivalent

Do not copy old PROJECT_PLAN executable names if current implementation uses different real executable names.


==================================================
27. Demo Script Layout
==================================================

Use something like:

scripts/demo/
├── common.sh
├── demo_basic_pubsub.sh
├── demo_large_message.sh
├── demo_multi_subscriber.sh
├── demo_backpressure.sh
├── demo_crash_recovery.sh
├── demo_ros2_adapter.sh
├── demo_benchmark.sh
└── run_all_smoke.sh

Adjust according to repository.

Scripts must:

set -euo pipefail where appropriate

use unique socket/topic/node names

have bounded timeouts

clean only their own resources

trap cleanup on EXIT

not use broad pkill

not require manual /dev/shm cleanup


==================================================
28. Demo 1 — Basic Pub/Sub
==================================================

Provide a real demonstration of:

mw_registryd
Publisher
Subscriber

Display useful information such as:

sequence
payload size
transport
correctness

If existing demo binaries expose latency, show it.

Do not fabricate output.


==================================================
29. Demo 2 — Large Message
==================================================

Demonstrate:

4 MiB

preferably:

SHM Copy
and/or
SHM Loan

Show actual:

message size
received count
payload correctness
latency/throughput if existing benchmark endpoint naturally exposes them

Do not implement a new performance benchmark just for demo.


==================================================
30. Demo 3 — Multi Subscriber
==================================================

Demonstrate:

1 Publisher
4 Subscribers

SHM mode.

Show evidence that subscribers reference the same logical Chunk:

same pool_id
same chunk_index
same generation

where current diagnostic/test interfaces allow.

Do NOT claim identical virtual pointer addresses.


==================================================
31. Demo 4 — Backpressure
==================================================

Demonstrate:

DROP_NEWEST

DROP_OLDEST

BLOCK_WITH_TIMEOUT

Use intentionally slow consumer / small queue.

Display actual:

queue behavior
drop/overflow counts
blocked behavior

Use existing Phase 5 capabilities.


==================================================
32. Demo 5 — Crash Recovery
==================================================

Demonstrate real:

kill -9

against Publisher or Subscriber.

Show before/after:

mwctl node list
or other actual registry state

and verify:

dead endpoint removed
system continues
replacement endpoint can connect

Script must kill only process IDs it started.


==================================================
33. Demo 6 — ROS2 Adapter
==================================================

Use actual ROS2 Jazzy environment.

Demonstrate one clean path such as:

Custom Middleware Publisher
       ↓
ROS2 Adapter
       ↓
ROS2 Image Topic
       ↓
ROS2 Subscriber

or String if it makes demo substantially simpler.

The documentation should additionally mention all three supported types.

Do not require Phase 9 to benchmark ROS2 Adapter.


==================================================
34. Demo 7 — Benchmark Dashboard
==================================================

Do not rerun full matrix.

Use committed:

phase8_1_reference

Display/link:

p50/p99

Throughput

CPU

Memory

and the selected comparison charts.


==================================================
35. Demo Smoke Acceptance
==================================================

Execute all non-interactive demo scripts that can reasonably be automated.

Create:

scripts/demo/run_all_smoke.sh

It should validate the demo mechanics.

ROS2 demo may be a separate smoke target requiring sourced ROS2 environment.

Report actual results.


==================================================
36. Terminal Demo Recording
==================================================

Create a real terminal demo transcript based on an actual successful run.

For example:

docs/assets/demo/terminal_demo.txt

or:

docs/assets/demo/terminal_demo.cast

Do not type a fake transcript manually.

Capture actual commands/output.


==================================================
37. Demo GIF
==================================================

PROJECT_PLAN requests a Demo GIF.

First inspect available tools:

command -v asciinema || true
command -v agg || true
command -v ffmpeg || true

Also check whether Python Pillow is available.

Do NOT:

sudo apt install
pip install globally

If suitable existing tooling is available:

generate a compact:

docs/assets/demo/demo.gif

from an ACTUAL captured demo.

If rendering requires Pillow and Pillow is already installed:

a small deterministic renderer may be used to render the actual terminal transcript.

Do not fabricate successful output frames.

If no rendering tool/dependency is available:

do NOT fake a GIF.

Report:

Demo GIF: DEFERRED — rendering tooling unavailable

The reproducible demo scripts + actual transcript remain mandatory.


==================================================
38. Interview / Portfolio Guide
==================================================

Create:

docs/INTERVIEW_GUIDE.md

This should help explain the implemented project, not provide generic textbook dumping.

Cover concise answers grounded in THIS project:

Why Pub/Sub?

Why Registry / Discovery?

Control Plane vs Data Plane?

Why one active Publisher per Topic in V1?

Why Shared Memory?

Why SHM does not automatically mean zero-copy?

SHM Copy vs SHM Loan?

Memory Pool?

Chunk lifecycle?

Reference counting?

Multi Subscriber sharing?

Ring Buffer?

Backpressure?

DROP_NEWEST vs DROP_OLDEST?

BLOCK_WITH_TIMEOUT?

Failure recovery?

Heartbeat?

Robust process-shared mutex?

Why UDS can be competitive for small messages?

Why SHM benefits large messages?

p50 vs p99?

How CPU/RSS were measured?

Why ROS2 Adapter instead of custom RMW?

ROS2 Adapter vs RMW?

Known limitations?

Use actual project examples/results.


==================================================
39. Resume Documentation
==================================================

Create:

docs/RESUME.md

Include:

Recommended project title

1 concise project summary

3-4 Chinese resume bullets

3-4 English resume bullets

Key technologies

Measured performance evidence

Interview talking points

Do NOT create fake percentages.

Pull all quantitative values from:

benchmark/results/phase8_1_reference/

or:

docs/reports/PHASE_8_1_REPORT.md

Do not use unverified numbers.


==================================================
40. Resume Positioning
==================================================

The project should be positioned around:

Linux + C++17

local multi-process Pub/Sub middleware

UDS Control Plane

Shared Memory Data Plane

Registry / Discovery

Memory Pool

Chunk Lifecycle

Reference Counting

Multi-Subscriber sharing

Backpressure

Loaned Sample

Fault Recovery

ROS2 Adapter

Benchmark / Profiling

Do NOT reduce the description to:

“Used shared memory for IPC.”

Do NOT claim:

full DDS

distributed middleware

custom RMW

production middleware

hard real-time


==================================================
41. Resume Quantification
==================================================

Now that measured data exists, final resume bullets MAY contain selected real numbers.

But:

use latest optimized reference data.

Include benchmark environment context where relevant.

Choose metrics that genuinely illustrate engineering results.

Also preserve tradeoffs.

Examples of appropriate form:

“... reduced X from measured A to B under <specific test>”

or:

“... achieved <measured result> for 4 MiB messages under <topology>”

Only use actual final values.

No invented “10x faster” headline.


==================================================
42. README Performance Summary
==================================================

README should include a compact table rather than dumping the whole benchmark dataset.

Choose representative sizes such as:

64 B

64 KiB

1 MiB

4 MiB

and/or selected topology.

Use latest results.

Provide link to full Benchmark documentation.

Include at least one case where:

UDS is competitive / wins

and one case where:

SHM / Loan shows its advantage

This prevents cherry-picked storytelling.


==================================================
43. Runtime Metrics Audit
==================================================

PROJECT_PLAN lists Runtime Metrics as a mandatory feature.

Inspect what is currently implemented.

Verify actual counters for relevant components.

Check whether current CLI exposes them.

If current project already provides sufficient runtime metrics:

document them.

If counters exist but `mwctl stats` or equivalent planned visibility is missing:

evaluate whether a minimal query can be added cleanly using existing metrics architecture.

If a small closure implementation is needed:

implement it with tests.

Do NOT build a large new observability subsystem.

At minimum final docs must accurately state what runtime metrics are currently accessible.


==================================================
44. mwctl Final Documentation
==================================================

Document every command that ACTUALLY exists.

Examples might include:

mwctl node list
mwctl topic list
mwctl topic info ...

If stats command exists after audit:

document it.

Do not document commands that were only proposed in PROJECT_PLAN but do not exist.


==================================================
45. Known Limitations Document
==================================================

Create or finalize:

docs/KNOWN_LIMITATIONS.md

Must include current real limitations such as applicable:

single host only

Linux-specific implementation

one active Publisher per Topic

ROS2 Adapter supports only String/Twist/Image

adapter serialization copies

no automatic reconnection after registry-daemon loss

robust queue recovery may discard uncertain queued samples

Linux pthread/shared-atomic ABI assumptions

reference benchmark measured on WSL2

normal OS scheduling / no CPU isolation

perf and strace unavailable during Phase 8.1

no custom RMW

no distributed transport

no security/authentication

no persistence

Do not turn limitations into marketing language.


==================================================
46. Future Work
==================================================

Future Work may include:

eventfd

lock-free/SPSC optimization

per-thread allocator cache

CPU affinity experiments

perf / strace profiling on suitable native Linux environment

PointCloud2 Adapter

multi-publisher semantics

remote TCP transport

CI

But label all as:

NOT IMPLEMENTED

Do not mix future work into Key Features.


==================================================
47. License Must NOT Be Invented
==================================================

Phase 7 reported:

repository license remains unselected.

Do NOT automatically create:

MIT
Apache-2.0
GPL

or any LICENSE file.

License selection belongs to project owner.

README may state:

License: not selected yet

if appropriate.

Do not treat this as Phase 9 failure.


==================================================
48. Final Build Verification
==================================================

Use:

.work/phase_9/

Run clean Core Debug build:

cmake -S . -B .work/phase_9/build_debug \
  -DCMAKE_BUILD_TYPE=Debug

cmake --build .work/phase_9/build_debug -j

ctest \
  --test-dir .work/phase_9/build_debug \
  --output-on-failure

Current expected baseline before Phase 9:

83/83

If tests are added, count may increase.

Requirement:

100% PASS.


==================================================
49. ASan / UBSan
==================================================

Run existing:

ASan

UBSan

using .work/phase_9 paths.

All tests must PASS.

Do not run performance numbers under sanitizers.


==================================================
50. Release Build
==================================================

Run clean:

Release

under:

.work/phase_9/build_release/

Verify:

build succeeds

core artifacts exist

demo binaries used by README exist

No performance-critical changes should be introduced in Phase 9 unless a genuine correctness closure fix required them.


==================================================
51. Install / Export
==================================================

Verify:

cmake --install

to a path under:

.work/phase_9/

Then verify external consumer:

find_package(mw CONFIG REQUIRED)

mw::mw_core

still works.


==================================================
52. ROS2 Regression
==================================================

Use:

.work/phase_9/ros2/

Run:

colcon build
colcon test

for existing Phase 7 ROS2 Adapter.

Expected existing baseline:

24/24

unless current test count structure differs.

String / Twist / Image support must remain intact.


==================================================
53. Benchmark Smoke Only
==================================================

Phase 9 should NOT rerun the 441-run matrix unless Phase 9 unexpectedly changes a performance-critical path.

Run benchmark smoke only to verify final README/demo tooling.

Use committed:

phase8_1_reference

for final performance data.

If Phase 9 only changes docs/scripts/CLI observability:

full benchmark rerun is NOT required.


==================================================
54. Documentation Link Validator
==================================================

Add or use a small script to validate local Markdown links.

It should detect:

broken relative links

references to reports that used to be in root

missing chart paths

missing docs

Do not require network access.

Run it against:

README.md
docs/**/*.md

All local links must resolve.


==================================================
55. Command Verification
==================================================

Extract or manually audit command examples from:

README
docs/DEMO.md
docs/ROS2_ADAPTER.md
docs/BENCHMARK.md

Every important command should correspond to a real executable/script.

Where practical run:

--help

or smoke command.

No fictional command examples.


==================================================
56. Documentation Consistency
==================================================

Search for stale claims such as:

Phase 3 current
Phase 4 current
Phase 8 current

when final project status should now be Phase 9 complete.

Also search for outdated:

PHASE_X_REPORT.md

root links.

Use:

git grep

and update current docs.

Do NOT rewrite historical CODEX_TASKS or historical reports solely to change their old wording.


==================================================
57. Architecture Dependency Audit
==================================================

Reconfirm:

Middleware Core depends on ROS2:
NO

Check source:

git grep -n "rclcpp" -- <actual core directories>

Check binary dependencies on:

libmw_core.so

using:

ldd
readelf -d

No ROS2 dependency should appear in core.


==================================================
58. Generated Assets Policy
==================================================

Permanent visual assets may live under:

docs/assets/

Keep them compact.

Allowed:

architecture diagram asset
selected benchmark charts
demo GIF/transcript

Do NOT commit:

huge logs

raw benchmark matrices

build outputs

ROS logs

perf raw data


==================================================
59. Final Repository Structure
==================================================

The repository root should remain clean.

Expected high-level structure similar to:

benchmark/
cmake/
CODEX_TASKS/
docs/
examples/
middleware/
registry/
ros2_adapter/
scripts/
tests/
tools/

AGENTS.md
CMakeLists.txt
PROJECT_PLAN.md
README.md
START_HERE.md
.gitignore
.clang-format

Do not create unnecessary root files.


==================================================
60. Final Root Cleanup
==================================================

Before commit:

remove:

.work/phase_9/

Inspect:

find . -maxdepth 1 -mindepth 1 -printf '%f\n' | sort

No root:

build_*
install_*
log_*

No temporary binaries/logs.

No raw full benchmark result directories should remain if they were safely deemed unnecessary and untracked.


==================================================
61. Final Documentation Review
==================================================

Review README as if you are:

1. a recruiter spending 60 seconds

2. a C++ interviewer

3. a robotics engineer

The first screen should quickly reveal:

what the project is

what was implemented

architecture

key measurable result

how to run it

But avoid giant badges/marketing clutter.


==================================================
62. Final Portfolio Evidence
==================================================

The final repo should visibly demonstrate:

Engineering depth:
IPC / SHM / lifecycle / queues

Correctness:
tests / sanitizers / fault injection

Systems design:
control plane / data plane

Robotics relevance:
ROS2 Adapter / Image

Performance engineering:
repeatable benchmark + Phase 8.1 evidence

Honesty:
known limitations + non-cherry-picked results


==================================================
63. Final Checklist File
==================================================

Create:

docs/PROJECT_COMPLETION_CHECKLIST.md

List mandatory requirements from PROJECT_PLAN.

For every item record:

PASS

or:

NOT IMPLEMENTED

with evidence link.

Do not mark an optional future feature as mandatory failure.

This should make final project completeness easy to audit.


==================================================
64. Phase 9 Report
==================================================

Create:

docs/reports/PHASE_9_REPORT.md

Include:

Scope

Files Added

Files Modified

Files Removed

Repository Cleanup

Full Mandatory Feature Audit

README Finalization

Architecture Documentation

Protocol Documentation

Memory Model

Message Lifecycle

Failure Model

ROS2 Adapter

Benchmark Finalization

Phase 8.1 Result Integration

Demo Scripts

Demo Smoke Results

Terminal Transcript

Demo GIF

Interview Guide

Resume Guide

Runtime Metrics Audit

Build Result

CTest

ASan

UBSan

Release Build

Install / Export

External Consumer

ROS2 Adapter Regression

Benchmark Smoke

Documentation Link Check

Command Verification

Core / ROS2 Dependency Audit

Root Cleanliness

Known Limitations

Future Work

License Status

Final Project Status


==================================================
65. Save Phase Task
==================================================

Save this task as:

CODEX_TASKS/PHASE_9.md

Keep all:

PHASE_0.md
...
PHASE_8_1.md


==================================================
66. Git Review
==================================================

Before commit:

git status
git status --short
git diff --check
git diff --stat
git diff

Make sure:

no build artifacts

no .work

no ROS logs

no giant raw benchmark data accidentally added

no old reports moved back to root

no unsupported claims

no Phase 10 work

no optional speculative feature implementation


==================================================
67. Commit Strategy
==================================================

Prefer clean logical commits.

Recommended:

docs: finalize project architecture and technical documentation

demo: add reproducible project demonstrations

docs: finalize benchmark portfolio and resume materials

If a real prior mandatory-feature closure fix was needed:

feat: complete <actual missing mandatory feature>

with its tests as a separate commit.

Do not create dozens of tiny documentation commits.

Do not amend Phase 8.1 history.


==================================================
68. GitHub Push Policy
==================================================

After all acceptance passes:

git push origin main

If GitHub authentication fails from Codex non-interactive environment:

DO NOT:

recommit
amend
rebase
reset
force push

Keep local commits.

Report:

GitHub Push:
MANUAL ACTION REQUIRED

Commits:
...

Manual command:

git push origin main


==================================================
69. Do NOT Create Git Tag Automatically
==================================================

PROJECT_PLAN mentions tags as optional.

Do NOT automatically create:

phase-9
v1.0
v0.1
release tags

unless the project owner explicitly requests it later.

Do not create a GitHub Release automatically.


==================================================
70. Final Response Format
==================================================

Return actual results only:

Phase 9 Implementation:
PASS / FAIL

Final Project Status:
COMPLETE / INCOMPLETE

Branch:
main

Commit(s):
<SHA + message>

GitHub Push:
PASS / MANUAL ACTION REQUIRED / FAIL

Mandatory Feature Audit:
PASS / FAIL
X/X mandatory items complete

Core Build:
PASS / FAIL

CTest:
PASS / FAIL
X/X tests passed

ASan:
PASS / FAIL

UBSan:
PASS / FAIL

Release Build:
PASS / FAIL

Install / Export:
PASS / FAIL

External Consumer:
PASS / FAIL

ROS2 Adapter Regression:
PASS / FAIL
X/X

Benchmark Smoke:
PASS / FAIL

Latest Benchmark Reference:
benchmark/results/phase8_1_reference/

Final README:
PASS / FAIL

Architecture Documentation:
PASS / FAIL

Architecture Diagram:
PASS / FAIL

Protocol Documentation:
PASS / FAIL

Memory Model Documentation:
PASS / FAIL

Message Lifecycle Documentation:
PASS / FAIL

Failure Model Documentation:
PASS / FAIL

ROS2 Documentation:
PASS / FAIL

Benchmark Documentation:
PASS / FAIL

Known Limitations:
PASS / FAIL

Project Completion Checklist:
PASS / FAIL

Runtime Metrics Audit:
PASS / FAIL

mwctl Final Documentation:
PASS / FAIL

Demo 1 Basic Pub/Sub:
PASS / FAIL

Demo 2 Large Message:
PASS / FAIL

Demo 3 Multi Subscriber:
PASS / FAIL

Demo 4 Backpressure:
PASS / FAIL

Demo 5 Crash Recovery:
PASS / FAIL

Demo 6 ROS2 Adapter:
PASS / FAIL

Demo 7 Benchmark:
PASS / FAIL

Demo Smoke:
PASS / FAIL

Terminal Demo Transcript:
PASS / FAIL

Demo GIF:
PASS / DEFERRED / FAIL
Reason if deferred: ...

README Local Link Check:
PASS / FAIL

Documentation Command Check:
PASS / FAIL

Middleware Core Depends On ROS2:
NO / YES

Root Clean:
YES / NO

Temporary .work Cleanup:
PASS / FAIL

Old Full Raw Benchmark Data:
REMOVED / PRESERVED
Reason: ...

Resume Guide:
PASS / FAIL

Interview Guide:
PASS / FAIL

License:
NOT SELECTED / <existing license>
Do not invent one.

Representative Final Benchmark Findings:
<actual concise data-backed findings from phase8_1_reference>

Final Known Limitations:
...

Files Added:
...

Files Modified:
...

Files Removed:
...

Phase Boundary:
Phase 9 is the final planned phase.
No additional project phase was implemented.

Next Action:
Project complete. Wait for project owner review.


==================================================
71. Final Engineering Rule
==================================================

Phase 9 is NOT the stage to make the code look impressive through new complexity.

The goals are:

accurate documentation
reproducible demos
traceable benchmark evidence
clear architecture
honest limitations
portfolio-quality presentation
interview-ready explanation

Do not sacrifice technical honesty for marketing.

Complete Phase 9 and STOP.