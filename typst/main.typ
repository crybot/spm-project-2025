#import "@preview/cetz:0.3.1"
#import "@preview/cetz-plot:0.1.0": plot, chart

#set cite(form: "normal", style: "alphanumeric")
#set figure(placement: auto)

#set heading(numbering: "1.")
#set page(
  paper: "us-letter",
  header: align(right)[],
  numbering: "1",
)
#set par(justify: true)
#set text(
  font: "New Computer Modern",
  size: 10pt,
)

#let appendix(body) = {
  set heading(numbering: "A.", supplement: [Appendix])
  counter(heading).update(0)
  body
}

#set table(
  stroke: none,
)

#show table: t => {
  set text(size: 9pt)
  t
}

// A function to plot execution time against the number of threads.
#let plot_thread_scaling(
  title: "Execution Time vs. Threads",
  threads: (),
  times: (),
  size: (10, 5), // A wider aspect ratio is often good for these plots
  y-label: "Time (s)",
  x-label: "Number of Threads",
) = {
  // 1. Prepare the data for plotting.
  // We convert the times from milliseconds to seconds for better readability.
  let ys = times.map(t => t / 1000.0)
  let xs = threads

  // The plot function requires an array of (x, y) points.
  let points = xs.zip(ys)

  // 2. Create custom ticks for the x-axis to match the thread counts.
  // This creates pairs like `(1, "1")`, `(2, "2")`, `(4, "4")`, etc.
  let x-ticks = threads.map(t => (t, str(t)))

  // 3. Render the plot inside a canvas.
  cetz.canvas({
    plot.plot(
      size: size,
      title: title,
      x-label: x-label,
      y-label: y-label,
      x-ticks: x-ticks,
      x-tick-step: none,
      y-tick-step: auto, {
        plot.add(points, mark: "o")
      }
    )
  })
}

#let plot_speedup(threads: (), times: (), size: (10, 5), x-label: "Number of Threads") = {
  let base_time = times.first()
  let speedups = times.map(t => base_time / t)
  let points = threads.zip(speedups)
  let ideal_speedup = threads.zip(threads) // A straight line y=x
  let x-ticks = threads.map(t => (t, str(t)))

  cetz.canvas({
    plot.plot(
      size: size,
      title: "Parallel Speedup",
      y-label: "Speedup (x)",
      x-label: x-label,
      x-ticks: x-ticks,
      x-tick-step: none,
      {
        plot.add(points, label: "Actual Speedup", mark: "o")
        plot.add(ideal_speedup, label: "Ideal Speedup", mark: "dashed")
      }
    )
  })
}

#let plot_efficiency(
  title: "Parallel Efficiency",
  threads: (),
  times: (),
  size: (10, 5)
) = {
  // --- 1. Pre-condition Check ---
  // The calculation is impossible without a 1-thread baseline time.
  assert(threads.len() > 0 and times.len() > 0, message: "Input arrays cannot be empty.")
  assert(threads.first() == 1, message: "The first entry in the 'threads' array must be 1 to provide a baseline.")

  let T_1 = times.first() // Time on 1 thread

  // --- 2. Calculate Efficiency Points ---
  let efficiency_points = threads.zip(times).map(point => {
    let p = point.at(0)     // Number of threads
    let T_p = point.at(1)   // Time on p threads

    let speedup = T_1 / T_p
    let efficiency = speedup / p

    return (p, efficiency) // Return the (x, y) point for the plot
  })

  cetz.canvas({
    plot.plot(
      title: title,
      size: size,
      x-label: "Number of Threads",
      y-label: "Efficiency (Speedup / Threads)",
      // We only provide custom x-ticks because the thread counts are irregular.
      // We let the library handle all other formatting automatically.
      x-ticks: threads.map(t => (t, str(t))),
      x-tick-step: none,
      {
        // The "o-" mark style creates points connected by a line.
        plot.add(efficiency_points, mark: "o")
      }
    )
  })
}



