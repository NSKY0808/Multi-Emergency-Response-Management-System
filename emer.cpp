#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr int GRAPH_INFINITY = std::numeric_limits<int>::max();
constexpr int MAX_EMERGENCIES = 2048;

enum class EmergencyStatus {
    Pending = 0,
    Assigned = 1,
    Handled = 2
};

struct Emergency {
    int id = 0;
    std::string reporter;
    std::string area;
    std::string type;
    int severity = 0;
    long long timestamp = 0;
    std::string assigned_team;
    EmergencyStatus status = EmergencyStatus::Pending;
    int node_index = 0;
    int eta_units = -1;
    std::string description;
    long long resolved_timestamp = 0;

    std::string status_string() const {
        switch (status) {
            case EmergencyStatus::Pending: return "PENDING";
            case EmergencyStatus::Assigned: return "ASSIGNED";
            case EmergencyStatus::Handled: return "HANDLED";
        }
        return "UNKNOWN";
    }
};

struct Officer {
    std::string id;
    std::string password;
};

struct Team {
    std::string name;
    int node_index = 0;
    bool is_available = true;
    int emergencies_handled = 0;
    double total_response_time = 0.0;
};

struct Action {
    int emergency_id = -1;
    std::string team_name;
    bool assignment_state = true;
};

struct TrieNode {
    std::unique_ptr<TrieNode> children[27];
    bool is_end = false;

    static int index_for(char c) {
        if (c == ' ') return 26;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c >= 'a' && c <= 'z') return c - 'a';
        return -1;
    }
};

class AreaTrie {
public:
    void insert(const std::string& text) {
        TrieNode* node = &root_;
        for (char c : text) {
            int idx = TrieNode::index_for(c);
            if (idx < 0) continue;
            if (!node->children[idx]) {
                node->children[idx] = std::make_unique<TrieNode>();
            }
            node = node->children[idx].get();
        }
        node->is_end = true;
    }

    bool search(const std::string& text) const {
        const TrieNode* node = &root_;
        for (char c : text) {
            int idx = TrieNode::index_for(c);
            if (idx < 0) continue;
            if (!node->children[idx]) return false;
            node = node->children[idx].get();
        }
        return node && node->is_end;
    }

private:
    TrieNode root_;
};

struct Edge {
    int to = 0;
    int weight = 0;
};

struct GraphNode {
    std::string name;
    std::vector<Edge> edges;
};

class CityGraph {
public:
    explicit CityGraph(int vertices) : nodes_(vertices) {}

    int vertex_count() const {
        return static_cast<int>(nodes_.size());
    }

    void set_name(int idx, const std::string& name) {
        if (idx >= 0 && idx < vertex_count()) nodes_[idx].name = name;
    }

    const GraphNode& node(int idx) const {
        return nodes_[idx];
    }

    void add_edge(int a, int b, int weight) {
        if (a < 0 || b < 0 || a >= vertex_count() || b >= vertex_count()) return;
        nodes_[a].edges.push_back({b, weight});
        nodes_[b].edges.push_back({a, weight});
    }

    void dijkstra(int source, std::vector<int>& dist, std::vector<int>& parent) const {
        dist.assign(vertex_count(), GRAPH_INFINITY);
        parent.assign(vertex_count(), -1);
        if (source < 0 || source >= vertex_count()) return;

        using NodeDistance = std::pair<int, int>;
        std::priority_queue<NodeDistance, std::vector<NodeDistance>, std::greater<NodeDistance>> pq;
        dist[source] = 0;
        pq.push({0, source});

        while (!pq.empty()) {
            auto [current_distance, current] = pq.top();
            pq.pop();
            if (current_distance > dist[current]) continue;

            for (const auto& edge : nodes_[current].edges) {
                if (dist[current] != GRAPH_INFINITY && dist[current] + edge.weight < dist[edge.to]) {
                    dist[edge.to] = dist[current] + edge.weight;
                    parent[edge.to] = current;
                    pq.push({dist[edge.to], edge.to});
                }
            }
        }
    }

    std::vector<int> shortest_path(int source, int destination, int& distance) const {
        std::vector<int> dist;
        std::vector<int> parent;
        dijkstra(source, dist, parent);
        distance = (destination >= 0 && destination < static_cast<int>(dist.size())) ? dist[destination] : GRAPH_INFINITY;
        std::vector<int> path;
        if (distance == GRAPH_INFINITY) return path;

        for (int current = destination; current != -1; current = parent[current]) {
            path.push_back(current);
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

private:
    std::vector<GraphNode> nodes_;
};

class SegmentTree {
public:
    void initialize(int n) {
        size_ = 1;
        while (size_ < n) size_ <<= 1;
        tree_.assign(size_ * 2, 0);
    }

    void set_value(int idx, int value) {
        if (idx < 0 || idx >= size_) return;
        idx += size_;
        tree_[idx] = value;
        for (idx /= 2; idx >= 1; idx /= 2) {
            tree_[idx] = std::max(tree_[idx * 2], tree_[idx * 2 + 1]);
        }
    }

    int max_value() const {
        return tree_.empty() ? 0 : tree_[1];
    }

private:
    int size_ = 1;
    std::vector<int> tree_;
};

long long current_timestamp() {
    return static_cast<long long>(std::time(nullptr));
}

std::string to_lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string trim(std::string text) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](unsigned char c) { return !is_space(c); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](unsigned char c) { return !is_space(c); }).base(), text.end());
    return text;
}

std::string sanitize_field(std::string value) {
    std::replace(value.begin(), value.end(), '|', '/');
    return trim(value);
}

std::string format_timestamp(long long ts) {
    if (ts <= 0) return "-";
    std::time_t raw = static_cast<std::time_t>(ts);
    std::tm tm_value {};
    localtime_s(&tm_value, &raw);
    std::ostringstream oss;
    oss << std::put_time(&tm_value, "%d %b %Y %H:%M");
    return oss.str();
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow_text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, narrow_text.data(), size, nullptr, nullptr);
    if (!narrow_text.empty() && narrow_text.back() == '\0') narrow_text.pop_back();
    return narrow_text;
}

class EmergencySystem {
public:
    EmergencySystem() : graph_(10) {}

    void initialize() {
        root_dir_ = executable_directory();
        build_city();
        build_defaults();
        severity_tree_.initialize(MAX_EMERGENCIES);
        load_teams();
        load_emergencies();
        load_history();
        rebuild_pending_heap();
    }

    const std::vector<std::string>& areas() const { return areas_; }
    const std::vector<std::string>& emergency_types() const { return emergency_types_; }
    const std::vector<Emergency>& emergencies() const { return emergencies_; }
    const std::vector<Emergency>& history_entries() const { return history_entries_; }

    std::vector<Team> teams_sorted() const {
        std::vector<Team> result;
        result.reserve(team_map_.size());
        for (const auto& pair : team_map_) result.push_back(pair.second);
        std::sort(result.begin(), result.end(), [](const Team& a, const Team& b) {
            return a.name < b.name;
        });
        return result;
    }

    bool add_emergency(const std::string& reporter, const std::string& area, const std::string& type,
                       int severity, const std::string& description, std::string& feedback) {
        if (emergencies_.size() >= MAX_EMERGENCIES) {
            feedback = "Emergency capacity reached.";
            return false;
        }

        std::string clean_area = canonical_area(area);
        if (clean_area.empty() || !area_trie_.search(clean_area)) {
            feedback = "Area is not part of the configured city map.";
            return false;
        }

        Emergency e;
        e.id = next_emergency_id();
        e.reporter = reporter.empty() ? "Anonymous Citizen" : sanitize_field(reporter);
        e.area = clean_area;
        e.type = sanitize_field(type);
        e.severity = std::max(1, std::min(10, severity));
        e.timestamp = current_timestamp();
        e.status = EmergencyStatus::Pending;
        e.node_index = area_index(clean_area);
        e.description = sanitize_field(description);

        emergencies_.push_back(e);
        severity_tree_.set_value(static_cast<int>(emergencies_.size()) - 1, e.severity);
        rebuild_pending_heap();
        persist_all();

        feedback = "Emergency #" + std::to_string(e.id) + " registered for " + e.area + ".";
        return true;
    }

    bool assign_next_emergency(std::string& feedback) {
        if (pending_heap_.empty()) {
            feedback = "No pending emergencies waiting for assignment.";
            return false;
        }

        int emergency_index = pending_heap_.top();
        pending_heap_.pop();
        if (emergency_index < 0 || emergency_index >= static_cast<int>(emergencies_.size())) {
            feedback = "Priority queue was out of sync.";
            rebuild_pending_heap();
            return false;
        }

        Emergency& emergency = emergencies_[emergency_index];
        if (emergency.status != EmergencyStatus::Pending) {
            feedback = "Selected emergency is no longer pending.";
            rebuild_pending_heap();
            return false;
        }

        int best_distance = GRAPH_INFINITY;
        Team* best_team = find_nearest_available_team(emergency.node_index, best_distance);
        if (!best_team) {
            feedback = "All teams are currently busy.";
            rebuild_pending_heap();
            return false;
        }

        emergency.assigned_team = best_team->name;
        emergency.status = EmergencyStatus::Assigned;
        emergency.eta_units = best_distance == GRAPH_INFINITY ? -1 : best_distance;
        best_team->is_available = false;
        severity_tree_.set_value(emergency_index, 0);

        undo_stack_.push_back({emergency.id, best_team->name, true});
        redo_stack_.clear();
        append_history(emergency);
        persist_all();
        rebuild_pending_heap();

        feedback = best_team->name + " dispatched to Emergency #" + std::to_string(emergency.id) +
                   " with ETA " + (emergency.eta_units < 0 ? std::string("N/A") : std::to_string(emergency.eta_units)) + ".";
        return true;
    }

