# Repository Hygiene + Phase 8.1 Profiling / Evidence-Based Optimization

Repository:

/home/chaos/projects/cpp_robot_middleware

GitHub:

https://github.com/LiuKaiDev/cpp_robot_middleware

Branch:

main

Current expected HEAD after Phase 8:

d3a30b7280c60353675c4528e9b5e76c3a5bf99c
docs: add phase 8 benchmark results

Earlier Phase 8 benchmark implementation commit:

cf353091f7149ac4f458ec77d98f23bb948155b2

Completed:

Phase 0 — Project Skeleton
Phase 1 — UDS Baseline
Phase 2 — Registry / Discovery
Phase 3 — SHM V1
Phase 4 — Memory Pool / Lifecycle
Phase 5 — Ring Buffer / Backpressure / Loaning
Phase 6 — Crash Recovery
Phase 7 — ROS2 Adapter
Phase 8 — Automated Benchmark

This task consists of TWO ordered parts:

Part A:
Repository hygiene / artifact cleanup

Part B:
Phase 8.1 — Profiling and evidence-based optimization

Do NOT start Phase 9.

==================================================
0. HARD RULE — DO NOT DESTROY SOURCE OR RESULTS
==================================================

Repository cleanup must NEVER delete:

middleware/
registry/
ros2_adapter/
benchmark source code
tests/
tools/
examples/
docs/
CODEX_TASKS/

README.md
PROJECT_PLAN.md
AGENTS.md
CMakeLists.txt

committed Phase 8 reference benchmark artifacts

benchmark/results/phase8_reference/

PHASE reports before they are safely moved

Do NOT run:

git clean -fdx
git reset --hard

Do NOT broadly delete:

benchmark/results/

Do NOT delete:

benchmark/results/phase8_full_cf35309

before Phase 8.1 profiling is complete.

That full raw Phase 8 dataset may still be needed for comparison and investigation.

==================================================
1. Git Preflight
==================================================

Before doing anything:

cd /home/chaos/projects/cpp_robot_middleware

pwd
git status -sb
git branch --show-current
git remote -v

git fetch origin

git rev-parse HEAD
git rev-parse origin/main

git log --oneline -10

Requirements:

branch = main
working tree clean
HEAD == origin/main

Expected HEAD:

d3a30b7280c60353675c4528e9b5e76c3a5bf99c

If HEAD != origin/main:

STOP.

Do NOT:

reset
rebase
amend
force push

Report the mismatch and do not start cleanup/profiling.

==================================================
2. Read Existing Project
==================================================

Read:

AGENTS.md
PROJECT_PLAN.md
README.md
START_HERE.md
GITHUB_REPO_INFO.md

PHASE_0_REPORT.md
PHASE_1_REPORT.md
PHASE_2_REPORT.md
PHASE_3_REPORT.md
PHASE_4_REPORT.md
PHASE_5_REPORT.md
PHASE_6_REPORT.md
PHASE_7_REPORT.md
PHASE_8_REPORT.md

CODEX_TASKS/PHASE_0.md
...
CODEX_TASKS/PHASE_8.md

docs/

Especially study:

PHASE_8_REPORT.md
docs/BENCHMARK.md

and PROJECT_PLAN sections:

Benchmark
Benchmark Fairness
Phase 8
Phase 8.1 Profiling
Phase 9

Inspect repository root:

find . -maxdepth 2 -mindepth 1 | sort

Before modifying anything, output:

Current Repository State

Temporary Directories Found

Permanent Artifacts Found

Files Planned To Move

Files Planned To Delete

Files Planned To Keep

Profiling Plan

==================================================
PART A — REPOSITORY HYGIENE
==================================================

==================================================
3. Goal Of Cleanup
==================================================

The repository root currently contains many generated directories such as:

build_external_phase8

build_phase8_asan
build_phase8_compile
build_phase8_debug
build_phase8_ubsan

build_release
build_ros2_benchmark
build_ros2_phase8