#align(center, text(17pt)[
  * SPM Project: Distributed External Memory Sorting*
])
#grid(
  columns: (1fr),
  align(center)[
    Marco Pampaloni \
    Department of Computer Science \
    #link("m.pampaloni2@studenti.unipi.it") \
    #datetime.today().display("[month repr:long] [day], [year]")
  ]
)

#outline()
#pagebreak()

/*************************************************************************************/ 
= Introduction <introduction>
== Problem Statement
The project specification required implementing a scalable parallel external memory merge sort. The file to sort is a
sequence of records composed by a 64 bit key, which is used for sorting, and a variable-sized payload with arbitrary
byte length $8 < "len" <  "PAYLOAD_MAX"$. The file cannot be assumed to entirely fit in a single's node central memory.

/*************************************************************************************/ 
= Implementation
== Architecture
The strict requirements outlined above prevent a naive merge-sort implementation, forcing a smart use of heap memory
allocations and partial results collection. In particular, since the file cannot fit in internal memory, I decided to
employ a classical external-memory merge sort approach, given its I/O optimality guarantees on shared-memory systems.
This algorithm, however, poses many implementation challenges that are not easily surmountable in practice.

The chosen algorithm works on two phases:

- *Phase 1*: The input file is read sequentially from disk. This incurs an optimal number of I/O-heavy system calls. The
  unsorted records are deserialized and collected in large batches that can fit in internal memory. Each batch is sorted
  using a fast in-memory sorting algorithm and subsequently stored on disk as a parially sorted collection of records.

- *Phase 2*: After the input file is exhausted and, _critically_, after each batch has been sorted and written to disk,
  a final merging phase can be executed; each locally sorted file is read sequentially while keeping a single block of
  data in memory for each file to minimize random I/O operations. At each merging iteration, the single best record
  (according to its comparison key) is chosen and written in a write buffer. When the buffer gets full it can be
  permanently stored on disk. This selection operation can be done in logarithmic time using a min-heap. After each
  partial file has been consumed, the algorithm terminates and the entire input file has been sorted and stored on disk.


Both phases are I/O-bound, but the second one in particular is inherently sequential and cannot be parallelized.
Moreover, it has to wait for the first phase to finish so stream-parallelism approaches are limited.

== Hardware
Local development and initial tests for shared-memory implementations have been carried out on an AMD Ryzen 7950X machine with 16 cores and
32 threads, while final tests have been performed on the University's _spmcluster_.
Even with a fast PCIE Gen 4.0 Nvme drive with 7GB/s sequential read bandwidth, the implementation remained I/O-bound,
though more data parallelism could be employed during the sorting phase.

== Shared-Memory Algorithm
To mitigate some of the performance issues caused by the low disk-bandwidth of the _spmcluster_, I decided to adopt
the following optimizations:

- The implementation features buffered I/O to efficiently read chunks of binary data from the input files. This reduces
  heavy read system calls;
- Given the variable nature of the records' sizes subject to sorting, allocating the memory necessary for
  serializing/deserializing the records' payloads on the stack cannot be done. Instead each payload has to be allocated on the heap,
  which should be done repeatedly and with high-frequence during both phases. For this reason, I devised a simple
  block-contiguous Memory
  Arena allocator which allocates large chunks of free-store memory upon creation and returns zero-copy
  contiguous views of such memory when a new allocation of given size is requested. This greatly reduces memory
  allocations and provides a flexible way of automatically managing allocations/deallocations using batches of records
  wrapped in smart pointers.
- Both phases employ stream-parallelism to hide I/O operations behind computation.
- Async I/O by means of double buffering and C++'s threads has also been tried, but did not improve performance (this
  was perhaps due to the overhead of thread management outweighing the benefits on this particular system) and I thus
  decided to not use it to unclutter the implementation.