    bool mark_handled(int emergency_id, std::string& feedback) {
        Emergency* emergency = find_emergency_by_id(emergency_id);
        if (!emergency) {
            feedback = "Emergency not found.";
            return false;
        }
        if (emergency->status != EmergencyStatus::Assigned) {
            feedback = "Only assigned emergencies can be marked handled.";
            return false;
        }

        Team* team = find_team(emergency->assigned_team);
        if (team) {
            team->is_available = true;
            team->emergencies_handled += 1;
            team->total_response_time += static_cast<double>(std::max(1, emergency->eta_units < 0 ? 10 : emergency->eta_units));
        }

        emergency->status = EmergencyStatus::Handled;
        emergency->resolved_timestamp = current_timestamp();
        severity_tree_.set_value(index_of_id(emergency_id), 0);
        undo_stack_.clear();
        redo_stack_.clear();
        append_history(*emergency);
        persist_all();
        rebuild_pending_heap();

        feedback = "Emergency #" + std::to_string(emergency_id) + " marked as handled.";
        return true;
    }

    bool escalate_severity(int emergency_id, std::string& feedback) {
        int index = index_of_id(emergency_id);
        if (index < 0) {
            feedback = "Emergency not found.";
            return false;
        }

        Emergency& emergency = emergencies_[index];
        if (emergency.severity >= 10) {
            feedback = "Severity is already at the maximum.";
            return false;
        }

        emergency.severity += 1;
        if (emergency.status == EmergencyStatus::Pending) {
            severity_tree_.set_value(index, emergency.severity);
            rebuild_pending_heap();
        }
        persist_all();

        feedback = "Emergency #" + std::to_string(emergency_id) + " severity raised to " + std::to_string(emergency.severity) + ".";
        return true;
    }

    bool undo_assignment(std::string& feedback) {
        if (undo_stack_.empty()) {
            feedback = "Nothing to undo.";
            return false;
        }

        Action action = undo_stack_.back();
        undo_stack_.pop_back();
        Emergency* emergency = find_emergency_by_id(action.emergency_id);
        Team* team = find_team(action.team_name);
        if (!emergency || !team) {
            feedback = "Undo history is no longer valid.";
            return false;
        }

        emergency->status = EmergencyStatus::Pending;
        emergency->assigned_team.clear();
        emergency->eta_units = -1;
        team->is_available = true;
        severity_tree_.set_value(index_of_id(action.emergency_id), emergency->severity);
        redo_stack_.push_back({action.emergency_id, action.team_name, false});
        persist_all();
        rebuild_pending_heap();

        feedback = "Assignment for Emergency #" + std::to_string(action.emergency_id) + " was undone.";
        return true;
    }

    bool redo_assignment(std::string& feedback) {
        if (redo_stack_.empty()) {
            feedback = "Nothing to redo.";
            return false;
        }

        Action action = redo_stack_.back();
        redo_stack_.pop_back();
        Emergency* emergency = find_emergency_by_id(action.emergency_id);
        Team* team = find_team(action.team_name);
        if (!emergency || !team) {
            feedback = "Redo history is no longer valid.";
            return false;
        }
        if (!team->is_available || emergency->status != EmergencyStatus::Pending) {
            feedback = "Redo cannot be completed because the team or emergency state changed.";
            return false;
        }

        std::vector<int> dist;
        std::vector<int> parent;
        graph_.dijkstra(team->node_index, dist, parent);

        emergency->status = EmergencyStatus::Assigned;
        emergency->assigned_team = team->name;
        emergency->eta_units = dist[emergency->node_index] == GRAPH_INFINITY ? -1 : dist[emergency->node_index];
        team->is_available = false;
        severity_tree_.set_value(index_of_id(action.emergency_id), 0);
        undo_stack_.push_back({action.emergency_id, action.team_name, true});
        persist_all();
        rebuild_pending_heap();

        feedback = "Assignment restored for Emergency #" + std::to_string(action.emergency_id) + ".";
        return true;
    }

    bool toggle_team(const std::string& team_name, std::string& feedback) {
        Team* team = find_team(team_name);
        if (!team) {
            feedback = "Team not found.";
            return false;
        }
        team->is_available = !team->is_available;
        persist_all();
        rebuild_pending_heap();
        feedback = team->name + (team->is_available ? " is now available." : " is now marked busy.");
        return true;
    }

    bool route_for_emergency(int emergency_id, std::string& route_text) const {
        const Emergency* emergency = find_emergency_by_id_const(emergency_id);
        if (!emergency) {
            route_text = "Emergency not found.";
            return false;
        }
        if (emergency->assigned_team.empty()) {
            route_text = "Assign a team first to show a dispatch route.";
            return false;
        }

        auto team_it = team_map_.find(emergency->assigned_team);
        if (team_it == team_map_.end()) {
            route_text = "Assigned team record is missing.";
            return false;
        }

        int distance = GRAPH_INFINITY;
        std::vector<int> path = graph_.shortest_path(team_it->second.node_index, emergency->node_index, distance);
        if (path.empty()) {
            route_text = "No valid route exists for the selected emergency.";
            return false;
        }

        std::ostringstream oss;
        oss << team_it->second.name << " route (" << distance << " units): ";
        for (size_t i = 0; i < path.size(); ++i) {
            oss << graph_.node(path[i]).name;
            if (i + 1 < path.size()) oss << " -> ";
        }
        route_text = oss.str();
        return true;
    }

    bool export_report(std::string& output_path) const {
        output_path = join_path(root_dir_, "project_report.txt");
        std::ofstream out(output_path);
        if (!out.is_open()) return false;

        out << "ADVANCED DATA STRUCTURES EMERGENCY COMMAND CENTER\n";
        out << "Generated: " << format_timestamp(current_timestamp()) << "\n\n";
        out << "Core data structures used:\n";
        out << "- Trie for area validation\n";
        out << "- Unordered maps for officer and team management\n";
        out << "- Weighted graph + Dijkstra for routing\n";
        out << "- Segment tree for max pending severity tracking\n";
        out << "- Priority queue for dispatch ordering\n";
        out << "- Undo/redo stacks for assignment recovery\n\n";
        out << "KPI snapshot:\n";
        out << "Total emergencies: " << emergencies_.size() << "\n";
        out << "Pending: " << pending_count() << "\n";
        out << "Assigned: " << assigned_count() << "\n";
        out << "Handled: " << handled_count() << "\n";
        out << "Max pending severity: " << severity_tree_.max_value() << "\n";
        out << "Critical hotspot: " << hotspot_area() << "\n";
        out << "Most active team: " << busiest_team() << "\n\n";

        out << "Emergency distribution by type:\n";
        for (const auto& pair : incidents_by_type()) {
            out << "- " << pair.first << ": " << pair.second << "\n";
        }

        out << "\nTeam scorecard:\n";
        for (const Team& team : teams_sorted()) {
            double avg = team.emergencies_handled > 0 ? team.total_response_time / team.emergencies_handled : 0.0;
            out << "- " << team.name << " | Base: " << graph_.node(team.node_index).name
                << " | Available: " << (team.is_available ? "Yes" : "No")
                << " | Handled: " << team.emergencies_handled
                << " | Avg Response: " << std::fixed << std::setprecision(2) << avg << "\n";
        }

        out << "\nLatest emergency log:\n";
        std::vector<Emergency> sorted = emergencies_;
        std::sort(sorted.begin(), sorted.end(), [](const Emergency& a, const Emergency& b) {
            return a.timestamp > b.timestamp;
        });
        for (size_t i = 0; i < sorted.size() && i < 12; ++i) {
            out << "- #" << sorted[i].id << " | " << sorted[i].type << " | " << sorted[i].area
                << " | Sev " << sorted[i].severity << " | " << sorted[i].status_string()
                << " | Team " << (sorted[i].assigned_team.empty() ? "-" : sorted[i].assigned_team) << "\n";
        }

        return true;
    }

    const Emergency* emergency_by_id(int id) const {
        return find_emergency_by_id_const(id);
    }

    const Team* team_by_name(const std::string& name) const {
        auto it = team_map_.find(name);
        return it == team_map_.end() ? nullptr : &it->second;
    }

    bool route_nodes_for_emergency(int emergency_id, std::vector<int>& nodes, int& distance) const {
        nodes.clear();
        distance = GRAPH_INFINITY;
        const Emergency* emergency = find_emergency_by_id_const(emergency_id);
        if (!emergency || emergency->assigned_team.empty()) return false;
        auto team_it = team_map_.find(emergency->assigned_team);
        if (team_it == team_map_.end()) return false;
        nodes = graph_.shortest_path(team_it->second.node_index, emergency->node_index, distance);
        return !nodes.empty() && distance != GRAPH_INFINITY;
    }

    std::string overview_text() const {
        std::ostringstream oss;
        oss << "Pending " << pending_count()
            << " | Assigned " << assigned_count()
            << " | Handled " << handled_count()
            << " | Max Severity " << severity_tree_.max_value();
        return oss.str();
    }

