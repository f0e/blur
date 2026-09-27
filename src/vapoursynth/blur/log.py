import sys

TRACE_ENABLED = False


def info(*args):
    print("[blur]", *args, file=sys.stderr, flush=True)


def status(key, value=""):  # for blur to read
    print(f"[blur:status] {key}={value}", file=sys.stderr, flush=True)


def trace(*args):
    if TRACE_ENABLED:
        info("trace:", *args)