- During Phase 1, batches are read sequentially and records are collected in contiguous regions of memory to exploit
  data locality.
  Meanwhile, different batches are allocated on distinct memory arenas to reduce false-sharing in later stages of the
  pipeline. This also improves memory utilization, since once a batch has been succesfully processed, all the memory of
  its memory arena can be freed. This is done automatically used smart pointers.

The following structs can be used to efficiently collect batches of records:

  ```cpp

struct RecordView {
  uint64_t key;
  std::span<char> payload;

  auto inline operator==(const RecordView& other) const -> bool {
    return key == other.key;
  }
  auto inline operator<=>(const RecordView& other) const -> std::strong_ordering {
    return key <=> other.key;
  }
};

struct ArenaBatch {
  MemoryArena<char> arena;
  std::vector<RecordView> records;

  ArenaBatch(size_t batch_size, size_t arena_size) : arena(arena_size) {
    records.reserve(batch_size);
  }

  auto totalBytes(size_t header_size = sizeof(uint64_t) + sizeof(uint32_t)) -> size_t {
    return arena.used() + records.size() * header_size;
  }
};
  ```


The I/O business logic is wrapped in an utility object which is used throughout the implementation: 

```cpp 
template <
    size_t BufferSize,
    typename Allocator = DefaultHeapAllocator<char>,
    typename RecordType = files::Record>
class BufferedRecordLoader;
```

By default, `BufferedRecordLoader` uses a `DefaultHeapAllocator` which simply allocates new contiguous blocks of memory
whenever requested, but can also be provided with a `MemoryArenaAllocator` to employ the optimization described above. When
this is the case, a `RecordView` has to be used as `RecordType`, which holds a non-owning zero-copy view over the
contiguously allocated region of memory.

The sections below describe how the sorting algorithm has been implemented exploiting FastFlow's and OpenMP data and
stream parallelism for shared-memory systems. A later section describes how the distributed merge sort has been
implemented using MPI and OpenMP.


=== FastFlow
FastFlow's implementation closely follows the algorithmic description presented above, but employs both stream and
data-parallelism to accelerate sorting and hide I/O latencies.
In particular, it implements two pipelines executed back to back.
The first one features an `Emitter` node that reads and collects batches of records from the input file and
asynchronously sends them to a `BatchSorter` farm which locally sorts the records and sends them to a collector node
that writes them on disk. On machines with very fast I/O like the one used for local development, this later stage can
profitably be turned into a farm. However, tests performed on the _spmcluster_ showed that this did not bring any
benefit to the implementation and I thus chose to use a single worker (while keeping the implementation general with a
`--num_writers` command line argument).

Finally, the last stage of the first pipeline collects the paths for the temporary sorted runs created by the previous
stage for later reuse.

The second phase is implemented as a simple 2-stage pipeline that implements the merging strategy described earlier,
with I/O latencies minimized by collecting small batches of sorted records from the merging phase, which is performed at
the same time. Here I could not use the memory arena optimization because records within a batch could not necessarily
be allocated from a single memory pool, due to the complex runs' intertwined relative ordering. Between the first and
second pipeline, an implicit barrier is employed.

=== OpenMP
OpenMP's implementation is identical to FastFlow's one, in that it still basically defines two pipelines with a
synchronization point in-between, but it does so leveraging OpenMP's task directives: once a batch of unsorted records
is collected from the first stage, a new task is spawned that sorts and stores the records on disk. This approach
perfectly scales with the speed of the underlying I/O device because it does not introduce any dependency between
different tasks, which can spawn at the speed with which batches are collected and start sorting them in parallel. With
fast disks or distributed systems this should work better than FastFlow's fixed farms of workers.

The second phase is again implemented as a simple 2-stage pipeline, but since we cannot rely on FastFlow's internal
queues, I implemented a simple mutex-based thread-safe queue implementation to exchange batches of sorted records to the
next and final stage. This is again implemented using a producer-consumer task setting:

