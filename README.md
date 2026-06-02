# Exam Timetable Scheduler

A full-stack DAA demo that generates a conflict-free exam timetable using graph coloring, greedy coloring, backtracking, and constraint checking.

## Structure

- `cpp/`: C++17 scheduler executable. It reads exam JSON from stdin and writes scheduler JSON to stdout.
- `server/`: Express API bridge. It runs the compiled C++ scheduler for `/run`.
- `client/`: Vite React UI for entering subjects, semesters, branches, rooms, session availability, and overlaps.

## Input Model

Each subject has:

- `name`
- `semester`
- `branch`
- `students`
- optional `overlaps`, a comma-separated UI field for extra student-overlap conflicts

Each timetable has one global `semester_set`: either odd semesters only (`1, 3, 5, 7`) or even semesters only (`2, 4, 6, 8`). The scheduler creates graph edges when two exams belong to the same branch and semester, or when an explicit overlap is entered. Users only choose whether Morning (`10 AM - 12 PM`) and/or Afternoon (`1 PM - 3 PM`) sessions are available; the program generates enough day/session slots from that availability and the number of subjects.

Room assignment is whole-room based: a room receives papers for only one exam. If an exam has 70 students and room capacity is 60, it uses two rooms; the second room has 10 students and its remaining 50 seats are reserved, not reused by another exam.

## Run

```bash
npm install --prefix server
npm install --prefix client
npm run build:cpp
```

Start the API:

```bash
npm run dev:server
```

Start the React UI in another terminal:

```bash
npm run dev:client
```

Open `http://127.0.0.1:5173/`.

## Checks

```bash
npm run build
npm run test:cpp
npm run test:api
```

`test:api` expects the API server to already be running on `http://127.0.0.1:4000`.
