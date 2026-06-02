#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Subject {
  std::string name;
  int semester = 0;
  std::string branch;
  int students = 0;
  std::vector<std::string> overlaps;
  int original_index = 0;
};

struct Edge {
  int a = 0;
  int b = 0;
  std::string reason;
};

struct ScheduleResult {
  std::vector<int> assignment;
  std::vector<int> slot_loads;
  std::vector<int> slot_room_counts;
  int scheduled_count = 0;
  int colors_used = 0;
  long long visited_nodes = 0;
};

static void addLog(std::vector<std::string>& logs, const std::string& message) {
  if (logs.size() < 500) logs.push_back(message);
}

static json subjectToJson(const Subject& subject) {
  return {
      {"name", subject.name},
      {"semester", subject.semester},
      {"branch", subject.branch},
      {"students", subject.students},
      {"overlaps", subject.overlaps},
  };
}

static bool hasAlgorithm(const std::set<std::string>& algorithms, const std::string& name) {
  return algorithms.empty() || algorithms.count(name) > 0;
}

static int roomsRequired(int students, int room_capacity) {
  return (students + room_capacity - 1) / room_capacity;
}

static std::string roomName(int index) {
  std::string suffix;
  int value = index;
  do {
    suffix.push_back(static_cast<char>('A' + (value % 26)));
    value = value / 26 - 1;
  } while (value >= 0);
  std::reverse(suffix.begin(), suffix.end());
  return "Room " + suffix;
}

// Reads user rows and keeps only meaningful exam subjects for the coloring graph.
static std::vector<Subject> validateSubjects(const json& input, std::vector<std::string>& slots,
                                             int& room_capacity, int& rooms,
                                             std::vector<json>& invalid_subjects,
                                             std::vector<std::string>& logs) {
  if (!input.contains("subjects") || !input["subjects"].is_array()) {
    throw std::runtime_error("Request must include a subjects array.");
  }

  room_capacity = std::max(1, input.value("room_capacity", 60));
  rooms = std::max(1, input.value("rooms", 1));
  const std::string semester_set = input.value("semester_set", "odd");
  const int required_remainder = semester_set == "even" ? 0 : 1;

  std::vector<Subject> subjects;
  std::set<std::string> seen_subject_keys;
  int row = 0;
  for (const auto& raw : input["subjects"]) {
    row++;
    Subject subject;
    subject.name = raw.value("name", "");
    subject.semester = raw.value("semester", 0);
    subject.branch = raw.value("branch", "");
    subject.students = raw.value("students", 0);
    subject.original_index = row;

    if (raw.contains("overlaps") && raw["overlaps"].is_array()) {
      for (const auto& overlap : raw["overlaps"]) {
        if (overlap.is_string() && !overlap.get<std::string>().empty()) {
          subject.overlaps.push_back(overlap.get<std::string>());
        }
      }
    }

    const bool invalid = subject.name.empty() || subject.branch.empty() || subject.semester < 1 ||
                         subject.semester > 8 || subject.students < 1 ||
                         subject.semester % 2 != required_remainder;
    if (invalid) {
      invalid_subjects.push_back(raw);
      addLog(logs, "[Validation] Removed invalid or wrong-semester-set subject at row " +
                       std::to_string(row) + ".");
      continue;
    }

    const std::string duplicate_key = subject.name + "|" + std::to_string(subject.semester) +
                                      "|" + subject.branch;
    if (!seen_subject_keys.insert(duplicate_key).second) {
      invalid_subjects.push_back(raw);
      addLog(logs, "[Validation] Removed duplicate exam row for " + subject.name +
                       " / semester " + std::to_string(subject.semester) + " / " +
                       subject.branch + ".");
      continue;
    }

    subjects.push_back(subject);
  }

  std::sort(subjects.begin(), subjects.end(), [](const Subject& a, const Subject& b) {
    if (a.semester != b.semester) return a.semester < b.semester;
    if (a.branch != b.branch) return a.branch < b.branch;
    return a.name < b.name;
  });

  std::vector<std::string> sessions;
  if (input.contains("sessions") && input["sessions"].is_array()) {
    for (const auto& item : input["sessions"]) {
      if (item.is_string() && !item.get<std::string>().empty()) {
        sessions.push_back(item.get<std::string>());
      }
    }
  }
  if (sessions.empty()) {
    sessions.push_back("Morning (10 AM - 12 PM)");
    sessions.push_back("Afternoon (1 PM - 3 PM)");
  }

  const int valid_subject_count = std::max(1, static_cast<int>(subjects.size()));
  const int days = std::max(1, (valid_subject_count + static_cast<int>(sessions.size()) - 1) /
                                  static_cast<int>(sessions.size()));
  for (int day = 1; day <= days; day++) {
    for (const std::string& session : sessions) {
      slots.push_back("Day " + std::to_string(day) + " - " + session);
    }
  }

  addLog(logs, "[Validation] Accepted " + std::to_string(subjects.size()) + " exam subject(s).");
  addLog(logs, "[Validation] Semester set is " + semester_set + ".");
  addLog(logs, "[Validation] Generated " + std::to_string(slots.size()) +
                   " slot(s) from selected session availability.");
  addLog(logs, "[Validation] Effective slot capacity is " +
                   std::to_string(room_capacity * rooms) + " student seat(s).");
  return subjects;
}