install
install_ros2_benchmark
install_ros2_phase8

log
log_ros2_benchmark
log_ros2_phase8
log_ros2_phase8_final
log_ros2_phase8_final_result
log_ros2_phase8_final_test
log_ros2_phase8_test

These are build/install/log work products and should not permanently clutter the repository root.

The desired root should mostly contain:

benchmark/
cmake/
CODEX_TASKS/
docs/
examples/
middleware/
registry/
ros2_adapter/
tests/
tools/

CMakeLists.txt
README.md
PROJECT_PLAN.md
AGENTS.md
.gitignore
.clang-format

plus only genuinely useful project metadata.

==================================================
4. Verify Before Deleting Temporary Directories
==================================================

Do not blindly rm directories.

For every candidate build/install/log directory:

check:

git status --short -- <path>
git ls-files -- <path>

If Git tracks meaningful files inside it:

DO NOT DELETE IT
until understood.

If it is clearly generated/untracked output:

it may be removed.

Inspect size where useful:

du -sh <path>

Do not delete files just because their names begin with:

build
install
log

without checking.

==================================================
5. Remove Root Build / Install / Log Artifacts
==================================================

After verifying they are generated artifacts, remove the Phase 8 work directories currently cluttering root.

Expected candidates include:

build_external_phase8
build_phase8_asan
build_phase8_compile
build_phase8_debug
build_phase8_ubsan
build_release
build_ros2_benchmark
build_ros2_phase8

install
install_ros2_benchmark
install_ros2_phase8

log
log_ros2_benchmark
log_ros2_phase8
log_ros2_phase8_final
log_ros2_phase8_final_result
log_ros2_phase8_final_test
log_ros2_phase8_test

Also inspect for other equivalent root-level generated directories from earlier phases.

Delete only confirmed generated artifacts.

==================================================
6. Preserve Benchmark Results
==================================================

KEEP:

benchmark/results/phase8_reference/

This contains compact committed benchmark evidence.

KEEP FOR NOW:

benchmark/results/phase8_full_cf35309/

This contains Phase 8 full raw results and must remain available throughout Phase 8.1.

If it is ignored/uncommitted and very large:

leave it local during Phase 8.1.

Do NOT move it into Git.

Do NOT delete it before profiling and report completion.

At the end of Phase 8.1:

if it is no longer needed,
report its size and recommend whether the project owner may delete it.

Do not automatically delete it unless it is definitely reproducible and no longer needed.

==================================================
7. Move Phase Reports Out Of Repository Root
==================================================

Create:

docs/reports/

Move using git mv:

PHASE_0_REPORT.md
PHASE_1_REPORT.md
PHASE_2_REPORT.md
PHASE_3_REPORT.md
PHASE_4_REPORT.md
PHASE_5_REPORT.md
PHASE_6_REPORT.md
PHASE_7_REPORT.md
PHASE_8_REPORT.md

to:

docs/reports/

Do not delete them.

These reports are historical acceptance evidence.

Expected structure:

docs/
└── reports/
    ├── PHASE_0_REPORT.md
    ├── PHASE_1_REPORT.md
    ├── PHASE_2_REPORT.md
    ├── PHASE_3_REPORT.md
    ├── PHASE_4_REPORT.md
    ├── PHASE_5_REPORT.md
    ├── PHASE_6_REPORT.md
    ├── PHASE_7_REPORT.md
    └── PHASE_8_REPORT.md

==================================================
8. Update Report References
==================================================

Search:

git grep -n "PHASE_[0-8]_REPORT"

Update actual links/references that assume the reports live at repository root.

Do NOT rewrite historical Phase task specifications merely because they once instructed:

create PHASE_X_REPORT.md

Those files are historical instructions and may legitimately mention their original creation location.

Only update current navigation/document links where needed.

==================================================
9. CODEX_TASKS Must Stay
==================================================

KEEP:

CODEX_TASKS/PHASE_0.md
...
CODEX_TASKS/PHASE_8.md

