#include <iostream>
#include <vector>
#include <mpi.h>

struct Ball
{
    int x; // Position
    int y; // Position
    int d; // Direction
};

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

    // Define a new MPI datatype for the Ball struct
    int blocklengths[3] = {1, 1, 1};
    MPI_Datatype types[3] = {MPI_INT, MPI_INT, MPI_INT};
    MPI_Aint offsets[3];
    offsets[0] = offsetof(Ball, x);
    offsets[1] = offsetof(Ball, y);
    offsets[2] = offsetof(Ball, d);

    MPI_Datatype MPI_BALL;
    MPI_Type_create_struct(3, blocklengths, offsets, types, &MPI_BALL);
    MPI_Type_commit(&MPI_BALL);

    int N, M, K, T;
    std::vector<Ball> balls;

    if (world_rank == 0)
    {
        // Root process reads the input
        std::cin >> N >> M >> K >> T;

        for (int i = 0; i < K; i++)
        {
            int x, y, d;
            char c;
            std::cin >> x >> y >> c;
            if (c == 'U')
                d = 0;
            else if (c == 'R')
                d = 1;
            else if (c == 'D')
                d = 2;
            else
                d = 3;

            balls.push_back({x, y, d});
        }
    }

    // Share the values of N, M, K, and T
    MPI_Bcast(&N, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&M, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&K, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&T, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Distribute the balls to the appropriate processes
    std::vector<std::vector<Ball>> ball_distribution(world_size);
    if (world_rank == 0)
    {
        for (int i = 0; i < K; i++)
        {
            int owner = balls[i].x % world_size;
            ball_distribution[owner].push_back(balls[i]);
        }
    }

    int ball_count;
    if (world_rank == 0)
    {
        ball_count = ball_distribution[0].size();
        for (int i = 1; i < world_size; i++)
        {
            ball_count = ball_distribution[i].size();
            MPI_Send(&ball_count, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
        }
    }
    else
    {
        MPI_Recv(&ball_count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    std::vector<Ball> my_balls(ball_count);

    if (world_rank == 0)
    {
        my_balls = ball_distribution[0];
        for (int i = 1; i < world_size; i++)
        {
            MPI_Send(ball_distribution[i].data(), ball_distribution[i].size(), MPI_BALL, i, 0, MPI_COMM_WORLD);
        }
    }
    else
    {
        MPI_Recv(my_balls.data(), ball_count, MPI_BALL, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // Print the balls for each process
    for (int i = 0; i < world_size; i++)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        if (world_rank == i)
        {
            std::cout << "Process " << world_rank << " has " << my_balls.size() << " balls" << std::endl;
            for (const auto &ball : my_balls)
            {
                std::cout << ball.x << " " << ball.y << " " << ball.d << std::endl;
            }
        }
    }

    // Clean up the custom datatype
    MPI_Type_free(&MPI_BALL);

    // Finalize the MPI environment
    MPI_Finalize();

    return 0;
}