// Builds a conflict graph. An edge means two exams cannot share the same time slot.
static std::vector<Edge> buildConflictGraph(const std::vector<Subject>& subjects,
                                            std::vector<std::vector<int>>& graph,
                                            std::vector<std::string>& logs) {
  const int n = static_cast<int>(subjects.size());
  graph.assign(n, {});
  std::vector<Edge> edges;
  std::set<std::pair<int, int>> seen;
  std::unordered_map<std::string, int> index_by_name;

  for (int i = 0; i < n; i++) {
    index_by_name[subjects[i].name] = i;
  }

  auto addEdge = [&](int a, int b, const std::string& reason) {
    if (a == b) return;
    if (a > b) std::swap(a, b);
    if (seen.insert({a, b}).second) {
      graph[a].push_back(b);
      graph[b].push_back(a);
      edges.push_back({a, b, reason});
      addLog(logs, "[Graph] Conflict edge: " + subjects[a].name + " -- " + subjects[b].name +
                       " (" + reason + ").");
    }
  };

  for (int i = 0; i < n; i++) {
    for (int j = i + 1; j < n; j++) {
      if (subjects[i].branch == subjects[j].branch && subjects[i].semester == subjects[j].semester) {
        addEdge(i, j, "same branch and semester");
      }
    }
  }

  for (int i = 0; i < n; i++) {
    for (const std::string& overlap_name : subjects[i].overlaps) {
      auto found = index_by_name.find(overlap_name);
      if (found != index_by_name.end()) {
        addEdge(i, found->second, "explicit student overlap");
      }
    }
  }

  addLog(logs, "[Graph] Built graph with " + std::to_string(n) + " vertex/vertices and " +
                   std::to_string(edges.size()) + " edge(s).");
  return edges;
}

static bool canPlace(int subject_index, int slot, const std::vector<Subject>& subjects,
                     const std::vector<std::vector<int>>& graph, const std::vector<int>& assignment,
                     const std::vector<int>& slot_room_counts, int room_capacity, int rooms) {
  const int required_rooms = roomsRequired(subjects[subject_index].students, room_capacity);
  if (slot_room_counts[slot] + required_rooms > rooms) return false;
  for (int neighbor : graph[subject_index]) {
    if (assignment[neighbor] == slot) return false;
  }
  return true;
}

static int countColorsUsed(const std::vector<int>& assignment) {
  std::set<int> used;
  for (int color : assignment) {
    if (color >= 0) used.insert(color);
  }
  return static_cast<int>(used.size());
}

static int countScheduled(const std::vector<int>& assignment) {
  return static_cast<int>(std::count_if(assignment.begin(), assignment.end(), [](int color) {
    return color >= 0;
  }));
}