Do not move or delete them.

They record what each implementation phase asked Codex to do.

==================================================
10. Temporary Work Directory Convention
==================================================

From now on all temporary work for a phase must live under:

.work/

For this phase use:

.work/phase_8_1/

Suggested:

.work/phase_8_1/
├── build_debug/
├── build_release/
├── build_asan/
├── build_ubsan/
├── build_profile/
├── ros2/
│   ├── build/
│   ├── install/
│   └── log/
├── perf/
├── strace/
└── logs/

Do NOT create:

build_phase8_1
build_final
install_final
log_final
build_profile_final2

in repository root.

==================================================
11. .gitignore Hygiene
==================================================

Update .gitignore to include:

/.work/

and robust root-level generated-directory rules where appropriate.

Prefer safe root-scoped patterns such as:

/build*/
/install*/
/log*/

but inspect existing .gitignore first.

Do not introduce an ignore rule that hides:

source directories
benchmark source
docs
committed phase8_reference results

Make sure:

benchmark/results/phase8_reference/

remains trackable.

==================================================
12. Development Workflow Documentation
==================================================

Update:

docs/DEVELOPMENT_WORKFLOW.md

if it exists.

Document:

All temporary build/install/log output must go under:

.work/phase_X/

Phase completion cleanup:

rm -rf .work/phase_X/

Permanent artifacts:

source
tests
documentation
phase reports
compact benchmark reference artifacts

must remain.

If DEVELOPMENT_WORKFLOW.md does not exist, create a concise:

docs/DEVELOPMENT_WORKFLOW.md

Do not create excessive process documentation.

==================================================
13. Root Directory Hygiene Check
==================================================

After cleanup:

find . -maxdepth 1 -mindepth 1 -printf '%f\n' | sort

Confirm root contains no:

build_*
install_*
log_*

temporary Phase 8 work directories.

The root should be visibly clean.

==================================================
14. Cleanup Must Not Become A Separate Unfinished Phase
==================================================

Do not stop after cleanup unless cleanup reveals a problem.

Part A is preparation for Phase 8.1.

Continue directly to profiling after validating repository integrity.

==================================================
15. Quick Post-Cleanup Regression
==================================================

Before profiling:

configure a clean Release build inside:

.work/phase_8_1/build_release/

and prove the cleanup did not break paths.

Also perform enough normal regression to verify moved reports did not affect build/test infrastructure.

==================================================
PART B — PHASE 8.1 PROFILING
==================================================

==================================================
16. Phase 8 Baseline Evidence
==================================================

Phase 8 measured:

4 transports:

UDS
SHM Copy
SHM Loan
ROS2 Baseline

sizes:

64 B
1 KB
4 KB
64 KB
1 MB
4 MB

topologies:

1->1
1->2
1->4

Phase 8 key reported observations included:

At 64 KiB:
UDS latency/throughput was strong relative to SHM.

At 4 MiB:
SHM Copy and especially SHM Loan had much lower latency than UDS.

SHM Loan showed clearer benefit at:
1 MiB
4 MiB

ROS2 large-message latency was much higher in the measured configuration.

SHM Copy / Loan recorded significant blocked waits in throughput tests.

These are profiling hypotheses only.

Do not treat the explanation as already known.

==================================================
17. Phase 8.1 Goal
==================================================

Use measurement tools to answer:

WHY did Phase 8 produce those results?

Use:

perf
strace

to analyze:

syscalls
CPU hotspots
context switches
allocation
copy activity

Then:

ONLY optimize a bottleneck if profiling evidence clearly identifies one.

No speculative optimization.

==================================================
18. Profiling Tool Preflight
==================================================

Check:

command -v perf || true
perf --version || true

command -v strace || true
strace --version || true

cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || true
cat /proc/sys/kernel/kptr_restrict 2>/dev/null || true

uname -a

Do not:

sudo apt install
sudo sysctl
modify host kernel settings

without explicit user permission.

