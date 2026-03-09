/*
 * CMPE 412 – Computer Simulation – 2026-Spring
 * Term Project 01: Multi-Server Queue Simulation
 *
 * Group Members:
 *   - Kuandyk Kyrykbayev
 *   - Akhmed Nazarov
 *
 * Description:
 *   Discrete-event, minute-by-minute simulation of a call center with
 *   multiple servers.  Inter-arrival and service times are generated
 *   probabilistically from configurable cumulative-distribution tables.
 *
 * Build:
 *   g++ -std=c++17 -O2 -o simulation main.cpp
 *
 * Run:
 *   ./simulation
 */

#include <algorithm>

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>

#include <queue>
#include <sstream>
#include <string>
#include <vector>

// ──────────────────────────────────────────────
// Utility: random double in [0,1)
// ──────────────────────────────────────────────
static double randUniform() {
  return static_cast<double>(rand()) / (RAND_MAX + 1.0);
}

// ──────────────────────────────────────────────
// Probability distribution entry
// ──────────────────────────────────────────────
struct ProbEntry {
  int value;          // e.g. time in minutes
  double probability; // individual probability
};

// Sample from a probability table using CDF lookup
static int sampleFromDistribution(const std::vector<ProbEntry> &dist) {
  double r = randUniform();
  double cum = 0.0;
  for (const auto &e : dist) {
    cum += e.probability;
    if (r < cum)
      return e.value;
  }
  return dist.back().value; // fallback
}

// Compute the weighted average of a distribution
static double distributionMean(const std::vector<ProbEntry> &dist) {
  double mean = 0.0;
  for (const auto &e : dist)
    mean += e.value * e.probability;
  return mean;
}

// ──────────────────────────────────────────────
// Event types
// ──────────────────────────────────────────────
enum class EventType { ARRIVAL, SERVICE_COMPLETE };

struct Event {
  int time;
  EventType type;
  int customerId;
  int serverId; // only meaningful for SERVICE_COMPLETE
};

// Compare events for a min-heap (earliest first, SERVICE_COMPLETE before
// ARRIVAL at same time)
struct EventCmp {
  bool operator()(const Event &a, const Event &b) const {
    if (a.time != b.time)
      return a.time > b.time; // min-heap
    // At the same time: SERVICE_COMPLETE has higher priority (value 1 > 0)
    return static_cast<int>(a.type) > static_cast<int>(b.type);
  }
};

// ──────────────────────────────────────────────
// Server
// ──────────────────────────────────────────────
struct Server {
  int id;
  bool busy = false;
  int customerId = -1;
  int serviceEnd = -1;
  int totalBusyTime = 0;
};

// ──────────────────────────────────────────────
// Customer record (for statistics)
// ──────────────────────────────────────────────
struct Customer {
  int id;
  int interArrivalTime = 0; // time gap from previous customer
  int arrivalTime = -1;
  int serviceStart = -1;
  int serviceEnd = -1;
  int serviceTime = 0;
  int waitingTime = 0; // serviceStart - arrivalTime
  int systemTime = 0;  // serviceEnd   - arrivalTime
  int assignedServer = -1;
};

// ──────────────────────────────────────────────
// Simulation parameters
// ──────────────────────────────────────────────
struct SimParams {
  std::string label;
  int numServers;
  int simDuration;    // total minutes
  int totalCustomers; // how many arrivals to generate
  std::vector<ProbEntry> interArrivalDist;
  std::vector<ProbEntry> serviceDist;
};

// ──────────────────────────────────────────────
// Simulation results (KPIs) — matching LAB-PS-01/02
// ──────────────────────────────────────────────
struct SimResults {
  std::string label;
  int numServers;
  int totalCustomers;
  int customersServed;

  // KPI 1: Average waiting time per customer (all customers)
  double avgWaitingTime;
  // KPI 2: P(wait) — fraction of customers who had to wait
  double probWait;
  // KPI 3: Probability of idle server (per server + overall)
  std::vector<double> serverIdleProb;
  double overallIdleProb;
  // KPI 4: Average service time
  double avgServiceTime;
  // KPI 5: Average time between arrivals
  double avgInterArrival;
  // KPI 6: Average waiting time of those who wait
  double avgWaitOfThoseWhoWait;
  // KPI 7: Average time customer spends in the system
  double avgSystemTime;