    std::string analytics_text() const {
        std::ostringstream oss;
        oss << "Hotspot: " << hotspot_area()
            << " | Busiest Team: " << busiest_team()
            << " | Avg Severity: " << std::fixed << std::setprecision(1) << average_severity();
        return oss.str();
    }

    std::string reporting_snapshot_text() const {
        std::ostringstream oss;
        oss << "Handled " << handled_count() << " | Pending " << pending_count()
            << " | Active Teams " << available_team_count() << "/" << team_map_.size()
            << " | Hotspot " << hotspot_area();
        return oss.str();
    }

    std::string incident_mix_text() const {
        auto mix = incidents_by_type();
        if (mix.empty()) return "No incident mix yet.";

        std::ostringstream oss;
        oss << "Incident Mix: ";
        for (size_t i = 0; i < mix.size() && i < 4; ++i) {
            if (i > 0) oss << " | ";
            oss << mix[i].first << ' ' << mix[i].second;
        }
        return oss.str();
    }

private:
    struct EmergencyComparator {
        const std::vector<Emergency>* emergencies = nullptr;

        bool operator()(int lhs, int rhs) const {
            const Emergency& a = (*emergencies)[lhs];
            const Emergency& b = (*emergencies)[rhs];
            if (a.severity != b.severity) return a.severity < b.severity;
            return a.timestamp > b.timestamp;
        }
    };

    std::string executable_directory() const {
        char buffer[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
        std::string path(buffer, buffer + len);
        size_t slash = path.find_last_of("\\/");
        return slash == std::string::npos ? "." : path.substr(0, slash);
    }

    std::string join_path(const std::string& base, const std::string& file) const {
        if (base.empty()) return file;
        return base + "\\" + file;
    }

    void build_city() {
        areas_ = {"Baner", "Aundh", "Kothrud", "Shivajinagar", "Kharadi",
                  "Hadapsar", "Wakad", "Dhanori", "Hinjewadi", "Viman Nagar"};
        emergency_types_ = {"Fire", "Medical", "Accident", "Flood", "Gas Leak",
                            "Power Outage", "Structural Damage", "Other"};

        for (size_t i = 0; i < areas_.size(); ++i) {
            area_trie_.insert(areas_[i]);
            graph_.set_name(static_cast<int>(i), areas_[i]);
        }

        graph_.add_edge(0, 1, 9);
        graph_.add_edge(0, 6, 8);
        graph_.add_edge(6, 8, 7);
        graph_.add_edge(1, 3, 7);
        graph_.add_edge(3, 2, 10);
        graph_.add_edge(3, 4, 11);
        graph_.add_edge(4, 9, 5);
        graph_.add_edge(4, 5, 8);
        graph_.add_edge(5, 7, 9);
        graph_.add_edge(7, 9, 6);
        graph_.add_edge(1, 7, 15);
        graph_.add_edge(2, 5, 14);
    }

    void build_defaults() {
        officer_map_["1001"] = {"1001", "alpha"};
        officer_map_["1002"] = {"1002", "beta"};
        officer_map_["1003"] = {"1003", "gamma"};

        add_team_if_missing("TeamAlpha", 6);
        add_team_if_missing("TeamBeta", 2);
        add_team_if_missing("TeamGamma", 4);
        add_team_if_missing("TeamDelta", 9);
    }

    void add_team_if_missing(const std::string& name, int node_index) {
        if (team_map_.find(name) == team_map_.end()) {
            team_map_[name] = {name, node_index, true, 0, 0.0};
        }
    }

    void load_emergencies() {
        std::ifstream file(join_path(root_dir_, "emergencies.txt"));
        if (!file.is_open()) return;

        emergencies_.clear();
        std::string line;
        while (std::getline(file, line)) {
            if (trim(line).empty()) continue;
            std::stringstream ss(line);
            std::vector<std::string> fields;
            std::string field;
            while (std::getline(ss, field, '|')) fields.push_back(field);
            if (fields.size() < 8) continue;

            Emergency e;
            try {
                e.id = std::stoi(fields[0]);
                e.area = canonical_area(fields[1]);
                e.type = fields[2];
                e.severity = std::stoi(fields[3]);
                e.timestamp = std::stoll(fields[4]);
                e.assigned_team = fields[5];
                e.status = static_cast<EmergencyStatus>(std::stoi(fields[6]));
                e.node_index = std::stoi(fields[7]);
                if (fields.size() > 8) e.reporter = fields[8];
                if (fields.size() > 9) e.description = fields[9];
                if (fields.size() > 10) e.eta_units = std::stoi(fields[10]);
                if (fields.size() > 11) e.resolved_timestamp = std::stoll(fields[11]);
            } catch (...) {
                continue;
            }

            if (e.reporter.empty()) e.reporter = "Legacy Citizen";
            if (e.area.empty()) e.area = graph_.node(std::max(0, std::min(graph_.vertex_count() - 1, e.node_index))).name;
            emergencies_.push_back(e);
        }

        severity_tree_.initialize(MAX_EMERGENCIES);
        for (size_t i = 0; i < emergencies_.size(); ++i) {
            severity_tree_.set_value(static_cast<int>(i), emergencies_[i].status == EmergencyStatus::Pending ? emergencies_[i].severity : 0);
        }
    }

    void save_emergencies() const {
        std::ofstream file(join_path(root_dir_, "emergencies.txt"));
        if (!file.is_open()) return;
        for (const Emergency& e : emergencies_) {
            file << e.id << '|'
                 << sanitize_field(e.area) << '|'
                 << sanitize_field(e.type) << '|'
                 << e.severity << '|'
                 << e.timestamp << '|'
                 << sanitize_field(e.assigned_team) << '|'
                 << static_cast<int>(e.status) << '|'
                 << e.node_index << '|'
                 << sanitize_field(e.reporter) << '|'
                 << sanitize_field(e.description) << '|'
                 << e.eta_units << '|'
                 << e.resolved_timestamp << '\n';
        }
    }

    void load_teams() {
        std::ifstream file(join_path(root_dir_, "teams.txt"));
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (trim(line).empty()) continue;
            std::stringstream ss(line);
            std::vector<std::string> fields;
            std::string field;
            while (std::getline(ss, field, '|')) fields.push_back(field);
            if (fields.size() < 5) continue;

            try {
                std::string name = fields[0];
                add_team_if_missing(name, std::stoi(fields[1]));
                Team& team = team_map_[name];
                team.node_index = std::stoi(fields[1]);
                team.is_available = std::stoi(fields[2]) == 1;
                team.emergencies_handled = std::stoi(fields[3]);
                team.total_response_time = std::stod(fields[4]);
            } catch (...) {
                continue;
            }
        }
    }

    void save_teams() const {
        std::ofstream file(join_path(root_dir_, "teams.txt"));
        if (!file.is_open()) return;
        std::vector<Team> ordered = teams_sorted();
        for (const Team& team : ordered) {
            file << sanitize_field(team.name) << '|'
                 << team.node_index << '|'
                 << (team.is_available ? 1 : 0) << '|'
                 << team.emergencies_handled << '|'
                 << team.total_response_time << '\n';
        }
    }