If perf is unavailable or WSL kernel permissions prevent useful profiling:

record exact limitation.

Still run all profiling tools that are available.

Do not fake perf data.

==================================================
19. Profiling Build
==================================================

Use Release optimization.

Build under:

.work/phase_8_1/build_profile/

Recommended:

CMAKE_BUILD_TYPE=Release

Keep useful symbols / frame pointers if current CMake permits a profiling build without changing optimization semantics.

For example:

-O3
-DNDEBUG
-g
-fno-omit-frame-pointer

Do not switch to Debug.

Do not benchmark ASan/UBSan binaries.

Record actual compiler flags.

==================================================
20. Profiling Must Not Change Baseline First
==================================================

Before modifying performance-critical implementation:

profile current Phase 8 code exactly as-is.

Create:

BASELINE PROFILE

before any optimization.

No code optimization is allowed until baseline profiling report has been produced.

==================================================
21. Representative Profiling Matrix
==================================================

Do not blindly profile all 441 Phase 8 runs.

Use representative cases chosen from Phase 8 evidence.

Mandatory custom-middleware cases:

A. Small-message overhead:
64 B
1->1

B. UDS-leading/crossover region:
64 KiB
1->1

C. Large-message:
1 MiB
1->1

D. Very large:
4 MiB
1->1

E. Fanout:
64 KiB
1->4

F. Large fanout:
4 MiB
1->4

For each relevant case profile:

UDS
SHM Copy
SHM Loan

ROS2 baseline should also receive representative profiling/context where tooling works, but do not modify ROS2 implementation.

At minimum profile ROS2:

64 KiB 1->1
4 MiB 1->1

==================================================
22. Use Existing Benchmark Harness
==================================================

Do not create an unrelated synthetic workload if Phase 8 benchmark runner can drive the same case.

Reuse:

same payload
same topology
same benchmark binaries
same Release behavior
same queue configuration

Profiling should explain the measured benchmark, not another program.

==================================================
23. perf stat
==================================================

For representative processes/cases collect meaningful counters where available.

Examples:

task-clock

cycles

instructions

branches

branch-misses

context-switches

cpu-migrations

page-faults

minor-faults

major-faults

Record:

publisher
subscriber(s)

Registry may be measured separately as auxiliary information.

Do not merge Registry CPU into Publisher CPU.

If WSL/perf does not support a counter:

record unsupported.

==================================================
24. perf record / report
==================================================

Use:

perf record

with call graph support where available.

Then:

perf report --stdio

or equivalent noninteractive report.

Identify top hotspots.

For custom Middleware specifically look for evidence involving:

publish path
UDS send/write
UDS receive/read
memcpy / memmove
payload fill
MemoryPool allocation
Chunk setup
queue push/pop
pthread mutex/condvar
futex
notification send/receive
release/refcount path

Do not assume these are bottlenecks before observing data.

==================================================
25. strace Syscall Profiling
==================================================

Use:

strace -c
or
strace -f -c

on representative custom cases.

Record at least:

syscall counts

time spent by syscall

dominant calls

Especially inspect actual evidence for:

send/write/sendmsg

recv/read/recvmsg

poll/epoll

futex

mmap/munmap

shm_open-related openat

close

clock_gettime

Do not assume which dominates.

==================================================
26. Verify Phase 4 / Phase 5 Design Claims
==================================================

Profiling should verify important architecture expectations.

SHM hot path should NOT perform per-message:

shm_open
ftruncate
mmap
munmap
shm_unlink

If strace shows that it does:

this is a correctness/design regression and must be investigated.

SHM Loan path should not show an application-buffer-to-SHM full payload copy.

If hotspot evidence shows unexpected memcpy proportional to payload size in the middleware loan path:

investigate.

==================================================
27. Copy Analysis
==================================================

For:

UDS
SHM Copy
SHM Loan

build an evidence-backed copy-path table.

For each transport document:

application payload generation

application -> middleware copy