  // Additional
  double avgQueueLength;
  int maxQueueLength;
  std::vector<double> serverUtilisation;
  double overallUtilisation;

  // For histogram
  std::map<int, int> waitTimeHistogram; // waitTime -> count
};

// ──────────────────────────────────────────────
// Run one simulation
// ──────────────────────────────────────────────
static SimResults runSimulation(const SimParams &params,
                                std::ostream &logStream) {

  // --- Initialise servers ---
  std::vector<Server> servers(params.numServers);
  for (int i = 0; i < params.numServers; ++i) {
    servers[i].id = i + 1;
  }

  // --- Generate inter-arrival times & arrival events ---
  std::vector<Customer> customers(params.totalCustomers);
  // FEL as a min-heap
  std::priority_queue<Event, std::vector<Event>, EventCmp> FEL;

  int arrivalClock = 0;
  for (int i = 0; i < params.totalCustomers; ++i) {
    int iat = sampleFromDistribution(params.interArrivalDist);
    arrivalClock += iat;
    customers[i].id = i + 1;
    customers[i].interArrivalTime = iat;
    customers[i].arrivalTime = arrivalClock;

    Event ev;
    ev.time = arrivalClock;
    ev.type = EventType::ARRIVAL;
    ev.customerId = i + 1;
    ev.serverId = -1;
    FEL.push(ev);
  }

  // --- Pre-generate service times for every customer ---
  for (int i = 0; i < params.totalCustomers; ++i) {
    customers[i].serviceTime = sampleFromDistribution(params.serviceDist);
  }

  // --- Log header ---
  logStream << "\n==========================================\n";
  logStream << " SCENARIO: " << params.label << "\n";
  logStream << " Servers: " << params.numServers
            << "  |  Customers: " << params.totalCustomers
            << "  |  Duration: " << params.simDuration << " min\n";
  logStream << " Avg inter-arrival (dist): " << std::fixed
            << std::setprecision(2) << distributionMean(params.interArrivalDist)
            << " min"
            << "  |  Avg service (dist): "
            << distributionMean(params.serviceDist) << " min\n";
  logStream << "==========================================\n\n";

  // Print inter-arrival distribution
  logStream << " Inter-Arrival Time Distribution:\n";
  logStream << " " << std::setw(12) << "Time(min)" << std::setw(15)
            << "Probability" << std::setw(15) << "Cumulative" << "\n";
  logStream << " " << std::string(42, '-') << "\n";
  double cumIA = 0;
  for (const auto &e : params.interArrivalDist) {
    cumIA += e.probability;
    logStream << " " << std::setw(12) << e.value << std::setw(15) << std::fixed
              << std::setprecision(2) << e.probability << std::setw(15) << cumIA
              << "\n";
  }
  logStream << "\n";

  // Print service time distribution
  logStream << " Service Time Distribution:\n";
  logStream << " " << std::setw(12) << "Time(min)" << std::setw(15)
            << "Probability" << std::setw(15) << "Cumulative" << "\n";
  logStream << " " << std::string(42, '-') << "\n";
  double cumST = 0;
  for (const auto &e : params.serviceDist) {
    cumST += e.probability;
    logStream << " " << std::setw(12) << e.value << std::setw(15) << std::fixed
              << std::setprecision(2) << e.probability << std::setw(15) << cumST
              << "\n";
  }
  logStream << "\n";

  // --- Waiting queue (FIFO) ---
  std::queue<int> waitingQueue; // stores customer IDs (1-based)

  // --- Statistics accumulators ---
  long long totalQueueLenSum = 0;
  int maxQueueLen = 0;

  // --- Tick-by-tick event log header ---
  logStream << " TICK-BY-TICK EVENT LOG:\n";
  logStream << " " << std::setw(6) << "Clock"
            << "  " << std::left << std::setw(55) << "Events" << std::right
            << std::setw(6) << "QLen";
  for (int s = 0; s < params.numServers; ++s)
    logStream << std::setw(10) << ("S" + std::to_string(s + 1));
  logStream << "\n";
  logStream << " " << std::string(69 + params.numServers * 10, '-') << "\n";

  // --- Helper lambdas ---
  auto findFreeServer = [&]() -> int {
    for (int i = 0; i < params.numServers; ++i)
      if (!servers[i].busy)
        return i;
    return -1;
  };

  auto assignCustomerToServer = [&](int custIdx, int servIdx, int currentTime) {
    Customer &c = customers[custIdx];
    Server &s = servers[servIdx];

    s.busy = true;
    s.customerId = c.id;
    int endTime = currentTime + c.serviceTime;
    s.serviceEnd = endTime;

    c.serviceStart = currentTime;
    c.serviceEnd = endTime;
    c.waitingTime = currentTime - c.arrivalTime;
    c.systemTime = endTime - c.arrivalTime;
    c.assignedServer = s.id;

    // Schedule SERVICE_COMPLETE event
    Event ev;
    ev.time = endTime;
    ev.type = EventType::SERVICE_COMPLETE;
    ev.customerId = c.id;
    ev.serverId = s.id;
    FEL.push(ev);
  };

  // --- Main simulation loop ---
  int lastTick = params.simDuration;
  if (!customers.empty())
    lastTick = std::max(lastTick, customers.back().arrivalTime + 100);

  for (int clock = 1; clock <= lastTick; ++clock) {

    std::string tickEvents;

    // ── Step (a): Collect all events at this clock tick from FEL ──
    std::vector<Event> completions;
    std::vector<Event> arrivals;
    while (!FEL.empty() && FEL.top().time == clock) {
      Event ev = FEL.top();
      FEL.pop();
      if (ev.type == EventType::SERVICE_COMPLETE)
        completions.push_back(ev);
      else
        arrivals.push_back(ev);
    }

    // ── Process SERVICE_COMPLETE events first ──
    for (auto &ev : completions) {
      int sIdx = ev.serverId - 1;
      servers[sIdx].totalBusyTime += customers[ev.customerId - 1].serviceTime;
      servers[sIdx].busy = false;
      servers[sIdx].customerId = -1;
      servers[sIdx].serviceEnd = -1;

      if (!tickEvents.empty())
        tickEvents += "; ";
      tickEvents += "C" + std::to_string(ev.customerId) + " done@S" +
                    std::to_string(ev.serverId);
    }

    // ── Step (b): Assign waiting customers to free servers ──
    while (!waitingQueue.empty()) {
      int freeIdx = findFreeServer();
      if (freeIdx < 0)
        break;
      int custId = waitingQueue.front();
      waitingQueue.pop();
      assignCustomerToServer(custId - 1, freeIdx, clock);

      if (!tickEvents.empty())
        tickEvents += "; ";
      tickEvents += "C" + std::to_string(custId) + "(Q)->S" +
                    std::to_string(servers[freeIdx].id);
    }

    // ── Step (c): Process ARRIVAL events ──
    for (auto &ev : arrivals) {
      int custIdx = ev.customerId - 1;

      if (!tickEvents.empty())
        tickEvents += "; ";
      tickEvents += "C" + std::to_string(ev.customerId) + " arr";

      int freeIdx = findFreeServer();
      if (freeIdx >= 0) {
        assignCustomerToServer(custIdx, freeIdx, clock);
        tickEvents += "->S" + std::to_string(servers[freeIdx].id);
      } else {
        waitingQueue.push(ev.customerId);
        tickEvents += "->Q(" + std::to_string((int)waitingQueue.size()) + ")";
      }
    }

    // ── Record queue statistics ──
    int qLen = static_cast<int>(waitingQueue.size());
    totalQueueLenSum += qLen;
    if (qLen > maxQueueLen)
      maxQueueLen = qLen;

    // ── Log this tick (only if something happened) ──
    if (!tickEvents.empty()) {
      logStream << " " << std::setw(6) << clock << "  " << std::left
                << std::setw(55) << tickEvents << std::right << std::setw(6)
                << qLen;
      for (int s = 0; s < params.numServers; ++s) {
        if (servers[s].busy)
          logStream << std::setw(10)
                    << ("C" + std::to_string(servers[s].customerId));
        else
          logStream << std::setw(10) << "Idle";
      }
      logStream << "\n";
    }

    // Stop early if nothing left
    bool allIdle = std::all_of(servers.begin(), servers.end(),
                               [](const Server &s) { return !s.busy; });
    if (FEL.empty() && waitingQueue.empty() && allIdle && clock > 1)
      break;
  }

  // ─────────────────────────────────────────
  //  DETAILED CUSTOMER TABLE (like Excel simulation table)
  // ─────────────────────────────────────────
  logStream << "\n SIMULATION TABLE (Customer Details):\n";
  logStream << " " << std::setw(5) << "Cust" << std::setw(8) << "IAT"
            << std::setw(10) << "Arrival" << std::setw(10) << "Service"
            << std::setw(12) << "SvcBegin" << std::setw(10) << "Wait"
            << std::setw(10) << "SvcEnd" << std::setw(12) << "System"
            << std::setw(10) << "Server"
            << "\n";
  logStream << " " << std::string(87, '-') << "\n";

  for (auto &c : customers) {
    logStream << " " << std::setw(5) << c.id << std::setw(8)
              << c.interArrivalTime << std::setw(10) << c.arrivalTime
              << std::setw(10) << c.serviceTime;
    if (c.serviceStart >= 0) {
      logStream << std::setw(12) << c.serviceStart << std::setw(10)
                << c.waitingTime << std::setw(10) << c.serviceEnd
                << std::setw(12) << c.systemTime << std::setw(10)
                << ("S" + std::to_string(c.assignedServer));
    } else {
      logStream << std::setw(12) << "N/A" << std::setw(10) << "N/A"
                << std::setw(10) << "N/A" << std::setw(12) << "N/A"
                << std::setw(10) << "N/A";
    }
    logStream << "\n";
  }
  logStream << "\n";

  // ─────────────────────────────────────────
  //  COMPUTE KPIs (matching LAB-PS requirements)
  // ─────────────────────────────────────────
  SimResults res;
  res.label = params.label;
  res.numServers = params.numServers;
  res.totalCustomers = params.totalCustomers;

  int served = 0;
  double totalWait = 0.0;
  double totalSystem = 0.0;
  double totalService = 0.0;
  double totalInterArr = 0.0;
  int customersWhoWaited = 0;
  double totalWaitOfWaiters = 0.0;

  for (auto &c : customers) {
    if (c.serviceStart >= 0) {
      served++;
      totalWait += c.waitingTime;
      totalSystem += c.systemTime;
      totalService += c.serviceTime;
      totalInterArr += c.interArrivalTime;
      if (c.waitingTime > 0) {
        customersWhoWaited++;
        totalWaitOfWaiters += c.waitingTime;
      }
      // Histogram
      res.waitTimeHistogram[c.waitingTime]++;
    }
  }

  res.customersServed = served;

  // KPI 1: Average waiting time per customer
  res.avgWaitingTime = (served > 0) ? totalWait / served : 0.0;

  // KPI 2: P(wait)
  res.probWait =
      (served > 0) ? static_cast<double>(customersWhoWaited) / served : 0.0;

  // KPI 3: Probability of idle server
  int actualEndTick = 0;
  for (auto &c : customers)
    if (c.serviceEnd > actualEndTick)
      actualEndTick = c.serviceEnd;

  res.serverIdleProb.resize(params.numServers);
  res.serverUtilisation.resize(params.numServers);
  double totalIdleProb = 0.0;
  double totalUtil = 0.0;
  for (int i = 0; i < params.numServers; ++i) {
    double util =
        (actualEndTick > 0)
            ? static_cast<double>(servers[i].totalBusyTime) / actualEndTick
            : 0.0;
    res.serverUtilisation[i] = util;
    res.serverIdleProb[i] = 1.0 - util;
    totalUtil += util;
    totalIdleProb += (1.0 - util);
  }
  res.overallUtilisation =
      (params.numServers > 0) ? totalUtil / params.numServers : 0.0;
  res.overallIdleProb =
      (params.numServers > 0) ? totalIdleProb / params.numServers : 0.0;

  // KPI 4: Average service time (actual, from generated data)
  res.avgServiceTime = (served > 0) ? totalService / served : 0.0;

  // KPI 5: Average time between arrivals (actual, from generated data)
  res.avgInterArrival =
      (params.totalCustomers > 0) ? totalInterArr / params.totalCustomers : 0.0;

  // KPI 6: Average waiting time of those who wait
  res.avgWaitOfThoseWhoWait =
      (customersWhoWaited > 0) ? totalWaitOfWaiters / customersWhoWaited : 0.0;

  // KPI 7: Average time customer spends in the system
  res.avgSystemTime = (served > 0) ? totalSystem / served : 0.0;

  // Additional
  res.avgQueueLength =
      (actualEndTick > 0)
          ? static_cast<double>(totalQueueLenSum) / actualEndTick
          : 0.0;
  res.maxQueueLength = maxQueueLen;

  return res;
}

