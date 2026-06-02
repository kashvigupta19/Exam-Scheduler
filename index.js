import cors from "cors";
import express from "express";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const rootDir = path.resolve(__dirname, "..");
const optimizerPath = path.join(rootDir, "cpp", "build", "optimizer");
const clientDist = path.join(rootDir, "client", "dist");
const port = Number(process.env.PORT || 4000);

const app = express();
app.use(cors());
app.use(express.json({ limit: "1mb" }));

const subjectCatalog = [
  { name: "DAA", semester: 4 },
  { name: "Software Engineering", semester: 5 },
  { name: "AIML", semester: 6 },
  { name: "FDA", semester: 3 },
  { name: "DSCO", semester: 4 },
  { name: "DBMS", semester: 5 },
  { name: "SDF", semester: 2 },
  { name: "DSA", semester: 3 },
  { name: "MFAIDS", semester: 7 },
  { name: "Physics", semester: 2 }
];
const sessionOptions = ["Morning (10 AM - 12 PM)", "Afternoon (1 PM - 3 PM)"];

function subjectsForSemesterSet(semesterSet) {
  const expected = semesterSet === "even" ? 0 : 1;
  return subjectCatalog.filter((subject) => subject.semester % 2 === expected);
}

function exampleDataset() {
  const semesterSet = "even";
  return {
    semester_set: semesterSet,
    room_capacity: 120,
    rooms: 2,
    sessions: sessionOptions,
    algorithms: ["greedy_coloring"],
    subjects: [
      { name: "DAA", semester: 4, branch: "CSE", students: 115, overlaps: ["DBMS"] },
      { name: "AIML", semester: 6, branch: "CSE", students: 92 },
      { name: "DSCO", semester: 4, branch: "ECE", students: 74 },
      { name: "SDF", semester: 2, branch: "CSE", students: 118 },
      { name: "Physics", semester: 2, branch: "ECE", students: 85 }
    ]
  };
}

function clamp(number, min, max) {
  return Math.max(min, Math.min(max, Number.isFinite(number) ? number : min));
}

function runOptimizer(payload) {
  return new Promise((resolve, reject) => {
    if (!existsSync(optimizerPath)) {
      reject({
        status: 500,
        message: "C++ optimizer binary is missing. Run `npm run build:cpp` from the project root."
      });
      return;
    }

    const child = spawn(optimizerPath, [], {
      cwd: rootDir,
      stdio: ["pipe", "pipe", "pipe"]
    });

    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => {
      child.kill("SIGKILL");
      reject({ status: 504, message: "Optimizer timed out." });
    }, 8000);

    child.stdout.on("data", (chunk) => {
      stdout += chunk.toString();
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("error", (error) => {
      clearTimeout(timer);
      reject({ status: 500, message: error.message });
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      if (code !== 0) {
        reject({
          status: 400,
          message: "Optimizer rejected the request.",
          details: stderr || stdout
        });
        return;
      }

      try {
        resolve(JSON.parse(stdout));
      } catch (error) {
        reject({
          status: 500,
          message: "Optimizer returned invalid JSON.",
          details: stdout,
          parse_error: error.message
        });
      }
    });

    child.stdin.write(JSON.stringify(payload));
    child.stdin.end();
  });
}

function normalizeRunRequest(body) {
  if (!body || !Array.isArray(body.subjects)) {
    const error = new Error("Request body must include a subjects array.");
    error.status = 400;
    throw error;
  }

  return {
    subjects: body.subjects,
    semester_set: body.semester_set === "even" ? "even" : "odd",
    room_capacity: Math.max(1, Number(body.room_capacity || 60)),
    rooms: Math.max(1, Number(body.rooms || 1)),
    sessions: Array.isArray(body.sessions) ? body.sessions : sessionOptions,
    algorithms: Array.isArray(body.algorithms) ? body.algorithms : []
  };
}

app.get("/health", (_request, response) => {
  response.json({ ok: true, optimizer_built: existsSync(optimizerPath) });
});

app.get("/example", (_request, response) => {
  response.json(exampleDataset());
});

app.post("/generate", (request, response) => {
  const count = clamp(Number(request.body?.count ?? 12), 1, 80);
  const roomCapacity = clamp(Number(request.body?.room_capacity ?? 100), 20, 500);
  const rooms = clamp(Number(request.body?.rooms ?? 2), 1, 20);
  const semesterSet = request.body?.semester_set === "even" ? "even" : "odd";
  const availableSubjects = subjectsForSemesterSet(semesterSet);
  const sessions = Array.isArray(request.body?.sessions) && request.body.sessions.length > 0
    ? request.body.sessions.filter((session) => sessionOptions.includes(session))
    : sessionOptions;
  const branches = ["CSE", "MnC", "ECE", "IT"];
  const subjects = Array.from({ length: count }, (_, index) => {
    const i = index + 1;
    const catalogSubject = availableSubjects[index % availableSubjects.length];
    return {
      name: catalogSubject.name,
      semester: catalogSubject.semester,
      branch: branches[index % branches.length],
      students: 45 + ((i * 19) % 110),
      overlaps: []
    };
  });

  response.json({
    subjects,
    semester_set: semesterSet,
    sessions: sessions.length > 0 ? sessions : sessionOptions,
    room_capacity: roomCapacity,
    rooms,
    algorithms: ["greedy_coloring", count <= 10 ? "backtracking" : ""].filter(Boolean)
  });
});

app.post("/run", async (request, response) => {
  try {
    const payload = normalizeRunRequest(request.body);
    const result = await runOptimizer(payload);
    response.json(result);
  } catch (error) {
    response.status(error.status || 500).json({
      error: error.message || "Unexpected server error.",
      details: error.details,
      parse_error: error.parse_error
    });
  }
});

if (existsSync(clientDist)) {
  app.use(express.static(clientDist));
  app.get(/.*/, (_request, response) => {
    response.sendFile(path.join(clientDist, "index.html"));
  });
}

app.listen(port, () => {
  console.log(`Optimizer API listening on http://127.0.0.1:${port}`);
});