    void load_history() {
        history_entries_.clear();
        std::ifstream file(join_path(root_dir_, "history.txt"));
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (trim(line).empty()) continue;
            std::stringstream ss(line);
            std::vector<std::string> fields;
            std::string field;
            while (std::getline(ss, field, '|')) fields.push_back(field);
            if (fields.size() < 8) continue;

            Emergency e;
            try {
                e.id = std::stoi(fields[0]);
                e.area = fields[1];
                e.type = fields[2];
                e.severity = std::stoi(fields[3]);
                e.timestamp = std::stoll(fields[4]);
                e.assigned_team = fields[5];
                e.status = static_cast<EmergencyStatus>(std::stoi(fields[6]));
                e.node_index = std::stoi(fields[7]);
                if (fields.size() > 8) e.reporter = fields[8];
                if (fields.size() > 9) e.description = fields[9];
                if (fields.size() > 10) e.eta_units = std::stoi(fields[10]);
                if (fields.size() > 11) e.resolved_timestamp = std::stoll(fields[11]);
            } catch (...) {
                continue;
            }
            history_entries_.push_back(e);
        }
    }

    void append_history(const Emergency& e) {
        std::ofstream file(join_path(root_dir_, "history.txt"), std::ios::app);
        if (!file.is_open()) return;
        file << e.id << '|'
             << sanitize_field(e.area) << '|'
             << sanitize_field(e.type) << '|'
             << e.severity << '|'
             << e.timestamp << '|'
             << sanitize_field(e.assigned_team) << '|'
             << static_cast<int>(e.status) << '|'
             << e.node_index << '|'
             << sanitize_field(e.reporter) << '|'
             << sanitize_field(e.description) << '|'
             << e.eta_units << '|'
             << e.resolved_timestamp << '\n';
        history_entries_.push_back(e);
    }

    void persist_all() const {
        save_emergencies();
        save_teams();
    }

    void rebuild_pending_heap() {
        pending_heap_ = std::priority_queue<int, std::vector<int>, EmergencyComparator>(EmergencyComparator{&emergencies_});
        for (size_t i = 0; i < emergencies_.size(); ++i) {
            if (emergencies_[i].status == EmergencyStatus::Pending) {
                pending_heap_.push(static_cast<int>(i));
            }
        }
    }

    int next_emergency_id() const {
        int max_id = 0;
        for (const Emergency& e : emergencies_) max_id = std::max(max_id, e.id);
        return max_id + 1;
    }

    std::string canonical_area(const std::string& area) const {
        std::string normalized = to_lower(trim(area));
        for (const std::string& candidate : areas_) {
            if (to_lower(candidate) == normalized) return candidate;
        }
        return std::string();
    }

    int area_index(const std::string& area) const {
        for (int i = 0; i < graph_.vertex_count(); ++i) {
            if (graph_.node(i).name == area) return i;
        }
        return 0;
    }

    Team* find_team(const std::string& name) {
        auto it = team_map_.find(name);
        return it == team_map_.end() ? nullptr : &it->second;
    }

    Emergency* find_emergency_by_id(int id) {
        for (Emergency& e : emergencies_) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    const Emergency* find_emergency_by_id_const(int id) const {
        for (const Emergency& e : emergencies_) {
            if (e.id == id) return &e;
        }
        return nullptr;
    }

    int index_of_id(int id) const {
        for (size_t i = 0; i < emergencies_.size(); ++i) {
            if (emergencies_[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    Team* find_nearest_available_team(int target_node, int& best_distance) {
        Team* best = nullptr;
        best_distance = GRAPH_INFINITY;
        for (auto& pair : team_map_) {
            Team& team = pair.second;
            if (!team.is_available) continue;
            std::vector<int> dist;
            std::vector<int> parent;
            graph_.dijkstra(team.node_index, dist, parent);
            if (target_node >= 0 && target_node < static_cast<int>(dist.size()) && dist[target_node] < best_distance) {
                best_distance = dist[target_node];
                best = &team;
            }
        }
        return best;
    }

    int pending_count() const {
        return std::count_if(emergencies_.begin(), emergencies_.end(), [](const Emergency& e) {
            return e.status == EmergencyStatus::Pending;
        });
    }

    int assigned_count() const {
        return std::count_if(emergencies_.begin(), emergencies_.end(), [](const Emergency& e) {
            return e.status == EmergencyStatus::Assigned;
        });
    }

    int handled_count() const {
        return std::count_if(emergencies_.begin(), emergencies_.end(), [](const Emergency& e) {
            return e.status == EmergencyStatus::Handled;
        });
    }

    int available_team_count() const {
        return std::count_if(team_map_.begin(), team_map_.end(), [](const auto& pair) {
            return pair.second.is_available;
        });
    }

    double average_severity() const {
        if (emergencies_.empty()) return 0.0;
        int total = 0;
        for (const Emergency& e : emergencies_) total += e.severity;
        return static_cast<double>(total) / static_cast<double>(emergencies_.size());
    }

    std::string hotspot_area() const {
        if (emergencies_.empty()) return "None";
        std::unordered_map<std::string, int> counts;
        for (const Emergency& e : emergencies_) counts[e.area] += 1;
        auto best = std::max_element(counts.begin(), counts.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        });
        return best == counts.end() ? "None" : best->first;
    }

    std::string busiest_team() const {
        if (team_map_.empty()) return "None";
        auto best = std::max_element(team_map_.begin(), team_map_.end(), [](const auto& a, const auto& b) {
            return a.second.emergencies_handled < b.second.emergencies_handled;
        });
        return best == team_map_.end() ? "None" : best->first;
    }

    std::vector<std::pair<std::string, int>> incidents_by_type() const {
        std::unordered_map<std::string, int> counts;
        for (const Emergency& e : emergencies_) counts[e.type] += 1;
        std::vector<std::pair<std::string, int>> result(counts.begin(), counts.end());
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second) return a.second > b.second;
            return a.first < b.first;
        });
        return result;
    }

    std::string root_dir_;
    AreaTrie area_trie_;
    CityGraph graph_;
    SegmentTree severity_tree_;
    std::vector<std::string> areas_;
    std::vector<std::string> emergency_types_;
    std::vector<Emergency> emergencies_;
    std::vector<Emergency> history_entries_;
    std::unordered_map<std::string, Officer> officer_map_;
    std::unordered_map<std::string, Team> team_map_;
    std::priority_queue<int, std::vector<int>, EmergencyComparator> pending_heap_{EmergencyComparator{&emergencies_}};
    std::vector<Action> undo_stack_;
    std::vector<Action> redo_stack_;
};

EmergencySystem g_system;

constexpr COLORREF COLOR_BASE = RGB(243, 246, 248);
constexpr COLORREF COLOR_PANEL = RGB(255, 255, 255);
constexpr COLORREF COLOR_TEXT = RGB(36, 43, 49);
constexpr COLORREF COLOR_CONTROL = RGB(210, 93, 64);
constexpr COLORREF COLOR_CONTROL_SOFT = RGB(255, 236, 231);
constexpr COLORREF COLOR_REPORT = RGB(25, 132, 140);
constexpr COLORREF COLOR_REPORT_SOFT = RGB(227, 247, 248);

enum class PageType {
    ControlRoom = 0,
    Reporting = 1
};

struct MapPoint {
    int x;
    int y;
};

std::array<MapPoint, 10> g_map_points = {{
    {150, 110}, {250, 130}, {305, 215}, {395, 170}, {520, 205},
    {560, 320}, {155, 245}, {440, 95}, {90, 330}, {650, 145}
}};

const std::array<std::pair<int, int>, 12> g_map_edges = {{
    {0, 1}, {0, 6}, {6, 8}, {1, 3}, {3, 2}, {3, 4},
    {4, 9}, {4, 5}, {5, 7}, {7, 9}, {1, 7}, {2, 5}
}};

HFONT g_title_font = nullptr;
HFONT g_body_font = nullptr;
HFONT g_small_font = nullptr;
HFONT g_mono_font = nullptr;
HBRUSH g_base_brush = nullptr;
HBRUSH g_panel_brush = nullptr;

HWND g_hwnd = nullptr;
HWND g_page_control = nullptr;
HWND g_page_reporting = nullptr;
HWND g_nav_control = nullptr;
HWND g_nav_reporting = nullptr;
HWND g_input_reporter = nullptr;
HWND g_input_area = nullptr;
HWND g_input_type = nullptr;
HWND g_input_severity = nullptr;
HWND g_input_description = nullptr;
HWND g_report_input_reporter = nullptr;
HWND g_report_input_area = nullptr;
HWND g_report_input_type = nullptr;
HWND g_report_input_severity = nullptr;
HWND g_report_input_description = nullptr;
HWND g_list_emergencies = nullptr;
HWND g_list_teams = nullptr;
HWND g_list_history = nullptr;
HWND g_map_view = nullptr;
HWND g_button_map_zoom_in = nullptr;
HWND g_button_map_zoom_out = nullptr;
HWND g_label_overview = nullptr;
HWND g_label_analytics = nullptr;
HWND g_label_report_title = nullptr;
HWND g_label_report_summary = nullptr;
HWND g_label_report_mix = nullptr;
HWND g_label_map_focus = nullptr;
HWND g_label_map_zoom = nullptr;
HWND g_label_map_info = nullptr;
HWND g_label_status = nullptr;

PageType g_active_page = PageType::ControlRoom;
int g_selected_emergency_id = -1;
std::string g_selected_team_name;
int g_selected_node_index = -1;
double g_map_zoom = 1.0;
int g_map_pan_x = 0;
int g_map_pan_y = 0;
bool g_map_dragging = false;
POINT g_map_last_mouse {0, 0};

enum ControlId {
    IDC_NAV_CONTROL = 50,
    IDC_NAV_REPORTING = 51,
    IDC_REPORTER = 101,
    IDC_AREA = 102,
    IDC_TYPE = 103,
    IDC_SEVERITY = 104,
    IDC_DESCRIPTION = 105,
    IDC_REPORT_PANEL_REPORTER = 106,
    IDC_REPORT_PANEL_AREA = 107,
    IDC_REPORT_PANEL_TYPE = 108,
    IDC_REPORT_PANEL_SEVERITY = 109,
    IDC_REPORT_PANEL_DESCRIPTION = 110,
    IDC_ADD = 201,
    IDC_ADD_REPORT = 211,
    IDC_MAP_ZOOM_IN = 212,
    IDC_MAP_ZOOM_OUT = 213,
    IDC_ASSIGN = 202,
    IDC_HANDLE = 203,
    IDC_ESCALATE = 204,
    IDC_UNDO = 205,
    IDC_REDO = 206,
    IDC_ROUTE = 207,
    IDC_TOGGLE_TEAM = 208,
    IDC_EXPORT = 209,
    IDC_REFRESH = 210,
    IDC_EMERGENCY_LIST = 301,
    IDC_TEAM_LIST = 302,
    IDC_HISTORY_LIST = 303,
    IDC_MAP = 304,
    IDC_OVERVIEW = 401,
    IDC_ANALYTICS = 402,
    IDC_REPORT_TITLE = 403,
    IDC_REPORT_SUMMARY = 404,
    IDC_REPORT_MIX = 405,
    IDC_STATUS = 406
};

void set_text(HWND control, const std::string& text) {
    SetWindowTextW(control, widen(text).c_str());
}