// ──────────────────────────────────────────────
// Print KPI summary (matching LAB format)
// ──────────────────────────────────────────────
static void printResults(const SimResults &res, std::ostream &out) {
  out << "\n─────────────────────────────────────────────────\n";
  out << " KPI Summary: " << res.label << "\n";
  out << "─────────────────────────────────────────────────\n";
  out << " Total customers generated       : " << res.totalCustomers << "\n";
  out << " Customers served                : " << res.customersServed << "\n";
  out << " Number of servers               : " << res.numServers << "\n";
  out << "─────────────────────────────────────────────────\n";
  out << std::fixed << std::setprecision(2);
  out << " 1. Avg waiting time / customer  : " << res.avgWaitingTime
      << " min\n";
  out << " 2. P(wait)                      : " << res.probWait * 100.0
      << " %\n";
  out << " 3. P(idle server):                \n";
  for (size_t i = 0; i < res.serverIdleProb.size(); ++i)
    out << "      Server " << (i + 1) << "                   : "
        << res.serverIdleProb[i] * 100.0 << " %\n";
  out << "      Overall                    : " << res.overallIdleProb * 100.0
      << " %\n";
  out << " 4. Avg service time             : " << res.avgServiceTime
      << " min\n";
  out << " 5. Avg time between arrivals    : " << res.avgInterArrival
      << " min\n";
  out << " 6. Avg wait (of those who wait) : " << res.avgWaitOfThoseWhoWait
      << " min\n";
  out << " 7. Avg time in system           : " << res.avgSystemTime << " min\n";
  out << "─────────────────────────────────────────────────\n";
  out << " Additional:\n";
  out << "   Avg queue length              : " << res.avgQueueLength << "\n";
  out << "   Max queue length              : " << res.maxQueueLength << "\n";
  for (size_t i = 0; i < res.serverUtilisation.size(); ++i)
    out << "   Server " << (i + 1)
        << " utilisation         : " << res.serverUtilisation[i] * 100.0
        << " %\n";
  out << "   Overall utilisation           : " << res.overallUtilisation * 100.0
      << " %\n";
  out << "─────────────────────────────────────────────────\n";
}

