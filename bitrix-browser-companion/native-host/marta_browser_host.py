#!/usr/bin/env python3
"""Native Messaging companion for the local Marta browser bridge.

stdout is reserved for Native Messaging frames; diagnostics go to stderr.
"""
import base64
import errno
import json
import os
import pathlib
import signal
import stat
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import threading

MAX_NATIVE_MESSAGE_BYTES = 1024 * 1024
MAX_NATIVE_INBOUND_MESSAGE_BYTES = 8 * 1024 * 1024
MAX_OUTPUT_BYTES = 700 * 1024


class ProtocolError(Exception):
    pass


class CommandError(Exception):
    def __init__(self, code, message, detail=None):
        super().__init__(message)
        self.code, self.message, self.detail = code, message, detail


def choose_workspace():
    """Return a directory selected through the local operating-system dialog."""
    if sys.platform != "darwin":
        raise CommandError("UNSUPPORTED_PLATFORM", "workspace selection is supported only on macOS")
    dialog = 'POSIX path of (choose folder with prompt "Choose Marta workspace")'
    completed = subprocess.run(["/usr/bin/osascript", "-e", dialog], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
    if completed.returncode:
        raise CommandError("WORKSPACE_SELECTION_CANCELLED", "workspace selection was cancelled")
    selected = completed.stdout.strip()
    if not selected:
        raise CommandError("WORKSPACE_SELECTION_CANCELLED", "workspace selection was cancelled")
    try:
        return pathlib.Path(selected).resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise CommandError("INVALID_ARGUMENT", "selected workspace is unavailable") from exc


def read_message(stream):
    header = stream.read(4)
    if not header:
        return None
    if len(header) != 4:
        raise ProtocolError("incomplete native message header")
    length = struct.unpack("<I", header)[0]
    if length > MAX_NATIVE_INBOUND_MESSAGE_BYTES:
        raise ProtocolError("native inbound message is too large")
    payload = stream.read(length)
    if len(payload) != length:
        raise ProtocolError("incomplete native message payload")
    try:
        return json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ProtocolError("invalid native message JSON") from exc


def write_message(stream, message):
    payload = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) > MAX_NATIVE_MESSAGE_BYTES:
        raise ProtocolError("native response is too large")
    stream.write(struct.pack("<I", len(payload)) + payload)
    stream.flush()


def _require_object(params):
    if not isinstance(params, dict):
        raise CommandError("INVALID_ARGUMENT", "params must be an object")


class Workspace:
    def __init__(self, root):
        self.root = pathlib.Path(root).resolve(strict=True)
        if not self.root.is_dir():
            raise ValueError("workspace must be a directory")
        self.root_fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY)

    def close(self):
        os.close(self.root_fd)

    def _parts(self, value, allow_root=False):
        if not isinstance(value, str) or "\0" in value:
            raise CommandError("INVALID_ARGUMENT", "path must be a relative UTF-8 path")
        candidate = pathlib.PurePath(value)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise CommandError("INVALID_ARGUMENT", "path must stay inside workspace")
        parts = tuple(part for part in candidate.parts if part != ".")
        if not parts and not allow_root:
            raise CommandError("INVALID_ARGUMENT", "workspace root is not a file path")
        return parts

    def path(self, value, allow_root=False):
        # Compatibility helper for validation-only callers. Operations below use
        # descriptor-relative traversal, never this path string.
        parts = self._parts(value, allow_root)
        fd = os.dup(self.root_fd)
        try:
            for part in parts:
                try:
                    info = os.stat(part, dir_fd=fd, follow_symlinks=False)
                except FileNotFoundError:
                    break
                if stat.S_ISLNK(info.st_mode):
                    raise CommandError("INVALID_ARGUMENT", "symlink paths are not allowed")
                if stat.S_ISDIR(info.st_mode):
                    next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0), dir_fd=fd)
                    os.close(fd)
                    fd = next_fd
        finally:
            os.close(fd)
        return self.root.joinpath(*parts)

    def open_dir(self, value="."):
        parts = self._parts(value, allow_root=True)
        fd = os.dup(self.root_fd)
        try:
            for part in parts:
                next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0), dir_fd=fd)
                os.close(fd)
                fd = next_fd
            return fd
        except OSError as exc:
            os.close(fd)
            if exc.errno in (errno.ELOOP, errno.ENOTDIR):
                raise CommandError("INVALID_ARGUMENT", "symlink paths are not allowed") from exc
            raise CommandError("NOT_FOUND", "directory not found") from exc

    def open_parent(self, value, create_parents=False):
        parts = self._parts(value)
        fd = os.dup(self.root_fd)
        try:
            for part in parts[:-1]:
                try:
                    next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0), dir_fd=fd)
                except FileNotFoundError:
                    if not create_parents:
                        raise CommandError("NOT_FOUND", "parent directory not found")
                    os.mkdir(part, dir_fd=fd)
                    next_fd = os.open(part, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0), dir_fd=fd)
                os.close(fd)
                fd = next_fd
            return fd, parts[-1]
        except Exception:
            os.close(fd)
            raise


