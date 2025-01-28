#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <cstring>
#include <thread>
#include <mpi.h>

struct Metadata
{
    int file_id;
    char file_name[256];
    long chunk_count;
    std::vector<std::vector<int>> chunk_nodes;
};

struct Chunk
{
    int file_id;
    long chunk_id;
    char data[32];
};

MPI_Datatype MPI_CHUNK;

// vector to store latest heartbeats from storage nodes
std::vector<std::chrono::time_point<std::chrono::system_clock>> heartbeats;
std::vector<bool> heartbeats_status;
std::atomic<bool> rank_heartbeat, kill_thread;

void heartbeat_monitor(int world_size, std::atomic<bool> &kill_thread)
{
    while (!kill_thread)
    {
        // listen to heartbeats from storage nodes, with MPI recieve functions
        for (int i = 1; i < world_size; i++)
        {
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(i, 0, MPI_COMM_WORLD, &flag, &status);
            if (flag)
            {
                int message;
                MPI_Recv(&message, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                auto now = std::chrono::system_clock::now();
                heartbeats[i - 1] = now;
            }
        }

        // if a storage node has not sent a heartbeat in the last 3 seconds, then report a failover to the main thread
        for (int i = 0; i < heartbeats.size(); i++)
        {
            auto now = std::chrono::system_clock::now();
            if (now - heartbeats[i] > std::chrono::seconds(3))
            {
                heartbeats_status[i] = false;
            }
            else
            {
                heartbeats_status[i] = true;
            }
        }

        // sleep for 0.1 seconds (corrected to use std::chrono::duration)
        std::this_thread::sleep_for(std::chrono::duration<double>(0.1));
    }
}

void heartbeat(int world_rank, std::atomic<bool> &rank_heartbeat, std::atomic<bool> &kill_thread)
{
    while (!kill_thread)
    {
        // send a heartbeat to the master process
        if (rank_heartbeat)
        {
            int message = 1;
            MPI_Send(&message, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
        }

        // sleep for 1 second (corrected to use std::chrono::duration)
        std::this_thread::sleep_for(std::chrono::duration<double>(1));
    }
}

std::vector<std::pair<int, int>> storage_load;
std::vector<Metadata> file_metadatas;

void upload_file_master(int world_size, std::string file_data)
{
    // find the space after the file name
    size_t space = file_data.find(" ");
    std::string file_name = file_data.substr(0, space);
    std::string file_path = file_data.substr(space + 1);

    // read the file in the file path into a single string
    std::ifstream file(file_path);
    std::string file_contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // create a metadata object for the file
    Metadata metadata;
    metadata.file_id = file_metadatas.size() + 1;
    strcpy(metadata.file_name, file_name.c_str());
    metadata.chunk_count = (file_contents.size() + 31) / 32;
    metadata.chunk_nodes.resize(metadata.chunk_count);

    // make an array of the chunk data, each chunk storing exactly 32 bytes of characters
    std::vector<std::vector<char>>
        chunks(metadata.chunk_count);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        chunks[i].resize(32);
        for (int j = 0; j < 32; j++)
        {
            chunks[i][j] = file_contents[i * 32 + j];
        }
    }

    // int last_chunk_size = file_contents.size() % 32;
    // chunks[metadata.chunk_count - 1][last_chunk_size] = '\0';

    // generate the chunk objects to send
    std::vector<Chunk> chunk_objects(metadata.chunk_count);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        chunk_objects[i].file_id = metadata.file_id;
        chunk_objects[i].chunk_id = i;
        for (int j = 0; j < 32; j++)
        {
            chunk_objects[i].data[j] = chunks[i][j];
        }
    }

    // plan the load balancing of the chunks to the storage nodes
    std::vector<std::vector<int>> storage_nodes(world_size - 1);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        // sort the storage nodes by load
        std::sort(storage_load.begin(), storage_load.end(), [](const std::pair<int, int> &a, const std::pair<int, int> &b)
                  { return a.second < b.second; });

        // assign the chunk to the storage node with the least load
        int node_count = 0;
        for (int node = 0; node < world_size - 1; node++)
        {
            if (heartbeats_status[storage_load[node].first])
            {
                metadata.chunk_nodes[i].push_back(storage_load[node].first);
                storage_load[node].second++;
                node_count++;
                storage_nodes[storage_load[node].first].push_back(i);
            }

            if (node_count == 3)
            {
                break;
            }
        }

        // if there are no storage nodes available, return an error message
        if (node_count == 0)
        {
            std::cout << "-1" << std::endl;
            return;
        }
    }

    // send a message to the storage nodes to expect chunks
    int message = 0;
    for (int i = 1; i < world_size; i++)
    {
        MPI_Request request;
        MPI_Isend(&message, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &request);
    }

    // send the chunks to the respective storage nodes
    for (int i = 0; i < storage_nodes.size(); i++)
    {
        // send a message on how many chunks to expect
        int chunk_count = storage_nodes[i].size();
        MPI_Request request;
        MPI_Isend(&chunk_count, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

        // send the chunks
        std::vector<Chunk> chunks_to_send(chunk_count);
        for (int j = 0; j < chunk_count; j++)
        {
            chunks_to_send[j] = chunk_objects[storage_nodes[i][j]];
        }
        MPI_Isend(chunks_to_send.data(), chunk_count, MPI_CHUNK, i + 1, 0, MPI_COMM_WORLD, &request);
    }

    // add file to the files metadata list
    file_metadatas.push_back(metadata);

    std::cout << "1" << std::endl;
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        std::cout << i << " " << metadata.chunk_nodes[i].size();
        for (int j = 0; j < metadata.chunk_nodes[i].size(); j++)
        {
            std::cout << " " << metadata.chunk_nodes[i][j] + 1;
        }
        std::cout << std::endl;
    }
}