// ──────────────────────────────────────────────
// Print waiting time histogram
// ──────────────────────────────────────────────
static void printHistogram(const SimResults &res, std::ostream &out) {
  if (res.waitTimeHistogram.empty())
    return;

  out << "\n WAITING TIME HISTOGRAM (" << res.label << "):\n";
  out << " " << std::setw(12) << "Wait(min)" << std::setw(10) << "Count"
      << std::setw(12) << "Frequency" << "   Bar\n";
  out << " " << std::string(55, '-') << "\n";

  int maxCount = 0;
  for (auto it = res.waitTimeHistogram.begin();
       it != res.waitTimeHistogram.end(); ++it)
    if (it->second > maxCount)
      maxCount = it->second;

  for (auto it = res.waitTimeHistogram.begin();
       it != res.waitTimeHistogram.end(); ++it) {
    int w = it->first;
    int cnt = it->second;
    double freq = static_cast<double>(cnt) / res.customersServed;
    int barLen = (maxCount > 0) ? static_cast<int>(30.0 * cnt / maxCount) : 0;
    out << " " << std::setw(12) << w << std::setw(10) << cnt << std::setw(11)
        << std::fixed << std::setprecision(2) << freq * 100.0 << "%"
        << "   " << std::string(barLen, '#') << "\n";
  }
  out << "\n";
}