class Dispatcher:
    def __init__(self, workspace, browser_token=None):
        self.workspace = Workspace(workspace)
        self.browser_token = browser_token
        self.lock = threading.Lock()

    def handle(self, command):
        command_id = command.get("id") if isinstance(command, dict) else None
        try:
            if not isinstance(command_id, str) or not command_id:
                raise CommandError("INVALID_ARGUMENT", "id is required")
            response = {"id": command_id, "ok": True, "result": self.call(command.get("method"), command.get("params"))}
            if len(json.dumps(response, ensure_ascii=False, separators=(",", ":")).encode("utf-8")) > MAX_NATIVE_MESSAGE_BYTES:
                raise CommandError("TOO_LARGE", "response exceeds Native Messaging frame limit")
            return response
        except CommandError as exc:
            error = {"code": exc.code, "message": exc.message}
            if exc.detail:
                error["detail"] = exc.detail
            return {"id": command_id, "ok": False, "error": error}
        except Exception:
            return {"id": command_id, "ok": False, "error": {"code": "INTERNAL_ERROR", "message": "companion command failed"}}

    def call(self, method, params):
        _require_object(params)
        handlers = {
            "smoke.echo": self._echo,
            "files.list": self._list,
            "files.read": self._read,
            "files.write": self._write,
            "files.delete": self._delete,
            "shell.run": self._shell,
            "workspace.choose": self._choose_workspace,
        }
        if method not in handlers:
            raise CommandError("INVALID_ARGUMENT", "unknown or extension-only method")
        with self.lock:
            return handlers[method](params)

    def _echo(self, params):
        if set(params) != {"value"} or not isinstance(params["value"], str):
            raise CommandError("INVALID_ARGUMENT", "smoke.echo requires string value")
        return {"value": params["value"]}

    def _choose_workspace(self, params):
        if params:
            raise CommandError("INVALID_ARGUMENT", "workspace.choose requires no params")
        selected = choose_workspace()
        replacement = Workspace(selected)
        previous = self.workspace
        self.workspace = replacement
        previous.close()
        return {"workspace": str(replacement.root)}

    def _list(self, params):
        if set(params) - {"path", "recursive"} or params.get("recursive", False):
            raise CommandError("INVALID_ARGUMENT", "only non-recursive listing is supported")
        relative = params.get("path", ".")
        fd = self.workspace.open_dir(relative)
        try:
            entries = []
            for entry in sorted(os.scandir(fd), key=lambda item: item.name):
                info = entry.stat(follow_symlinks=False)
                if stat.S_ISLNK(info.st_mode):
                    continue
                prefix = "" if relative in ("", ".") else relative.rstrip("/") + "/"
                entries.append({"path": prefix + entry.name, "type": "dir" if stat.S_ISDIR(info.st_mode) else "file", "size": info.st_size})
            return {"entries": entries}
        finally:
            os.close(fd)

    def _read(self, params):
        if set(params) - {"path", "encoding"} or params.get("encoding", "utf-8") != "utf-8":
            raise CommandError("INVALID_ARGUMENT", "only utf-8 text reads are supported")
        parent_fd, name = self.workspace.open_parent(params.get("path"))
        try:
            fd = os.open(name, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0), dir_fd=parent_fd)
            with os.fdopen(fd, "r", encoding="utf-8") as source:
                info = os.fstat(source.fileno())
                if not stat.S_ISREG(info.st_mode):
                    raise CommandError("INVALID_ARGUMENT", "target must be a file")
                content = source.read()
            return {"path": params["path"], "content": content, "size": info.st_size}
        except UnicodeDecodeError as exc:
            raise CommandError("INVALID_ARGUMENT", "file is not utf-8 text") from exc
        except FileNotFoundError as exc:
            raise CommandError("NOT_FOUND", "file not found") from exc
        finally:
            os.close(parent_fd)

    def _write(self, params):
        allowed = {"path", "content", "createParents", "overwrite"}
        if set(params) - allowed or not isinstance(params.get("content"), str) or not isinstance(params.get("overwrite"), bool):
            raise CommandError("INVALID_ARGUMENT", "write requires path, string content and explicit overwrite")
        parent_fd, name = self.workspace.open_parent(params.get("path"), params.get("createParents", False))
        try:
            try:
                info = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
                if not stat.S_ISREG(info.st_mode):
                    raise CommandError("INVALID_ARGUMENT", "target must be a file")
                if not params["overwrite"]:
                    raise CommandError("INVALID_ARGUMENT", "refusing to overwrite existing file")
                created = False
            except FileNotFoundError:
                created = True
            temp_name = ".marta-" + next(tempfile._get_candidate_names())
            fd = os.open(temp_name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600, dir_fd=parent_fd)
            with os.fdopen(fd, "w", encoding="utf-8") as output:
                output.write(params["content"])
                output.flush()
                os.fsync(output.fileno())
            os.replace(temp_name, name, src_dir_fd=parent_fd, dst_dir_fd=parent_fd)
        finally:
            try: os.unlink(temp_name, dir_fd=parent_fd)
            except (FileNotFoundError, UnboundLocalError): pass
            os.close(parent_fd)
        return {"path": params["path"], "size": len(params["content"].encode("utf-8")), "created": created}

    def _delete(self, params):
        if set(params) != {"path"}:
            raise CommandError("INVALID_ARGUMENT", "delete requires only path")
        parent_fd, name = self.workspace.open_parent(params.get("path"))
        try:
            info = os.stat(name, dir_fd=parent_fd, follow_symlinks=False)
            if not stat.S_ISREG(info.st_mode):
                raise CommandError("NOT_FOUND", "file not found")
            os.unlink(name, dir_fd=parent_fd)
            return {"path": params["path"], "deleted": True}
        except FileNotFoundError as exc:
            raise CommandError("NOT_FOUND", "file not found") from exc
        finally:
            os.close(parent_fd)

    def _shell(self, params):
        allowed = {"command", "cwd", "timeoutSeconds"}
        timeout = params.get("timeoutSeconds", 120)
        if set(params) - allowed or not isinstance(params.get("command"), str) or not isinstance(timeout, int) or not 1 <= timeout <= 120:
            raise CommandError("INVALID_ARGUMENT", "shell requires command and timeoutSeconds in 1..120")
        cwd_fd = self.workspace.open_dir(params.get("cwd", "."))
        env = os.environ.copy()
        for key in ("MARTA_BROWSER_TOKEN", "BROWSER_TOKEN", "AUTHORIZATION"):
            env.pop(key, None)
        # A tiny exec helper fchdirs by descriptor. Unlike a pathname this is
        # immune to replacement races; unlike Popen(preexec_fn) it is safe while
        # the long-poll thread is alive.
        helper = "import os,sys; os.fchdir(int(sys.argv[1])); os.execv('/bin/zsh', ['/bin/zsh','-lc',sys.argv[2]])"
        try:
            proc = subprocess.Popen([sys.executable, "-c", helper, str(cwd_fd), params["command"]], pass_fds=(cwd_fd,), env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        finally:
            os.close(cwd_fd)
        timed_out = False
        try:
            stdout, stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            os.killpg(proc.pid, signal.SIGTERM)
            try:
                stdout, stderr = proc.communicate(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid, signal.SIGKILL)
                stdout, stderr = proc.communicate()
        truncated = len(stdout) + len(stderr) > MAX_OUTPUT_BYTES
        if truncated:
            remaining = MAX_OUTPUT_BYTES
            stdout, remaining = stdout[:remaining], max(0, remaining - len(stdout[:remaining]))
            stderr = stderr[:remaining]
        return {"exitCode": proc.returncode, "stdout": stdout.decode("utf-8", "replace"), "stderr": stderr.decode("utf-8", "replace"), "timedOut": timed_out, "truncated": truncated}


def load_config(path):
    with open(path, encoding="utf-8") as config_file:
        config = json.load(config_file)
    required = ("martaUrl", "browserToken", "portalHost", "userId", "clientId", "workspace")
    missing = [key for key in required if key not in config]
    if missing:
        raise ValueError("missing config keys: " + ", ".join(missing))
    return config


class MartaBridge:
    """Long-poll transport; browser commands are relayed over Native Messaging."""
    def __init__(self, config, dispatcher, output):
        self.config, self.dispatcher, self.output = config, dispatcher, output
        self.output_lock = threading.Lock()

    def headers(self):
        return {"Authorization": "Bearer " + self.config["browserToken"], "X-Portal-Host": str(self.config["portalHost"]), "X-User-Id": str(self.config["userId"]), "X-Client-Id": str(self.config["clientId"])}

    def request(self, path, payload=None, binary=None, extra_headers=None):
        body = binary if binary is not None else (json.dumps(payload).encode("utf-8") if payload is not None else b"")
        headers = self.headers()
        headers["Content-Type"] = "image/jpeg" if binary is not None else "application/json"
        headers.update(extra_headers or {})
        request = urllib.request.Request(self.config["martaUrl"].rstrip("/") + path, data=body, headers=headers, method="POST")
        with urllib.request.urlopen(request, timeout=35) as response:
            return response.status, response.read()

    def post_result(self, envelope):
        try:
            self.request("/api/browser-bridge/result", envelope)
        except (urllib.error.URLError, OSError) as exc:
            print("marta browser host: result delivery failed: " + str(exc), file=sys.stderr)

    def send_native(self, command):
        with self.output_lock:
            write_message(self.output, command)

    @staticmethod
    def validate_command_json(body):
        command = json.loads(body.decode("utf-8"))
        if not isinstance(command, dict):
            raise ValueError("poll response must be a JSON object")
        if not isinstance(command.get("id"), str) or not isinstance(command.get("method"), str) or not isinstance(command.get("params"), dict):
            raise ValueError("poll response is not a command envelope")
        return command

    def relay_browser_result(self, envelope):
        result = envelope.get("result") if envelope.get("ok") else None
        if isinstance(result, dict) and "imageDataUrl" in result:
            try:
                raw = base64.b64decode(result.pop("imageDataUrl").split(",", 1)[1], validate=True)
                status, body = self.request("/api/browser-bridge/captures", binary=raw, extra_headers={"X-Tab-Id": str(result["tabId"]), "X-Tab-Url": result["url"]})
                if status != 201:
                    raise ValueError("capture upload rejected")
                result.update(json.loads(body.decode("utf-8")))
            except Exception:
                envelope = {"id": envelope.get("id"), "ok": False, "error": {"code": "INTERNAL_ERROR", "message": "capture upload failed"}}
        self.post_result(envelope)

    def poll_forever(self):
        while True:
            try:
                status, body = self.request("/api/browser-bridge/poll", {})
                if status == 204:
                    continue
                command = self.validate_command_json(body)
                if command.get("method") in {"files.list", "files.read", "files.write", "files.delete", "shell.run"}:
                    self.post_result(self.dispatcher.handle(command))
                else:
                    try:
                        self.send_native(command)
                    except ProtocolError:
                        self.post_result({"id": command["id"], "ok": False, "error": {"code": "TOO_LARGE", "message": "command exceeds Native Messaging frame limit"}})
            except Exception as exc:
                print("marta browser host: poll failed: " + str(exc), file=sys.stderr)
                time.sleep(2)


def main():
    config_path = os.environ.get("MARTA_BROWSER_CONFIG") or str(pathlib.Path(__file__).parents[1] / "config" / "config.local.json")
    config = load_config(config_path)
    dispatcher = Dispatcher(config["workspace"], config["browserToken"])
    bridge = MartaBridge(config, dispatcher, sys.stdout.buffer)
    threading.Thread(target=bridge.poll_forever, daemon=True).start()
    while True:
        try:
            command = read_message(sys.stdin.buffer)
        except ProtocolError as exc:
            print("marta browser host: rejected native frame: " + str(exc), file=sys.stderr)
            # A stream with an invalid frame header cannot be resynchronised
            # safely. Close this native port; MV3 reconnect backoff starts a
            # fresh host process.
            return
        if command is None:
            return
        if isinstance(command, dict) and isinstance(command.get("ok"), bool):
            bridge.relay_browser_result(command)
        else:
            bridge.send_native(dispatcher.handle(command))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("marta browser host: " + str(exc), file=sys.stderr)
        raise