std::string get_window_text(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring buffer(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(control, buffer.data(), length + 1);
    buffer.resize(static_cast<size_t>(length));
    return narrow(buffer);
}

void add_combo_item(HWND combo, const std::string& text) {
    std::wstring wide = widen(text);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
}

COLORREF active_accent() {
    return g_active_page == PageType::ControlRoom ? COLOR_CONTROL : COLOR_REPORT;
}

COLORREF active_soft_accent() {
    return g_active_page == PageType::ControlRoom ? COLOR_CONTROL_SOFT : COLOR_REPORT_SOFT;
}

void listview_add_column(HWND list, int column, int width, const wchar_t* text) {
    LVCOLUMNW column_info {};
    column_info.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column_info.cx = width;
    column_info.pszText = const_cast<LPWSTR>(text);
    column_info.iSubItem = column;
    ListView_InsertColumn(list, column, &column_info);
}

void listview_set_item(HWND list, int row, int column, const std::string& text) {
    std::wstring wide = widen(text);
    if (column == 0) {
        LVITEMW item {};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.iSubItem = 0;
        item.pszText = wide.data();
        ListView_InsertItem(list, &item);
    } else {
        ListView_SetItemText(list, row, column, wide.data());
    }
}

int selected_row(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

void invalidate_map();
void update_map_info_panel();

int selected_emergency_id() {
    int row = selected_row(g_list_emergencies);
    if (row < 0) return -1;
    wchar_t buffer[32] {};
    ListView_GetItemText(g_list_emergencies, row, 0, buffer, 31);
    return _wtoi(buffer);
}

std::string selected_team_name() {
    int row = selected_row(g_list_teams);
    if (row < 0) return {};
    wchar_t buffer[64] {};
    ListView_GetItemText(g_list_teams, row, 0, buffer, 63);
    return narrow(buffer);
}

void sync_map_selection() {
    g_selected_emergency_id = selected_emergency_id();
    g_selected_team_name = selected_team_name();
    g_selected_node_index = -1;

    if (g_selected_emergency_id >= 0) {
        const Emergency* emergency = g_system.emergency_by_id(g_selected_emergency_id);
        if (emergency) g_selected_node_index = emergency->node_index;
    } else if (!g_selected_team_name.empty()) {
        const Team* team = g_system.team_by_name(g_selected_team_name);
        if (team) g_selected_node_index = team->node_index;
    }

    if (g_label_map_focus) {
        if (g_selected_emergency_id >= 0) {
            const Emergency* emergency = g_system.emergency_by_id(g_selected_emergency_id);
            if (emergency) {
                set_text(g_label_map_focus, "Map focus: Emergency #" + std::to_string(emergency->id) + " at " + emergency->area +
                    " [" + emergency->status_string() + "]");
            }
        } else if (!g_selected_team_name.empty()) {
            const Team* team = g_system.team_by_name(g_selected_team_name);
            if (team) {
                set_text(g_label_map_focus, "Map focus: " + team->name + " stationed at " + g_system.areas()[team->node_index]);
            }
        } else if (g_selected_node_index >= 0) {
            set_text(g_label_map_focus, "Map focus: " + g_system.areas()[g_selected_node_index]);
        } else {
            set_text(g_label_map_focus, "Map focus: click a node or select an emergency/team to highlight it.");
        }
    }
    update_map_info_panel();
    invalidate_map();
}

void invalidate_map() {
    if (g_map_view) InvalidateRect(g_map_view, nullptr, TRUE);
}

void update_map_zoom_label() {
    if (!g_label_map_zoom) return;
    std::ostringstream oss;
    oss << "Zoom " << static_cast<int>(g_map_zoom * 100.0) << "%";
    set_text(g_label_map_zoom, oss.str());
}

void update_map_info_panel() {
    if (!g_label_map_info) return;

    if (g_selected_node_index < 0 || g_selected_node_index >= static_cast<int>(g_system.areas().size())) {
        set_text(g_label_map_info, "Node info: click a location to inspect active emergencies, assigned teams, and local status.");
        return;
    }

    std::string area = g_system.areas()[g_selected_node_index];
    int active_count = 0;
    int handled_count = 0;
    int highest_severity = 0;
    std::vector<std::string> local_teams;

    for (const Emergency& emergency : g_system.emergencies()) {
        if (emergency.node_index != g_selected_node_index) continue;
        if (emergency.status == EmergencyStatus::Handled) {
            handled_count++;
        } else {
            active_count++;
            highest_severity = std::max(highest_severity, emergency.severity);
        }
    }

    for (const Team& team : g_system.teams_sorted()) {
        if (team.node_index == g_selected_node_index) {
            local_teams.push_back(team.name + (team.is_available ? " (Ready)" : " (Busy)"));
        }
    }

    std::ostringstream oss;
    oss << "Node info: " << area
        << " | Active " << active_count
        << " | Handled " << handled_count
        << " | Highest Severity " << highest_severity;
    if (!local_teams.empty()) {
        oss << " | Teams ";
        for (size_t i = 0; i < local_teams.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << local_teams[i];
        }
    }

    set_text(g_label_map_info, oss.str());
}

void clamp_map_pan() {
    if (!g_map_view) return;
    RECT rect;
    GetClientRect(g_map_view, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    int map_half_width = static_cast<int>(320 * g_map_zoom);
    int map_half_height = static_cast<int>(170 * g_map_zoom);

    int max_pan_x = std::max(0, map_half_width - width / 2 + 40);
    int max_pan_y = std::max(0, map_half_height - height / 2 + 24);

    g_map_pan_x = std::max(-max_pan_x, std::min(max_pan_x, g_map_pan_x));
    g_map_pan_y = std::max(-max_pan_y, std::min(max_pan_y, g_map_pan_y));
}

void set_map_zoom(double new_zoom) {
    g_map_zoom = std::max(0.6, std::min(2.2, new_zoom));
    clamp_map_pan();
    update_map_zoom_label();
    invalidate_map();
}

void refresh_dashboard() {
    ListView_DeleteAllItems(g_list_emergencies);
    std::vector<Emergency> ordered = g_system.emergencies();
    std::sort(ordered.begin(), ordered.end(), [](const Emergency& a, const Emergency& b) {
        if (a.status != b.status) return static_cast<int>(a.status) < static_cast<int>(b.status);
        if (a.severity != b.severity) return a.severity > b.severity;
        return a.timestamp > b.timestamp;
    });

    for (size_t i = 0; i < ordered.size(); ++i) {
        const Emergency& e = ordered[i];
        listview_set_item(g_list_emergencies, static_cast<int>(i), 0, std::to_string(e.id));
        listview_set_item(g_list_emergencies, static_cast<int>(i), 1, e.area);
        listview_set_item(g_list_emergencies, static_cast<int>(i), 2, e.type);
        listview_set_item(g_list_emergencies, static_cast<int>(i), 3, std::to_string(e.severity));
        listview_set_item(g_list_emergencies, static_cast<int>(i), 4, e.status_string());
        listview_set_item(g_list_emergencies, static_cast<int>(i), 5, e.assigned_team.empty() ? "-" : e.assigned_team);
        listview_set_item(g_list_emergencies, static_cast<int>(i), 6, e.eta_units < 0 ? "-" : std::to_string(e.eta_units));
        listview_set_item(g_list_emergencies, static_cast<int>(i), 7, e.reporter);
    }

    ListView_DeleteAllItems(g_list_teams);
    std::vector<Team> teams = g_system.teams_sorted();
    for (size_t i = 0; i < teams.size(); ++i) {
        const Team& team = teams[i];
        double avg = team.emergencies_handled > 0 ? team.total_response_time / team.emergencies_handled : 0.0;
        listview_set_item(g_list_teams, static_cast<int>(i), 0, team.name);
        listview_set_item(g_list_teams, static_cast<int>(i), 1, g_system.areas()[team.node_index]);
        listview_set_item(g_list_teams, static_cast<int>(i), 2, team.is_available ? "AVAILABLE" : "BUSY");
        listview_set_item(g_list_teams, static_cast<int>(i), 3, std::to_string(team.emergencies_handled));
        std::ostringstream avg_text;
        avg_text << std::fixed << std::setprecision(1) << avg;
        listview_set_item(g_list_teams, static_cast<int>(i), 4, avg_text.str());
    }

    ListView_DeleteAllItems(g_list_history);
    std::vector<Emergency> history = g_system.history_entries();
    std::sort(history.begin(), history.end(), [](const Emergency& a, const Emergency& b) {
        return a.timestamp > b.timestamp;
    });

    for (size_t i = 0; i < history.size() && i < 20; ++i) {
        const Emergency& e = history[i];
        listview_set_item(g_list_history, static_cast<int>(i), 0, std::to_string(e.id));
        listview_set_item(g_list_history, static_cast<int>(i), 1, format_timestamp(e.timestamp));
        listview_set_item(g_list_history, static_cast<int>(i), 2, e.status_string());
        listview_set_item(g_list_history, static_cast<int>(i), 3, e.area);
        listview_set_item(g_list_history, static_cast<int>(i), 4, e.assigned_team.empty() ? "-" : e.assigned_team);
        listview_set_item(g_list_history, static_cast<int>(i), 5, e.type);
    }

    set_text(g_label_overview, g_system.overview_text());
    set_text(g_label_analytics, g_system.analytics_text());
    set_text(g_label_report_summary, g_system.reporting_snapshot_text());
    set_text(g_label_report_mix, g_system.incident_mix_text());
    sync_map_selection();
}

void show_feedback(const std::string& text, bool success = true) {
    set_text(g_label_status, text);
    MessageBoxW(g_hwnd, widen(text).c_str(),
        success ? L"Emergency Command Center" : L"Action Needed",
        MB_OK | (success ? MB_ICONINFORMATION : MB_ICONWARNING));
}

void switch_page(PageType page) {
    g_active_page = page;
    ShowWindow(g_page_control, page == PageType::ControlRoom ? SW_SHOW : SW_HIDE);
    ShowWindow(g_page_reporting, page == PageType::Reporting ? SW_SHOW : SW_HIDE);
    SetWindowTextW(g_nav_control, page == PageType::ControlRoom ? L"> Control Room" : L"Control Room");
    SetWindowTextW(g_nav_reporting, page == PageType::Reporting ? L"> Reporting" : L"Reporting");
    InvalidateRect(g_hwnd, nullptr, TRUE);
    invalidate_map();
}

void clear_intake_form(HWND area_box, HWND type_box, HWND severity_box, HWND description_box) {
    set_text(description_box, "");
    SendMessageW(area_box, CB_SETCURSEL, 0, 0);
    SendMessageW(type_box, CB_SETCURSEL, 0, 0);
    SendMessageW(severity_box, CB_SETCURSEL, 4, 0);
}

void draw_map(HDC dc, const RECT& rect) {
    HBRUSH bg = CreateSolidBrush(RGB(249, 251, 252));
    FillRect(dc, &rect, bg);
    DeleteObject(bg);

    auto map_x = [&](int raw_x) {
        int center_x = (rect.left + rect.right) / 2;
        return center_x + g_map_pan_x + static_cast<int>((raw_x - 360) * g_map_zoom);
    };
    auto map_y = [&](int raw_y) {
        return 48 + g_map_pan_y + static_cast<int>((raw_y - 48) * g_map_zoom);
    };

    RECT title_bar = rect;
    title_bar.bottom = title_bar.top + 38;
    HBRUSH accent = CreateSolidBrush(COLOR_CONTROL_SOFT);
    FillRect(dc, &title_bar, accent);
    DeleteObject(accent);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, COLOR_CONTROL);
    SelectObject(dc, g_body_font);
    DrawTextW(dc, L"City Operations Map", -1, &title_bar, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    HPEN road_pen = CreatePen(PS_SOLID, 2, RGB(178, 188, 198));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, road_pen));
    for (const auto& edge : g_map_edges) {
        MoveToEx(dc, map_x(g_map_points[edge.first].x), map_y(g_map_points[edge.first].y + 48), nullptr);
        LineTo(dc, map_x(g_map_points[edge.second].x), map_y(g_map_points[edge.second].y + 48));
    }
    SelectObject(dc, old_pen);
    DeleteObject(road_pen);

    std::vector<int> route_nodes;
    int route_distance = GRAPH_INFINITY;
    if (g_selected_emergency_id >= 0 && g_system.route_nodes_for_emergency(g_selected_emergency_id, route_nodes, route_distance)) {
        HPEN route_pen = CreatePen(PS_SOLID, 4, COLOR_REPORT);
        HPEN route_old = static_cast<HPEN>(SelectObject(dc, route_pen));
        for (size_t i = 1; i < route_nodes.size(); ++i) {
            MoveToEx(dc, map_x(g_map_points[route_nodes[i - 1]].x), map_y(g_map_points[route_nodes[i - 1]].y + 48), nullptr);
            LineTo(dc, map_x(g_map_points[route_nodes[i]].x), map_y(g_map_points[route_nodes[i]].y + 48));
        }
        SelectObject(dc, route_old);
        DeleteObject(route_pen);
    }

    std::unordered_map<int, int> emergency_counts;
    for (const Emergency& emergency : g_system.emergencies()) {
        if (emergency.status != EmergencyStatus::Handled) {
            emergency_counts[emergency.node_index] += 1;
        }
    }

    std::vector<Team> teams = g_system.teams_sorted();
    std::unordered_map<int, int> team_counts;
    for (const Team& team : teams) team_counts[team.node_index] += 1;

    for (size_t i = 0; i < g_system.areas().size(); ++i) {
        int x = map_x(g_map_points[i].x);
        int y = map_y(g_map_points[i].y + 48);
        bool is_selected = g_selected_node_index == static_cast<int>(i);
        int node_half = static_cast<int>((is_selected ? 11 : 8) * g_map_zoom);
        node_half = std::max(node_half, 6);
        RECT node_rect {x - node_half, y - node_half, x + node_half, y + node_half};
        HBRUSH node_brush = CreateSolidBrush(is_selected ? COLOR_REPORT : RGB(84, 102, 122));
        FillRect(dc, &node_rect, node_brush);
        DeleteObject(node_brush);

        SetTextColor(dc, COLOR_TEXT);
        RECT label_rect {x - 50, y + 10, x + 70, y + 34};
        DrawTextW(dc, widen(g_system.areas()[i]).c_str(), -1, &label_rect, DT_CENTER | DT_TOP | DT_NOPREFIX);

        if (emergency_counts.contains(static_cast<int>(i))) {
            COLORREF hotspot_color = (g_selected_node_index == static_cast<int>(i)) ? RGB(181, 54, 38) : COLOR_CONTROL;
            HBRUSH hot_brush = CreateSolidBrush(hotspot_color);
            HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, hot_brush));
            int radius = std::max(18, static_cast<int>(18 * g_map_zoom));
            Ellipse(dc, x - radius, y - radius, x + radius, y + radius);
            SelectObject(dc, old_brush);
            DeleteObject(hot_brush);

            SetTextColor(dc, RGB(255, 255, 255));
            RECT sev_rect {x - 9, y - 10, x + 9, y + 10};
            DrawTextW(dc, widen(std::to_string(emergency_counts[static_cast<int>(i)])).c_str(), -1, &sev_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    int placed = 0;
    for (const Team& team : teams) {
        int x = map_x(g_map_points[team.node_index].x + 20 + (placed % 2) * 12);
        int y = map_y(g_map_points[team.node_index].y + 34 + (placed % 3) * 10);
        RECT team_rect {x - 6, y - 6, x + 6, y + 6};
        COLORREF team_color = team.is_available ? COLOR_REPORT : RGB(90, 97, 105);
        if (!g_selected_team_name.empty() && team.name == g_selected_team_name) {
            team_color = RGB(0, 92, 102);
            Rectangle(dc, x - 9, y - 9, x + 9, y + 9);
        }
        HBRUSH team_brush = CreateSolidBrush(team_color);
        FillRect(dc, &team_rect, team_brush);
        DeleteObject(team_brush);
        placed++;
    }

    SetTextColor(dc, COLOR_TEXT);
    RECT legend {rect.left + 340, rect.top + 10, rect.right - 10, rect.top + 35};
    DrawTextW(dc, L"Red circles: active emergencies   Teal line: route   Wheel: zoom   Drag: pan", -1, &legend, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    if (g_selected_node_index >= 0) {
        RECT card {rect.right - 212, rect.top + 46, rect.right - 12, rect.top + 122};
        HBRUSH card_brush = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &card, card_brush);
        DeleteObject(card_brush);
        FrameRect(dc, &card, static_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

        SetTextColor(dc, COLOR_REPORT);
        RECT header {card.left + 10, card.top + 8, card.right - 10, card.top + 28};
        DrawTextW(dc, L"Node Details", -1, &header, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(dc, COLOR_TEXT);
        std::string details = get_window_text(g_label_map_info);
        RECT body {card.left + 10, card.top + 30, card.right - 10, card.bottom - 8};
        DrawTextW(dc, widen(details).c_str(), -1, &body, DT_WORDBREAK | DT_TOP | DT_NOPREFIX);
    }
}

void create_control_page(HWND parent) {
    g_page_control = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        0, 96, 1404, 680, parent, nullptr, nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Control Room", WS_CHILD | WS_VISIBLE,
        24, 18, 300, 30, g_page_control, nullptr, nullptr, nullptr);
    g_label_overview = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        250, 18, 540, 24, g_page_control, reinterpret_cast<HMENU>(IDC_OVERVIEW), nullptr, nullptr);
    g_label_analytics = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        250, 46, 650, 22, g_page_control, reinterpret_cast<HMENU>(IDC_ANALYTICS), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Emergency Intake", WS_CHILD | WS_VISIBLE, 24, 86, 250, 22, g_page_control, nullptr, nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Reporter", WS_CHILD | WS_VISIBLE, 24, 122, 120, 18, g_page_control, nullptr, nullptr, nullptr);
    g_input_reporter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        24, 144, 250, 26, g_page_control, reinterpret_cast<HMENU>(IDC_REPORTER), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Area", WS_CHILD | WS_VISIBLE, 24, 182, 120, 18, g_page_control, nullptr, nullptr, nullptr);
    g_input_area = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        24, 204, 250, 180, g_page_control, reinterpret_cast<HMENU>(IDC_AREA), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Emergency Type", WS_CHILD | WS_VISIBLE, 24, 242, 120, 18, g_page_control, nullptr, nullptr, nullptr);
    g_input_type = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        24, 264, 250, 180, g_page_control, reinterpret_cast<HMENU>(IDC_TYPE), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Severity", WS_CHILD | WS_VISIBLE, 24, 302, 120, 18, g_page_control, nullptr, nullptr, nullptr);
    g_input_severity = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        24, 324, 250, 180, g_page_control, reinterpret_cast<HMENU>(IDC_SEVERITY), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Incident Notes", WS_CHILD | WS_VISIBLE, 24, 362, 120, 18, g_page_control, nullptr, nullptr, nullptr);
    g_input_description = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
        24, 384, 250, 118, g_page_control, reinterpret_cast<HMENU>(IDC_DESCRIPTION), nullptr, nullptr);

    CreateWindowW(L"BUTTON", L"Register Emergency", WS_CHILD | WS_VISIBLE,
        24, 520, 250, 32, g_page_control, reinterpret_cast<HMENU>(IDC_ADD), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Auto Assign", WS_CHILD | WS_VISIBLE,
        24, 560, 118, 30, g_page_control, reinterpret_cast<HMENU>(IDC_ASSIGN), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Mark Handled", WS_CHILD | WS_VISIBLE,
        156, 560, 118, 30, g_page_control, reinterpret_cast<HMENU>(IDC_HANDLE), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Escalate", WS_CHILD | WS_VISIBLE,
        24, 598, 118, 30, g_page_control, reinterpret_cast<HMENU>(IDC_ESCALATE), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Toggle Team", WS_CHILD | WS_VISIBLE,
        156, 598, 118, 30, g_page_control, reinterpret_cast<HMENU>(IDC_TOGGLE_TEAM), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Undo", WS_CHILD | WS_VISIBLE,
        24, 636, 56, 28, g_page_control, reinterpret_cast<HMENU>(IDC_UNDO), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Redo", WS_CHILD | WS_VISIBLE,
        88, 636, 56, 28, g_page_control, reinterpret_cast<HMENU>(IDC_REDO), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Route", WS_CHILD | WS_VISIBLE,
        152, 636, 56, 28, g_page_control, reinterpret_cast<HMENU>(IDC_ROUTE), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE,
        216, 636, 58, 28, g_page_control, reinterpret_cast<HMENU>(IDC_REFRESH), nullptr, nullptr);

    DWORD list_style = WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER;
    g_list_emergencies = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
        304, 96, 730, 252, g_page_control, reinterpret_cast<HMENU>(IDC_EMERGENCY_LIST), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_list_emergencies, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    listview_add_column(g_list_emergencies, 0, 50, L"ID");
    listview_add_column(g_list_emergencies, 1, 108, L"Area");
    listview_add_column(g_list_emergencies, 2, 130, L"Type");
    listview_add_column(g_list_emergencies, 3, 54, L"Sev");
    listview_add_column(g_list_emergencies, 4, 100, L"Status");
    listview_add_column(g_list_emergencies, 5, 110, L"Team");
    listview_add_column(g_list_emergencies, 6, 54, L"ETA");
    listview_add_column(g_list_emergencies, 7, 110, L"Reporter");

    g_list_teams = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
        304, 368, 420, 248, g_page_control, reinterpret_cast<HMENU>(IDC_TEAM_LIST), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_list_teams, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    listview_add_column(g_list_teams, 0, 120, L"Team");
    listview_add_column(g_list_teams, 1, 110, L"Base");
    listview_add_column(g_list_teams, 2, 90, L"State");
    listview_add_column(g_list_teams, 3, 50, L"Done");
    listview_add_column(g_list_teams, 4, 50, L"Avg");

    g_map_view = CreateWindowExW(WS_EX_CLIENTEDGE, L"EmergencyMapView", L"", WS_CHILD | WS_VISIBLE,
        744, 368, 600, 248, g_page_control, reinterpret_cast<HMENU>(IDC_MAP), nullptr, nullptr);
    g_button_map_zoom_out = CreateWindowW(L"BUTTON", L"-", WS_CHILD | WS_VISIBLE,
        1210, 626, 32, 24, g_page_control, reinterpret_cast<HMENU>(IDC_MAP_ZOOM_OUT), nullptr, nullptr);
    g_button_map_zoom_in = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE,
        1248, 626, 32, 24, g_page_control, reinterpret_cast<HMENU>(IDC_MAP_ZOOM_IN), nullptr, nullptr);
    g_label_map_zoom = CreateWindowW(L"STATIC", L"Zoom 100%", WS_CHILD | WS_VISIBLE,
        1288, 626, 90, 22, g_page_control, nullptr, nullptr, nullptr);
    g_label_map_focus = CreateWindowW(L"STATIC", L"Map focus: click a node or select an emergency/team to highlight it.",
        WS_CHILD | WS_VISIBLE, 744, 626, 460, 22, g_page_control, nullptr, nullptr, nullptr);
    g_label_map_info = CreateWindowW(L"STATIC", L"Node info: click a location to inspect active emergencies, assigned teams, and local status.",
        WS_CHILD | WS_VISIBLE, 744, 650, 634, 34, g_page_control, nullptr, nullptr, nullptr);

    for (const std::string& area : g_system.areas()) add_combo_item(g_input_area, area);
    for (const std::string& type : g_system.emergency_types()) add_combo_item(g_input_type, type);
    for (int i = 1; i <= 10; ++i) add_combo_item(g_input_severity, std::to_string(i));
    SendMessageW(g_input_area, CB_SETCURSEL, 0, 0);
    SendMessageW(g_input_type, CB_SETCURSEL, 0, 0);
    SendMessageW(g_input_severity, CB_SETCURSEL, 4, 0);
    set_text(g_input_reporter, "Control Room");
}

