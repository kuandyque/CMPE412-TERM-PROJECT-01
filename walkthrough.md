# CMPE 412 – Term Project 01: Walkthrough

## Group Members
- Kuandyk Kyrykbayev
- Akhmed Nazarov

## Project Overview

A discrete-event, minute-by-minute simulation of a multi-server call center queue, implemented in C++. Inter-arrival and service times are generated probabilistically from configurable distribution tables.

## How to Build & Run

```bash
# Compile
g++ -std=c++17 -O2 -o simulation main.cpp

# Or with MSVC
cl /EHsc /std:c++17 /O2 /Fe:simulation.exe main.cpp

# Run
./simulation        # Linux/macOS
simulation.exe      # Windows
```

## Data Structures

| Structure | Implementation | Purpose |
|---|---|---|
| **Future Event List (FEL)** | `std::priority_queue` (min-heap) | Stores ARRIVAL and SERVICE_COMPLETE events, ordered by time |
| **Server Activity List** | `std::vector<Server>` | Tracks each server's busy/idle state and current customer |
| **Customer Waiting List** | `std::queue<int>` | FIFO queue of customer IDs waiting for a free server |
| **Statistics Table** | Per-customer records + per-tick accumulators | Collects all data needed for KPI computation |

## Probability Distributions (Baseline Defaults)

**Inter-Arrival Times (minutes):**

| Time | Probability | Cumulative |
|------|------------|------------|
| 2    | 0.20       | 0.20       |
| 3    | 0.30       | 0.50       |
| 4    | 0.25       | 0.75       |
| 5    | 0.15       | 0.90       |
| 6    | 0.10       | 1.00       |

**Service Times (minutes):**

| Time | Probability | Cumulative |
|------|------------|------------|
| 1    | 0.15       | 0.15       |
| 2    | 0.30       | 0.45       |
| 3    | 0.35       | 0.80       |
| 4    | 0.20       | 1.00       |

## Simulation Algorithm (per clock tick)

1. **Process SERVICE_COMPLETE events** at current time → free the server, update statistics
2. **Assign waiting customers** to any newly freed servers (FIFO order)
3. **Process ARRIVAL events** at current time → assign to free server or enqueue in waiting list
4. **Record tick statistics** (queue length, server states)

## KPIs Computed (matching LAB-PS-01/02)

1. **Average waiting time per customer** — total wait / total customers
2. **P(wait)** — fraction of customers who waited > 0 minutes
3. **P(idle server)** — per server and overall idle probability
4. **Average service time** — actual average from generated data
5. **Average time between arrivals** — actual average from generated data
6. **Average waiting time of those who wait** — average wait excluding zero-wait customers
7. **Average time customer spends in the system** — waiting + service time

Additionally: waiting time histogram, average queue length, max queue length, and per-server utilisation.

## Evaluation Scenarios

| # | Scenario | Parameter Changed | Expected Effect |
|---|----------|-------------------|-----------------|
| 1 | **Baseline** | Default (2 servers, 100 customers) | Reference point |
| 2a | **Fast Arrival** | Avg IAT ↓ (≈2.05 min) | ↑ wait, ↑ utilisation, ↑ P(wait) |
| 2b | **Slow Arrival** | Avg IAT ↑ (≈5.85 min) | ↓ wait, ↓ utilisation, ↓ P(wait) |
| 3a | **Fast Service** | Avg svc ↓ (≈1.80 min) | ↓ wait, ↓ utilisation |
| 3b | **Slow Service** | Avg svc ↑ (≈5.00 min) | ↑ wait, ↑ utilisation, ↑ P(wait) |
| 4a | **1 Server** | Servers ↓ to 1 | ↑ wait, ↑ utilisation, ↑ P(wait) |
| 4b | **4 Servers** | Servers ↑ to 4 | ↓ wait, ↓ utilisation, ↓ P(wait) |

## Output Files

- **Console** — KPI summaries for each scenario + comparison table
- **simulation_log.txt** — Complete detailed output including:
  - Probability distribution tables with CDF
  - Tick-by-tick event log (arrivals, completions, queue state, server states)
  - Customer simulation table (IAT, arrival, service, wait, system time, assigned server)
  - KPI summary for each scenario
  - Waiting time histogram
  - Side-by-side comparison table
  - Evaluation commentary (Q2, Q3, Q4)

## Evaluation Commentary

### Q2: Effect of Changing Inter-Arrival Times
- **Faster arrivals** (lower avg IAT) → more customers per unit time → servers saturate faster → longer queues, higher waiting times, higher P(wait), higher utilisation, lower P(idle).
- **Slower arrivals** (higher avg IAT) → fewer customers per unit time → servers idle more → shorter/no queues, lower waiting times, lower P(wait), lower utilisation, higher P(idle).

### Q3: Effect of Changing Service Times
- **Faster service** (lower avg service time) → servers free up quickly → shorter queues, lower waiting times, lower utilisation, higher P(idle).
- **Slower service** (higher avg service time) → servers busy longer → longer queues, higher waiting times, higher P(wait), higher utilisation, lower P(idle).

### Q4: Effect of Changing the Number of Servers
- **Fewer servers** → less concurrent capacity → queues build faster → higher waiting times, higher P(wait), higher per-server utilisation.
- **More servers** → more capacity → queues rarely form → lower/zero waiting times, lower P(wait), lower per-server utilisation.

The comparison table printed by the simulation demonstrates all of these effects quantitatively.
