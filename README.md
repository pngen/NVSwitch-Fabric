# NVSwitch Fabric

**NVSwitch Fabric 1.0.0** is a production-quality, vendor-neutral C++20 runtime for
discovering, representing, measuring, explaining, and governing **NVSwitch-class
accelerator switching fabrics**.

## The Systems Question

Which NVSwitch-class switching fabric exists, which accelerators and switch elements
are connected through it, which switch-mediated paths are currently reachable and
healthy, what degradation or congestion affects those paths, and is the current fabric
evidence authoritative enough to permit communication through them?

> A collection of NVLink-capable devices does not imply a healthy NVSwitch fabric.

A switch may exist while ports are unavailable, paths are degraded, partitions have
changed, topology evidence is stale, a switch or endpoint has restarted, routing
evidence has been invalidated, or the current software environment cannot observe
enough fabric state to authorize use. **Fabric topology is treated as governed,
generation-bound evidence.**

## Systems Boundary

NVSwitch Fabric **owns**:

- discovery of NVSwitch-class switching elements where supported;
- typed switch / switch-port identity, accelerator-to-switch adjacency, and
  switch-to-switch adjacency where exposed;
- logical NVSwitch fabric topology, topology snapshots, and topology generations;
- switch, port/link, and partition generations;
- partition membership, reachability, switch/port operational state;
- switch-mediated path enumeration, path health, switch-level degradation and
  congestion evidence where observable;
- switch failure-domain modeling; route eligibility and deterministic ranking;
- route explanations; stale topology / switch / port / partition rejection;
- partition-change invalidation; endpoint/switch incarnation fencing where dynamic
  evidence originates from workers;
- durable static fabric capability knowledge; conservative recovery semantics after
  restart; optional NVIDIA-specific hardware backends; and vendor-neutral core
  interfaces allowing analogous future switched accelerator fabrics.

NVSwitch Fabric **does not own**: raw NVLink peer-link semantics, generic GPU/PCIe
topology, workload placement, collective algorithms/execution/scheduling, NCCL or MPI
replacement, RDMA, GPUDirect RDMA/Storage, network routing, Ethernet/InfiniBand
switching, bandwidth arbitration across all transports, generic communication planning,
global fabric scheduling, GPU admission, memory allocation, accelerator reservation,
model/cluster-scheduler behavior, DPU/NIC control, CXL switching, or generic congestion
control.

**NVLink Fabric** answers questions about direct/link-level NVLink connectivity and path
quality. **NVSwitch Fabric** answers questions about the *switched fabric itself*: which
switches exist, which ports attach to which endpoints or switches, which partitions
exist, how traffic may traverse switch elements, which switch-mediated paths remain
valid, what fabric condition constrains them, and whether the evidence is authoritative.
It is not a duplicate of NVLink Fabric.

## Provenance: REAL / SYNTHETIC / UNSUPPORTED

Every hardware-facing result carries an explicit **Provenance** and an **EvidenceSource**:

- **REAL** — genuinely observed from live hardware via supported public APIs
  (sources: NVML, CUDA_RUNTIME, CUDA_DRIVER, NVIDIA_FABRIC_MANAGER, OPERATING_SYSTEM,
  BENCHMARK, DERIVED, PERSISTED).
- **SYNTHETIC** — a constructed fixture (source: SYNTHETIC_FIXTURE); never hardware
  evidence, and never upgraded to REAL.
- **UNSUPPORTED** — the capability is not supported in this environment.

`UNKNOWN` never silently becomes `UP`, `PRESENT`, `HEALTHY`, or `SUPPORTED`. Persistence
preserves provenance; reloading a SYNTHETIC snapshot never transforms it into REAL.

## Topology & Switch/Port Model

The fabric is modeled as an explicit graph. Nodes are accelerator devices and NVSwitch
devices. Edges are accelerator-to-switch links and switch-to-switch links (where
exposed). The graph supports disconnected fabrics, multiple switches/ports, redundant
paths, partitioned fabrics, switch/port/endpoint failure, generation changes, and
ambiguous or unsupported topology detail. Traversal is deterministic and bounded; it
never infers hidden hardware topology merely because two GPUs can communicate.

Strongly typed identities are used throughout: DeviceId, SwitchId, PortId
(switch+index), LinkId, FabricId, PartitionId, TopologySnapshotId, PathId, WorkerId, and
typed generations (TopologyGeneration, SwitchGeneration, PortGeneration,
PartitionGeneration, LinkGeneration, RouteGeneration), plus CoordinatorEpoch and
WorkerBootId. Raw strings or integers are never interchanged across distinct identities.

A switch record carries stable identity, backend identity, vendor, product/family,
architecture/generation (where observable), switch generation, port count, capability
set, operational state, fabric/partition membership, worker/incarnation source,
provenance, and freshness. Each switch port is represented independently.

## Partitions & Generations