void upload_file_node(std::vector<Chunk> &chunks)
{
    // recieve the message on how many chunks to expect
    int chunk_count;
    MPI_Recv(&chunk_count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // recieve the chunks
    std::vector<Chunk> chunks_recieved(chunk_count);
    MPI_Recv(chunks_recieved.data(), chunk_count, MPI_CHUNK, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // add the chunks to the chunks vector
    chunks.insert(chunks.end(), chunks_recieved.begin(), chunks_recieved.end());
}

void retrieve_file_master(int world_size, std::string file_data)
{
    // find the space after the file name
    size_t space = file_data.find(" ");
    std::string file_name = file_data.substr(0, space);
    std::string file_path = file_data.substr(space + 1);

    // create a metadata object for the file
    Metadata metadata;
    for (int i = 0; i < file_metadatas.size(); i++)
    {
        if (strcmp(file_metadatas[i].file_name, file_name.c_str()) == 0)
        {
            metadata = file_metadatas[i];
            break;
        }
    }

    if (metadata.file_id == 0)
    {
        std::cout << "-1" << std::endl;
        return;
    }

    // ask the storage nodes for the chunks of the file
    int message = 1;
    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // send a message to the storage nodes to retrieve the chunk
            MPI_Request request;
            MPI_Isend(&message, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the file id to the storage nodes
            MPI_Isend(&metadata.file_id, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);
        }
    }

    // recieve the chunks from the storage nodes
    std::vector<std::vector<Chunk>> chunks(world_size - 1);
    std::vector<int> chunk_count(world_size - 1);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        for (int j = 0; j < metadata.chunk_nodes[i].size(); j++)
        {
            chunk_count[metadata.chunk_nodes[i][j]]++;
        }
    }

    // // sleep here for 0.1 seconds to allow the storage nodes to send the chunks
    // std::this_thread::sleep_for(std::chrono::duration<double>(0.1));

    std::cout << "";

    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // recieve the chunks
            chunks[i].resize(chunk_count[i]);
            MPI_Recv(chunks[i].data(), chunk_count[i], MPI_CHUNK, i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    std::vector<Chunk> chunks_to_print;
    std::vector<int> chunk_recieved(metadata.chunk_count, 0);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        for (int j = 0; j < chunks.size(); j++)
        {
            for (int k = 0; k < chunks[j].size(); k++)
            {
                if (chunks[j][k].chunk_id == i)
                {
                    chunks_to_print.push_back(chunks[j][k]);
                    chunk_recieved[i]++;
                    break;
                }
            }
        }
    }

    // check if all the chunks are available
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        if (chunk_recieved[i] == 0)
        {
            std::cout << "-1" << std::endl;
            return;
        }
    }

    // print the chunks
    for (int i = 0; i < chunks_to_print.size() - 1; i++)
    {
        for (int j = 0; j < 32; j++)
        {
            std::cout << chunks_to_print[i].data[j];
        }
    }
    for (int j = 0; j < 32; j++)
    {
        if (chunks_to_print[chunks_to_print.size() - 1].data[j] == '\0')
        {
            std::cout << '\0' << 'a' << std::endl;
            break;
        }
        std::cout << chunks_to_print[chunks_to_print.size() - 1].data[j];
    }
}

