#include <mpi.h>
// #include <print>

auto main() -> int {
  MPI_Init(nullptr, nullptr);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  char processor_name[MPI_MAX_PROCESSOR_NAME];
  int name_len;
  MPI_Get_processor_name(processor_name, &name_len);

  std::printf(
      "Hello world from processor %s, rank %d out of %d processors\n",
      processor_name,
      world_rank,
      world_size
  );

  MPI_Finalize();
}