middleware -> kernel copy / syscall boundary where applicable

SHM copy into Chunk

subscriber owning copy if any

SampleView direct read

Do not claim kernel-internal copies based only on guesswork.

Clearly distinguish:

source-level known copies

profiling-observed copy hotspots

kernel implementation assumptions

==================================================
28. Allocation Analysis
==================================================

Inspect profiling output for:

malloc
free
operator new
operator delete

or allocator functions appearing as meaningful hotspots.

Also inspect existing architecture for allocations on measured hot path.

If no meaningful allocation hotspot is observed:

say so.

Do not introduce a custom allocator because “allocations might be expensive.”

==================================================
29. Context Switch Analysis
==================================================

Compare context-switch behavior between:

UDS
SHM Copy
SHM Loan

especially:

64 B 1->1
64 KiB 1->1
4 MiB 1->1
1->4 cases

Relate observed context-switch / futex behavior to actual benchmark data carefully.

Do not claim causation without supporting evidence.

==================================================
30. Investigate SHM Blocking
==================================================

Phase 8 recorded substantial:

blocked_count
blocked_time_ns

for SHM Copy / Loan throughput tests.

Profile representative throughput cases.

Determine whether waits are associated with:

subscriber queue capacity
BLOCK_WITH_TIMEOUT behavior
futex/condition variable waits
pool pressure
subscriber consumption rate
other observed mechanism

Do not change Backpressure semantics just to make throughput numbers larger.

==================================================
31. Investigate Small-Message Behavior
==================================================

Phase 8 observed that UDS remained strong through small/medium sizes.

Profile:

64 B
4 KB
64 KB

to determine evidence for fixed SHM costs such as:

queue synchronization
handle notification
registry/data bookkeeping
pool operations
futex
syscalls

Only report observed contributors.

==================================================
32. Investigate Large-Message Behavior
==================================================

Profile:

1 MiB
4 MiB

Compare:

UDS
SHM Copy
SHM Loan

Focus on:

CPU cycles
copy hotspots
syscall time
context switches
blocking

Determine why SHM/Loan scaling differs from UDS.

Use evidence.

==================================================
33. ROS2 Profiling Boundary
==================================================

ROS2 is an external baseline.

You may profile:

ROS2 Publisher
ROS2 Subscriber

using perf/strace if possible.

But do NOT:

modify rmw_fastrtps_cpp
modify Fast DDS
change ROS2 internals
enable special undocumented tuning

Phase 8.1 optimization scope is the custom Middleware only.

==================================================
34. Profiling Artifact Layout
==================================================

Permanent compact profiling evidence should go under:

benchmark/profiling/

Suggested:

benchmark/profiling/
├── README.md
├── phase8_1_summary.json
├── hotspot_summary.csv
├── syscall_summary.csv
├── selected_perf_reports/
└── selected_strace_summaries/

Do NOT commit:

huge perf.data files
hundreds of MB of raw traces

Raw profiling files live under:

.work/phase_8_1/perf/
.work/phase_8_1/strace/

and are deleted at final cleanup unless specifically needed.

==================================================
35. Create PHASE_8_1_REPORT
==================================================

Create:

docs/reports/PHASE_8_1_REPORT.md

NOT repository root.

This establishes the new report-location convention.

==================================================
36. Baseline Profiling Report Before Optimization
==================================================

Before changing any measured core code, record:

Top CPU hotspots by case

Top syscalls by case

context-switch counts

page faults

copy-related hotspots

allocation-related hotspots

blocking/futex evidence

Most important evidence-backed bottleneck(s)

Confidence level:

HIGH
MEDIUM
LOW

Only HIGH-confidence or strong MEDIUM-confidence problems may justify optimization in this Phase.

==================================================
37. Optimization Decision Gate
==================================================

After profiling, explicitly output:

Optimization Decision:

A. OPTIMIZATION JUSTIFIED

or

B. NO OPTIMIZATION JUSTIFIED

If B:

do not modify performance-critical code.

Phase 8.1 may still PASS.

A profiling phase is successful if it establishes trustworthy evidence even when no optimization is justified.

==================================================
38. Allowed Optimization Scope
==================================================

If optimization IS justified:

implement only:

1 or at most 2

small, evidence-backed changes.

Requirements:

local
explainable
low-risk
measurable

Examples are NOT instructions, only categories:

remove redundant copy

avoid unnecessary allocation

remove avoidable syscall

reduce proven excessive synchronization

avoid duplicate metadata work

Do not implement something merely because it appears in this list.

==================================================
39. Forbidden Speculative Optimizations
==================================================

Do NOT add unless profiling specifically proves it is the needed fix and project owner scope clearly allows it:

lock-free queue

lock-free allocator

Treiber stack

per-thread cache

eventfd conversion

SCM_RIGHTS

memfd_create migration

CPU affinity

real-time scheduler policy

busy spinning

custom memcpy

SIMD payload copy

huge pages

scheduler manipulation

These are NOT resume decorations.

==================================================
40. Special Rule For Advanced Features
==================================================

PROJECT_PLAN lists items like:

eventfd
per-thread memory cache
CPU affinity
scheduler experiment

as advanced/optional areas.

Do not automatically implement them in Phase 8.1.

If profiling suggests one might help:

record it as:

Future Optimization Candidate

unless the evidence is exceptionally clear and the change is small enough to verify safely.

==================================================
41. Optimization Correctness Gate
==================================================

If any optimization is implemented:

run all existing correctness regression.

At minimum:

Core CTest

ASan

UBSan

ROS2 Adapter regression

Phase 8 benchmark smoke

No existing semantics may change.

Especially preserve:

backpressure behavior
crash recovery
refcount lifecycle
LoanedSample
SampleView
ROS2 adapter

==================================================
42. Before / After Benchmark
==================================================

For every optimization:

run the exact same representative benchmark cases:

BEFORE

and

AFTER

using:

same machine
same Release flags
same payload
same topology
same duration
same repetitions
same configuration

Do not compare:

old Debug result

to:

new Release result.

==================================================
43. Minimum Before/After Cases
==================================================

If optimizing a custom transport, verify at least:

64 B 1->1

64 KiB 1->1

1 MiB 1->1

4 MiB 1->1

64 KiB 1->4

4 MiB 1->4

for affected transport(s).

Use >= 3 repetitions.

Report:

p50
p99
throughput
Publisher CPU
Subscriber CPU
context switches where available

==================================================
44. Optimization Acceptance
==================================================

An optimization may remain only if:

correctness remains PASS

and

target metric improves measurably/repeatably

and

there is no serious regression elsewhere.

Do not keep a change that:

improves one cherry-picked case by 2%
but harms other relevant cases significantly

unless there is a clearly documented tradeoff and project owner accepts it.

Default:

revert ineffective optimization cleanly.

Do NOT use reset --hard.

Use normal source edits/revert of your own uncommitted change.

==================================================
45. Full Matrix Rerun Rule
==================================================

If NO performance-critical implementation changes are kept:

do NOT rerun the entire 441-run Phase 8 matrix unnecessarily.

Use existing Phase 8 results as baseline.

If a performance-critical core optimization IS kept:

rerun the complete mandatory Phase 8 main matrix so final benchmark data reflects the new implementation.

That means:

UDS
SHM Copy
SHM Loan
ROS2

64 B
1 KB
4 KB
64 KB
1 MB
4 MB

1->1
1->2
1->4

latency
throughput

required repetitions

plus the Phase 8 backpressure experiment.

ROS2 does not need to change, but rerunning in the same session preserves fairness.

Generate a new run ID.

Do NOT overwrite original Phase 8 results.

==================================================
46. Preserve Original Phase 8 Results
==================================================

Never overwrite:

benchmark/results/phase8_reference/

Original Phase 8 reference remains historical evidence.