```cpp
#pragma omp parallel
  {
#pragma omp single  // Producer task, executed by a single thread
    {
#pragma omp task shared(out_file, task_queue)  // Consumer task
      {
        while (true) {
          auto batch_to_write = task_queue.pop();
          if (batch_to_write == nullptr)
            break;
          writeOutputBatch(std::move(batch_to_write), out_file);
        }
      } // consumer
      ...
#pragma omp taskwait
    } // single
  } // parallel


```

== Distributed Algorithm
=== MPI + OpenMP
The distributed implementation of the external-memory merge sort algorithms leverages MPI for inter-node communication
and OpenMP for intra-node parallelism. I chose to use OpenMP for its simplicity and because I found it to scale better
with respect to the underlying hardware than a naive FastFlow's implementation. For this reason, most of the code from
OpenMP's shared-memory implementation could be reused, wrapped into a `ParallelSorterOMP` class.

The shared-memory algorithm can be moved to a distributed setting with minimal modifications. The nodes network is
divided into a single emitter (root) node responsible for reading the input file and collecting large batches of
records, which is scattered to multiple worker nodes that independently employ the same shared-memory algorithm
described earlier.

Because each record can hold a variable amount of bytes as payload, a 2-stage communication must be employed to send the
data over the worker nodes (alternatively, a custom MPI data type that describes the memory configuration of the record
batches could have been used, but it didn't suit well with the block-contiguous memory arena allocator employed). This
involves performing two MPI calls:
- ```cpp
MPI_Scatter(send_counts.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, ROOT_RANK, communicator)
```
- ```cpp
MPI_Scatterv(
    serialized_batch.bytes.data(),
    send_counts.data(),
    displacements.data(),
    MPI_BYTE,
    MPI_IN_PLACE,
    0,
    MPI_BYTE,
    ROOT_RANK,
    communicator
);
```
The first one scatters the sizes in bytes of the buffers that each worker should allocate to receive the batch of
record. The second function call sends the actual data, serialized in a byte stream, with the displacement array
delimiting each batch within it. The workers receive the scattered counts, allocate buffers to accomodate the data and
deserialize it using the same memory-arena-backed `BufferedRecordLoader` as the shared-memory implementation. Then a new
task is spawned to sort the data and write it to disk, trying to hide I/O and communication latencies.

Finally, when the input file has been exhausted by the emitter node, an `MPI_Scatter` call with negative counts is
performed, which signals the workers the EOF has been reached. At this point, each worker node can perform a local merge
of each sorted run stored on disk, resulting in a single sorted file per node.

Given the NFS mounted file system available on the _spmcluster_, I decided to employ a fixed, rank-based, naming scheme
for each node's last locally merged sorted file, so that no additional communication between nodes is necessary. The emitter node, then acts as a
collector and eventually performs a final merge step using the sorted files generated by the workers.

/*************************************************************************************/ 
= Results

== Experimental Setup
All performance evaluations were conducted using a synthetically generated base input file consisting of one million
unsorted records. Each record was created with a randomly generated 64-bit key, while its payload size was chosen from a
uniform distribution ranging between 8 and 64 bytes. For the weak scalability analysis, this base file was scaled
proportionally with the number of processes. A larger initial dataset was avoided to ensure the execution time of each
experiment remained within the 10-minute limit imposed by the project requirements.

== Analysis Limitations
The performance evaluation has been carried out on the _spmcluster_, whose hardware architecture poses significant
challenges for collecting reliable and reproducible measurements: concurrent I/O-heavy tasks from other users on any
node inevitably saturate the disk and network bandwidth of the single frontend, causing notable fluctuations in the
measured execution times. 

== Performance Evaluation
As already pointed out, the configuraion of the cluster used for testing the distributed implementation of the
algorithm presents a heavy I/O bottleneck that prevents proper scaling. In fact, the provided implementation only shows
a very modest speedup for the single-node shared-memory implementation, while showing virtually no performance
improvements for the distributed version. This section provides scaling, speedup and efficiency plots for each algorithm configuration.

=== OpenMP Shared-Memory Implementation
The first analysis focuses on the shared-memory implementation using OpenMP on a single node. As shown by the speedup curve in @fig-omp-speedup, the algorithm benefits from multiple cores on CPU-bound tasks, but the gains quickly diminish.

#figure(
  scale(
    plot_speedup(
      threads: (1, 2, 4, 6, 8),
      times: (8169, 7065, 6686, 6824, 6890),
    )
  ),
  caption: [
    Parallel speedup of the OpenMP implementation. The curve shows sub-linear speedup, flattening out after 4 threads, which indicates that the algorithm is becoming limited by bottlenecks other than CPU, likely I/O bandwidth to the NFS server.
  ]
) <fig-omp-speedup>

