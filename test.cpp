// #include <mpi.h>
// #include <iostream>

// int main(int argc, char **argv)
// {
//     // Initialize the MPI environment
//     MPI_Init(&argc, &argv);

//     // Get the number of processes
//     int world_size;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size);

//     // Get the rank of the process
//     int world_rank;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

//     // Get the name of the processor
//     char processor_name[MPI_MAX_PROCESSOR_NAME];
//     int name_len;
//     MPI_Get_processor_name(processor_name, &name_len);

//     // Print a hello world message
//     std::cout << "Hello world from processor " << processor_name
//               << ", rank " << world_rank << " out of " << world_size
//               << " processors" << std::endl;

//     // Finalize the MPI environment
//     MPI_Finalize();
//     return 0;
// }

// #include <mpi.h>
// #include <iostream>

// int main(int argc, char **argv)
// {
//     // Initialize the MPI environment
//     MPI_Init(&argc, &argv);

//     // Get the rank of the process
//     int world_rank;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

//     // Get the number of processes
//     int world_size;
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size);

//     int local_value = world_rank; // Each process has its rank as its local value
//     int global_sum = 0;           // Variable to store the sum on the root process

//     // Perform a reduction: sum up all ranks, with root process (rank 0) collecting the result
//     MPI_Reduce(&local_value, &global_sum, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

//     // Root process prints the result
//     if (world_rank == 0)
//     {
//         std::cout << "Sum of ranks across " << world_size << " processes is " << global_sum << std::endl;
//     }

//     // Finalize the MPI environment
//     MPI_Finalize();
//     return 0;
// }

// #include <mpi.h>
// #include <iostream>

// int main(int argc, char **argv)
// {
//     MPI_Init(&argc, &argv);

//     int world_rank;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

//     if (world_rank == 0)
//     {
//         int data = 100; // Data to send
//         MPI_Send(&data, 1, MPI_INT, 1, 0, MPI_COMM_WORLD);
//         std::cout << "Rank 0 sent data " << data << " to Rank 1" << std::endl;
//     }
//     else if (world_rank == 1)
//     {
//         int received_data;
//         MPI_Recv(&received_data, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
//         std::cout << "Rank 1 received data " << received_data << " from Rank 0" << std::endl;
//     }

//     MPI_Finalize();
//     return 0;
// }

// #include <mpi.h>
// #include <iostream>

// int main(int argc, char **argv)
// {
//     MPI_Init(&argc, &argv);

//     int world_rank;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

//     int data;
//     if (world_rank == 0)
//     {
//         data = 42; // Only the root initializes the data
//     }

//     // Broadcast the data to all processes
//     MPI_Bcast(&data, 1, MPI_INT, 0, MPI_COMM_WORLD);

//     std::cout << "Process " << world_rank << " received data: " << data << std::endl;

//     MPI_Finalize();
//     return 0;
// }

// #include <mpi.h>
// #include <iostream>
// #include <vector>

// int main(int argc, char **argv)
// {
//     MPI_Init(&argc, &argv);

//     int world_rank, world_size;
//     MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
//     MPI_Comm_size(MPI_COMM_WORLD, &world_size);

//     // Root process initializes data
//     std::vector<int> data = {10, 20, 30, 40};
//     int chunk;

//     if (world_rank == 0)
//     {
//         std::cout << "Root process scattering data: ";
//         for (int val : data)
//             std::cout << val << " ";
//         std::cout << std::endl;
//     }

//     // Scatter data to all processes
//     MPI_Scatter(data.data(), 1, MPI_INT, &chunk, 1, MPI_INT, 0, MPI_COMM_WORLD);

//     // Each process modifies its chunk
//     chunk *= 2;

//     // Gather the modified chunks back at the root
//     std::vector<int> result(world_size);
//     MPI_Gather(&chunk, 1, MPI_INT, result.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

//     if (world_rank == 0)
//     {
//         std::cout << "Root process gathered results: ";
//         for (int val : result)
//             std::cout << val << " ";
//         std::cout << std::endl;
//     }

//     MPI_Finalize();
//     return 0;
// }