// Greedy coloring orders high-conflict/high-enrollment exams first and assigns the earliest legal slot.
static ScheduleResult greedyColoring(const std::vector<Subject>& subjects,
                                     const std::vector<std::vector<int>>& graph,
                                     int slot_count, int room_capacity, int rooms,
                                     std::vector<std::string>& logs) {
  const int n = static_cast<int>(subjects.size());
  ScheduleResult result;
  result.assignment.assign(n, -1);
  result.slot_loads.assign(slot_count, 0);
  result.slot_room_counts.assign(slot_count, 0);

  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (graph[a].size() != graph[b].size()) return graph[a].size() > graph[b].size();
    if (subjects[a].students != subjects[b].students) return subjects[a].students > subjects[b].students;
    return subjects[a].name < subjects[b].name;
  });

  for (int subject_index : order) {
    bool placed = false;
    for (int slot = 0; slot < slot_count; slot++) {
      if (canPlace(subject_index, slot, subjects, graph, result.assignment,
                   result.slot_room_counts, room_capacity, rooms)) {
        result.assignment[subject_index] = slot;
        result.slot_loads[slot] += subjects[subject_index].students;
        result.slot_room_counts[slot] += roomsRequired(subjects[subject_index].students, room_capacity);
        placed = true;
        addLog(logs, "[Greedy Coloring] Placed " + subjects[subject_index].name + " in slot " +
                         std::to_string(slot + 1) + ".");
        break;
      }
    }
    if (!placed) {
      addLog(logs, "[Greedy Coloring] Could not place " + subjects[subject_index].name +
                       " within the available slots and room capacity.");
    }
  }

  result.scheduled_count = countScheduled(result.assignment);
  result.colors_used = countColorsUsed(result.assignment);
  return result;
}

static bool isBetterBacktrackingCandidate(const ScheduleResult& candidate,
                                          const ScheduleResult& best) {
  if (candidate.scheduled_count != best.scheduled_count) {
    return candidate.scheduled_count > best.scheduled_count;
  }
  if (candidate.colors_used != best.colors_used) {
    return candidate.colors_used < best.colors_used;
  }
  int candidate_load = 0;
  int best_load = 0;
  for (int load : candidate.slot_loads) candidate_load += load;
  for (int load : best.slot_loads) best_load += load;
  return candidate_load > best_load;
}

static void backtrackDfs(const std::vector<Subject>& subjects,
                         const std::vector<std::vector<int>>& graph,
                         const std::vector<int>& order, int order_position,
                         int slot_count, int room_capacity, int rooms, ScheduleResult& current,
                         ScheduleResult& best, std::vector<std::string>& logs) {
  current.visited_nodes++;

  const int remaining = static_cast<int>(order.size()) - order_position;
  if (current.scheduled_count + remaining < best.scheduled_count) {
    addLog(logs, "[Backtracking] Pruned branch because it cannot schedule more exams than best.");
    return;
  }

  if (order_position == static_cast<int>(order.size())) {
    current.colors_used = countColorsUsed(current.assignment);
    if (isBetterBacktrackingCandidate(current, best)) {
      best = current;
      addLog(logs, "[Backtracking] New best schedules " +
                       std::to_string(best.scheduled_count) + " exam(s) using " +
                       std::to_string(best.colors_used) + " slot color(s).");
    }
    return;
  }

  const int subject_index = order[order_position];
  for (int slot = 0; slot < slot_count; slot++) {
    if (!canPlace(subject_index, slot, subjects, graph, current.assignment,
                  current.slot_room_counts, room_capacity, rooms)) {
      continue;
    }

    current.assignment[subject_index] = slot;
    current.slot_loads[slot] += subjects[subject_index].students;
    current.slot_room_counts[slot] += roomsRequired(subjects[subject_index].students, room_capacity);
    current.scheduled_count++;

    backtrackDfs(subjects, graph, order, order_position + 1, slot_count, room_capacity, rooms, current,
                 best, logs);

    current.scheduled_count--;
    current.slot_room_counts[slot] -= roomsRequired(subjects[subject_index].students, room_capacity);
    current.slot_loads[slot] -= subjects[subject_index].students;
    current.assignment[subject_index] = -1;
  }

  addLog(logs, "[Backtracking] Exploring branch where " + subjects[subject_index].name +
                   " is left unscheduled.");
  backtrackDfs(subjects, graph, order, order_position + 1, slot_count, room_capacity, rooms, current,
               best, logs);
}