The parallel efficiency, plotted in @fig-omp-efficiency, drops accordingly as more threads are added. This confirms that even on a single machine, performance is ultimately capped by the I/O throughput to the centralized NFS storage, preventing linear scalability.

#figure(
  scale(
    plot_efficiency(
      threads: (1, 2, 4, 6, 8),
      times: (8169, 7065, 6686, 6824, 6890)
    )
  ),
  caption: [
    Parallel efficiency of the OpenMP implementation, calculated as Speedup / Threads. The curve shows a clear decline as more computational resources are added.
  ]
) <fig-omp-efficiency>

=== FastFlow Implementation

A similar trend is observed for the FastFlow implementation. The stream-parallel pipeline model proves effective at hiding some I/O latency, leading to modest but consistent speedup as seen in @fig-ff-speedup.

#figure(
  scale(
    plot_speedup(
      threads: (1, 2, 4, 6, 8),
      times: (9471, 9410, 7907, 7551, 7060),
    )
  ),
  caption: [
    Parallel speedup of the FastFlow implementation. The speedup is modest, confirming that while the pipeline architecture is effective, its scalability is ultimately capped by the performance of the shared I/O subsystem.
  ]
) <fig-ff-speedup>

However, as the corresponding efficiency plot (see @fig-ff-efficiency) confirms, this approach also saturates quickly. The program cannot perform faster than the rate at which the underlying storage can provide data.

#figure(
  scale(
    plot_efficiency(
      threads: (1, 2, 4, 6, 8),
      times: (9471, 9410, 7907, 7551, 7060),
    )
  ),
  caption: [
    Parallel efficiency of the FastFlow pipeline implementation.
  ]
) <fig-ff-efficiency>

=== MPI Distributed Implementation

Finally, scalability is plotted for the distributed MPI implementation. The strong scalability test (@fig-mpi-speedup) shows a nearly flat speedup curve, indicating that adding more nodes provides almost no performance benefit for a fixed problem size.

#figure(
  scale(
    plot_speedup(
      threads: (2, 4, 6, 7),
      times: (12086, 11661, 10994, 11581),
      x-label: "Number of Worker Nodes"
    )
  ),
  caption: [
    Distributed speedup (strong scalability) of the MPI implementation, relative to the 2-node baseline. The curve is nearly flat, confirming that the bottleneck created by the centralized NFS storage and 10GbE network prevents the algorithm from scaling effectively as more nodes are added.
  ]
) <fig-mpi-speedup>

The situation is further highlighted by the weak scalability analysis in @fig-mpi-weak-scaling, where execution time increases dramatically as more nodes are added to solve a proportionally larger problem.

#figure(
  scale(
    plot_thread_scaling(
      title: "MPI Weak Scalability on Cluster",
      threads: (2, 4, 7),
      times: (12086, 21921, 49417),
      x-label: "Number of Worker Nodes"
    )
  ),
  caption: [
    Weak scalability analysis of the distributed MPI implementation. The execution time is measured as both the number of worker nodes and the total problem size are increased proportionally, maintaining a constant workload per node. The ideal result would be a flat horizontal line, but the plot shows significant degradation, with communication and I/O costs dominating.
  ]
) <fig-mpi-weak-scaling>

