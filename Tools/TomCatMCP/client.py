"""Dependency-free client for the Editor's loopback API; also usable by Skills."""
from __future__ import annotations

import argparse
from collections import OrderedDict
import json
import os
from pathlib import Path
import threading
import urllib.error
import urllib.request
import uuid

from tools import BY_NAME, READ_ONLY


class AutomationError(Exception):
    def __init__(self, code, message, request_id=None):
        super().__init__(message)
        self.code, self.request_id = code, request_id

    def result(self):
        error = {"code": self.code, "message": str(self)}
        if self.request_id:
            error["request_id"] = self.request_id
        return {"ok": False, "error": error}


class Client:
    def __init__(self, project=None, port=None, token=None, timeout=70):
        self.project = str(Path(project or os.environ["TOMCAT_PROJECT"]).resolve())
        self.port = int(port or os.environ.get("TOMCAT_AUTOMATION_PORT", "8091"))
        self.token = token or os.environ["TOMCAT_AUTOMATION_TOKEN"]
        if not 1 <= self.port <= 65535 or len(self.token) < 16 or "\r" in self.token or "\n" in self.token:
            raise ValueError("Invalid automation port or token (minimum 16 characters).")
        self.url = f"http://127.0.0.1:{self.port}"
        self.timeout = timeout
        self.session = None
        self.requests = OrderedDict()
        self.lock = threading.RLock()
        # Never send bearer tokens via an environment-configured HTTP proxy.
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def _send(self, path, payload=None):
        try:
            data = None if payload is None else json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
        except (TypeError, ValueError) as exc:
            raise AutomationError("INVALID_ARGUMENT", "Arguments must be finite, JSON-compatible values.") from exc
        request = urllib.request.Request(self.url + path, data=data, headers={
            "Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
        try:
            with self.http.open(request, timeout=self.timeout) as response:
                body = response.read(16 * 1024 * 1024 + 1)
                if len(body) > 16 * 1024 * 1024:
                    raise AutomationError("RESPONSE_TOO_LARGE", "Use pagination or narrower queries.")
                result = json.loads(body)
                if not isinstance(result, dict) or type(result.get("ok")) is not bool:
                    raise AutomationError("OUTCOME_UNKNOWN", "Editor returned an invalid response envelope; inspect state before retrying writes.")
                return result
        except urllib.error.HTTPError as exc:
            code = "OUTCOME_UNKNOWN" if exc.code >= 500 else "HTTP_REJECTED"
            raise AutomationError(code, f"Editor HTTP {exc.code}; check connection settings and editor state.") from exc
        except (urllib.error.URLError, TimeoutError, OSError, ValueError) as exc:
            raise AutomationError("OUTCOME_UNKNOWN", "Connection failed or response was incomplete. Do not repeat a write with a new request ID.") from exc

    def status(self):
        result = self._send("/health")
        if not result.get("ok"):
            raise AutomationError("HEALTH_FAILED", str(result.get("error")))
        data = result["data"]
        if os.path.normcase(os.path.realpath(data["project"])) != os.path.normcase(os.path.realpath(self.project)):
            raise AutomationError("PROJECT_MISMATCH", "The connected Editor has a different project open.")
        if data["protocol_version"] != 1:
            raise AutomationError("VERSION_MISMATCH", "Unsupported automation protocol.")
        if self.session is not None and self.session != data["session_id"]:
            raise AutomationError("SESSION_MISMATCH", "Editor restarted. Reconnect the client; old requests must not be replayed.")
        self.session = data["session_id"]
        return result

    def call(self, name, arguments=None):
        with self.lock:
            try:
                if name not in BY_NAME:
                    raise AutomationError("UNKNOWN_TOOL", name)
                if arguments is not None and not isinstance(arguments, dict):
                    raise AutomationError("INVALID_ARGUMENT", "Tool arguments must be an object.")
                arguments = dict(arguments or {})
                # Validate key shape here; native code remains authoritative for values.
                schema = BY_NAME[name]["inputSchema"]
                if set(arguments) - set(schema["properties"]) or set(schema["required"]) - set(arguments):
                    raise AutomationError("INVALID_ARGUMENT", "Missing or unknown tool arguments.")
                retry_id = arguments.pop("request_id", None)
                expected_version = arguments.pop("scene_version", None)
                if retry_id is not None and (not isinstance(retry_id, str) or not retry_id):
                    raise AutomationError("INVALID_ARGUMENT", "request_id must be a nonempty string.")
                if expected_version is not None and not isinstance(expected_version, str):
                    raise AutomationError("INVALID_ARGUMENT", "scene_version must be a string.")
                if name == "editor_get_status":
                    return self.status()
                if retry_id:
                    if retry_id not in self.requests:
                        raise AutomationError("REQUEST_EXPIRED", "Request is absent from this client's last 256 writes. Inspect state; do not blindly replay.")
                    payload = self.requests[retry_id]
                    if payload["tool"] != name or payload["arguments"] != arguments:
                        raise AutomationError("REQUEST_ID_REUSED", "A retry must use the original tool and arguments.")
                    self.status()  # verifies the original editor session still exists
                else:
                    status = self.status()["data"]
                    payload = {"tool": name, "arguments": arguments, "project": self.project}
                    if name not in READ_ONLY:
                        payload.update(session_id=self.session, scene_version=expected_version or status["scene_version"], request_id=uuid.uuid4().hex)
                        self.requests[payload["request_id"]] = payload
                        if len(self.requests) > 256:
                            self.requests.popitem(last=False)
                try:
                    result = self._send("/call", payload)
                except AutomationError as exc:
                    exc.request_id = payload.get("request_id")
                    raise
                if "request_id" in payload:
                    result["request_id"] = payload["request_id"]
                return result
            except AutomationError as exc:
                return exc.result()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tool", choices=[*BY_NAME, "list"])
    parser.add_argument("arguments", nargs="?", default="{}", help="JSON arguments")
    parser.add_argument("--project")
    args = parser.parse_args()
    if args.tool == "list":
        from tools import TOOLS
        result = TOOLS
    else:
        result = Client(project=args.project).call(args.tool, json.loads(args.arguments))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not isinstance(result, dict) or result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
