# Development Workflow

## 1. Create the GitHub repository

Create an empty repository named:

`cpp_robot_middleware`

Recommended: Public.

Do not initialize it with README, `.gitignore`, or license because this
bootstrap package provides the initial repository context.

## 2. Clone in WSL

Use your preferred GitHub authentication method.

Example with SSH:

```bash
cd ~/code
git clone git@github.com:<YOUR_GITHUB_USER>/cpp_robot_middleware.git
cd cpp_robot_middleware
```

## 3. Copy the bootstrap package contents into the repository root

The repository root should contain at least:

```text
PROJECT_PLAN.md
AGENTS.md
GITHUB_REPO_INFO.md
START_HERE.md
CODEX_TASKS/PHASE_0.md
docs/DEVELOPMENT_WORKFLOW.md
.gitignore
```

## 4. Create the bootstrap commit

```bash
git add PROJECT_PLAN.md AGENTS.md GITHUB_REPO_INFO.md START_HERE.md   CODEX_TASKS docs .gitignore
git commit -m "chore: add project plan and Codex bootstrap"
git push -u origin main
```

## 5. Create the Phase 0 branch

```bash
git switch -c feat/phase-0-bootstrap
```

## 6. Open from WSL in VS Code

From the repository root:

```bash
code .
```

Then start Codex from your VS Code / terminal workflow inside this repository.

## 7. Give Codex the Phase 0 task

Use the full contents of:

`CODEX_TASKS/PHASE_0.md`

Because `AGENTS.md` is repository-wide guidance, keep it short and stable;
phase-specific requirements belong in `CODEX_TASKS/`.

## 8. Human acceptance after Codex finishes

At minimum run the exact commands documented by Codex and confirm:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Also verify the install/export and external-consumer commands from the Phase 0
report.

## 9. Commit Phase 0 only after acceptance

Suggested commits (split if the change set naturally supports it):

```bash
git add .
git commit -m "build: scaffold C++17 middleware project"
```

If tests/docs are substantial, split them:

```text
build: scaffold C++17 middleware project
test: add phase 0 packaging smoke tests
docs: add phase 0 report
```

Push:

```bash
git push -u origin feat/phase-0-bootstrap
```

After review/merge into `main`, tag the accepted state:

```bash
git switch main
git pull --ff-only
git tag phase-0
git push origin phase-0
```

Then request the Phase 1 Codex task. Do not let Codex continue directly into
Phase 1 from the Phase 0 prompt.
