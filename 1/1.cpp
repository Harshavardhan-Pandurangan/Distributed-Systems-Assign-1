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

    // iterate through the adjacency list and remove all incoming and outgoing edges to blocked vertices
    if (world_rank == 0)
    {
        for (int i = 0; i < B; i++)
        {
            for (int j = 0; j < V; j++)
            {
                adj[j].erase(std::remove(adj[j].begin(), adj[j].end(), blocked[i]), adj[j].end());
            }
        }
    }

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
    std::vector<int> visited(V, 0);

    // if the start is in blocked vertices, then exit the program with distance -1
    if (std::find(blocked.begin(), blocked.end(), start) != blocked.end())
    {
        if (world_rank == 0)
        {
            // set all values to -1
            dist.assign(V, -1);
            for (int i = 0; i < K; i++)
            {
                std::cout << dist[exits[i]] << " ";
            }
            std::cout << std::endl;
        }
        MPI_Finalize();
        return 0;
    }

    // Distributed 1D Parallel BFS
    int level = 0;
    std::vector<int> curr_queue(V, 0), next_queue(V, 0);
    if (start % world_size == world_rank)
    {
        dist[start] = 0;
        curr_queue[start] = 1;
        visited[start] = 1;
    }

    while (true)
    {
        // process the current queue
        for (int i = 0; i < V; i++)
        {
            if (curr_queue[i] == 0 || i % world_size != world_rank)
            {
                continue;
            }

            for (int j = 0; j < my_adj[i].size(); j++)
            {
                int v = my_adj[i][j];
                if (visited[v] == 0)
                {
                    dist[v] = level + 1;
                    next_queue[v] = 1;
                    visited[v] = 1;
                }
            }
        }

        // sync everyone's visited array, dist array and next queue array
        MPI_Allreduce(MPI_IN_PLACE, visited.data(), V, MPI_INT, MPI_LOR, MPI_COMM_WORLD);
        MPI_Allreduce(MPI_IN_PLACE, dist.data(), V, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        MPI_Allreduce(MPI_IN_PLACE, next_queue.data(), V, MPI_INT, MPI_LOR, MPI_COMM_WORLD);

        // check if the next queue is empty
        int sum = 0;
        for (int i = 0; i < V; i++)
        {
            sum += next_queue[i];
        }
        if (sum == 0)
        {
            break;
        }

        // swap the current queue with the next queue
        curr_queue.swap(next_queue);
        next_queue.assign(V, 0);

        // increment the level
        level++;
    }

    // Finalize the MPI environment
    MPI_Finalize();

    // print the distance array for the exit vertices
    if (world_rank == 0)
    {
        for (int i = 0; i < K; i++)
        {
            if (dist[exits[i]] == std::numeric_limits<int>::max())
            {
                dist[exits[i]] = -1;
            }
            std::cout << dist[exits[i]] << " ";
        }
        std::cout << std::endl;
    }

    return 0;
}
