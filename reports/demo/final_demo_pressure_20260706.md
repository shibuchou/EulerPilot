# EulerPilot final demo pressure check

Date: `2026-07-06`

Host: `192.168.1.121 / openEuler 24.03 LTS SP3`

Purpose: freeze the final demonstration path after SP4 evidence was merged, without changing the 32-entry final evidence compact scope.

## Result

Overall result: `pass`

Checked paths:

- `demo/demo_all_final.sh --mode offline`: pass
- `demo/demo_all_final.sh --cleanup`: pass
- `demo/demo_all_final.sh --mode live`: pass
- Web Console `/api/health`: pass
- Web Console `/api/evidence/summary`: pass, total `32`, required missing `0`, warnings `0`
- Web Console `demo_offline` action through REST API: pass
- Web Console non-whitelist action rejection: pass, HTTP `404`
- Post-cleanup lab cgroup/veth residue check: pass

## Demo logs

- `reports/demo/demo_all_final_offline_20260706-145521.log`
- `reports/demo/demo_cleanup_20260706-145521.log`
- `reports/demo/demo_all_final_live_20260706-145544.log`

## Live result

Live run generated:

- `results/policy_engine/security-network-resource-20260706-145547`

Summary:

```text
result=pass
repeat=1
success_cases=1
failure_rollback=pass
result_dir=results/policy_engine/security-network-resource-20260706-145547
```

The live chain covered:

```text
security_policy burst_connect
  -> policy_engine
  -> resource_control + network_qos
  -> failure rollback case
  -> cleanup
```

## Web Console checks

The console was started on 121 with:

```bash
web_console/scripts/run_console.sh --daemon
```

Observed API results:

- `GET /api/health`: `ok=true`
- `GET /api/evidence/summary`: `total=32`, `required_missing=0`, `warnings=0`
- `POST /api/actions/demo_offline/start`: job succeeded, exit code `0`
- `GET /api/jobs`: restored the recent `demo_offline` job
- `POST /api/actions/not_in_whitelist/start`: rejected with HTTP `404`

Cleanup check:

```text
lab_cgroup_clean=1
lab_veth_clean=1
```

## Notes

This report is a live demo pressure record. It is intentionally not added to `configs/final_evidence_manifest.json`; the final compact evidence remains the frozen 32-entry submission evidence set.
