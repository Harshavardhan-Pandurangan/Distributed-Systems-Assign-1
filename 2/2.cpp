#include <iostream>
#include <vector>
#include <mpi.h>
#include <cstring>

struct Ball
{
    int id; // ID
    int x;  // Position
    int y;  // Position
    int d;  // Direction
};

int main(int argc, char **argv)
{
    // Initialize the MPI environment
    MPI_Init(&argc, &argv);

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    // Define a new MPI datatype for the Ball struct
    int blocklengths[4] = {1, 1, 1, 1};
    MPI_Datatype types[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_INT};
    MPI_Aint offsets[4];
    offsets[0] = offsetof(Ball, id);
    offsets[1] = offsetof(Ball, x);
    offsets[2] = offsetof(Ball, y);
    offsets[3] = offsetof(Ball, d);

    MPI_Datatype MPI_BALL;
    MPI_Type_create_struct(4, blocklengths, offsets, types, &MPI_BALL);
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

            balls.push_back({i, x, y, d});
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
        for (const auto &ball : balls)
        {
            int owner = ball.x * world_size / N;
            ball_distribution[owner].push_back(ball);
        }
    }

    int ball_count = 0;
    if (world_rank == 0)
    {
        ball_count = ball_distribution[0].size();
        for (int i = 1; i < world_size; i++)
        {
            int count = ball_distribution[i].size();
            MPI_Send(&count, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
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

    int dx[4] = {-1, 0, 1, 0};
    int dy[4] = {0, 1, 0, -1};
    int dd[4] = {0, 1, 0, 2};

    // handle collisions
    std::vector<std::vector<int>> map(N, std::vector<int>(M, 0));

    for (int t = 0; t < T; t++)
    {
        std::vector<Ball> send_top_balls, send_bottom_balls, receive_top_balls, receive_bottom_balls;

        // Update positions and determine ownership
        int i = 0;
        while (i < my_balls.size())
        {
            my_balls[i].x = (my_balls[i].x + dx[my_balls[i].d] + N) % N;
            my_balls[i].y = (my_balls[i].y + dy[my_balls[i].d] + M) % M;

            int owner = my_balls[i].x * world_size / N;
            if (owner != world_rank)
            {
                if ((world_rank - owner + world_size) % world_size == 1)
                {
                    send_top_balls.push_back(my_balls[i]);
                }
                else
                {
                    send_bottom_balls.push_back(my_balls[i]);
                }
                my_balls.erase(my_balls.begin() + i);
            }
            else
            {
                i++;
            }
        }

        // Allocate space for receiving balls
        int top_count = 0, bottom_count = 0;

        MPI_Request send_requests[2], recv_requests[2];
        MPI_Status statuses[2];

        // Exchange counts first
        int send_top_balls_size = send_top_balls.size();
        int send_bottom_balls_size = send_bottom_balls.size();
        MPI_Send(&send_top_balls_size, 1, MPI_INT, (world_rank - 1 + world_size) % world_size, 0, MPI_COMM_WORLD);
        MPI_Send(&send_bottom_balls_size, 1, MPI_INT, (world_rank + 1) % world_size, 0, MPI_COMM_WORLD);

        MPI_Irecv(&top_count, 1, MPI_INT, (world_rank - 1 + world_size) % world_size, 0, MPI_COMM_WORLD, &recv_requests[0]);
        MPI_Irecv(&bottom_count, 1, MPI_INT, (world_rank + 1) % world_size, 0, MPI_COMM_WORLD, &recv_requests[1]);

        MPI_Waitall(2, recv_requests, statuses);

        // Resize receive buffers
        receive_top_balls.resize(top_count);
        receive_bottom_balls.resize(bottom_count);

        // Send and receive balls
        MPI_Isend(send_top_balls.data(), send_top_balls.size(), MPI_BALL, (world_rank - 1 + world_size) % world_size, 1, MPI_COMM_WORLD, &send_requests[0]);
        MPI_Isend(send_bottom_balls.data(), send_bottom_balls.size(), MPI_BALL, (world_rank + 1) % world_size, 1, MPI_COMM_WORLD, &send_requests[1]);

        MPI_Irecv(receive_top_balls.data(), top_count, MPI_BALL, (world_rank - 1 + world_size) % world_size, 1, MPI_COMM_WORLD, &recv_requests[0]);
        MPI_Irecv(receive_bottom_balls.data(), bottom_count, MPI_BALL, (world_rank + 1) % world_size, 1, MPI_COMM_WORLD, &recv_requests[1]);

        // Handle collisions
        map = std::vector<std::vector<int>>(N, std::vector<int>(M, 0));
        for (const auto &ball : my_balls)
        {
            map[ball.x][ball.y]++;
        }

        MPI_Waitall(2, recv_requests, statuses);

        // Update local state
        my_balls.insert(my_balls.end(), receive_top_balls.begin(), receive_top_balls.end());
        my_balls.insert(my_balls.end(), receive_bottom_balls.begin(), receive_bottom_balls.end());

        for (const auto &ball : receive_top_balls)
        {
            map[ball.x][ball.y]++;
        }
        for (const auto &ball : receive_bottom_balls)
        {
            map[ball.x][ball.y]++;
        }

        // Process collisions
        for (auto &ball : my_balls)
        {
            ball.d = (ball.d + dd[map[ball.x][ball.y] - 1]) % 4;
        }
    }

    // get all the balls to the root process
    std::vector<std::vector<Ball>> all_balls(world_size);
    for (int i = 0; i < world_size; i++)
    {
        if (world_rank == i)
        {
            all_balls[i] = my_balls;
        }
    }

    // Gather all the balls to the root process
    for (int i = 0; i < world_size; i++)
    {
        MPI_Barrier(MPI_COMM_WORLD);
        if (world_rank == i)
        {
            for (int j = 0; j < world_size; j++)
            {
                if (j == i)
                {
                    continue;
                }

                int count = 0;
                MPI_Recv(&count, 1, MPI_INT, j, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                all_balls[j].resize(count);
                MPI_Recv(all_balls[j].data(), count, MPI_BALL, j, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
        else
        {
            int count = my_balls.size();
            MPI_Send(&count, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            MPI_Send(my_balls.data(), count, MPI_BALL, i, 0, MPI_COMM_WORLD);
        }
    }

    // Print the balls from the root process
    if (world_rank == 0)
    {
        // sort the balls by id
        std::vector<Ball> all_balls_sorted;
        for (int i = 0; i < world_size; i++)
        {
            all_balls_sorted.insert(all_balls_sorted.end(), all_balls[i].begin(), all_balls[i].end());
        }

        std::sort(all_balls_sorted.begin(), all_balls_sorted.end(), [](const Ball &a, const Ball &b)
                  { return a.id < b.id; });

        std::cout << std::endl;

        for (const auto &ball : all_balls_sorted)
        {
            std::cout << ball.x << " " << ball.y << " ";
            if (ball.d == 0)
                std::cout << "U";
            else if (ball.d == 1)
                std::cout << "R";
            else if (ball.d == 2)
                std::cout << "D";
            else
                std::cout << "L";
            std::cout << std::endl;
        }
    }

    // Clean up the custom datatype
    MPI_Type_free(&MPI_BALL);

    MPI_Finalize();
    return 0;
}