void create_reporting_page(HWND parent) {
    g_page_reporting = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
        0, 96, 1404, 680, parent, nullptr, nullptr, nullptr);

    g_label_report_title = CreateWindowW(L"STATIC", L"Reporting & Situation Review", WS_CHILD | WS_VISIBLE,
        24, 18, 460, 30, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_TITLE), nullptr, nullptr);
    g_label_report_summary = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        24, 56, 640, 22, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_SUMMARY), nullptr, nullptr);
    g_label_report_mix = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
        24, 84, 760, 22, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_MIX), nullptr, nullptr);

    CreateWindowW(L"BUTTON", L"Export Project Report", WS_CHILD | WS_VISIBLE,
        1180, 28, 180, 32, g_page_reporting, reinterpret_cast<HMENU>(IDC_EXPORT), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Quick Intake", WS_CHILD | WS_VISIBLE,
        860, 18, 160, 22, g_page_reporting, nullptr, nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Reporter", WS_CHILD | WS_VISIBLE,
        860, 48, 90, 18, g_page_reporting, nullptr, nullptr, nullptr);
    g_report_input_reporter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Reporting Desk", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        860, 68, 200, 24, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_PANEL_REPORTER), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Area", WS_CHILD | WS_VISIBLE,
        1074, 48, 50, 18, g_page_reporting, nullptr, nullptr, nullptr);
    g_report_input_area = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        1074, 68, 120, 200, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_PANEL_AREA), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Type", WS_CHILD | WS_VISIBLE,
        1208, 48, 50, 18, g_page_reporting, nullptr, nullptr, nullptr);
    g_report_input_type = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        1208, 68, 110, 200, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_PANEL_TYPE), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Severity", WS_CHILD | WS_VISIBLE,
        1330, 48, 60, 18, g_page_reporting, nullptr, nullptr, nullptr);
    g_report_input_severity = CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
        1330, 68, 58, 200, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_PANEL_SEVERITY), nullptr, nullptr);
    CreateWindowW(L"STATIC", L"Short note", WS_CHILD | WS_VISIBLE,
        860, 100, 90, 18, g_page_reporting, nullptr, nullptr, nullptr);
    g_report_input_description = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        860, 120, 458, 24, g_page_reporting, reinterpret_cast<HMENU>(IDC_REPORT_PANEL_DESCRIPTION), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Log Emergency", WS_CHILD | WS_VISIBLE,
        1330, 120, 110, 24, g_page_reporting, reinterpret_cast<HMENU>(IDC_ADD_REPORT), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Recent Incident Log", WS_CHILD | WS_VISIBLE,
        24, 130, 250, 22, g_page_reporting, nullptr, nullptr, nullptr);

    DWORD list_style = WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER;
    g_list_history = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", list_style,
        24, 160, 1336, 470, g_page_reporting, reinterpret_cast<HMENU>(IDC_HISTORY_LIST), nullptr, nullptr);
    ListView_SetExtendedListViewStyle(g_list_history, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    listview_add_column(g_list_history, 0, 60, L"ID");
    listview_add_column(g_list_history, 1, 170, L"Logged");
    listview_add_column(g_list_history, 2, 110, L"Status");
    listview_add_column(g_list_history, 3, 150, L"Area");
    listview_add_column(g_list_history, 4, 140, L"Team");
    listview_add_column(g_list_history, 5, 160, L"Type");

    for (const std::string& area : g_system.areas()) add_combo_item(g_report_input_area, area);
    for (const std::string& type : g_system.emergency_types()) add_combo_item(g_report_input_type, type);
    for (int i = 1; i <= 10; ++i) add_combo_item(g_report_input_severity, std::to_string(i));
    SendMessageW(g_report_input_area, CB_SETCURSEL, 0, 0);
    SendMessageW(g_report_input_type, CB_SETCURSEL, 0, 0);
    SendMessageW(g_report_input_severity, CB_SETCURSEL, 4, 0);
}

