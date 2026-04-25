"""Arbor Live Coding helpers.

Triggers and observes Live Coding compiles from Python. Wraps Arbor's C++
``UArborCompileTools`` UFUNCTIONs which in turn call ``ILiveCodingModule``.

Closes the gap where callers used to file-poll generated headers / grep
``LogLiveCoding`` to know when a compile finished — see issue #9.

Typical synchronous use::

    import arbor.compile as compile

    result = compile.compile_and_wait()
    if not result["success"]:
        raise RuntimeError(f"Compile failed: {result['message']}")
    # New code is now patched into the running editor.

Typical asynchronous use (e.g. when the bridge wants to poll between calls)::

    compile.start()                     # returns immediately
    while compile.is_compiling():
        time.sleep(0.5)
    last = compile.last_result()
    if not last["has_result"]:
        # Nothing happened — Live Coding probably wasn't enabled.

Caveat: ``ILiveCodingModule::OnPatchComplete`` fires on both success and
failure paths the same way and doesn't carry the result code. Use
``compile_and_wait`` (which captures the result) when you need to discriminate
success vs failure programmatically. The async path can detect "did anything
happen" but not "did it succeed".
"""

import json

import unreal


def _check_module_loaded():
    if not hasattr(unreal, "ArborCompileTools"):
        raise RuntimeError(
            "unreal.ArborCompileTools is missing — rebuild the Arbor plugin and "
            "restart the editor (Live Coding cannot register new UFUNCTIONs)."
        )


def is_compiling():
    """Return True iff a Live Coding compile is currently running."""
    _check_module_loaded()
    return bool(unreal.ArborCompileTools.is_live_coding_compiling())


def compile_and_wait():
    """Trigger a Live Coding compile and block until it completes.

    Returns:
        Dict: ``{success, message, result_code, duration_sec, started}``.
        ``result_code`` is one of ``Success`` / ``NoChanges`` / ``Failure`` /
        ``Cancelled`` / ``NotStarted`` / ``CompileStillActive`` / ``InProgress``.

    Note: blocks the editor's main thread for the duration of the compile.
    For UI-friendly behaviour (still responsive), use ``start()`` + poll
    ``is_compiling()`` from a non-blocking caller (e.g. the bridge's TS side).
    """
    _check_module_loaded()
    raw = unreal.ArborCompileTools.compile_and_wait()
    return json.loads(raw)


def start():
    """Trigger a Live Coding compile asynchronously. Returns immediately.

    Returns:
        Dict: ``{success, message}``. ``success=False`` only if the compile
        couldn't be started (LiveCoding disabled, already compiling, etc.).
        Use ``is_compiling()`` and ``last_result()`` to track progress.
    """
    _check_module_loaded()
    raw = unreal.ArborCompileTools.start_live_coding_compile()
    return json.loads(raw)


def last_result():
    """Return the most recent ``OnPatchComplete`` event captured.

    Returns:
        Dict: ``{success, message, has_result, completed_at_utc, time_since_seconds}``.
        ``has_result=False`` means no patch has landed since the helper attached
        (e.g. fresh editor session, no compiles run yet).
    """
    _check_module_loaded()
    raw = unreal.ArborCompileTools.get_last_compile_result()
    return json.loads(raw)