// ──────────────────────────────────────────────
// Comparison table for evaluation scenarios
// ──────────────────────────────────────────────
static void printComparisonTable(const std::vector<SimResults> &results,
                                 std::ostream &out) {
  out << "\n==================================================================="
         "==\n";
  out << "                   EVALUATION COMPARISON TABLE                       "
         " \n";
  out << "====================================================================="
         "\n\n";

  // Header
  out << std::left << std::setw(32) << " KPI";
  for (auto &r : results)
    out << std::right << std::setw(16) << r.label;
  out << "\n " << std::string(32 + results.size() * 16 - 1, '-') << "\n";

  auto row = [&](const std::string &label,
                 std::function<std::string(const SimResults &)> fn) {
    out << std::left << std::setw(32) << (" " + label);
    for (auto &r : results)
      out << std::right << std::setw(16) << fn(r);
    out << "\n";
  };

  auto fmt2 = [](double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << v;
    return ss.str();
  };
  auto fmtPct = [](double v) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << v * 100.0 << "%";
    return ss.str();
  };

  row("Servers",
      [](const SimResults &r) { return std::to_string(r.numServers); });
  row("Customers",
      [](const SimResults &r) { return std::to_string(r.totalCustomers); });
  row("1. Avg wait time (min)",
      [&](const SimResults &r) { return fmt2(r.avgWaitingTime); });
  row("2. P(wait)", [&](const SimResults &r) { return fmtPct(r.probWait); });
  row("3. P(idle) overall",
      [&](const SimResults &r) { return fmtPct(r.overallIdleProb); });
  row("4. Avg service (min)",
      [&](const SimResults &r) { return fmt2(r.avgServiceTime); });
  row("5. Avg inter-arrival",
      [&](const SimResults &r) { return fmt2(r.avgInterArrival); });
  row("6. Avg wait (waiters)",
      [&](const SimResults &r) { return fmt2(r.avgWaitOfThoseWhoWait); });
  row("7. Avg system time",
      [&](const SimResults &r) { return fmt2(r.avgSystemTime); });
  row("Avg queue length",
      [&](const SimResults &r) { return fmt2(r.avgQueueLength); });
  row("Max queue length",
      [&](const SimResults &r) { return std::to_string(r.maxQueueLength); });
  row("Overall utilisation",
      [&](const SimResults &r) { return fmtPct(r.overallUtilisation); });

  out << "\n";
}