void apply_fonts(HWND hwnd) {
    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
        return TRUE;
    }, 0);
    SendMessageW(g_label_overview, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
    SendMessageW(g_label_report_title, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
    SendMessageW(g_label_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_mono_font), TRUE);
}

void handle_add_emergency(HWND reporter_box, HWND area_box, HWND type_box, HWND severity_box, HWND description_box) {
    std::string reporter = get_window_text(reporter_box);
    wchar_t text_buffer[256] {};
    GetWindowTextW(area_box, text_buffer, 255);
    std::string area = narrow(text_buffer);
    GetWindowTextW(type_box, text_buffer, 255);
    std::string type = narrow(text_buffer);
    GetWindowTextW(severity_box, text_buffer, 255);
    int severity = std::max(1, _wtoi(text_buffer));
    std::string description = get_window_text(description_box);

    std::string feedback;
    bool ok = g_system.add_emergency(reporter, area, type, severity, description, feedback);
    refresh_dashboard();
    if (ok) clear_intake_form(area_box, type_box, severity_box, description_box);
    show_feedback(feedback, ok);
}

void handle_assign() {
    std::string feedback;
    bool ok = g_system.assign_next_emergency(feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_mark_handled() {
    int id = selected_emergency_id();
    if (id < 0) {
        show_feedback("Select an emergency first.", false);
        return;
    }
    std::string feedback;
    bool ok = g_system.mark_handled(id, feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_escalate() {
    int id = selected_emergency_id();
    if (id < 0) {
        show_feedback("Select an emergency first.", false);
        return;
    }
    std::string feedback;
    bool ok = g_system.escalate_severity(id, feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_undo() {
    std::string feedback;
    bool ok = g_system.undo_assignment(feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_redo() {
    std::string feedback;
    bool ok = g_system.redo_assignment(feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_route() {
    int id = selected_emergency_id();
    if (id < 0) {
        show_feedback("Select an assigned emergency to inspect its route.", false);
        return;
    }
    std::string route_text;
    bool ok = g_system.route_for_emergency(id, route_text);
    show_feedback(route_text, ok);
}

void handle_toggle_team() {
    std::string team_name = selected_team_name();
    if (team_name.empty()) {
        show_feedback("Select a team first.", false);
        return;
    }
    std::string feedback;
    bool ok = g_system.toggle_team(team_name, feedback);
    refresh_dashboard();
    show_feedback(feedback, ok);
}

void handle_export() {
    std::string path;
    bool ok = g_system.export_report(path);
    show_feedback(ok ? "Project report exported to " + path + "." : "Could not write project report.", ok);
}

LRESULT CALLBACK MapProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(w_param);
            set_map_zoom(g_map_zoom + (delta > 0 ? 0.1 : -0.1));
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            g_map_pan_x = 0;
            g_map_pan_y = 0;
            clamp_map_pan();
            invalidate_map();
            return 0;
        }
        case WM_LBUTTONDOWN: {
            SetCapture(hwnd);
            g_map_dragging = true;
            g_map_last_mouse = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            int click_x = GET_X_LPARAM(l_param);
            int click_y = GET_Y_LPARAM(l_param);
            auto map_x = [&](int raw_x) {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int center_x = (rect.left + rect.right) / 2;
                return center_x + g_map_pan_x + static_cast<int>((raw_x - 360) * g_map_zoom);
            };
            auto map_y = [&](int raw_y) {
                return 48 + g_map_pan_y + static_cast<int>((raw_y - 48) * g_map_zoom);
            };
            bool hit_node = false;
            for (size_t i = 0; i < g_map_points.size(); ++i) {
                int dx = click_x - map_x(g_map_points[i].x);
                int dy = click_y - map_y(g_map_points[i].y + 48);
                int radius = std::max(20, static_cast<int>(22 * g_map_zoom));
                if (dx * dx + dy * dy <= radius * radius) {
                    g_selected_node_index = static_cast<int>(i);
                    g_selected_emergency_id = -1;
                    g_selected_team_name.clear();
                    if (g_label_map_focus) {
                        set_text(g_label_map_focus, "Map focus: " + g_system.areas()[i] + " selected from tactical view.");
                    }
                    update_map_info_panel();
                    invalidate_map();
                    hit_node = true;
                    break;
                }
            }
            if (!hit_node && g_label_map_focus) {
                set_text(g_label_map_focus, "Map focus: drag to pan, wheel to zoom, double-click to recenter.");
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_map_dragging && (w_param & MK_LBUTTON)) {
                int current_x = GET_X_LPARAM(l_param);
                int current_y = GET_Y_LPARAM(l_param);
                g_map_pan_x += current_x - g_map_last_mouse.x;
                g_map_pan_y += current_y - g_map_last_mouse.y;
                g_map_last_mouse = {current_x, current_y};
                clamp_map_pan();
                invalidate_map();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (GetCapture() == hwnd) ReleaseCapture();
            g_map_dragging = false;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            draw_map(dc, rect);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            g_base_brush = CreateSolidBrush(COLOR_BASE);
            g_panel_brush = CreateSolidBrush(COLOR_PANEL);
            g_title_font = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Semibold");
            g_body_font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_small_font = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_mono_font = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");

            CreateWindowW(L"STATIC", L"Advanced DS Emergency Response Platform", WS_CHILD | WS_VISIBLE,
                24, 20, 700, 30, hwnd, nullptr, nullptr, nullptr);
            CreateWindowW(L"STATIC", L"Desktop prototype before the full mobile + server rebuild", WS_CHILD | WS_VISIBLE,
                24, 52, 420, 20, hwnd, nullptr, nullptr, nullptr);

            g_nav_control = CreateWindowW(L"BUTTON", L"> Control Room", WS_CHILD | WS_VISIBLE,
                980, 24, 170, 34, hwnd, reinterpret_cast<HMENU>(IDC_NAV_CONTROL), nullptr, nullptr);
            g_nav_reporting = CreateWindowW(L"BUTTON", L"Reporting", WS_CHILD | WS_VISIBLE,
                1164, 24, 170, 34, hwnd, reinterpret_cast<HMENU>(IDC_NAV_REPORTING), nullptr, nullptr);

            create_control_page(hwnd);
            create_reporting_page(hwnd);
            g_label_status = CreateWindowW(L"STATIC", L"System ready.", WS_CHILD | WS_VISIBLE,
                24, 788, 1340, 24, hwnd, reinterpret_cast<HMENU>(IDC_STATUS), nullptr, nullptr);

            apply_fonts(hwnd);
            update_map_zoom_label();
            refresh_dashboard();
            switch_page(PageType::ControlRoom);
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(w_param)) {
                case IDC_NAV_CONTROL: switch_page(PageType::ControlRoom); return 0;
                case IDC_NAV_REPORTING: switch_page(PageType::Reporting); return 0;
                case IDC_ADD: handle_add_emergency(g_input_reporter, g_input_area, g_input_type, g_input_severity, g_input_description); return 0;
                case IDC_ADD_REPORT: handle_add_emergency(g_report_input_reporter, g_report_input_area, g_report_input_type, g_report_input_severity, g_report_input_description); return 0;
                case IDC_MAP_ZOOM_IN: set_map_zoom(g_map_zoom + 0.1); return 0;
                case IDC_MAP_ZOOM_OUT: set_map_zoom(g_map_zoom - 0.1); return 0;
                case IDC_ASSIGN: handle_assign(); return 0;
                case IDC_HANDLE: handle_mark_handled(); return 0;
                case IDC_ESCALATE: handle_escalate(); return 0;
                case IDC_UNDO: handle_undo(); return 0;
                case IDC_REDO: handle_redo(); return 0;
                case IDC_ROUTE: handle_route(); return 0;
                case IDC_TOGGLE_TEAM: handle_toggle_team(); return 0;
                case IDC_EXPORT: handle_export(); return 0;
                case IDC_REFRESH: refresh_dashboard(); set_text(g_label_status, "Dashboard refreshed."); return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = reinterpret_cast<LPNMHDR>(l_param);
            if ((hdr->hwndFrom == g_list_emergencies || hdr->hwndFrom == g_list_teams) && hdr->code == LVN_ITEMCHANGED) {
                sync_map_selection();
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkMode(dc, TRANSPARENT);
            HWND control = reinterpret_cast<HWND>(l_param);
            COLORREF text_color = COLOR_TEXT;
            if (control == g_label_overview || control == g_label_analytics) {
                text_color = COLOR_CONTROL;
            } else if (control == g_label_report_title || control == g_label_report_summary || control == g_label_report_mix) {
                text_color = COLOR_REPORT;
            }
            SetTextColor(dc, text_color);
            return reinterpret_cast<LRESULT>(g_base_brush);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(w_param);
            SetBkColor(dc, COLOR_PANEL);
            SetTextColor(dc, COLOR_TEXT);
            return reinterpret_cast<LRESULT>(g_panel_brush);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps {};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, g_base_brush);

            RECT hero = {0, 0, rect.right, 88};
            HBRUSH accent = CreateSolidBrush(active_accent());
            FillRect(dc, &hero, accent);
            DeleteObject(accent);

            RECT soft_band = {0, 88, rect.right, 96};
            HBRUSH soft = CreateSolidBrush(active_soft_accent());
            FillRect(dc, &soft_band, soft);
            DeleteObject(soft);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            DeleteObject(g_title_font);
            DeleteObject(g_body_font);
            DeleteObject(g_small_font);
            DeleteObject(g_mono_font);
            DeleteObject(g_base_brush);
            DeleteObject(g_panel_brush);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    INITCOMMONCONTROLSEX controls {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g_system.initialize();

    WNDCLASSW map_wc {};
    map_wc.style = CS_DBLCLKS;
    map_wc.lpfnWndProc = MapProc;
    map_wc.hInstance = instance;
    map_wc.lpszClassName = L"EmergencyMapView";
    map_wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    map_wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&map_wc);

    WNDCLASSW wc {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"EmergencyCommandCenterWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, L"EmergencyCommandCenterWindow", L"Emergency Command Center",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 1420, 880,
        nullptr, nullptr, instance, nullptr);

    if (!hwnd) return 0;

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