// Raw execution times for the distributed implementation are reported in @tab-execution-times-multi.

= Varying Payload Size

All previous analyses were conducted using files containing records with a maximum size of 64 bytes. Here we show how the distributed algorithm scales with a maximum size of 16 bytes instead, while maintaining the overall size of the file close to the original analysis.

@fig-mpi-speedup-p16 shows marginally better strong-scaling results, especially moving from a 2-node to a 4-node configuration, but eventually flattens out indicating similar I/O saturation.

#figure(
  scale(
    plot_speedup(
      threads: (2, 4, 6, 7),
      times: (16519, 12972, 13105, 13215),
      x-label: "Number of Worker Nodes"
    )
  ),
  caption: [
    MPI strong scalability with a maximum payload of 16 bytes.
  ]
) <fig-mpi-speedup-p16>

= Cost Model Analysis
An approximate cost model for the distributed implementation of the outlined merge sort algorithm can be provided by
employing the BSP model. Because the implementation introduces a global synchronization barrier between the two phases
of the algorithm, we can independently focus our analysis on each of them.

We define the following quantities:
- $N$ is the total number of records to sor
- $B$ is the batch size that each worker node receives
- $R$ is the average payload length in bytes
- $T_"read"$ is the time required to read a single record from the NFS
- $T_"write"$ is the time required to write a single record to the NFS
- $p$ is the number of worker nodes
- $g$ is the cost of sending a single word through the network

Both $T_"read"$ and $T_"write"$ can be implicitly scaled by the operating systems page size and the size of the buffers
used to implement I/O operations.

== Superstep 1
During phase 1, the first superstep consists of the emitter node performing a scan of the input file and scattering
batches of records to the worker nodes:

$ T_"Emitter"^"(1)" = N T_"read" $

Since sorting and writing to disk can be done employing stream parallelism, each worker nodes incur a cost of:

$ 
T_"Worker"^"(1)" = N/(B p) max(B log(B), B T_"write") = N/p max(log (B), T_"write")
$

and the first superstep's computation cost is thus
$T_"comp"^"(1)" = T_"Emitter"^"(1)" + T_"Worker"^"(1)"$ because even though I/O latencies can in theory be hidden behind
the worker's computations, NFS contention eventually serializes nodes operations.

The cost of the first superstep's communication is $ h =  N R g + l$.

== Superstep 2
After the input file has been consumed, the worker nodes can merge their local sorted runs into a single output file.
This is done in parallel, but again NFS contention is the primary bottleneck:

$ T^"(2)" = T_"Worker"^"(2)" = N/p T_"read" + N/p log(N/(B p)) + N/p T_"write" + l $

No additional communication is required in this step

== Superstep 3
The final superstep involves the emitter node performing a final merge pass over $p$ sorted runs stored on disk, without
additional communication requirements:

$ T^"(3)" = T_"Emitter"^"(3)" = N T_"read" + N log(p) + N T_"write" + l $

== Total cost
The total cost is therefore

$
T_"total" &= T^"(1)" + T^"(2)" + T^"(3)" \
&= N T_"read" + N/p max(log(B), T_"write") + N R g + l \
&+ N/p T_"read" + N/p log(N/(B p)) + N/p T_"write" + l \
&+ N T_"read" + N log(p) + N T_"write" + l
// approx N T_"read" + B T_"write" + N R g + l + N/p (T_"read" + T_"write") + l + N(T_"read" + T"_write") + l
$

Rearranging we get

$
T_"total" &=
underbrace(T_"read" (2N + N/p) + T_"write" (N + N/p), "I/O")
+ underbrace(N/p max(log(B), T_"write"), "Possibly hidden I/O")
+ underbrace(N/p log (N/(B p)) + N log p, "Computation")
+ underbrace(N R g + 3l, "Communication")
$