void retrieve_file_node(std::vector<Chunk> &chunks, int world_rank)
{
    // recieve the message on the file id
    int file_id;
    // std::cout << "Recieving file id" << std::endl;
    MPI_Recv(&file_id, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // search all the chunks for the file id
    std::vector<Chunk> chunks_to_send;
    for (int i = 0; i < chunks.size(); i++)
    {
        if (chunks[i].file_id == file_id)
        {
            chunks_to_send.push_back(chunks[i]);
        }
    }

    // send the chunks to the master process
    MPI_Send(chunks_to_send.data(), chunks_to_send.size(), MPI_CHUNK, 0, 0, MPI_COMM_WORLD);
}

void search_master(std::string file_data, int world_size)
{
    // find the space after the file name
    size_t space = file_data.find(" ");
    std::string file_name = file_data.substr(0, space);
    std::string word = file_data.substr(space + 1);

    Metadata metadata = {};
    for (int i = 0; i < file_metadatas.size(); i++)
    {
        if (strcmp(file_metadatas[i].file_name, file_name.c_str()) == 0)
        {
            metadata = file_metadatas[i];
            break;
        }
    }

    if (metadata.file_id == 0)
    {
        std::cout << "-1" << std::endl;
        return;
    }

    // check if the word is longer than 32 characters
    if (word.size() > 32)
    {
        std::cout << "-1" << std::endl;
        return;
    }

    // check if all the chunks are available
    std::vector<bool> chunk_recieved(metadata.chunk_count, false);
    for (int i = 0; i < metadata.chunk_count; i++)
    {
        for (int j = 0; j < metadata.chunk_nodes[i].size(); j++)
        {
            if (heartbeats_status[metadata.chunk_nodes[i][j]])
            {
                chunk_recieved[i] = true;
                break;
            }
        }
    }

    for (int i = 0; i < metadata.chunk_count; i++)
    {
        if (!chunk_recieved[i])
        {
            std::cout << "-1" << std::endl;
            return;
        }
    }

    // ask the storage nodes for the word's position in the file
    int message = 2;
    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // send a message to the storage nodes to search for the word
            MPI_Request request;
            MPI_Isend(&message, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the file id to the storage nodes
            MPI_Isend(&metadata.file_id, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the word length to the storage nodes
            int word_length = word.size();
            MPI_Isend(&word_length, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the word to the storage nodes
            MPI_Isend(word.c_str(), word.size(), MPI_CHAR, i + 1, 0, MPI_COMM_WORLD, &request);
        }
    }

    // recieve the positions of the word from the storage nodes
    std::vector<std::vector<int>> word_positions(world_size - 1);
    std::vector<std::vector<int>> word_chunk_ids(world_size - 1);
    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // recieve the positions and chunk ids of the word
            int word_count;
            MPI_Recv(&word_count, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            word_positions[i].resize(word_count);
            MPI_Recv(word_positions[i].data(), word_count, MPI_INT, i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            word_chunk_ids[i].resize(word_count);
            MPI_Recv(word_chunk_ids[i].data(), word_count, MPI_INT, i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    std::vector<int> word_positions_calculator;
    std::vector<int> word_chunk_ids_calculator;
    for (int i = 0; i < word_positions.size(); i++)
    {
        for (int j = 0; j < word_positions[i].size(); j++)
        {
            // if not found already, add the word position to the list
            bool found = false;
            for (int k = 0; k < word_positions_calculator.size(); k++)
            {
                if (word_chunk_ids_calculator[k] == word_chunk_ids[i][j] && word_positions_calculator[k] == word_positions[i][j])
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                word_positions_calculator.push_back(word_positions[i][j]);
                word_chunk_ids_calculator.push_back(word_chunk_ids[i][j]);
            }
        }
    }

    std::vector<bool> search_validity(word_positions_calculator.size(), false);
    for (int i = 0; i < word_positions_calculator.size(); i++)
    {
        if (word_chunk_ids_calculator[i] == 0 && word_positions_calculator[i] == 0)
        {
            search_validity[i] = true;
        }
        else if (word_positions_calculator[i] + word.size() < 32)
        {
            search_validity[i] = true;
        }
        else if (word_positions_calculator[i] + word.size() == 32)
        {
            if (metadata.chunk_count == word_chunk_ids_calculator[i] + 1)
            {
                search_validity[i] = true;
            }
            else
            {
                search_validity[i] = false;
            }
        }
        else
        {
            search_validity[i] = false;
        }
    }

    // get more info on the invalid searches
    std::vector<std::pair<int, int>> invalid_searches;
    for (int i = 0; i < word_positions_calculator.size(); i++)
    {
        if (!search_validity[i])
        {
            invalid_searches.push_back({word_positions_calculator[i], word_chunk_ids_calculator[i]});
        }
    }

    // separate to vectors for request
    std::vector<std::vector<int>> word_positions_calculator_separated(world_size - 1);
    std::vector<std::vector<int>> word_chunk_ids_calculator_separated(world_size - 1);
    for (int i = 0; i < invalid_searches.size(); i++)
    {
        for (int j = 0; j < 3; j++)
        {
            if (heartbeats_status[metadata.chunk_nodes[invalid_searches[i].second + 1][j]])
            {
                word_positions_calculator_separated[metadata.chunk_nodes[invalid_searches[i].second + 1][j]].push_back(invalid_searches[i].first);
                word_chunk_ids_calculator_separated[metadata.chunk_nodes[invalid_searches[i].second + 1][j]].push_back(invalid_searches[i].second + 1);

                break;
            }
        }
    }

    // ask the storage nodes for the validity of the search
    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // send a message to the storage nodes to search for the word
            int search_size = word_positions_calculator_separated[i].size();
            MPI_Request request;
            MPI_Isend(&search_size, 1, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the word positions to the storage nodes
            MPI_Isend(word_positions_calculator_separated[i].data(), search_size, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);

            // send the word chunk ids to the storage nodes
            MPI_Isend(word_chunk_ids_calculator_separated[i].data(), search_size, MPI_INT, i + 1, 0, MPI_COMM_WORLD, &request);
        }
    }

    // recieve the validity of the search from the storage nodes
    std::vector<std::vector<int>> search_validity_separated(world_size - 1);
    for (int i = 0; i < world_size - 1; i++)
    {
        if (heartbeats_status[i])
        {
            // recieve the positions and chunk ids of the word
            int search_size = word_positions_calculator_separated[i].size();
            search_validity_separated[i].resize(search_size);
            MPI_Recv(search_validity_separated[i].data(), search_size, MPI_INT, i + 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }

    // combine the search validity
    for (int i = 0; i < world_size - 1; i++)
    {
        for (int j = 0; j < search_validity_separated[i].size(); j++)
        {
            int idx = -1;
            for (int k = 0; k < word_positions_calculator.size(); k++)
            {
                if (word_positions_calculator[k] == word_positions_calculator_separated[i][j] && word_chunk_ids_calculator[k] == word_chunk_ids_calculator_separated[i][j] - 1)
                {
                    idx = k;
                    break;
                }
            }
            if (idx != -1)
            {
                if (search_validity_separated[i][j])
                {
                    search_validity[idx] = true;
                }
                else
                {
                    search_validity[idx] = false;
                }
            }
        }
    }

    // print the search results
    int valid_search_count = 0;
    for (int i = 0; i < search_validity.size(); i++)
    {
        if (search_validity[i])
        {
            valid_search_count++;
        }
    }
    std::cout << valid_search_count << std::endl;
    std::vector<int> word_positions_calculator_valid;
    for (int i = 0; i < search_validity.size(); i++)
    {
        if (search_validity[i])
        {
            word_positions_calculator_valid.push_back(word_positions_calculator[i] + (32 * word_chunk_ids_calculator[i]));
        }
    }
    // sort the word positions in ascending order
    std::sort(word_positions_calculator_valid.begin(), word_positions_calculator_valid.end());
    for (int i = 0; i < word_positions_calculator_valid.size(); i++)
    {
        std::cout << word_positions_calculator_valid[i] << " ";
    }
    std::cout << std::endl;
}

void search_node(std::vector<Chunk> &chunks)
{
    // recieve the message on the file id
    int file_id;
    MPI_Recv(&file_id, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // recieve the word length
    int word_length;
    MPI_Recv(&word_length, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // recieve the word
    char word[33];
    word[word_length] = '\0';
    word[word_length + 1] = '\0';
    MPI_Recv(word, 32, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // move all letters one step to the right
    for (int i = 32; i > 0; i--)
    {
        word[i] = word[i - 1];
    }
    word[0] = ' ';

    // search the chunks for the word
    std::vector<int> word_positions;
    std::vector<int> word_chunk_ids;

    for (int i = 0; i < chunks.size(); i++)
    {
        if (chunks[i].file_id == file_id)
        {
            // search the chunk for the word
            for (int j = 0; j < 32; j++)
            {
                bool flag = true;
                for (int k = 0; k < 32; k++)
                {
                    if (j + k >= 32)
                    {
                        break;
                    }
                    else if (word[k] == '\0')
                    {
                        if (chunks[i].data[j + k] != '\0' && chunks[i].data[j + k] != ' ')
                        {
                            flag = false;
                        }

                        break;
                    }

                    if (chunks[i].data[j + k] != word[k])
                    {
                        flag = false;
                        break;
                    }
                }
                if (flag)
                {
                    word_positions.push_back(j + 1);
                    word_chunk_ids.push_back(chunks[i].chunk_id);
                }
            }

            if (chunks[i].chunk_id == 0)
            {
                // check if the data starts with the word
                bool flag = true;
                for (int k = 1; k < word_length + 1; k++)
                {
                    if (chunks[i].data[k - 1] != word[k])
                    {
                        flag = false;
                        break;
                    }
                }
                if (flag)
                {
                    word_positions.push_back(0);
                    word_chunk_ids.push_back(chunks[i].chunk_id);
                }
            }
        }
    }

    // send the positions of the word to the master process
    int word_count = word_positions.size();
    MPI_Send(&word_count, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
    MPI_Send(word_positions.data(), word_count, MPI_INT, 0, 0, MPI_COMM_WORLD);
    MPI_Send(word_chunk_ids.data(), word_count, MPI_INT, 0, 0, MPI_COMM_WORLD);

    // recieve the message on the search size
    int search_size;
    MPI_Recv(&search_size, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // recieve the word positions
    std::vector<int> word_positions_calculator(search_size);
    MPI_Recv(word_positions_calculator.data(), search_size, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // recieve the word chunk ids
    std::vector<int> word_chunk_ids_calculator(search_size);
    MPI_Recv(word_chunk_ids_calculator.data(), search_size, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // check the validity of the search
    std::vector<int> search_validity(search_size);

    for (int i = 0; i < search_size; i++)
    {
        char *data = chunks[word_chunk_ids_calculator[i]].data;
        int pos = 32 - word_positions_calculator[i];
        bool flag = true;
        for (int j = 0; j < 32; j++)
        {
            if (word[j + pos] == '\0')
            {
                if (data[j] == '\0' || data[j] == ' ')
                {
                    flag = true;
                    break;
                }
                else
                {
                    flag = false;
                    break;
                }
            }
            if (data[j] != word[j + pos])
            {
                flag = false;
                break;
            }
        }
        if (flag)
        {
            search_validity[i] = 1;
        }
        else
        {
            search_validity[i] = 0;
        }
    }

    // send the validity of the search to the master process
    MPI_Send(search_validity.data(), search_size, MPI_INT, 0, 0, MPI_COMM_WORLD);
}

void list_master(std::string file_data)
{
    // find the space after the file name
    size_t space = file_data.find(" ");
    std::string file_name = file_data.substr(0, space);
    std::string file_path = file_data.substr(space + 1);

    Metadata metadata = {};
    for (int i = 0; i < file_metadatas.size(); i++)
    {
        if (strcmp(file_metadatas[i].file_name, file_name.c_str()) == 0)
        {
            metadata = file_metadatas[i];
            break;
        }
    }

    if (metadata.file_id == 0)
    {
        std::cout << "-1" << std::endl;
        return;
    }

    for (int i = 0; i < metadata.chunk_count; i++)
    {
        // count the number of storage nodes that have the chunk
        int node_count = 0;
        for (int j = 0; j < metadata.chunk_nodes[i].size(); j++)
        {
            if (heartbeats_status[metadata.chunk_nodes[i][j]])
            {
                node_count++;
            }
        }

        std::cout << i << " " << node_count;
        for (int j = 0; j < metadata.chunk_nodes[i].size(); j++)
        {
            if (heartbeats_status[metadata.chunk_nodes[i][j]])
            {
                std::cout << " " << metadata.chunk_nodes[i][j] + 1;
            }
        }
        std::cout << std::endl;
    }
}

void failover_master(int world_size, int node)
{
    if (node >= 1 && node < world_size)
    {
        // print the failover message
        std::cout << "1" << std::endl;

        // send a message to the storage node to fail
        int message = 4;
        MPI_Request request;
        MPI_Isend(&message, 1, MPI_INT, node, 0, MPI_COMM_WORLD, &request);
    }
}

void recover_master(int world_size, int node)
{
    if (node >= 1 && node < world_size)
    {
        // send a message to the storage node to recover
        int message = 5;
        MPI_Request request;
        MPI_Isend(&message, 1, MPI_INT, node, 0, MPI_COMM_WORLD, &request);
    }
}

void exit_master(int world_size)
{
    // exit the program, broadcast a message to all storage nodes to stop the heartbeats
    int message = 6;
    for (int i = 1; i < world_size; i++)
    {
        MPI_Request request;
        MPI_Isend(&message, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &request);
    }

    kill_thread = true;
}

int main(int argc, char **argv)
{
    // Initialize the MPI environment
    MPI_Init(&argc, &argv);

    int world_size, world_rank;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    // define a MPI Datatype in Chunks
    int blocklengths[3] = {1, 1, 32};
    MPI_Datatype types[3] = {MPI_INT, MPI_LONG, MPI_CHAR};
    MPI_Aint offsets[3];
    offsets[0] = offsetof(Chunk, file_id);
    offsets[1] = offsetof(Chunk, chunk_id);
    offsets[2] = offsetof(Chunk, data);

    // MPI_Datatype MPI_CHUNK;
    MPI_Type_create_struct(3, blocklengths, offsets, types, &MPI_CHUNK);
    MPI_Type_commit(&MPI_CHUNK);

    // If process 0, then serve as the master process, else serve as the storage node
    if (world_rank == 0)
    {
        // Master process

        // resize the bookkeeping vectors
        heartbeats_status.resize(world_size - 1);
        for (int i = 0; i < world_size - 1; i++)
        {
            storage_load.push_back({i, 0});
        }
        heartbeats.resize(world_size - 1);
        for (int i = 0; i < heartbeats.size(); i++)
        {
            heartbeats[i] = std::chrono::system_clock::now();
        }
        std::thread monitor(heartbeat_monitor, world_size, std::ref(kill_thread));

        // read the stdin for commands to perform failover or other operations
        std::string command;
        while (std::getline(std::cin, command))
        {
            // if the command starts with "upload", then upload a file to the storage nodes
            if (command.find("upload") == 0)
            {
                // take the command after the space, eg. "upload file1.txt ./../files/file1.txt"
                std::string file_data = command.substr(7);

                upload_file_master(world_size, file_data);
            }
            // if the command starts with "retrieve", then retrieve a file from the storage nodes
            else if (command.find("retrieve") == 0)
            {
                // take the command after the space, eg. "retrieve file1.txt ./../files/file1.txt"
                std::string file_data = command.substr(9);

                retrieve_file_master(world_size, file_data);
            }
            // if the command starts with "search", then search for a word in the files on the storage nodes
            else if (command.find("search") == 0)
            {
                // take the command after the space, eg. "search file1.txt word"
                std::string file_data = command.substr(7);

                search_master(file_data, world_size);
            }
            // if the command starts with "list", then list the files on the storage nodes
            else if (command.find("list_file") == 0)
            {
                // take the command after the space, eg. "list file1.txt"
                std::string file_data = command.substr(10);

                list_master(file_data);
            }
            // if the command starts with "fail", then fail the storage node after the space, eg. "fail 2"
            else if (command.find("failover") == 0)
            {
                // take the command after the space, eg. "failover 2"
                int node = std::stoi(command.substr(9));

                failover_master(world_size, node);
            }
            // if the command starts with "recover", then recover the storage node after the space, eg. "recover 2"
            else if (command.find("recover") == 0)
            {
                // take the command after the space, eg. "recover 2"
                int node = std::stoi(command.substr(8));

                recover_master(world_size, node);
            }
            // if the command is "exit", then exit the program
            else if (command.find("exit") == 0)
            {
                // end all threads on all processes
                exit_master(world_size);

                break;
            }
        }

        // stop the monitor thread
        monitor.join();
    }
    else
    {
        // Storage node

        // initialize the bookkeeping vectors
        std::vector<Chunk> chunks;

        // create a thread to send heartbeats to the master process
        rank_heartbeat = true;
        std::thread heartbeat_thread(heartbeat, world_rank, std::ref(rank_heartbeat), std::ref(kill_thread));

        // wait for master node communications
        while (true)
        {
            // iprobe for a message from the master process, then recieve the message if there is one
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(0, 0, MPI_COMM_WORLD, &flag, &status);
            if (!flag)
            {
                continue;
            }

            int message;
            MPI_Recv(&message, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (message == 0)
            {
                // upload file
                upload_file_node(chunks);
            }
            else if (message == 1)
            {
                // retrieve file
                retrieve_file_node(chunks, world_rank);
            }
            else if (message == 2)
            {
                // search word in file
                search_node(chunks);
            }
            // else if (message == 3)
            // {
            //     // list file
            // }
            else if (message == 4)
            {
                rank_heartbeat = false;
            }
            else if (message == 5)
            {
                rank_heartbeat = true;
            }
            else if (message == 6)
            {
                kill_thread = true;
                break;
            }
        }

        // stop the heartbeat thread
        heartbeat_thread.join();
    }

    // Clean up the MPI environment
    MPI_Finalize();

    return 0;
}