// ──────────────────────────────────────────────
// Main
// ──────────────────────────────────────────────
int main() {
  srand(static_cast<unsigned>(time(nullptr)));

  // Open log file
  std::ofstream logFile("simulation_log.txt");
  if (!logFile.is_open()) {
    std::cerr << "Error: cannot open simulation_log.txt for writing.\n";
    return 1;
  }

  // Helper: write to both console and log file
  auto dual = [&](const std::string &text) {
    std::cout << text;
    logFile << text;
  };

  dual("================================================================\n");
  dual("  CMPE 412 - Multi-Server Queue Simulation                      \n");
  dual("  Kuandyk Kyrykbayev, Akhmed Nazarov                            \n");
  dual("================================================================\n");

  // ─── Default distributions ───
  // Inter-arrival times: values 2-6 with various probabilities (from LAB-PS-02)
  std::vector<ProbEntry> defaultInterArrival = {
      {2, 0.20}, {3, 0.30}, {4, 0.25}, {5, 0.15}, {6, 0.10}};

  // Service times: values 1-4 with various probabilities (from LAB-PS-02)
  std::vector<ProbEntry> defaultService = {
      {1, 0.15}, {2, 0.30}, {3, 0.35}, {4, 0.20}};

  const int DEFAULT_SERVERS = 2;     // 2 servers as in LAB-PS-02
  const int DEFAULT_CUSTOMERS = 100; // 100 customers as in LAB-PS
  const int DEFAULT_DURATION = 500;

  // Collect results for comparison
  std::vector<SimResults> allResults;

  // ═══════════════════════════════════════════
  // SCENARIO 1: Baseline
  // ═══════════════════════════════════════════
  {
    SimParams p;
    p.label = "Baseline";
    p.numServers = DEFAULT_SERVERS;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = defaultInterArrival;
    p.serviceDist = defaultService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 1: Baseline completed.\n");
    printResults(res, std::cout);
    printHistogram(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 2a: Faster arrivals (decrease inter-arrival)
  // ═══════════════════════════════════════════
  {
    std::vector<ProbEntry> fasterArrival = {
        {1, 0.35}, {2, 0.35}, {3, 0.20}, {4, 0.10}};

    SimParams p;
    p.label = "Fast Arrival";
    p.numServers = DEFAULT_SERVERS;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = fasterArrival;
    p.serviceDist = defaultService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 2a: Faster Arrivals completed.\n");
    printResults(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 2b: Slower arrivals (increase inter-arrival)
  // ═══════════════════════════════════════════
  {
    std::vector<ProbEntry> slowerArrival = {
        {4, 0.15}, {5, 0.25}, {6, 0.30}, {7, 0.20}, {8, 0.10}};

    SimParams p;
    p.label = "Slow Arrival";
    p.numServers = DEFAULT_SERVERS;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = slowerArrival;
    p.serviceDist = defaultService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 2b: Slower Arrivals completed.\n");
    printResults(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 3a: Faster service (decrease service time)
  // ═══════════════════════════════════════════
  {
    std::vector<ProbEntry> fasterService = {
        {1, 0.45}, {2, 0.35}, {3, 0.15}, {4, 0.05}};

    SimParams p;
    p.label = "Fast Service";
    p.numServers = DEFAULT_SERVERS;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = defaultInterArrival;
    p.serviceDist = fasterService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 3a: Faster Service completed.\n");
    printResults(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 3b: Slower service (increase service time)
  // ═══════════════════════════════════════════
  {
    std::vector<ProbEntry> slowerService = {
        {3, 0.10}, {4, 0.25}, {5, 0.30}, {6, 0.25}, {7, 0.10}};

    SimParams p;
    p.label = "Slow Service";
    p.numServers = DEFAULT_SERVERS;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = defaultInterArrival;
    p.serviceDist = slowerService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 3b: Slower Service completed.\n");
    printResults(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 4a: Fewer servers (1 server)
  // ═══════════════════════════════════════════
  {
    SimParams p;
    p.label = "1 Server";
    p.numServers = 1;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = defaultInterArrival;
    p.serviceDist = defaultService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 4a: 1 Server completed.\n");
    printResults(res, std::cout);
  }

  // ═══════════════════════════════════════════
  // SCENARIO 4b: More servers (4 servers)
  // ═══════════════════════════════════════════
  {
    SimParams p;
    p.label = "4 Servers";
    p.numServers = 4;
    p.simDuration = DEFAULT_DURATION;
    p.totalCustomers = DEFAULT_CUSTOMERS;
    p.interArrivalDist = defaultInterArrival;
    p.serviceDist = defaultService;

    std::ostringstream detailedLog;
    SimResults res = runSimulation(p, detailedLog);
    allResults.push_back(res);

    logFile << detailedLog.str();
    printResults(res, logFile);
    printHistogram(res, logFile);

    dual("\n>>> Scenario 4b: 4 Servers completed.\n");
    printResults(res, std::cout);
  }

  // ─── Print comparison table to both outputs ───
  {
    std::ostringstream table;
    printComparisonTable(allResults, table);
    dual(table.str());
  }

  // ─── Commentary ───
  {
    std::string commentary = R"(
================================================================
                   EVALUATION COMMENTARY
================================================================

 Q2: Effect of changing inter-arrival times
 -------------------------------------------
 - FASTER arrivals (lower avg inter-arrival time): More customers
   arrive per unit time. Servers become saturated faster, leading
   to longer queues, higher waiting times, higher P(wait), and
   higher server utilisation. P(idle) decreases.

 - SLOWER arrivals (higher avg inter-arrival time): Fewer customers
   per unit time. Servers remain idle more often, resulting in
   shorter/no queues, lower waiting times, lower P(wait), and
   lower server utilisation. P(idle) increases.

 Q3: Effect of changing service times
 --------------------------------------
 - FASTER service (lower avg service time): Each customer occupies
   a server for less time. Servers free up more quickly, leading
   to shorter queues, lower waiting times, and lower server
   utilisation. P(idle) increases.

 - SLOWER service (higher avg service time): Each customer keeps
   the server busy longer. This leads to longer queues, higher
   waiting times, higher P(wait), and higher server utilisation.
   P(idle) decreases.

 Q4: Effect of changing the number of servers
 ----------------------------------------------
 - FEWER servers: Less capacity to handle concurrent customers.
   Queues build up faster, resulting in higher waiting times,
   higher P(wait), and higher per-server utilisation.

 - MORE servers: More capacity available. Queues rarely form,
   resulting in lower/zero waiting times, lower P(wait), and
   lower per-server utilisation (each server is idle more often).

 The comparison table above demonstrates these effects
 quantitatively.

)";
    dual(commentary);
  }

  logFile.close();
  dual("Detailed log saved to: simulation_log.txt\n");
  dual("Simulation complete.\n");

  return 0;
}