== Analysis
As can be seen, this approximate cost model matches the empirical evidence collected by running the experiments on the
cluster machine: the largest term is the I/O cost, with multiple full read and write passes over the entire dataset.
Notice how this term barely changes as the number of nodes $p$ increases. Depending on the batch size and the NFS
contention, the second term can possibly hide the I/O cost of writing sorted runs on disk. The computation cost
correctly scales with the number of nodes, but notice how the final merge phase cost ($N log p$) actually increases with
the number of processors used,
consolidating our weak-scaling findings.
Finally, the communication cost scales linearly with the size of the input dataset.



/*************************************************************************************/ 
= Conclusions
The experiments carried out on the _spmcluster_ provided concrete evidence of the heavy bottlenecked I/O that the
distributed algorithm goes through. All tested configurations performed poorly on a medium size dataset that could in
theory be stored in internal memory and did not show any scaling capacity. The provided implementations for a shared
memory system outperformed their distributed counterpart by a large margin, but still showed a modest and almost flat
speedup over the number of threads used, with decreasing efficiency as the number of threads went up. The empirical and
theoretical analysis showed that the main bottleneck is the centralized shared NFS, which created extremely high contention
over the nodes, dwarfing any introduced parallelism.

#pagebreak()

#show: appendix
= Raw execution times <appendix-a> \

This appendix reports raw execution time plots and tables for all the provided implementations.

#figure(
  table(
    columns: 6,
    table.hline(),
    table.header([*Implementation*], [*N=1*], [*N=2*], [*N=4*], [*N=6*], [*N=8*]),
    table.hline(),
    [OMP],  [8169], [7065], [6686], [6824], [6890],
    [FF],  [9471], [9410], [7907], [7551], [7060],
    table.hline()
  ),
  caption: [Raw execution times (in milliseconds) for the two shared-memory implementations running on a single _spmcluster_'s node with a varying amount of available threads.]
) <tab-execution-times-single>

#figure(
  table(
    columns: 5,
    table.hline(),
    table.header([*Implementation*], [*N=2*], [*N=4*], [*N=6*], [*N=7*]),
    table.hline(),
    [MPI + OMP],  [12086], [11661], [10994], [11581],
    table.hline()
  ),
  caption: [Raw execution times (in milliseconds) for the distributed implementations running on a varying amount of nodes.]
) <tab-execution-times-multi>

#figure(
  scale(
    plot_thread_scaling(
      title: "OpenMP Execution Time vs. Threads (medium.bin)",
      threads: (1, 2, 4, 6, 8),
      times: (8169, 7065, 6686, 6824, 6890),
    )
  ),
  caption: [
    Execution time of the OpenMP implementation on a single node. Performance improves up to 4 threads, after which the overhead of thread management and I/O saturation begins to dominate, leading to diminishing returns.
  ]
) <fig-omp-times>


#figure(
  scale(
    plot_thread_scaling(
      title: "FastFlow Execution Time vs. Workers (medium.bin)",
      threads: (1, 2, 4, 6, 8),
      times: (9471, 9410, 7907, 7551, 7060),
      x-label: "Number of Workers"
    )
  ),
  caption: [
    Execution time of the FastFlow implementation. A notable performance improvement is visible when scaling from 2 to 4 workers, as the pipeline parallelism effectively hides I/O latency. Gains diminish with more workers due to the same underlying I/O bottleneck.
  ]
) <fig-ff-times>
#figure(
  scale(
    plot_thread_scaling(
      title: "MPI Execution Time vs. Nodes (medium.bin)",
      threads: (2, 4, 6, 7), // Using 'threads' param for 'nodes' for simplicity
      times: (12086, 11661, 10994, 11581),
      x-label: "Number of Worker Nodes"
    )
  ),
  caption: [
    Execution time of the distributed MPI implementation, varying the number of worker nodes. Each node utilized 8 OpenMP threads. The performance gains are minimal, and performance degrades beyond 6 nodes, indicating high communication overhead.
  ]
) <fig-mpi-times>

#place.flush()


