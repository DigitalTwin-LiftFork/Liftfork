"""Local warehouse transport coordinator. Standard library only."""
import argparse
import json
import sqlite3
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import RLock

TERMINAL = {"completed", "failed", "cancelled", "interrupted"}
ACTIVE = {"assigned", "moving", "unloading", "withdrawing"}
STAGES = ACTIVE | TERMINAL


class Coordinator:
    def __init__(self, database):
        self.db = sqlite3.connect(database, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS jobs(
          id TEXT PRIMARY KEY, pallet TEXT NOT NULL, destination TEXT NOT NULL,
          status TEXT NOT NULL, session TEXT, message TEXT NOT NULL DEFAULT '',
          cancel_requested INTEGER NOT NULL DEFAULT 0, created REAL NOT NULL, updated REAL NOT NULL);
        """)
        # A restarted server never silently re-dispatches a previously assigned job.
        self.db.execute("UPDATE jobs SET status='interrupted', message='서버 재시작 후 현장 확인 필요', updated=? "
                        "WHERE status IN ('assigned','moving','unloading','withdrawing')", (time.time(),))
        self.db.commit()
        self.snapshot = {}
        self.seen = 0.0
        self.session = None
        self.charging_command = None
        self.lock = RLock()

    def rows(self):
        return [dict(row) for row in self.db.execute("SELECT * FROM jobs ORDER BY created DESC")]

    def online(self):
        return bool(self.snapshot) and time.time() - self.seen < 5

    def state(self):
        with self.lock:
            jobs = self.rows()
            slots = [dict(slot) for slot in self.snapshot.get("slots", [])]
            for slot in slots:
                job = next((j for j in jobs if j["destination"] == slot["id"] and j["status"] not in TERMINAL), None)
                slot["reserved_by"] = job["id"] if job else None
            return {"online": self.online(), "snapshot": self.snapshot, "slots": slots, "jobs": jobs}

    def create(self, data):
        with self.lock:
            if not self.online():
                raise ValueError("시뮬레이션 연결을 먼저 확인하세요")
            pallet = str(data.get("pallet", ""))
            destination = str(data.get("destination", ""))
            if not any(p["id"] == pallet for p in self.snapshot.get("pallets", [])):
                raise ValueError("팔레트 번호를 확인하세요")
            slot = next((s for s in self.snapshot.get("slots", []) if s["id"] == destination), None)
            if slot is None or slot.get("occupied"):
                raise ValueError("실제 연결된 빈 선반을 선택하세요")
            jobs = self.rows()
            if any(j["status"] not in TERMINAL and
                   (j["destination"] == destination or j["pallet"] == pallet) for j in jobs):
                raise ValueError("해당 팔레트 또는 선반에 진행 중인 작업이 있습니다")
            job_id = uuid.uuid4().hex[:12]
            now = time.time()
            self.db.execute("INSERT INTO jobs(id,pallet,destination,status,created,updated) VALUES(?,?,?,'queued',?,?)",
                            (job_id, pallet, destination, now, now))
            self.db.commit()
            return {"id": job_id}

    def cancel(self, job_id):
        with self.lock:
            row = self.db.execute("SELECT * FROM jobs WHERE id=?", (job_id,)).fetchone()
            if row is None or row["status"] in TERMINAL:
                raise ValueError("취소 가능한 작업이 없습니다")
            if row["status"] == "queued":
                self.db.execute("UPDATE jobs SET status='cancelled', updated=? WHERE id=?", (time.time(), job_id))
            else:
                self.db.execute("UPDATE jobs SET cancel_requested=1, updated=? WHERE id=?", (time.time(), job_id))
            self.db.commit()
            return {"ok": True}

    def charging(self, data):
        with self.lock:
            if not self.online():
                raise ValueError("Factory 연결을 먼저 확인하세요")
            action = data.get("action")
            if action not in {"request", "cancel"}:
                raise ValueError("충전 명령을 확인하세요")
            self.charging_command = {"id": uuid.uuid4().hex, "action": action}
            return {"ok": True, "id": self.charging_command["id"]}

    def sync(self, data):
        with self.lock:
            session = str(data.get("session", ""))
            if not session:
                raise ValueError("session is required")
            if self.session and session != self.session and self.online():
                raise ValueError("이미 연결된 지게차 세션이 있습니다")
            if session != self.session:
                self.db.execute("UPDATE jobs SET status='interrupted', message='시뮬레이션 재시작 후 현장 확인 필요', updated=? "
                                "WHERE status IN ('assigned','moving','unloading','withdrawing') AND session<>?",
                                (time.time(), session))
            if self.session and session != self.session:
                self.charging_command = None
            self.session = session
            if self.charging_command and data.get("charge_ack") == self.charging_command["id"]:
                self.charging_command = None
            self.snapshot = data
            self.seen = time.time()
            report = data.get("report")
            if report:
                row = self.db.execute("SELECT * FROM jobs WHERE id=? AND session=?",
                                      (report.get("id"), session)).fetchone()
                status = report.get("status")
                if row and row["status"] not in TERMINAL and status in STAGES:
                    if status == "completed":
                        slot = next((s for s in data.get("slots", []) if s["id"] == row["destination"]), None)
                        if not slot or slot.get("pallet") != row["pallet"] or data.get("loaded"):
                            status = "failed"
                            report = dict(report, message="하역 완료 보고와 실제 선반 위치가 일치하지 않습니다")
                    self.db.execute("UPDATE jobs SET status=?,message=?,updated=? WHERE id=?",
                                    (status, str(report.get("message", ""))[:500], self.seen, row["id"]))
            current = self.db.execute("SELECT * FROM jobs WHERE session=? AND status IN "
                                      "('assigned','moving','unloading','withdrawing') LIMIT 1", (session,)).fetchone()
            battery = data.get("battery", {})
            battery_available = battery.get("percent", 100) > battery.get("low_threshold", 20) and battery.get("state") not in {"waiting", "returning", "connecting", "charging", "disconnecting"}
            if current is None and data.get("ready") and battery_available and not report and not self.charging_command:
                for row in self.db.execute("SELECT * FROM jobs WHERE status='queued' ORDER BY created"):
                    if row["pallet"] != data.get("loaded"):
                        continue
                    slot = next((s for s in data.get("slots", []) if s["id"] == row["destination"]), None)
                    if slot and not slot.get("occupied"):
                        self.db.execute("UPDATE jobs SET status='assigned',session=?,updated=? WHERE id=?",
                                        (session, self.seen, row["id"]))
                        current = self.db.execute("SELECT * FROM jobs WHERE id=?", (row["id"],)).fetchone()
                        break
            self.db.commit()
            return {"job": dict(current) if current else None, "ack": report.get("id") if report else None, "charging_command": self.charging_command}


class Handler(BaseHTTPRequestHandler):
    def reply(self, status, data, content_type="application/json; charset=utf-8"):
        body = json.dumps(data, ensure_ascii=False).encode() if content_type.startswith("application/json") else data
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/":
            self.reply(200, Path(__file__).with_name("index.html").read_bytes(), "text/html; charset=utf-8")
        elif self.path == "/api/state":
            self.reply(200, self.server.coordinator.state())
        else:
            self.reply(404, {"error": "not found"})

    def do_POST(self):
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if size <= 0 or size > 2_000_000:
                raise ValueError("invalid body size")
            data = json.loads(self.rfile.read(size))
            if not isinstance(data, dict):
                raise ValueError("invalid body")
            if self.path == "/api/sync":
                result = self.server.coordinator.sync(data)
            elif self.path == "/api/charging":
                result = self.server.coordinator.charging(data)
            elif self.path == "/api/jobs":
                result = self.server.coordinator.create(data)
            elif self.path.startswith("/api/jobs/") and self.path.endswith("/cancel"):
                result = self.server.coordinator.cancel(self.path.split("/")[3])
            else:
                self.reply(404, {"error": "not found"})
                return
            self.reply(200, result)
        except (ValueError, KeyError, TypeError) as error:
            self.reply(400, {"error": str(error)})

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--database", default=str(Path(__file__).parents[2] / "Saved" / "transport.sqlite"))
    args = parser.parse_args()
    Path(args.database).parent.mkdir(parents=True, exist_ok=True)
    httpd = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    httpd.coordinator = Coordinator(args.database)
    print(f"운반 작업 화면 http://127.0.0.1:{args.port}", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()

