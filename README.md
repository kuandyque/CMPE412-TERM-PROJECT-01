# CMPE 412 – Term Project 01: Multi-Server Queue Simulation

**Course:** CMPE 412 – Computer Simulation (2026-Spring)  
**University:** Kadir Has University – Computer Engineering Dept.

## Group Members

- Kuandyk Kyrykbayev
- Akhmed Nazarov

## Description

A discrete-event, minute-by-minute simulation of a call center with multiple servers, implemented in C++.

- Inter-arrival and service times are generated **probabilistically** from configurable distribution tables.
- The simulation uses a **Future Event List (FEL)** priority queue, a **Server Activity List**, and a **Customer Waiting Queue**.
- All 7 KPIs from the lab assignments are computed and displayed.
- 7 evaluation scenarios are run automatically (baseline + varying inter-arrival, service times, and server count).

## Build

```bash
g++ -std=c++17 -O2 -o simulation main.cpp
```

Or with Make:

```bash
make
```

## Run

```bash
./simulation
```

On Windows:

```cmd
simulation.exe
```

## Output

- **Console:** KPI summaries for each scenario + comparison table
- **simulation_log.txt:** Detailed tick-by-tick event log, customer simulation tables, histograms, and full KPIs for all scenarios

## KPIs Computed

1. Average waiting time per customer
2. P(wait) — probability that a customer waits
3. P(idle server) — per server and overall
4. Average service time (minutes)
5. Average time between arrivals (minutes)
6. Average waiting time of those who wait (minutes)
7. Average time customer spends in the system (minutes)

## Evaluation Scenarios

| # | Scenario | What changes |
|---|----------|-------------|
| 1 | Baseline | Default parameters (2 servers, 100 customers) |
| 2a | Fast Arrival | Lower avg inter-arrival time |
| 2b | Slow Arrival | Higher avg inter-arrival time |
| 3a | Fast Service | Lower avg service time |
| 3b | Slow Service | Higher avg service time |
| 4a | 1 Server | Decrease server count |
| 4b | 4 Servers | Increase server count |