Partitioning is first class. A partition has identity, generation, fabric, members,
switch set, state (ACTIVE / INACTIVE / RECONFIGURING / REVALIDATION_REQUIRED / STALE /
UNSUPPORTED), authority, and lifecycle. Partition transitions are generation-bound: a
route authorized under generation N is not authoritative after the partition advances to
N+1 without revalidation. A stale partition observation fails closed. The runtime does
not claim to create or modify hardware partitions unless a supported backend genuinely
implements and proves that function — observation/governance is sufficient.

## Reachability & Paths

Reachability is explicit and never equates *device present* with *device reachable* nor
*link present* with *usable switch-mediated path*. Reachability accounts for endpoint,
switch, port/link, partition, and topology generations, operational state, backend
support, freshness, and policy. Typed outcomes include REACHABLE, REACHABLE_DEGRADED,
UNREACHABLE, PARTITION_BLOCKED, SWITCH_DOWN, PORT_DOWN, ENDPOINT_STALE, TOPOLOGY_STALE,
REVALIDATION_REQUIRED, INSUFFICIENT_EVIDENCE, and UNSUPPORTED.

A switch-mediated path identifies source, destination, switch sequence, port sequence,
individual links, the generations it was observed under, capability, health state, and
provenance. Path enumeration is deterministic and remains bounded under malformed or
dense synthetic topologies using configurable limits.

## Fabric Health, Degradation & Congestion

Health is defined through explicit, inspectable evidence (switch/port/endpoint state,
redundant-path availability, degraded links, partition state, error telemetry,
measurement deviation, stale evidence). Typed health includes HEALTHY, DEGRADED,
PARTIALLY_REACHABLE, PARTITIONED, SWITCH_FAILURE, PORT_FAILURE, ENDPOINT_FAILURE,
REVALIDATION_REQUIRED, INSUFFICIENT_EVIDENCE, and UNSUPPORTED. If a composite score is
exposed, all factors remain inspectable. Congestion/degradation evidence is reported
with an explanation and an evidence-classified outcome; it is never inferred from a
single slow transfer.

## Route Governance & Deterministic Ranking

Route governance determines whether a switch-mediated path is currently eligible and
which eligible path should be preferred. A route decision carries its id, generation,
source/destination, fabric/topology/partition generations, candidate and rejected paths
with explicit rejection reasons, the selected candidate, policy, evidence references,
freshness requirements, authority, and explanation. Possible outcomes include
ROUTE_ALLOWED, ROUTE_ALLOWED_DEGRADED, NO_ROUTE, PARTITION_BLOCKED, SWITCH_DOWN,
PORT_DOWN, STALE_TOPOLOGY, STALE_PARTITION, STALE_SWITCH, REVALIDATION_REQUIRED,
INSUFFICIENT_EVIDENCE, POLICY_REJECTED, and UNSUPPORTED.

Hard filters run first (source/destination current, topology/partition current, all
required switches/ports/links current and usable, supported fabric, minimum capability,
freshness, policy exclusions). Eligible candidates are then ranked by named factors
(fewer switches, fewer hops, directness, redundancy, measured throughput/latency,
active-link count, degradation, congestion evidence, freshness, confidence,
failure-domain diversity, policy preference), with deterministic tie-breaking on the
canonical path key. Insertion order cannot change the result.

Fail closed: a route authorized under stale fabric evidence must not execute merely
because the underlying device IDs still exist.

## Failure Domains

Switch/fabric failure domains are modeled explicitly, exposing whether multiple
candidate paths share the same switch, port group, partition, endpoint, or fabric
component. Downstream schedulers and communication planners may exploit this truth;
NVSwitch Fabric itself is not a scheduler.

## Backend Architecture

The core is vendor-neutral. An explicit Backend interface separates capability discovery
from dynamic state. Backends include:

- **Synthetic switched-fabric backend** (nvswitch_fabric_synthetic) — rigorous,
  visible-SYNTHETIC fixtures covering single/multi-switch, redundant/parallel links,
  disconnected endpoints, failed/degraded ports and switches, partition split/merge,
  switch/endpoint restart, stale evidence, asymmetric/conflicting observation, route
  invalidation, deterministic alternate selection, failure-domain diversity, synthetic
  congestion, and bounded traversal under dense topology. No synthetic record is ever
  presented as hardware proof.
- **NVIDIA / NVML backend** (nvswitch_fabric_nvidia) — uses only public supported NVML
  interfaces (version/capability, device discovery, compute capability). It handles
  missing/unsupported symbols and never fabricates switch presence.
- **CUDA measurement helper** (nvswitch_fabric_cuda) — real device discovery,
  allocation, H2D, a real compiled kernel, synchronization, D2H, CPU-reference parity,
  free, and device-memory baseline return.

The core does not depend on synthetic assumptions, and consumers that only need the
vendor-neutral core do not require hardware libraries.

## Build