If optimized full matrix is produced, use a new location such as:

benchmark/results/phase8_1_reference/

Commit only compact sanitized aggregate artifacts.

Do not commit huge raw CSV collections.

==================================================
47. Profiling Results Interpretation
==================================================

PHASE_8_1_REPORT must answer:

Why was UDS competitive for small messages?

What explains SHM crossover behavior?

What explains the SHM Copy vs Loan difference at large sizes?

Where does CPU time go in each custom transport?

Which syscalls dominate?

How significant are futex/context-switch costs?

Is allocation a real hotspot?

Are payload copies actually dominant where expected?

What explains the measured blocking in SHM throughput?

What evidence exists for the selected optimization?

If no optimization was performed:

why not?

==================================================
48. Do Not Overclaim
==================================================

Do not write:

“We proved X is the bottleneck”

unless profiling actually supports it.

Prefer:

“perf stat/report and strace show...”

“this suggests...”

“the dominant observed cost was...”

Distinguish measurement from inference.

==================================================
49. README Update
==================================================

Update README only enough to add:

Phase 8.1 Profiling status

links to:

docs/reports/PHASE_8_1_REPORT.md
benchmark/profiling/

If an optimization is actually retained:

mention it precisely.

Do not perform Phase 9 final README rewrite.

==================================================
50. docs/BENCHMARK.md
==================================================

Add a small Profiling section explaining:

profiling tools
representative cases
how to reproduce
relationship to Phase 8 measurements
where profiling results live

Do not turn it into Phase 9 final performance narrative yet.

==================================================
51. Phase 8.1 Report Required Sections
==================================================

docs/reports/PHASE_8_1_REPORT.md

must include:

Scope

Repository Hygiene

Root Before / After

Reports Moved

Temporary Directories Removed

New .work Convention

Git Revision Profiled

Build Configuration

Machine Information

perf Availability

strace Availability

perf Restrictions

Profiling Methodology

Representative Case Matrix

Baseline Benchmark References

CPU Hotspots

Syscall Analysis

Context Switch Analysis

Allocation Analysis

Copy Analysis

Blocking / Futex Analysis

UDS Findings

SHM Copy Findings

SHM Loan Findings

ROS2 Context

Small Message Findings

Large Message Findings

1->4 Fanout Findings

Identified Bottlenecks

Optimization Decision

Optimization Implemented
or
No Optimization Justified

Before / After Results if applicable

Correctness Regression

ASan

UBSan

ROS2 Adapter Regression

Phase 8 Benchmark Smoke

Full Matrix Rerun if applicable

Known Limitations

Future Optimization Candidates

Temporary Artifact Cleanup

Phase Boundary

Final line:

Phase 9 was not implemented.

==================================================
52. New CODEX Task
==================================================

Save this task as:

CODEX_TASKS/PHASE_8_1.md

Do not overwrite Phase 0-8 task files.

==================================================
53. Final Cleanup — Mandatory
==================================================

Before commit:

all Phase 8.1 temporary build/install/log/perf raw directories must be under:

.work/phase_8_1/

Once all results and compact permanent evidence are safely written:

remove:

.work/phase_8_1/

Do not leave it behind.

Exception:

if a raw perf artifact is essential for unresolved diagnosis,
report it and leave only if strictly necessary.

Default is delete.

==================================================
54. Final Root Cleanliness
==================================================

Before commit run:

find . -maxdepth 1 -mindepth 1 -printf '%f\n' | sort

Root must NOT contain:

build_*
build-*
install_*
install-*
log_*
log-*

.work should either:

not exist

or contain no Phase 8.1 work.

Root PHASE_*_REPORT.md files should no longer exist because reports were moved to:

docs/reports/

==================================================
55. Git Review
==================================================

Run:

git status
git status --short
git diff --check
git diff --stat
git diff

Check:

no source accidentally deleted

no tests removed

no benchmark reference deleted

no giant raw perf files

no giant Phase 8 raw result files newly added

no Phase 9 work