// Exact backtracking tries all legal color placements for small graphs to benchmark greedy coloring.
static ScheduleResult backtrackingColoring(const std::vector<Subject>& subjects,
                                           const std::vector<std::vector<int>>& graph,
                                           int slot_count, int room_capacity, int rooms,
                                           std::vector<std::string>& logs) {
  const int n = static_cast<int>(subjects.size());
  std::vector<int> order(n);
  for (int i = 0; i < n; i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (graph[a].size() != graph[b].size()) return graph[a].size() > graph[b].size();
    return subjects[a].students > subjects[b].students;
  });

  ScheduleResult current;
  current.assignment.assign(n, -1);
  current.slot_loads.assign(slot_count, 0);
  current.slot_room_counts.assign(slot_count, 0);

  ScheduleResult best;
  best.assignment.assign(n, -1);
  best.slot_loads.assign(slot_count, 0);
  best.slot_room_counts.assign(slot_count, 0);

  backtrackDfs(subjects, graph, order, 0, slot_count, room_capacity, rooms, current, best, logs);
  best.visited_nodes = current.visited_nodes;
  return best;
}

static json subjectsToJson(const std::vector<Subject>& subjects) {
  json rows = json::array();
  for (const auto& subject : subjects) rows.push_back(subjectToJson(subject));
  return rows;
}

static json edgesToJson(const std::vector<Edge>& edges, const std::vector<Subject>& subjects) {
  json rows = json::array();
  for (const auto& edge : edges) {
    rows.push_back({
        {"source", subjects[edge.a].name},
        {"target", subjects[edge.b].name},
        {"reason", edge.reason},
    });
  }
  return rows;
}

static json timetableToJson(const ScheduleResult& result, const std::vector<Subject>& subjects,
                            const std::vector<std::string>& slots, int room_capacity, int rooms) {
  json timetable = json::array();
  int last_used_slot = -1;
  for (int assigned_slot : result.assignment) {
    if (assigned_slot >= 0) last_used_slot = std::max(last_used_slot, assigned_slot);
  }
  const int visible_slot_count =
      std::min(static_cast<int>(slots.size()), std::max(1, last_used_slot + 1));

  for (int slot = 0; slot < visible_slot_count; slot++) {
    json exams = json::array();
    json room_allocations = json::array();
    int next_room_index = 0;
    for (int i = 0; i < static_cast<int>(subjects.size()); i++) {
      if (result.assignment[i] == slot) {
        const Subject& subject = subjects[i];
        const int required_rooms = roomsRequired(subject.students, room_capacity);
        int remaining_students = subject.students;
        json assigned_rooms = json::array();
        for (int room_offset = 0; room_offset < required_rooms; room_offset++) {
          const int students_in_room = std::min(room_capacity, remaining_students);
          assigned_rooms.push_back({
              {"name", roomName(next_room_index++)},
              {"students", students_in_room},
              {"capacity", room_capacity},
          });
          remaining_students -= students_in_room;
        }

        json exam = subjectToJson(subject);
        exam["rooms_required"] = required_rooms;
        exam["reserved_unused_seats"] = required_rooms * room_capacity - subject.students;
        exam["rooms"] = assigned_rooms;
        exams.push_back(exam);
        room_allocations.push_back({
            {"subject", subject.name},
            {"semester", subject.semester},
            {"branch", subject.branch},
            {"students", subject.students},
            {"rooms_required", required_rooms},
            {"reserved_unused_seats", required_rooms * room_capacity - subject.students},
            {"rooms", assigned_rooms},
        });
      }
    }
    const int rooms_used = result.slot_room_counts.empty() ? 0 : result.slot_room_counts[slot];
    const int slot_capacity = room_capacity * rooms;
    timetable.push_back({
        {"slot_index", slot + 1},
        {"slot", slots[slot]},
        {"load", result.slot_loads.empty() ? 0 : result.slot_loads[slot]},
        {"rooms_used", rooms_used},
        {"rooms_total", rooms},
        {"remaining_rooms", rooms - rooms_used},
        {"remaining_capacity", (rooms - rooms_used) * room_capacity},
        {"room_allocations", room_allocations},
        {"exams", exams},
    });
  }
  return timetable;
}

static json unscheduledToJson(const ScheduleResult& result, const std::vector<Subject>& subjects) {
  json rows = json::array();
  for (int i = 0; i < static_cast<int>(subjects.size()); i++) {
    if (result.assignment[i] < 0) rows.push_back(subjectToJson(subjects[i]));
  }
  return rows;
}