Requires CMake >= 3.20 and a C++20 compiler (MSVC 2022 validated; /W4 /WX). Optional:
NVIDIA CUDA toolkit + NVML for the NVIDIA/CUDA backends.

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release
    ctest --test-dir build -C Release

## Tests

A dependency-free deterministic test framework runs under CTest with **no timeouts**.
Suites:

- **core** — identity/generation typing, wire codec, synthetic reachability/paths/routes,
  partition gating, switch-failure/partition-change/endpoint authority invalidation,
  stale-generation rejection, provenance preservation, integrity/reference detection,
  persistence round-trip, corruption/truncation/trailing-garbage rejection, UNKNOWN
  neutrality, route determinism, bounded dense traversal, measurement invalidation.
- **property** — seeded randomized operation streams that check integrity, generation
  monotonicity, SYNTHETIC-never-REAL, and route determinism after every event, emitting
  seed + operation index on failure.
- **concurrency** — real concurrent reader/writer threads (switch/port/partition mutation
  racing route/path/reachability queries, persistence racing mutation, measurement
  racing), joined and checked for invariants.
- **adversarial** — duplicate identities, impossible port indices, dangling references,
  NaN/negative values, absurd lengths/counts, stale route authority, corrupt/truncated/
  trailing persistence, malformed TCP frames (magic/CRC/length/truncation).
- **runtime** — real OS-process worker death (spawn + TerminateProcess) invalidates
  authority, real coordinator restart advances epoch and revalidates, stale-boot replay
  is rejected, and the framed transport round-trips.

## Examples & CLI

Installed-API examples: synthetic single switch, redundant two-switch deterministic
alternate route, route governance (failure/partition/stale), inspection/explanation,
and NVIDIA discovery + real CUDA proof. A CLI is provided:

    nvswitch_fabric_inspect --scenario TWO_SWITCH_REDUNDANT --topology
    nvswitch_fabric_inspect --scenario SINGLE_SWITCH_TWO_GPU --reach --a 1 --b 2
    nvswitch_fabric_inspect --scenario TWO_SWITCH_REDUNDANT --route --a 1 --b 2
    nvswitch_fabric_inspect --cuda
    nvswitch_fabric_inspect --nvidia --devices
    nvswitch_fabric_inspect --persisted <state.nvf>

It makes REAL / SYNTHETIC / UNSUPPORTED immediately obvious and supports deterministic
JSON output.

## Installation & Downstream Use

    cmake --install build --config Release --prefix <prefix>

An independent consumer project (outside the repository tree) configures with
"find_package(NVSwitchFabric REQUIRED)" (and CMake "find_package" / "target_link_libraries"),
compiles against the installed headers, links nvswitch_fabric::nvswitch_fabric and
nvswitch_fabric::nvswitch_fabric_synthetic, and runs successfully. No source-tree
includes and no hidden build-tree dependencies are required.

## Hardware Validation

Validated on a single **NVIDIA GeForce RTX 5090** (compute capability 12.0, Blackwell
consumer, driver 616.86, CUDA 12.9 tooling). This is **CUDA hardware proof only**; a
single RTX 5090 does not prove NVLink or NVSwitch.

- CUDA device discovery, architecture, real allocation, real kernel, synchronization,
  D2H, CPU-reference parity, free, and baseline return: **REAL PASS**.
- Multiple physical GPUs, CUDA peer access, NVLink/NVSwitch discovery, physical
  NVSwitch count, physical switch-port/partition discovery, real switch-mediated path
  discovery/reachability, real NVSwitch bandwidth/latency, Fabric Manager integration,
  and real switch/port failure telemetry: **UNSUPPORTED** in this environment
  (no NVSwitch hardware / no multi-GPU, and no supported public NVSwitch-class
  enumeration API on this platform).
- Synthetic switched-fabric behavior (single/multi-switch, partitioning, switch failure,
  alternate-path recovery, bounded dense traversal): **SYNTHETIC PASS**.
- NVML backend: device discovery **REAL PASS**; nvSwitchPresent=0 (honest).

Real switched-path bandwidth/latency measurement is only claimed when actual NVSwitch
hardware exists and the measurement is technically attributable; otherwise the strongest
provable statement is labeled accurately (e.g. "real GPU peer transfer, NVSwitch
topology present").

## Genuine Limitations

- NVSwitch Fabric is not a collective library; it does not implement or schedule
  collective algorithms, does not replace NCCL or MPI, and does not own generic NVLink or
  GPU peer-link semantics.
- It does not claim hardware route programming unless a supported backend genuinely
  exposes and implements that ability (none does here).
- Synthetic switched fabrics are synthetic.
- Actual NVSwitch capabilities depend on installed hardware and supported public APIs.
- Custom-kernel CUDA proof is built with nvcc; the kernel module is kept free of STL
  because the installed nvcc 12.9 cannot compile the MSVC 14.44 STL headers.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