==================================================
56. Commit Strategy
==================================================

Prefer two clean commits.

Commit 1:

chore: organize repository artifacts and work directories

Contains:

report moves
.gitignore
workflow documentation
root cleanup-related tracked changes

Do NOT commit generated deletion noise if those files were never tracked.

Commit 2:

perf: profile phase 8 benchmark bottlenecks

Contains:

profiling scripts/config if added
compact profiling results
PHASE_8_1_REPORT
evidence-backed optimization if justified
related tests/docs

If profiling requires a separate optimization commit, at most three commits is acceptable:

chore: organize repository artifacts and work directories
perf: add phase 8 profiling evidence
perf: optimize <actual measured bottleneck>

Only if the third commit is truly justified.

==================================================
57. Push
==================================================

After all commits:

git push origin main

If non-interactive GitHub authentication fails:

DO NOT:

recommit
amend
rebase
reset
force push

Keep commits intact.

Report:

GitHub Push:
MANUAL ACTION REQUIRED

Commits:
...

Manual command:

git push origin main

==================================================
58. Final Response Format
==================================================

Return only actual results:

Repository Hygiene:
PASS / FAIL

Root Temporary Directories Removed:
...

Reports Moved To:
docs/reports/

Phase Reports Preserved:
YES / NO

Phase 8 Reference Results Preserved:
YES / NO

Phase 8 Full Raw Results Preserved:
YES / NO

New Temporary Work Convention:
.work/phase_X/

Root Clean:
YES / NO

Phase 8.1 Implementation:
PASS / FAIL

Branch:
main

Commit(s):
<SHA + message>

GitHub Push:
PASS / MANUAL ACTION REQUIRED / FAIL

Profiled Git Revision:
...

Release/Profile Build:
PASS / FAIL

Core Regression:
PASS / FAIL
X/X

ASan:
PASS / FAIL

UBSan:
PASS / FAIL

ROS2 Adapter Regression:
PASS / FAIL

perf:
AVAILABLE / LIMITED / UNAVAILABLE

strace:
AVAILABLE / UNAVAILABLE

Representative Profiling Cases:
PASS / FAIL

UDS Hotspot:
<measured finding>

SHM Copy Hotspot:
<measured finding>

SHM Loan Hotspot:
<measured finding>

ROS2 Profiling Context:
<measured finding or limitation>

Dominant Syscalls:
...

Context Switch Finding:
...

Allocation Finding:
...

Copy Finding:
...

Blocking/Futex Finding:
...

Optimization Decision:
OPTIMIZATION JUSTIFIED / NO OPTIMIZATION JUSTIFIED

Optimization Implemented:
<description / NONE>

Before/After Validation:
PASS / NOT APPLICABLE / FAIL

Measured Improvement:
<actual data / NONE>

Full Phase 8 Matrix Rerun:
PASS / NOT REQUIRED / FAIL

Original Phase 8 Reference Preserved:
YES / NO

New Optimized Reference:
<path / NONE>

Temporary .work Cleanup:
PASS / FAIL

Files Added:
...

Files Moved:
...

Files Modified:
...

Known Limitations:
...

Future Optimization Candidates:
...

Phase Boundary:
Phase 9 was not implemented.

Next Action:
Wait for Phase 9 task from project owner.

==================================================
59. Engineering Priority
==================================================

Priority:

Repository Hygiene
    ↓
Measurement Integrity
    ↓
Profiling Evidence
    ↓
Root-Cause Understanding
    ↓
Correctness
    ↓
Repeatable Before/After Validation
    ↓
Only then optimization

Forbidden mindset:

“SHM is slow, add lock-free”
“eventfd sounds faster”
“CPU affinity looks good on resume”
“ROS2 beat us, tune until we win”

The goal is not to manufacture a benchmark victory.

The goal is:

measure
understand
prove
then improve only when justified.

Complete repository cleanup and Phase 8.1.

STOP.

DO NOT START PHASE 9.