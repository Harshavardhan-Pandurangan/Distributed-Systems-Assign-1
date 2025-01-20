#include <iostream>
#include <vector>
#include <mpi.h>

int main(int argc, char **argv)
{
    // Initialize the MPI environment
    MPI_Init(&argc, &argv);

    // Get the number of processes
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Get the rank of the process
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    int V, E, K, start, B;
    std::vector<std::vector<int>> adj;
    std::vector<int> exits, blocked;

    if (world_rank == 0)
    {
        // Root process reads the input
        std::cin >> V >> E;

        adj.resize(V);
        for (int i = 0; i < E; i++)
        {
            int u, v, d;
            std::cin >> u >> v >> d;
            adj[v].push_back(u);
            if (d == 1)
            {
                adj[u].push_back(v);
            }
        }

        std::cin >> K;
        exits.resize(K);
        for (int i = 0; i < K; i++)
        {
            std::cin >> exits[i];
        }

        std::cin >> start;

        std::cin >> B;
        blocked.resize(B);
        for (int i = 0; i < B; i++)
        {
            std::cin >> blocked[i];
        }
    }

    // Broadcast the values of V, E, K, exit, and B
    MPI_Bcast(&V, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&E, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&start, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&B, 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::vector<int> my_vertices;
    std::vector<std::vector<int>> my_adj(V);

    if (world_rank != 0)
    {
        // Non-root processes allocate memory for the vectors
        exits.resize(K);
        blocked.resize(B);
    }

    // Broadcast appropriate vertices' adjacency list to each process
    if (world_rank == 0)
    {
        for (int i = 0; i < V; i++)
        {
            int owner = i % world_size;
            if (owner == 0)
            {
                my_adj[i] = adj[i];
            }
            else
            {
                int size = adj[i].size();
                MPI_Send(&size, 1, MPI_INT, owner, 0, MPI_COMM_WORLD);
                MPI_Send(adj[i].data(), size, MPI_INT, owner, 0, MPI_COMM_WORLD);
            }
        }
    }
    else
    {
        for (int i = 0; i < V; i++)
        {
            if (i % world_size == world_rank)
            {
                int size;
                MPI_Recv(&size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                my_adj[i].resize(size);
                MPI_Recv(my_adj[i].data(), size, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }

    // Broadcast the starting vertices and blocked vertices
    MPI_Bcast(exits.data(), K, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(blocked.data(), B, MPI_INT, 0, MPI_COMM_WORLD);

    // vertices belonging to each process
    for (int i = 0; i < V; i++)
    {
        if (i % world_size == world_rank)
        {
            my_vertices.push_back(i);
        }
    }

    std::vector<int> dist(V, std::numeric_limits<int>::max());
    std::vector<int> visited(K, 0);

    // Distributed 1D Parallel BFS
    std::vector<int> frontier;
    for (int i = 0; i < K; i++)
    {
        if (exits[i] % world_size == world_rank)
        {
            dist[exits[i]] = 0;
            frontier.push_back(exits[i]);
        }
    }

    for (int i = 0; i < B; i++)
    {
        if (blocked[i] % world_size == world_rank)
        {
            dist[blocked[i]] = -1;
        }
    }

    while (!frontier.empty())
    {
        std::vector<int> new_frontier;
        for (int u : frontier)
        {
            for (int v : my_adj[u])
            {
                if (dist[v] == std::numeric_limits<int>::max())
                {
                    dist[v] = dist[u] + 1;
                    new_frontier.push_back(v);
                }
            }
        }
        frontier = new_frontier;
    }

    // Gather the results
    std::vector<int> global_dist(V);
    MPI_Gather(dist.data(), V / world_size, MPI_INT, global_dist.data(), V / world_size, MPI_INT, 0, MPI_COMM_WORLD);

    if (world_rank == 0)
    {
        for (int i = 0; i < V; i++)
        {
            if (global_dist[i] != std::numeric_limits<int>::max())
            {
                std::cout << i << " " << global_dist[i] << std::endl;
            }
            else
            {
                std::cout << i << " " << "INF" << std::endl;
            }
        }
    }

    // Finalize the MPI environment
    MPI_Finalize();

    return 0;
}
