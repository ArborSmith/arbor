"""Arbor automation test runner.

Drives `FAutomationTestFramework` directly so test runs from the bridge skip
the controller-layer pain points (single-queue serialisation, FPS gate,
log-only results). Wraps the C++ ``UArborAutomationTools`` UFUNCTIONs.

Closes issue #11.

Typical use::

    import arbor.automation as automation

    # Discover what's there:
    print(automation.list_tests("MyGame.Combat"))

    # Run a filtered suite, get structured results:
    result = automation.run_tests("MyGame.Combat")
    if not result["success"]:
        for t in result["tests"]:
            if not t["success"]:
                print(f"FAIL  {t['path']}  ({t['error_count']} errors)")
                for e in t["errors"]:
                    print(f"  - {e['type']}: {e['message']}")

Each test runs synchronously on the editor's main thread — fine for unit
tests and lightweight integration tests. PIE-style multi-frame tests with
heavy latent commands work but the editor will appear frozen for the
duration of the run; a future async variant will fix that.
"""

import json

import unreal


def _check_module_loaded():
    if not hasattr(unreal, "ArborAutomationTools"):
        raise RuntimeError(
            "unreal.ArborAutomationTools is missing — rebuild the Arbor plugin and "
            "restart the editor (Live Coding cannot register new UFUNCTIONs)."
        )


def list_tests(filter=""):
    """List every registered test path matching the filter.

    Args:
        filter: Empty = all tests. Otherwise only tests whose full path matches
            the filter exactly OR starts with ``filter + "."`` — same semantics
            as the engine's ``Automation RunTests`` filter, minus the
            substring fallback (which is rarely what you want).

    Returns:
        List[str] of full test paths, sorted in registration order.
    """
    _check_module_loaded()
    raw = unreal.ArborAutomationTools.list_tests(filter)
    result = json.loads(raw)
    return result.get("tests", [])


def run_tests(filter, timeout=300.0, max_errors_per_test=5):
    """Run every test matching the filter and block until done.

    Args:
        filter: Required. Tests whose full path matches exactly or starts with
            ``filter + "."`` will run. Use ``""`` only when you really want to
            run everything (slow).
        timeout: Per-test latent-command timeout in seconds. ``-1`` disables.
        max_errors_per_test: Caps how many error/warning messages are returned
            per test in the result payload (full counts are still reported via
            ``error_count`` / ``warning_count``). ``0`` = no cap.

    Returns:
        Dict::

            {
              "success": bool,           # True iff every test passed
              "summary": {
                "total":         int,
                "passed":        int,
                "failed":        int,
                "duration_sec":  float,
                "filter":        str,
              },
              "tests": [
                {
                  "path":           str,
                  "success":        bool,
                  "duration_sec":   float,
                  "error_count":    int,
                  "warning_count":  int,
                  "timeout":        bool,        # only present if true
                  "errors": [
                    {"type": "error"|"warning", "message": str,
                     "file": str?, "line": int?},
                    ...
                  ],
                },
                ...
              ],
            }
    """
    _check_module_loaded()
    raw = unreal.ArborAutomationTools.run_tests_and_wait(
        filter, float(timeout), int(max_errors_per_test)
    )
    return json.loads(raw)


def assert_run(filter, timeout=300.0):
    """Convenience: run filtered tests and raise AssertionError on any failure.

    Useful for "compile + assert tests pass" pipelines where you don't want
    to inspect the result dict yourself.
    """
    result = run_tests(filter, timeout=timeout)
    if result["success"]:
        return result
    failed = [t for t in result["tests"] if not t["success"]]
    summary = result["summary"]
    lines = [f"{summary['failed']}/{summary['total']} test(s) failed:"]
    for t in failed:
        lines.append(f"  - {t['path']} ({t['error_count']} errors)")
        for err in t["errors"][:3]:
            lines.append(f"      {err['type']}: {err['message']}")
    raise AssertionError("\n".join(lines))
