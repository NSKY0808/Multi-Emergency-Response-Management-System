# Advanced DS Emergency Command Center

Desktop emergency response simulator built as a course project for Advanced Data Structures.  
The application models how a control room can register emergencies, prioritize them, assign the nearest response teams, inspect routes, and generate reports through a Windows GUI.

## Overview

This project expands a basic emergency management program into a multi-panel desktop command center. It combines data-structure-driven decision making with a GUI that supports:

- emergency intake
- team dispatch
- route visualization
- reporting and history review
- tactical city map interaction

The system is designed around a fictional city network with named areas, response teams, incident history, and persistent local data files.

## Main Features

- `Control Room` page for live operations
- `Reporting` page for history and quick intake
- emergency registration from multiple panels
- automatic nearest-team dispatch
- severity-based prioritization
- undo/redo for assignment actions
- team availability toggling
- route highlighting for selected emergencies
- interactive tactical map with:
  - zoom in/out
  - mouse-wheel zoom
  - drag-to-pan
  - node selection
  - node detail block
- exportable text report
- persistent storage using local `.txt` files

## Data Structures Used

This project intentionally demonstrates multiple Advanced Data Structures concepts:

- `Trie`
  - validates and stores area names
- `Unordered Map`
  - stores officers and response teams
- `Weighted Graph`
  - models the city road network
- `Dijkstra's Algorithm`
  - finds the shortest route and nearest team
- `Segment Tree`
  - tracks the maximum pending severity
- `Priority Queue`
  - dispatches emergencies by urgency
- `Stacks`
  - supports undo/redo of assignments
- `Vectors`
  - store emergencies, history, graph adjacency, and UI data

## GUI Structure

### 1. Control Room

Used for live command-center operations:

- register emergencies
- assign teams
- mark incidents handled
- escalate severity
- manage team state
- inspect routes
- interact with the city map

### 2. Reporting

Used for analysis and quick review:

- view recent incident log
- see summary analytics
- log a new emergency quickly
- export a project report

## Map Interaction

The tactical map is a custom desktop visualization, not a live external map API.

It supports:

- node-based city layout
- emergency hotspots
- team positions
- selected route highlighting
- zoom controls
- wheel zoom
- drag panning
- node detail popup card

## Files

- `emer.cpp`
  - main application source
- `emer.exe`
  - compiled desktop executable
- `emer_new.exe`
  - newer compiled build if `emer.exe` was locked during rebuild
- `emergencies.txt`
  - current emergency data
- `teams.txt`
  - team state and statistics
- `history.txt`
  - incident log history

## Build Instructions

This project is built for Windows using MinGW `g++`.

### Compile

```powershell
g++ emer.cpp -std=c++20 -municode -mwindows -lole32 -lcomctl32 -o emer.exe
```

If `emer.exe` is already open and Windows locks the file, compile to a new name:

```powershell
g++ emer.cpp -std=c++20 -municode -mwindows -lole32 -lcomctl32 -o emer_new.exe
```

### Run

Double-click `emer.exe` or run:

```powershell
.\emer.exe
```

## How the System Works

1. A user logs an emergency with area, type, severity, and description.
2. The emergency is stored and inserted into the priority structure.
3. The system finds the nearest available team using Dijkstra on the city graph.
4. Dispatch information is reflected in the GUI and map.
5. Completed incidents are moved into handled/history state.
6. Reports can be exported from the Reporting page.

## Educational Value

This project is useful for demonstrating:

- practical use of data structures in a real-world style problem
- transition from console logic to GUI application design
- state persistence
- event-driven desktop programming
- visual representation of graph-based systems

## Limitations

- the map is simulated, not GPS-backed
- data is stored in plain text files
- the system runs locally on one machine
- no real mobile client/server architecture yet
- no real-time network synchronization

## Future Scope

Possible next upgrades:

- mobile SOS client
- laptop-hosted backend server
- web dashboard
- live map API integration
- authentication by role
- filters and search
- SLA timers and alerts
- team workload analytics
- multi-user live updates

## Author Note

This project was developed as an Advanced Data Structures course project and later expanded into a more ambitious emergency command-center prototype with a richer GUI and wider operational scope.