int main() {
  try {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    json input = json::parse(buffer.str());

    std::set<std::string> algorithms;
    for (const auto& item : input.value("algorithms", json::array())) {
      if (item.is_string()) algorithms.insert(item.get<std::string>());
    }

    const auto total_start = Clock::now();
    std::vector<std::string> logs;
    std::vector<json> invalid_subjects;
    std::vector<std::string> slots;
    int room_capacity = 0;
    int rooms = 0;

    std::vector<Subject> subjects =
        validateSubjects(input, slots, room_capacity, rooms, invalid_subjects, logs);
    const int slot_capacity = room_capacity * rooms;

    std::vector<std::vector<int>> graph;
    std::vector<Edge> edges = buildConflictGraph(subjects, graph, logs);

    ScheduleResult greedy;
    double greedy_runtime = 0.0;
    if (hasAlgorithm(algorithms, "greedy_coloring")) {
      const auto start = Clock::now();
      greedy = greedyColoring(subjects, graph, static_cast<int>(slots.size()), room_capacity, rooms, logs);
      greedy_runtime = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    } else {
      greedy.assignment.assign(subjects.size(), -1);
      greedy.slot_loads.assign(slots.size(), 0);
      greedy.slot_room_counts.assign(slots.size(), 0);
    }

    json comparison = json::object();
    comparison["greedy_coloring"] = {
        {"scheduled", greedy.scheduled_count},
        {"unscheduled", static_cast<int>(subjects.size()) - greedy.scheduled_count},
        {"colors_used", greedy.colors_used},
        {"runtime_ms", greedy_runtime},
    };

    json exact_timetable = json::array();
    json exact_unscheduled = json::array();
    if (hasAlgorithm(algorithms, "backtracking")) {
      if (subjects.size() <= 10) {
        const auto start = Clock::now();
        ScheduleResult exact =
            backtrackingColoring(subjects, graph, static_cast<int>(slots.size()), room_capacity, rooms, logs);
        const double exact_runtime =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        exact_timetable = timetableToJson(exact, subjects, slots, room_capacity, rooms);
        exact_unscheduled = unscheduledToJson(exact, subjects);
        comparison["backtracking"] = {
            {"scheduled", exact.scheduled_count},
            {"unscheduled", static_cast<int>(subjects.size()) - exact.scheduled_count},
            {"colors_used", exact.colors_used},
            {"runtime_ms", exact_runtime},
            {"visited_nodes", exact.visited_nodes},
        };
      } else {
        addLog(logs, "[Backtracking] Skipped because subject count is above the cap of 10.");
        comparison["backtracking"] = {{"skipped", true}, {"reason", "subject_count_above_10"}};
      }
    }

    const double total_runtime =
        std::chrono::duration<double, std::milli>(Clock::now() - total_start).count();
    const double utilization = slots.empty() || slot_capacity == 0
                                   ? 0.0
                                   : static_cast<double>(
                                         std::accumulate(greedy.slot_loads.begin(),
                                                         greedy.slot_loads.end(), 0)) /
                                         static_cast<double>(slots.size() * slot_capacity);

    json output = {
        {"subjects", subjectsToJson(subjects)},
        {"invalid_subjects", invalid_subjects},
        {"slots", slots},
        {"room_capacity", room_capacity},
        {"rooms", rooms},
        {"slot_capacity", slot_capacity},
        {"conflict_edges", edgesToJson(edges, subjects)},
        {"timetable", timetableToJson(greedy, subjects, slots, room_capacity, rooms)},
        {"unscheduled_subjects", unscheduledToJson(greedy, subjects)},
        {"exact_timetable", exact_timetable},
        {"exact_unscheduled_subjects", exact_unscheduled},
        {"metrics",
         {
             {"subjects", subjects.size()},
             {"conflicts", edges.size()},
             {"scheduled", greedy.scheduled_count},
             {"unscheduled", static_cast<int>(subjects.size()) - greedy.scheduled_count},
             {"colors_used", greedy.colors_used},
             {"utilization", utilization},
             {"runtime_ms", total_runtime},
         }},
        {"comparison", comparison},
        {"logs", logs},
    };

    std::cout << output.dump(2) << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    json error = {{"error", ex.what()}};
    std::cerr << error.dump() << std::endl;
    return 1;
  }
}
