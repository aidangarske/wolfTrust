# wolfTrust CI

Three tiers, modeled on wolfProvider's CI: a fast per-PR lane, a heavy
nightly M33MU lane, and label-selected opt-in for pulling M33MU coverage
onto a PR before merge.

## At a glance

| Tier | Trigger | Purpose |
|------|---------|---------|
| **Fast (per-PR)** | every PR; push to master/main/dev/churn | host unit tests, cross-compile, compiler matrix, sanitizers, valgrind, integrations, core/port split guard |
| **Nightly M33MU** | `cron: 0 8 * * *`, or `workflow_dispatch` | full M33MU emulator matrix — 11 concurrent jobs (see below) |
| **PR opt-in** | add a `ci:*` label to a PR | run one M33MU scenario, or the whole matrix, on the PR branch |

The heavy M33MU workflow (`stm32h563-build.yml`) does **not** run on
`pull_request` — PRs stay fast. It runs on push to `master`/`main`/
`wolfTrust-dev`, on the nightly schedule (via `nightly.yml`), and by
label opt-in.

## Nightly M33MU matrix (11 concurrent jobs)

`nightly.yml` calls `stm32h563-build.yml`, which fans out to:

- `wolfboot-m33mu` — upstream wolfBoot signed boot + update/rollback lifecycle
- `wolfboot-wolftrust-m33mu` × `{zephyr, freertos}` — full FF-M lifecycle
  (IPC dispatch, ITS/PS, key-ops, key negatives, attestation COSE_Sign1,
  forged-handle + oversized-vector rejects, SECURED-policy reject)
- `wolfboot-wolftrust-m33mu-scenarios` × 8 scenarios:
  `positive` `restart` `crossdomain` `confboot` `devstorage` `devcrypto`
  `vaultrecover` `vaultrecoversec`

`nightly.yml` also runs the fast lane + `core-port-split`.

## Running M33MU on a PR (label opt-in)

`pr-m33mu-select.yml` pulls heavy M33MU onto a PR without editing code:

| Label | Effect |
|-------|--------|
| `ci:<scenario>` | run that one scenario on the PR branch (e.g. `ci:devcrypto`, `ci:vaultrecoversec`). Add several to run several. |
| `ci:m33mu` / `ci:all` | run the full heavy workflow (both lifecycles + all 8 scenarios) on the PR branch. |
| (no label) | nothing runs — a normal PR is unaffected. |

`<scenario>` is one of the 8 scenario names above. The dispatcher fires
only on label change; re-add a label to re-run after a push, then drop
the labels when done — nothing to revert in the tree.

Off-PR equivalent (runs against a branch, no labels):

```bash
gh workflow run pr-m33mu-select.yml --ref <branch> -f jobs="positive devcrypto"
gh workflow run pr-m33mu-select.yml --ref <branch> -f jobs="all"
```

The local box gate `run_m33mu.sh` (a Zephyr+FreeRTOS lifecycle) and the
`make test-target` loop remain the pre-push mirror of the M33MU jobs.
